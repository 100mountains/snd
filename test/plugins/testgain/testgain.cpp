// TestGain -- SND's minimal known-good VST3 plugin, used by --selftest so
// hosting tests never depend on what happens to be installed on a machine.
// Stereo in/out, one automatable Gain parameter (linear, default 1.0).
// Single-component on purpose: exercises the host's single-component path.

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>

using namespace Steinberg;
using namespace Steinberg::Vst;

enum { kGainId = 100 };

class TestGain : public SingleComponentEffect {
public:
    static FUnknown* createInstance(void*)
    {
        return (IAudioProcessor*)new TestGain();
    }

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE
    {
        tresult result = SingleComponentEffect::initialize(context);
        if (result != kResultOk)
            return result;

        addAudioInput(STR16("Input"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Output"), SpeakerArr::kStereo);

        parameters.addParameter(STR16("Gain"), nullptr, 0, 1.0,
                                ParameterInfo::kCanAutomate, kGainId);
        return kResultOk;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE
    {
        return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                          SpeakerArrangement* outputs, int32 numOuts) SMTG_OVERRIDE
    {
        if (numIns == 1 && numOuts == 1 && inputs[0] == outputs[0])
            return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
        return kResultFalse;
    }

    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE
    {
        if (data.inputParameterChanges) {
            int32 numParams = data.inputParameterChanges->getParameterCount();
            for (int32 i = 0; i < numParams; ++i) {
                if (auto* queue = data.inputParameterChanges->getParameterData(i)) {
                    if (queue->getParameterId() == kGainId) {
                        int32 points = queue->getPointCount();
                        ParamValue value;
                        int32 offset;
                        if (points > 0 &&
                            queue->getPoint(points - 1, offset, value) == kResultTrue)
                            gain = value;
                    }
                }
            }
        }

        if (data.numSamples <= 0 || data.numInputs == 0 || data.numOutputs == 0)
            return kResultOk;

        auto& in = data.inputs[0];
        auto& out = data.outputs[0];
        int32 channels = std::min(in.numChannels, out.numChannels);

        for (int32 c = 0; c < channels; ++c) {
            float* src = in.channelBuffers32[c];
            float* dst = out.channelBuffers32[c];
            if (!src || !dst)
                continue;
            for (int32 s = 0; s < data.numSamples; ++s)
                dst[s] = src[s] * (float)gain;
        }
        out.silenceFlags = 0;
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE
    {
        if (!state)
            return kResultFalse;
        double g = gain;
        int32 written = 0;
        state->write(&g, sizeof(g), &written);
        return written == sizeof(g) ? kResultOk : kResultFalse;
    }

    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE
    {
        if (!state)
            return kResultFalse;
        double g = 1.0;
        int32 read = 0;
        state->read(&g, sizeof(g), &read);
        if (read != sizeof(g))
            return kResultFalse;
        gain = g;
        setParamNormalized(kGainId, g);
        return kResultOk;
    }

    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) SMTG_OVERRIDE
    {
        if (id == kGainId)
            gain = value;
        return SingleComponentEffect::setParamNormalized(id, value);
    }

private:
    ParamValue gain = 1.0;
};

static const FUID kTestGainUID(0x5EDA7E57, 0x0A1B2C3D, 0x4E5F6071, 0x82934A5B);

bool InitModule() { return true; }
bool DeinitModule() { return true; }

BEGIN_FACTORY_DEF("SND", "https://github.com/100mountains/snd", "mailto:none@invalid")

DEF_CLASS2(INLINE_UID_FROM_FUID(kTestGainUID),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           "SND TestGain",
           0,
           Vst::PlugType::kFx,
           "0.1.0",
           kVstVersionString,
           TestGain::createInstance)

END_FACTORY
