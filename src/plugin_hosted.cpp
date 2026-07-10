#include "snd/plugin_hosted.h"

#include <algorithm>
#include <cstdlib>

namespace snd::plugin {

namespace {

bool numericId(const std::string& s, uint32_t& out)
{
    if (s.empty())
        return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size())
        return false;
    out = (uint32_t)v;
    return true;
}

uint32_t fnv1a(const std::string& s)
{
    uint32_t h = 2166136261u;
    for (const char c : s) {
        h ^= (uint8_t)c;
        h *= 16777619u;
    }
    return h;
}

} // namespace

uint32_t hostedParamClientId(const std::string& hostParamId)
{
    uint32_t numeric = 0;
    if (numericId(hostParamId, numeric))
        return numeric;
    return fnv1a(hostParamId);
}

HostedProcessor::HostedProcessor(std::unique_ptr<Instance> instance,
                                 Description desc)
    : instance_(std::move(instance)), desc_(std::move(desc))
{
    spec_.name = desc_.name;
    spec_.vendor = desc_.vendor;
    spec_.version = desc_.version.empty() ? "1.0.0" : desc_.version;
    spec_.subCategories = desc_.category;
    // VST3 category strings carry "Instrument"; the AU format maps aumu/aumf
    // the same way when it fills Description::category.
    spec_.isInstrument = desc_.category.find("Instrument") != std::string::npos;
    spec_.wantsMidi = true; // Instance::processMidi ignores it for effect-only
    spec_.inputChannels = spec_.isInstrument ? 0 : 2;
    spec_.outputChannels = 2;
    spec_.hasUi = false; // the editor is the plugin's NATIVE window
    // uid: deterministic from the format identifier (wrappers use it; the
    // in-process graph doesn't)
    spec_.uid[0] = fnv1a(desc_.identifier);
    spec_.uid[1] = fnv1a(desc_.format + desc_.identifier);
    spec_.uid[2] = fnv1a(desc_.name + desc_.identifier);
    spec_.uid[3] = fnv1a(desc_.identifier + desc_.name);

    if (instance_) {
        const auto& params = instance_->parameters();
        spec_.params.reserve(params.size());
        params_.reserve(params.size());
        for (Parameter* p : params) {
            if (!p)
                continue;
            client::ParamSpec ps;
            ps.id = hostedParamClientId(p->id());
            ps.name = p->name();
            ps.defaultValue = p->value(); // current value at load = the default the graph starts from
            ps.automatable = p->automatable();
            spec_.params.push_back(std::move(ps));
            params_.push_back({p, hostedParamClientId(p->id()), p->value()});
        }
    }
}

HostedProcessor::~HostedProcessor()
{
    if (instance_) {
        instance_->closeEditor();
        instance_->unprepare();
    }
}

void HostedProcessor::prepare(double sampleRate, uint32_t maxBlockFrames)
{
    if (instance_)
        instance_->prepare(sampleRate, maxBlockFrames);
}

void HostedProcessor::process(const float* const* in, float* const* out,
                              uint32_t frames, const midi::Buffer& midiIn,
                              midi::Buffer& midiOut)
{
    const uint32_t outCh = (uint32_t)std::max(0, spec_.outputChannels);
    const auto zeroOut = [&] {
        for (uint32_t c = 0; c < outCh; ++c)
            if (out && out[c])
                std::fill(out[c], out[c] + frames, 0.0f);
    };
    if (!instance_) {
        zeroOut();
        return;
    }

    // graph-side edits -> plugin: push only real changes (setValue is queued
    // + thread-safe by the host contract, so this is fine on the audio
    // thread; unchanged params cost one atomic read each)
    for (BridgedParam& bp : params_) {
        const double v = param(bp.clientId);
        if (v != bp.lastSent) {
            bp.lastSent = v;
            bp.param->setValue(v);
        }
    }

    const client::Transport& t = transport();
    if (t.tempoBpm != lastBpm_ || t.timeSigNumerator != lastTsNum_ ||
        t.timeSigDenominator != lastTsDen_) {
        lastBpm_ = t.tempoBpm;
        lastTsNum_ = t.timeSigNumerator;
        lastTsDen_ = t.timeSigDenominator;
        instance_->setTransport(t.tempoBpm, t.timeSigNumerator,
                                t.timeSigDenominator);
    }

    const uint32_t inCh =
        spec_.isInstrument ? 0u : (uint32_t)std::max(0, spec_.inputChannels);
    if (!instance_->processMidi(in, inCh, out, outCh, frames, midiIn,
                                &midiOut))
        zeroOut();
}

void HostedProcessor::saveExtra(std::vector<uint8_t>& out)
{
    if (!instance_)
        return;
    std::vector<uint8_t> state;
    if (instance_->saveState(state))
        out.insert(out.end(), state.begin(), state.end());
}

bool HostedProcessor::loadExtra(const uint8_t* data, size_t size)
{
    if (!instance_)
        return false;
    if (!instance_->loadState(data, size))
        return false;
    // the blob replaced the plugin's parameter values; refresh what "last
    // sent" means so the next process() doesn't stomp them with stale atomics
    for (BridgedParam& bp : params_)
        bp.lastSent = bp.param->value();
    return true;
}

uint32_t HostedProcessor::latencySamples() const
{
    return instance_ ? instance_->latencySamples() : 0;
}

bool HostedProcessor::hasNativeEditor()
{
    return instance_ && instance_->hasEditor();
}

bool HostedProcessor::openNativeEditor(const std::string& windowTitle)
{
    return instance_ && instance_->openEditor(windowTitle);
}

void HostedProcessor::closeNativeEditor()
{
    if (instance_)
        instance_->closeEditor();
}

bool HostedProcessor::nativeEditorOpen() const
{
    return instance_ && instance_->editorOpen();
}

void HostedProcessor::idle()
{
    if (instance_)
        instance_->idle();
}

double HostedProcessor::nativeParameterValue(uint32_t paramId) const
{
    for (const BridgedParam& bp : params_)
        if (bp.clientId == paramId)
            return bp.param->value();
    return 0.0;
}

std::unique_ptr<HostedProcessor> makeHostedProcessor(HostManager& manager,
                                                     const Description& desc,
                                                     std::string* error)
{
    std::unique_ptr<Instance> instance = manager.create(desc);
    if (!instance) {
        if (error)
            *error = desc.format + " plugin '" + desc.name +
                     "' failed to load (" + desc.identifier + ")";
        return nullptr;
    }
    return std::make_unique<HostedProcessor>(std::move(instance), desc);
}

} // namespace snd::plugin
