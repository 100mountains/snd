// VST3 wrapper for snd::plugin::client -- compiled into EACH plugin target
// by snd_add_plugin(), never into libsnd. Bridges one Processor (found via
// sndPluginSpec()/sndCreateProcessor()) onto a SingleComponentEffect and
// registers it with a runtime-built factory.

#include "snd/plugin_client.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

// the two functions every SND plugin defines
const snd::plugin::client::PluginSpec& sndPluginSpec();
std::unique_ptr<snd::plugin::client::Processor> sndCreateProcessor();

namespace snd::plugin::client {
// mac editor factory (editor_view_mac.mm); returns nullptr where unsupported
Steinberg::IPlugView* createEditorView(Processor& proc, UiHost& host, int width,
                                       int height, bool resizable, int minW,
                                       int minH);
#if defined(_WIN32)
Steinberg::IPlugView* createEditorView(Processor&, UiHost&, int, int, bool, int, int)
{
    return nullptr; // Windows editor embedding pending
}
#endif
} // namespace snd::plugin::client

namespace {

using namespace Steinberg;
using namespace snd::plugin::client;

class SndEffect final : public Vst::SingleComponentEffect, public UiHost {
public:
    SndEffect() : spec_(sndPluginSpec())
    {
        proc_ = sndCreateProcessor();
        const size_t n = spec_.params.size();
        values_.reset(new std::atomic<double>[n]);
        ids_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            values_[i].store(spec_.params[i].defaultValue);
            ids_[i] = spec_.params[i].id;
        }
        proc_->_bindParams(values_.get(), ids_.data(), n);
    }

    static FUnknown* createInstance(void*)
    {
        return (Vst::IAudioProcessor*)new SndEffect();
    }

    // --- component setup ---------------------------------------------------

    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        tresult r = SingleComponentEffect::initialize(context);
        if (r != kResultOk)
            return r;

        if (!spec_.isInstrument)
            addAudioInput(USTRING("In"), Vst::SpeakerArr::kStereo);
        addAudioOutput(USTRING("Out"), Vst::SpeakerArr::kStereo);
        if (spec_.wantsMidi || spec_.isInstrument)
            addEventInput(USTRING("MIDI In"), 16);

        for (auto& p : spec_.params) {
            auto title16 = VST3::StringConvert::convert(p.name);
            int32 flags = p.automatable ? Vst::ParameterInfo::kCanAutomate : 0;
            parameters.addParameter((const Vst::TChar*)title16.c_str(), nullptr, 0,
                                    p.defaultValue, flags, (Vst::ParamID)p.id);
        }
        return kResultOk;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) override
    {
        return symbolicSampleSize == Vst::kSample32 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setBusArrangements(Vst::SpeakerArrangement* inputs,
                                          int32 numIns,
                                          Vst::SpeakerArrangement* outputs,
                                          int32 numOuts) override
    {
        // stereo-only, by design of the client API
        if (numOuts != 1 || outputs[0] != Vst::SpeakerArr::kStereo)
            return kResultFalse;
        if (spec_.isInstrument)
            return numIns == 0 ? kResultTrue : kResultFalse;
        return numIns == 1 && inputs[0] == Vst::SpeakerArr::kStereo ? kResultTrue
                                                                    : kResultFalse;
    }

    tresult PLUGIN_API setupProcessing(Vst::ProcessSetup& setup) override
    {
        sampleRate_ = setup.sampleRate;
        maxBlock_ = (uint32_t)setup.maxSamplesPerBlock;
        return SingleComponentEffect::setupProcessing(setup);
    }

    tresult PLUGIN_API setActive(TBool state) override
    {
        if (state)
            proc_->prepare(sampleRate_, maxBlock_);
        else
            proc_->reset();
        return SingleComponentEffect::setActive(state);
    }

    // --- processing ----------------------------------------------------------

    tresult PLUGIN_API process(Vst::ProcessData& data) override
    {
        // parameter changes -> atomic store (last point of each queue)
        if (data.inputParameterChanges) {
            int32 nQueues = data.inputParameterChanges->getParameterCount();
            for (int32 q = 0; q < nQueues; ++q) {
                auto* queue = data.inputParameterChanges->getParameterData(q);
                if (!queue)
                    continue;
                int32 points = queue->getPointCount();
                if (points <= 0)
                    continue;
                int32 offset = 0;
                Vst::ParamValue value = 0;
                if (queue->getPoint(points - 1, offset, value) != kResultTrue)
                    continue;
                setValueById((uint32_t)queue->getParameterId(), value);
            }
        }

        // events -> midi::Buffer
        midiIn_.clear();
        if (data.inputEvents && (spec_.wantsMidi || spec_.isInstrument)) {
            int32 n = data.inputEvents->getEventCount();
            for (int32 i = 0; i < n; ++i) {
                Vst::Event e{};
                if (data.inputEvents->getEvent(i, e) != kResultOk)
                    continue;
                uint32_t frame = (uint32_t)std::max<int32>(0, e.sampleOffset);
                if (e.type == Vst::Event::kNoteOnEvent)
                    midiIn_.push_back(snd::midi::Message::noteOn(
                        (uint8_t)e.noteOn.channel, (uint8_t)e.noteOn.pitch,
                        (uint8_t)std::clamp((int)(e.noteOn.velocity * 127.0f), 1, 127),
                        frame));
                else if (e.type == Vst::Event::kNoteOffEvent)
                    midiIn_.push_back(snd::midi::Message::noteOff(
                        (uint8_t)e.noteOff.channel, (uint8_t)e.noteOff.pitch, frame));
            }
        }

        if (data.numSamples <= 0 || data.numOutputs < 1)
            return kResultOk;

        auto& outBus = data.outputs[0];
        if (outBus.numChannels < 2 || !outBus.channelBuffers32)
            return kResultOk;
        float* outs[2] = {outBus.channelBuffers32[0], outBus.channelBuffers32[1]};

        const float* ins[2] = {nullptr, nullptr};
        bool haveIn = false;
        if (!spec_.isInstrument && data.numInputs > 0 &&
            data.inputs[0].numChannels >= 2 && data.inputs[0].channelBuffers32) {
            ins[0] = data.inputs[0].channelBuffers32[0];
            ins[1] = data.inputs[0].channelBuffers32[1];
            haveIn = ins[0] && ins[1];
        }

        if (data.processContext) {
            Transport t;
            auto* ctx = data.processContext;
            if (ctx->state & Vst::ProcessContext::kTempoValid)
                t.tempoBpm = ctx->tempo;
            if (ctx->state & Vst::ProcessContext::kTimeSigValid) {
                t.timeSigNumerator = ctx->timeSigNumerator;
                t.timeSigDenominator = ctx->timeSigDenominator;
            }
            if (ctx->state & Vst::ProcessContext::kProjectTimeMusicValid)
                t.projectQuarterNotes = ctx->projectTimeMusic;
            if (ctx->state & Vst::ProcessContext::kBarPositionValid)
                t.barStartQuarterNotes = ctx->barPositionMusic;
            t.playing = (ctx->state & Vst::ProcessContext::kPlaying) != 0;
            proc_->_setTransport(t);
        }

        midiOut_.clear();
        proc_->process(haveIn ? ins : nullptr, outs, (uint32_t)data.numSamples,
                       midiIn_, midiOut_);

        if (data.outputEvents) {
            for (auto& m : midiOut_) {
                Vst::Event e{};
                e.busIndex = 0;
                e.sampleOffset = (int32)m.frame;
                if (m.isNoteOn()) {
                    e.type = Vst::Event::kNoteOnEvent;
                    e.noteOn.channel = (int16)m.channel();
                    e.noteOn.pitch = (int16)m.data1;
                    e.noteOn.velocity = (float)m.data2 / 127.0f;
                    e.noteOn.noteId = -1;
                } else if (m.isNoteOff()) {
                    e.type = Vst::Event::kNoteOffEvent;
                    e.noteOff.channel = (int16)m.channel();
                    e.noteOff.pitch = (int16)m.data1;
                    e.noteOff.noteId = -1;
                } else
                    continue;
                data.outputEvents->addEvent(e);
            }
        }
        return kResultOk;
    }

    // --- state: version, count, (id,value)*, extra blob ---------------------

    tresult PLUGIN_API getState(IBStream* state) override
    {
        if (!state)
            return kResultFalse;
        int32 version = 1;
        int32 count = (int32)ids_.size();
        state->write(&version, sizeof(version));
        state->write(&count, sizeof(count));
        for (size_t i = 0; i < ids_.size(); ++i) {
            uint32_t id = ids_[i];
            double v = values_[i].load(std::memory_order_relaxed);
            state->write(&id, sizeof(id));
            state->write(&v, sizeof(v));
        }
        std::vector<uint8_t> extra;
        proc_->saveExtra(extra);
        int32 extraLen = (int32)extra.size();
        state->write(&extraLen, sizeof(extraLen));
        if (extraLen > 0)
            state->write(extra.data(), extraLen);
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream* state) override
    {
        if (!state)
            return kResultFalse;
        int32 version = 0, count = 0;
        if (state->read(&version, sizeof(version)) != kResultOk || version != 1)
            return kResultFalse;
        state->read(&count, sizeof(count));
        for (int32 i = 0; i < count; ++i) {
            uint32_t id = 0;
            double v = 0;
            state->read(&id, sizeof(id));
            state->read(&v, sizeof(v));
            setValueById(id, v);
            SingleComponentEffect::setParamNormalized((Vst::ParamID)id, v);
        }
        int32 extraLen = 0;
        state->read(&extraLen, sizeof(extraLen));
        if (extraLen > 0) {
            std::vector<uint8_t> extra((size_t)extraLen);
            int32 got = 0;
            state->read(extra.data(), extraLen, &got);
            if (got == extraLen)
                proc_->loadExtra(extra.data(), extra.size());
        }
        return kResultOk;
    }

    // controller-side sets (host UI/automation lane) mirror into the store
    tresult PLUGIN_API setParamNormalized(Vst::ParamID tag,
                                          Vst::ParamValue value) override
    {
        setValueById((uint32_t)tag, value);
        return SingleComponentEffect::setParamNormalized(tag, value);
    }

    // --- editor --------------------------------------------------------------

    IPlugView* PLUGIN_API createView(FIDString name) override
    {
        if (!spec_.hasUi || !name || strcmp(name, Vst::ViewType::kEditor) != 0)
            return nullptr;
        return createEditorView(*proc_, *this, spec_.uiWidth, spec_.uiHeight,
                                spec_.uiResizable, spec_.uiMinWidth,
                                spec_.uiMinHeight);
    }

    // --- UiHost (editor writes go through the host for automation) ----------

    double get(uint32_t id) const override
    {
        for (size_t i = 0; i < ids_.size(); ++i)
            if (ids_[i] == id)
                return values_[i].load(std::memory_order_relaxed);
        return 0.0;
    }

    void set(uint32_t id, double v) override
    {
        v = std::clamp(v, 0.0, 1.0);
        beginEdit((Vst::ParamID)id);
        setParamNormalized((Vst::ParamID)id, v); // mirrors into the store too
        performEdit((Vst::ParamID)id, v);
        endEdit((Vst::ParamID)id);
    }

private:
    void setValueById(uint32_t id, double v)
    {
        for (size_t i = 0; i < ids_.size(); ++i)
            if (ids_[i] == id) {
                values_[i].store(std::clamp(v, 0.0, 1.0),
                                 std::memory_order_relaxed);
                return;
            }
    }

    const PluginSpec& spec_;
    std::unique_ptr<Processor> proc_;
    std::unique_ptr<std::atomic<double>[]> values_;
    std::vector<uint32_t> ids_;
    snd::midi::Buffer midiIn_;
    snd::midi::Buffer midiOut_;
    double sampleRate_ = 48000.0;
    uint32_t maxBlock_ = 0;
};

} // namespace

// --- runtime factory (what BEGIN_FACTORY_DEF would do with literals) --------

Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory()
{
    using namespace Steinberg;
    if (!gPluginFactory) {
        auto& spec = sndPluginSpec();

        PFactoryInfo factoryInfo(spec.vendor.c_str(), "", "",
                                 Vst::kDefaultFactoryFlags);
        gPluginFactory = new CPluginFactory(factoryInfo);

        FUID uid(spec.uid[0], spec.uid[1], spec.uid[2], spec.uid[3]);
        PClassInfo2 classInfo(uid, PClassInfo::kManyInstances, kVstAudioEffectClass,
                              spec.name.c_str(), Vst::kDistributable,
                              spec.subCategories.c_str(), nullptr,
                              spec.version.c_str(), kVstVersionString);
        gPluginFactory->registerClass(&classInfo, SndEffect::createInstance);
    } else
        gPluginFactory->addRef();
    return gPluginFactory;
}
