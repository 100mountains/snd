// VST3 backend for snd::plugin.
//
// Built directly on the VST3 SDK's own hosting utilities: Module (binary
// loading), PlugProvider (component+controller instantiation and connection),
// HostProcessData (bus buffer management), ParameterChanges (sample-accurate
// parameter delivery). See docs/research/vst3-sdk-audit.md and
// docs/research/juce-hosting-audit.md for where this design comes from.

#include "vst3_format.h"
#include "../editor_window.h"

#if defined(__linux__)
#include <poll.h>
#endif

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/base/funknownimpl.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
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

class VST3Instance;

// The editor's channel back into the host: knob tweaks in the plugin GUI
// arrive as performEdit and must reach the processor, or the editor is
// decorative. (The SDK's PlugProvider installs a do-nothing handler.)
class VST3ComponentHandler final
    : public U::Implements<U::Directly<Vst::IComponentHandler>> {
public:
    explicit VST3ComponentHandler(VST3Instance* owner) : owner_(owner) {}
    void disconnect() { owner_ = nullptr; }

    tresult PLUGIN_API beginEdit(Vst::ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(Vst::ParamID id, Vst::ParamValue value) override;
    tresult PLUGIN_API endEdit(Vst::ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override;

private:
    VST3Instance* owner_ = nullptr;
};

// Plugin-initiated editor resizes (host must resize the window, then onSize).
// On Linux the frame ALSO provides Steinberg::Linux::IRunLoop -- plugins
// need it for their timers and fd watching; without it most editors freeze.
#if defined(__linux__)
class VST3PlugFrame final
    : public U::Implements<U::Directly<IPlugFrame, Steinberg::Linux::IRunLoop>> {
#else
class VST3PlugFrame final : public U::Implements<U::Directly<IPlugFrame>> {
#endif
public:
    explicit VST3PlugFrame(VST3Instance* owner) : owner_(owner) {}
    void disconnect() { owner_ = nullptr; }

    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override;

#if defined(__linux__)
    tresult PLUGIN_API registerEventHandler(Steinberg::Linux::IEventHandler* handler,
                                            Steinberg::Linux::FileDescriptor fd) override
    {
        if (!handler)
            return kInvalidArgument;
        fds_.push_back({handler, fd});
        return kResultTrue;
    }

    tresult PLUGIN_API unregisterEventHandler(
        Steinberg::Linux::IEventHandler* handler) override
    {
        fds_.erase(std::remove_if(fds_.begin(), fds_.end(),
                                  [&](auto& e) { return e.handler == handler; }),
                   fds_.end());
        return kResultTrue;
    }

    tresult PLUGIN_API registerTimer(Steinberg::Linux::ITimerHandler* handler,
                                     Steinberg::Linux::TimerInterval ms) override
    {
        if (!handler)
            return kInvalidArgument;
        timers_.push_back({handler, std::max<uint64_t>(1, ms),
                           std::chrono::steady_clock::now() +
                               std::chrono::milliseconds(std::max<uint64_t>(1, ms))});
        return kResultTrue;
    }

    tresult PLUGIN_API unregisterTimer(Steinberg::Linux::ITimerHandler* handler) override
    {
        timers_.erase(std::remove_if(timers_.begin(), timers_.end(),
                                     [&](auto& t) { return t.handler == handler; }),
                      timers_.end());
        return kResultTrue;
    }

    // called from Instance::idle() on the main thread
    void service()
    {
        auto now = std::chrono::steady_clock::now();
        for (auto& t : timers_)
            if (now >= t.next) {
                t.next = now + std::chrono::milliseconds(t.intervalMs);
                t.handler->onTimer();
            }
        if (!fds_.empty()) {
            std::vector<pollfd> pfds;
            for (auto& e : fds_)
                pfds.push_back({e.fd, POLLIN, 0});
            if (poll(pfds.data(), (nfds_t)pfds.size(), 0) > 0)
                for (size_t i = 0; i < pfds.size(); ++i)
                    if (pfds[i].revents & POLLIN)
                        fds_[i].handler->onFDIsSet(fds_[i].fd);
        }
    }
#endif

private:
    VST3Instance* owner_ = nullptr;
#if defined(__linux__)
    struct FdEntry {
        Steinberg::Linux::IEventHandler* handler;
        Steinberg::Linux::FileDescriptor fd;
    };
    struct TimerEntry {
        Steinberg::Linux::ITimerHandler* handler;
        uint64_t intervalMs;
        std::chrono::steady_clock::time_point next;
    };
    std::vector<FdEntry> fds_;
    std::vector<TimerEntry> timers_;
#endif
};

class VST3Instance final : public Instance {
public:
    VST3Instance(Description desc, VST3::Hosting::Module::Ptr mod,
                 IPtr<Vst::PlugProvider> prov)
        : desc_(std::move(desc)), module_(std::move(mod)), provider_(std::move(prov))
    {
        component_ = provider_->getComponentPtr();
        controller_ = provider_->getControllerPtr();
        processor_ = FUnknownPtr<Vst::IAudioProcessor>(component_);
        midiMapping_ = FUnknownPtr<Vst::IMidiMapping>(controller_);
        handler_ = owned(new VST3ComponentHandler(this));
        if (controller_)
            controller_->setComponentHandler(handler_);
        buildParameterList();
    }

    ~VST3Instance() override
    {
        closeEditor();
        if (handler_)
            handler_->disconnect();
        if (controller_)
            controller_->setComponentHandler(nullptr);
        unprepare();
        parameters_.clear();
        ownedParameters_.clear();
        midiMapping_ = nullptr;
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
        latency_ = processor_->getLatencySamples();

        if (!processData_.prepare(*component_, (int32)maxBlockFrames, Vst::kSample32)) {
            component_->setActive(false);
            return false;
        }

        processContext_ = {};
        processContext_.sampleRate = sampleRate;
        processContext_.tempo = tempo_.load(std::memory_order_relaxed);
        processContext_.timeSigNumerator = tsNum_.load(std::memory_order_relaxed);
        processContext_.timeSigDenominator = tsDen_.load(std::memory_order_relaxed);
        processContext_.state = Vst::ProcessContext::kPlaying |
                                Vst::ProcessContext::kTempoValid |
                                Vst::ProcessContext::kTimeSigValid |
                                Vst::ProcessContext::kProjectTimeMusicValid |
                                Vst::ProcessContext::kBarPositionValid;
        processData_.processContext = &processContext_;
        processData_.inputEvents = &eventList_; // filled by processMidi()
        processData_.outputEvents = &outputEventList_;
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
        static const midi::Buffer kNoMidi;
        return processMidi(in, inChannels, out, outChannels, frames, kNoMidi, nullptr);
    }

    bool processMidi(const float* const* in, uint32_t inChannels,
                     float* const* out, uint32_t outChannels, uint32_t frames,
                     const midi::Buffer& midiIn, midi::Buffer* midiOut) override
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

        // MIDI in: notes become VST3 events; CC/pitchbend/aftertouch travel
        // as parameter changes through the plugin's IMidiMapping (that's the
        // VST3 way -- there are no raw CC events).
        eventList_.clear();
        for (auto& m : midiIn) {
            if (m.isNoteOn()) {
                Vst::Event e{};
                e.busIndex = 0;
                e.sampleOffset = (int32)m.frame;
                e.type = Vst::Event::kNoteOnEvent;
                e.noteOn.channel = (int16)m.channel();
                e.noteOn.pitch = (int16)m.data1;
                e.noteOn.velocity = (float)m.data2 / 127.0f;
                e.noteOn.noteId = -1;
                eventList_.addEvent(e);
            } else if (m.isNoteOff()) {
                Vst::Event e{};
                e.busIndex = 0;
                e.sampleOffset = (int32)m.frame;
                e.type = Vst::Event::kNoteOffEvent;
                e.noteOff.channel = (int16)m.channel();
                e.noteOff.pitch = (int16)m.data1;
                e.noteOff.velocity = (float)m.data2 / 127.0f;
                e.noteOff.noteId = -1;
                eventList_.addEvent(e);
            } else if (midiMapping_ && (m.isControl() || m.isPitchBend())) {
                Vst::CtrlNumber ctrl =
                    m.isPitchBend() ? Vst::kPitchBend : (Vst::CtrlNumber)m.data1;
                Vst::ParamID pid = Vst::kNoParamId;
                if (midiMapping_->getMidiControllerAssignment(
                        0, (int16)m.channel(), ctrl, pid) == kResultOk &&
                    pid != Vst::kNoParamId) {
                    double v = m.isPitchBend()
                                   ? (double)(m.data1 | (m.data2 << 7)) / 16383.0
                                   : (double)m.data2 / 127.0;
                    int32 qIndex = 0;
                    if (auto* queue =
                            inputParamChanges_.addParameterData(pid, qIndex)) {
                        int32 pIndex = 0;
                        queue->addPoint((int32)m.frame, v, pIndex);
                    }
                }
            }
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
        outputEventList_.clear();

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

        // MIDI out: note events the plugin emitted this block.
        if (ok && midiOut) {
            int32 nEvents = outputEventList_.getEventCount();
            for (int32 i = 0; i < nEvents; ++i) {
                Vst::Event e{};
                if (outputEventList_.getEvent(i, e) != kResultOk)
                    continue;
                if (e.type == Vst::Event::kNoteOnEvent)
                    midiOut->push_back(midi::Message::noteOn(
                        (uint8_t)e.noteOn.channel, (uint8_t)e.noteOn.pitch,
                        (uint8_t)std::clamp((int)(e.noteOn.velocity * 127.0f), 1, 127),
                        (uint32_t)std::max<int32>(0, e.sampleOffset)));
                else if (e.type == Vst::Event::kNoteOffEvent)
                    midiOut->push_back(midi::Message::noteOff(
                        (uint8_t)e.noteOff.channel, (uint8_t)e.noteOff.pitch,
                        (uint32_t)std::max<int32>(0, e.sampleOffset)));
            }
        }

        // musical clock for tempo-synced plugins
        const double tempo = tempo_.load(std::memory_order_relaxed);
        const int num = tsNum_.load(std::memory_order_relaxed);
        const int den = tsDen_.load(std::memory_order_relaxed);
        processContext_.tempo = tempo;
        processContext_.timeSigNumerator = num;
        processContext_.timeSigDenominator = den;
        processContext_.projectTimeMusic =
            (double)processContext_.projectTimeSamples / sampleRate_ * tempo / 60.0;
        const double quartersPerBar = 4.0 * num / std::max(1, den);
        processContext_.barPositionMusic =
            std::floor(processContext_.projectTimeMusic / quartersPerBar) *
            quartersPerBar;

        processContext_.projectTimeSamples += frames;
        return ok;
    }

    void setTransport(double tempoBpm, int timeSigNumerator,
                      int timeSigDenominator) override
    {
        if (tempoBpm > 0)
            tempo_.store(tempoBpm, std::memory_order_relaxed);
        if (timeSigNumerator > 0)
            tsNum_.store(timeSigNumerator, std::memory_order_relaxed);
        if (timeSigDenominator > 0)
            tsDen_.store(timeSigDenominator, std::memory_order_relaxed);
    }

    void idle() override
    {
#if defined(__linux__)
        editorwin::pump(); // X11 window events (close/resize)
        if (frame_)
            frame_->service(); // the plugin's IRunLoop timers + fds
#endif
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

    uint32_t latencySamples() const override { return latency_; }

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

        refreshParamCaches();
        return true;
    }

    bool hasEditor() override
    {
        if (!controller_)
            return false;
        if (view_)
            return true;
        if (!checkedEditor_) {
            checkedEditor_ = true;
            IPtr<IPlugView> probe = owned(controller_->createView(Vst::ViewType::kEditor));
            hasEditorCached_ = probe != nullptr;
        }
        return hasEditorCached_;
    }

    bool openEditor(const std::string& windowTitle) override
    {
        if (editorWin_)
            return true;
        if (!controller_)
            return false;

#if defined(__APPLE__)
        const char* platformType = kPlatformTypeNSView;
#elif defined(_WIN32)
        const char* platformType = kPlatformTypeHWND;
#else
        const char* platformType = kPlatformTypeX11EmbedWindowID;
#endif

        view_ = owned(controller_->createView(Vst::ViewType::kEditor));
        if (!view_)
            return false;
        if (view_->isPlatformTypeSupported(platformType) != kResultTrue) {
            view_ = nullptr;
            return false;
        }

        ViewRect rect{};
        if (view_->getSize(&rect) != kResultOk || rect.getWidth() <= 0 ||
            rect.getHeight() <= 0) {
            rect.left = rect.top = 0;
            rect.right = 500;
            rect.bottom = 320;
        }
        bool resizable = view_->canResize() == kResultTrue;

        frame_ = owned(new VST3PlugFrame(this));
        view_->setFrame(frame_); // before attached(): plugins query it there

        editorwin::Callbacks cbs;
        cbs.onClose = [this] { closeEditor(); };
        cbs.onResized = [this](int w, int h) {
            if (inPluginResize_ || !view_ || view_->canResize() != kResultTrue)
                return;
            ViewRect r{0, 0, w, h};
            view_->checkSizeConstraint(&r);
            view_->onSize(&r);
        };
        editorWin_ = editorwin::create(windowTitle, rect.getWidth(), rect.getHeight(),
                                       resizable, std::move(cbs));
        if (!editorWin_) {
            teardownEditorView(false);
            return false;
        }

        if (view_->attached(editorwin::contentView(editorWin_), platformType) !=
            kResultOk) {
            teardownEditorView(false);
            editorwin::destroy(editorWin_);
            editorWin_ = nullptr;
            return false;
        }
        return true;
    }

    void closeEditor() override
    {
        if (!editorWin_)
            return;
        teardownEditorView(true);
        void* win = editorWin_;
        editorWin_ = nullptr; // editorOpen() is false from here on
        editorwin::destroy(win);
    }

    bool editorOpen() const override { return editorWin_ != nullptr; }

    // called from the component handler / plug frame
    void editorChangedParam(Vst::ParamID id, double value)
    {
        auto it = paramsById_.find(std::to_string(id));
        if (it != paramsById_.end())
            static_cast<VST3Parameter*>(it->second)->updateCacheOnly(value);
        SpinGuard g(pendingLock_);
        pendingAudio_.emplace_back(id, value);
    }

    void onRestartComponent(int32 flags)
    {
        if (flags & Vst::kParamValuesChanged)
            refreshParamCaches();
    }

    void pluginRequestedResize(const ViewRect& size)
    {
        if (!editorWin_ || !view_)
            return;
        inPluginResize_ = true;
        editorwin::setContentSize(editorWin_, size.getWidth(), size.getHeight());
        ViewRect r = size;
        view_->onSize(&r);
        inPluginResize_ = false;
    }

private:
    void refreshParamCaches()
    {
        if (!controller_)
            return;
        for (auto* p : parameters_) {
            Vst::ParamID pid = (Vst::ParamID)std::stoul(p->id());
            static_cast<VST3Parameter*>(p)->updateCacheOnly(
                controller_->getParamNormalized(pid));
        }
    }

    void teardownEditorView(bool attached)
    {
        if (view_) {
            if (attached)
                view_->removed();
            view_->setFrame(nullptr);
            view_ = nullptr;
        }
        if (frame_) {
            frame_->disconnect();
            frame_ = nullptr;
        }
    }

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
    IPtr<VST3ComponentHandler> handler_;

    // editor
    IPtr<IPlugView> view_;
    IPtr<VST3PlugFrame> frame_;
    void* editorWin_ = nullptr;
    bool checkedEditor_ = false, hasEditorCached_ = false;
    bool inPluginResize_ = false;

    Vst::HostProcessData processData_;
    Vst::ProcessContext processContext_{};
    Vst::ParameterChanges inputParamChanges_;
    Vst::ParameterChanges outputParamChanges_;
    Vst::EventList eventList_;       // MIDI in, per block
    Vst::EventList outputEventList_; // MIDI the plugin emits
    FUnknownPtr<Vst::IMidiMapping> midiMapping_; // CC/bend -> parameter route

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
    uint32_t latency_ = 0;
    std::atomic<double> tempo_{120.0};
    std::atomic<int> tsNum_{4}, tsDen_{4};
    bool prepared_ = false;
};

tresult PLUGIN_API VST3ComponentHandler::performEdit(Vst::ParamID id,
                                                     Vst::ParamValue value)
{
    if (owner_)
        owner_->editorChangedParam(id, value);
    return kResultOk;
}

tresult PLUGIN_API VST3ComponentHandler::restartComponent(int32 flags)
{
    if (owner_)
        owner_->onRestartComponent(flags);
    return kResultOk;
}

tresult PLUGIN_API VST3PlugFrame::resizeView(IPlugView* view, ViewRect* newSize)
{
    if (owner_ && view && newSize)
        owner_->pluginRequestedResize(*newSize);
    return kResultTrue;
}

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
