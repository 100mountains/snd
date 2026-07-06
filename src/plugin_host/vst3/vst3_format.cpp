// VST3 backend for snd::plugin.
//
// Built directly on the VST3 SDK's own hosting utilities: Module (binary
// loading), PlugProvider (component+controller instantiation and connection),
// HostProcessData (bus buffer management), ParameterChanges (sample-accurate
// parameter delivery). See docs/research/vst3-sdk-audit.md and
// docs/research/juce-hosting-audit.md for where this design comes from.

#include "vst3_format.h"

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/base/funknownimpl.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace snd::plugin {

namespace {

using namespace Steinberg;

// A tiny spin lock, per the audited JUCE pattern: the audio thread must never
// block on a full mutex while the main thread holds it.
class SpinLock {
public:
    void lock() { while (flag.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { flag.clear(std::memory_order_release); }

private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
};

struct SpinGuard {
    explicit SpinGuard(SpinLock& l) : lock(l) { lock.lock(); }
    ~SpinGuard() { lock.unlock(); }
    SpinLock& lock;
};

// One host context for the process; VST3 plugins receive this as their host.
Vst::HostApplication& hostContext()
{
    static Vst::HostApplication instance;
    return instance;
}

void ensurePluginContext()
{
    static bool done = false;
    if (!done) {
        Vst::PluginContextFactory::instance().setPluginContext(&hostContext());
        done = true;
    }
}

std::string makeIdentifier(const std::string& path, const VST3::UID& uid)
{
    return path + "|" + uid.toString();
}

bool splitIdentifier(const std::string& identifier, std::string& path, std::string& uid)
{
    auto pos = identifier.rfind('|');
    if (pos == std::string::npos)
        return false;
    path = identifier.substr(0, pos);
    uid = identifier.substr(pos + 1);
    return true;
}

// ---------------------------------------------------------------------------

class VST3Parameter final : public Parameter {
public:
    VST3Parameter(std::string id, std::string name, bool canAutomate, double initial)
        : paramIdString(std::move(id)), paramName(std::move(name)),
          canAuto(canAutomate), cached(initial) {}

    const std::string& id() const override { return paramIdString; }
    const std::string& name() const override { return paramName; }
    bool automatable() const override { return canAuto; }

    double value() const override { return cached.load(std::memory_order_relaxed); }

    void setValue(double v) override
    {
        v = std::clamp(v, 0.0, 1.0);
        cached.store(v, std::memory_order_relaxed);
        if (onSet)
            onSet(v);
    }

    // Wired by the owning instance: queues the change for both the processor
    // (next process() call) and the controller (next idle()).
    std::function<void(double)> onSet;

    // Called when the plugin itself changed the value (output param changes).
    void updateCacheOnly(double v) { cached.store(v, std::memory_order_relaxed); }

private:
    std::string paramIdString;
    std::string paramName;
    bool canAuto = true;
    std::atomic<double> cached;
};

// ---------------------------------------------------------------------------

class VST3Instance final : public Instance {
public:
    VST3Instance(Description desc, VST3::Hosting::Module::Ptr mod,
                 IPtr<Vst::PlugProvider> prov)
        : desc_(std::move(desc)), module_(std::move(mod)), provider_(std::move(prov))
    {
        component_ = provider_->getComponentPtr();
        controller_ = provider_->getControllerPtr();
        processor_ = FUnknownPtr<Vst::IAudioProcessor>(component_);
        buildParameterList();
    }

    ~VST3Instance() override
    {
        unprepare();
        parameters_.clear();
        ownedParameters_.clear();
        processor_ = nullptr;
        controller_ = nullptr;
        component_ = nullptr;
        provider_ = nullptr; // PlugProvider terminates component/controller
        module_ = nullptr;
    }

    const Description& description() const override { return desc_; }

    bool prepare(double sampleRate, uint32_t maxBlockFrames) override
    {
        if (!processor_ || !component_)
            return false;
        unprepare();

        Vst::ProcessSetup setup{};
        setup.processMode = Vst::kRealtime;
        setup.symbolicSampleSize = Vst::kSample32;
        setup.maxSamplesPerBlock = (int32)maxBlockFrames;
        setup.sampleRate = sampleRate;
        if (processor_->setupProcessing(setup) != kResultOk)
            return false;

        // Bus arrangements. Pitfall from the JUCE audit: never pass a null
        // pointer even for zero buses -- some plugins crash on it.
        int32 numInBuses = component_->getBusCount(Vst::kAudio, Vst::kInput);
        int32 numOutBuses = component_->getBusCount(Vst::kAudio, Vst::kOutput);

        std::vector<Vst::SpeakerArrangement> inArr((size_t)std::max<int32>(numInBuses, 1),
                                                   Vst::SpeakerArr::kStereo);
        std::vector<Vst::SpeakerArrangement> outArr((size_t)std::max<int32>(numOutBuses, 1),
                                                    Vst::SpeakerArr::kStereo);
        // The call may legitimately refuse (plugin keeps its own layout); we
        // proceed with whatever the buses actually are.
        processor_->setBusArrangements(inArr.data(), numInBuses, outArr.data(), numOutBuses);

        for (int32 i = 0; i < numInBuses; ++i)
            component_->activateBus(Vst::kAudio, Vst::kInput, i, true);
        for (int32 i = 0; i < numOutBuses; ++i)
            component_->activateBus(Vst::kAudio, Vst::kOutput, i, true);

        if (component_->setActive(true) != kResultOk)
            return false;
        processor_->setProcessing(true);

        if (!processData_.prepare(*component_, (int32)maxBlockFrames, Vst::kSample32)) {
            component_->setActive(false);
            return false;
        }

        processContext_ = {};
        processContext_.sampleRate = sampleRate;
        processContext_.tempo = 120.0;
        processContext_.state = Vst::ProcessContext::kPlaying | Vst::ProcessContext::kTempoValid;
        processData_.processContext = &processContext_;
        processData_.inputEvents = &eventList_;   // stays empty: no MIDI in SND
        processData_.inputParameterChanges = &inputParamChanges_;
        processData_.outputParameterChanges = &outputParamChanges_;

        sampleRate_ = sampleRate;
        maxBlock_ = maxBlockFrames;
        prepared_ = true;
        return true;
    }

    void unprepare() override
    {
        if (!prepared_)
            return;
        SpinGuard g(processLock_);
        processor_->setProcessing(false);
        component_->setActive(false);
        processData_.unprepare();
        prepared_ = false;
    }

    bool process(const float* const* in, uint32_t inChannels,
                 float* const* out, uint32_t outChannels, uint32_t frames) override
    {
        if (!prepared_ || frames == 0 || frames > maxBlock_)
            return false;

        SpinGuard g(processLock_);

        // Drain pending parameter changes into the VST3 queue (offset 0).
        inputParamChanges_.clearQueue();
        {
            SpinGuard pg(pendingLock_);
            for (auto& p : pendingAudio_) {
                int32 qIndex = 0;
                if (auto* queue = inputParamChanges_.addParameterData(p.first, qIndex)) {
                    int32 pIndex = 0;
                    queue->addPoint(0, p.second, pIndex);
                }
            }
            pendingAudio_.clear();
        }

        processData_.numSamples = (int32)frames;

        // Copy SND's input channels into bus 0; zero any extra bus channels.
        if (processData_.numInputs > 0) {
            auto& bus = processData_.inputs[0];
            for (int32 c = 0; c < bus.numChannels; ++c) {
                float* dst = bus.channelBuffers32[c];
                if (!dst) continue;
                if ((uint32_t)c < inChannels && in && in[c])
                    std::memcpy(dst, in[c], sizeof(float) * frames);
                else
                    std::memset(dst, 0, sizeof(float) * frames);
            }
            bus.silenceFlags = 0;
        }
        for (int32 b = 1; b < processData_.numInputs; ++b) {
            auto& bus = processData_.inputs[b];
            for (int32 c = 0; c < bus.numChannels; ++c)
                if (bus.channelBuffers32[c])
                    std::memset(bus.channelBuffers32[c], 0, sizeof(float) * frames);
        }

        outputParamChanges_.clearQueue();

        bool ok = processor_->process(processData_) == kResultOk;

        // Copy bus 0 output back out.
        if (ok && processData_.numOutputs > 0) {
            auto& bus = processData_.outputs[0];
            for (uint32_t c = 0; c < outChannels; ++c) {
                if (!out || !out[c]) continue;
                if ((int32)c < bus.numChannels && bus.channelBuffers32[c])
                    std::memcpy(out[c], bus.channelBuffers32[c], sizeof(float) * frames);
                else
                    std::memset(out[c], 0, sizeof(float) * frames);
            }
        }

        // Reflect plugin-driven parameter changes into the cache.
        int32 nOut = outputParamChanges_.getParameterCount();
        for (int32 i = 0; i < nOut; ++i) {
            auto* q = outputParamChanges_.getParameterData(i);
            if (!q) continue;
            int32 points = q->getPointCount();
            if (points <= 0) continue;
            int32 offset = 0;
            Vst::ParamValue v = 0;
            if (q->getPoint(points - 1, offset, v) == kResultTrue) {
                auto it = paramsById_.find(std::to_string(q->getParameterId()));
                if (it != paramsById_.end())
                    static_cast<VST3Parameter*>(it->second)->updateCacheOnly(v);
            }
        }

        processContext_.projectTimeSamples += frames;
        return ok;
    }

    void idle() override
    {
        if (!controller_)
            return;
        std::vector<std::pair<Vst::ParamID, double>> pending;
        {
            std::lock_guard<std::mutex> g(controllerPendingMutex_);
            pending.swap(pendingController_);
        }
        for (auto& p : pending)
            controller_->setParamNormalized(p.first, p.second);
    }

    const std::vector<Parameter*>& parameters() const override { return parameters_; }

    Parameter* parameterById(const std::string& id) const override
    {
        auto it = paramsById_.find(id);
        return it == paramsById_.end() ? nullptr : it->second;
    }

    bool saveState(std::vector<uint8_t>& out) override
    {
        if (!component_)
            return false;
        MemoryStream stream;
        if (component_->getState(&stream) != kResultOk)
            return false;
        out.assign(stream.getData(), stream.getData() + stream.getSize());
        return true;
    }

    bool loadState(const uint8_t* data, size_t size) override
    {
        if (!component_ || !data || size == 0)
            return false;

        // Restored state is the new truth: drop any parameter changes still
        // queued from before the restore, or they'd overwrite it on the next
        // process()/idle(). (Same hazard the JUCE audit flags around restore.)
        {
            SpinGuard g(pendingLock_);
            pendingAudio_.clear();
        }
        {
            std::lock_guard<std::mutex> g(controllerPendingMutex_);
            pendingController_.clear();
        }

        MemoryStream stream((void*)data, (TSize)size);
        if (component_->setState(&stream) != kResultOk)
            return false;

        // Keep the controller's view in sync, per spec.
        if (controller_) {
            stream.seek(0, IBStream::kIBSeekSet, nullptr);
            controller_->setComponentState(&stream);
        }

        // Refresh cached parameter values from the controller.
        if (controller_) {
            for (auto* p : parameters_) {
                Vst::ParamID pid = (Vst::ParamID)std::stoul(p->id());
                static_cast<VST3Parameter*>(p)->updateCacheOnly(
                    controller_->getParamNormalized(pid));
            }
        }
        return true;
    }

private:
    void buildParameterList()
    {
        if (!controller_)
            return;
        int32 count = controller_->getParameterCount();
        for (int32 i = 0; i < count; ++i) {
            Vst::ParameterInfo info{};
            if (controller_->getParameterInfo(i, info) != kResultOk)
                continue;

            auto idStr = std::to_string(info.id);
            auto title = VST3::StringConvert::convert(info.title);
            bool canAutomate = (info.flags & Vst::ParameterInfo::kCanAutomate) != 0;
            double initial = controller_->getParamNormalized(info.id);

            auto param = std::make_unique<VST3Parameter>(idStr, title, canAutomate, initial);
            Vst::ParamID pid = info.id;
            auto* self = this;
            param->onSet = [self, pid](double v) {
                {
                    SpinGuard g(self->pendingLock_);
                    self->pendingAudio_.emplace_back(pid, v);
                }
                {
                    std::lock_guard<std::mutex> g(self->controllerPendingMutex_);
                    self->pendingController_.emplace_back(pid, v);
                }
            };

            paramsById_[idStr] = param.get();
            parameters_.push_back(param.get());
            ownedParameters_.push_back(std::move(param));
        }
    }

    Description desc_;
    VST3::Hosting::Module::Ptr module_;
    IPtr<Vst::PlugProvider> provider_;
    IPtr<Vst::IComponent> component_;
    IPtr<Vst::IEditController> controller_;
    FUnknownPtr<Vst::IAudioProcessor> processor_;

    Vst::HostProcessData processData_;
    Vst::ProcessContext processContext_{};
    Vst::ParameterChanges inputParamChanges_;
    Vst::ParameterChanges outputParamChanges_;
    Vst::EventList eventList_; // deliberately always empty: no MIDI

    SpinLock processLock_;
    SpinLock pendingLock_;
    std::vector<std::pair<Vst::ParamID, double>> pendingAudio_;
    std::mutex controllerPendingMutex_;
    std::vector<std::pair<Vst::ParamID, double>> pendingController_;

    std::vector<std::unique_ptr<VST3Parameter>> ownedParameters_;
    std::vector<Parameter*> parameters_;
    std::unordered_map<std::string, Parameter*> paramsById_;

    double sampleRate_ = 0;
    uint32_t maxBlock_ = 0;
    bool prepared_ = false;
};

// ---------------------------------------------------------------------------

class VST3Format final : public Format {
public:
    const char* name() const override { return "VST3"; }

    std::vector<std::string> defaultSearchPaths() const override
    {
#if defined(__APPLE__)
        std::vector<std::string> paths = {"/Library/Audio/Plug-Ins/VST3"};
        if (const char* home = std::getenv("HOME"))
            paths.insert(paths.begin(), std::string(home) + "/Library/Audio/Plug-Ins/VST3");
        return paths;
#elif defined(_WIN32)
        std::vector<std::string> paths;
        if (const char* pf = std::getenv("COMMONPROGRAMFILES"))
            paths.push_back(std::string(pf) + "\\VST3");
        return paths;
#else
        std::vector<std::string> paths = {"/usr/lib/vst3", "/usr/local/lib/vst3"};
        if (const char* home = std::getenv("HOME"))
            paths.insert(paths.begin(), std::string(home) + "/.vst3");
        return paths;
#endif
    }

    bool fileMightBePlugin(const std::string& path) const override
    {
        if (path.size() < 5 || path.substr(path.size() - 5) != ".vst3")
            return false;
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

    std::vector<Description> scan(const std::string& path) override
    {
        ensurePluginContext();
        std::vector<Description> result;
        if (path.empty())
            return result;

        std::string err;
        auto module = VST3::Hosting::Module::create(path, err);
        if (!module)
            return result;

        // Per the audit: no way to know how many plugins live in a .vst3
        // without iterating its factory.
        for (auto& classInfo : module->getFactory().classInfos()) {
            if (classInfo.category() != kVstAudioEffectClass)
                continue;
            Description d;
            d.format = "VST3";
            d.path = path;
            d.identifier = makeIdentifier(path, classInfo.ID());
            d.name = classInfo.name();
            d.vendor = classInfo.vendor();
            d.version = classInfo.version();
            d.category = classInfo.subCategoriesString();
            result.push_back(std::move(d));
        }
        return result;
    }

    std::unique_ptr<Instance> create(const Description& desc) override
    {
        ensurePluginContext();

        std::string path, uidString;
        if (!splitIdentifier(desc.identifier, path, uidString))
            return nullptr;

        std::string err;
        auto module = VST3::Hosting::Module::create(path, err);
        if (!module)
            return nullptr;

        auto uid = VST3::UID::fromString(uidString);
        if (!uid)
            return nullptr;

        for (auto& classInfo : module->getFactory().classInfos()) {
            if (!(classInfo.ID() == *uid))
                continue;
            IPtr<Vst::PlugProvider> provider =
                owned(new Vst::PlugProvider(module->getFactory(), classInfo, true));
            if (!provider->initialize())
                return nullptr;
            return std::make_unique<VST3Instance>(desc, module, provider);
        }
        return nullptr;
    }
};

} // namespace

std::unique_ptr<Format> createVST3Format()
{
    return std::make_unique<VST3Format>();
}

} // namespace snd::plugin
