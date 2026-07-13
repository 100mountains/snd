// snd::plugin -- hosting adapter: a scanned third-party plugin (VST3/AU) as a
// client::Processor, so it can live in a client-graph (bob::Graph) beside
// native modules.
//
// The bridge is thin because both sides already exist: host::Instance does
// the format work (audio, MIDI, params, state, native editor window), and
// client::Processor is what the graph drives. What this adds:
//
//   - audio/MIDI/transport/latency forwarding (planar float32 both sides)
//   - parameter bridge: the graph-side ParamStore's atomics (read via
//     Processor::param()) are pushed to the live host::Parameter* whenever
//     they change, from the audio thread (Parameter::setValue is queued +
//     thread-safe by contract). Plugin-side edits (the user turning knobs in
//     the plugin's own window) are readable via nativeParameterValue().
//   - saveExtra/loadExtra carry the plugin's FULL state blob
//     (Instance::saveState/loadState -- main thread, as the graph saves are)
//   - the editor is the plugin's NATIVE window, not an ImGui drawUi: the
//     spec reports hasUi = false, and the graph opens the real editor
//     through the NativeEditorHost side-interface below.
#pragma once

#include "snd/plugin_client.h"
#include "snd/plugin_host.h"

#include <memory>
#include <string>

namespace snd::plugin {

// Side-interface for graph nodes whose GUI is a native plugin window rather
// than an ImGui drawUi. A graph shell dynamic_casts the node's
// client::Processor* to this: non-null means "hosted plugin" -- route
// double-click to openNativeEditor() instead of the ImGui editor slot, and
// pump idle() from the UI frame loop.
class NativeEditorHost {
public:
    virtual ~NativeEditorHost() = default;

    virtual bool hasNativeEditor() = 0;
    // true if the editor is (already) open. Main thread only.
    virtual bool openNativeEditor(const std::string& windowTitle) = 0;
    virtual void closeNativeEditor() = 0;
    // The user closing the window is handled inside the host layer -- poll
    // this for the current state.
    virtual bool nativeEditorOpen() const = 0;

    // Pump once per UI frame while the node exists (flushes queued parameter
    // changes to the plugin's controller side; cheap when nothing is queued).
    virtual void idle() = 0;

    // The plugin's CURRENT normalized value for a bridged parameter (the
    // ParamSpec id from spec().params). This is how plugin-side edits -- the
    // user in the native editor -- flow back to graph-side knobs/state: read
    // it at idle time and mirror into the graph's param store.
    //
    // CONTRACT after loadExtra(): the loaded blob is the plugin's
    // authoritative full state, parameters included. Resync the graph's
    // param store from these values before the next audio block, or a stale
    // store value is indistinguishable from a fresh user edit and gets
    // pushed back into the plugin.
    virtual double nativeParameterValue(uint32_t paramId) const = 0;
};

// client::Processor over a host::Instance. Create via makeHostedProcessor()
// (or directly from an Instance in tests). The spec() lives inside and keeps
// a stable address for the processor's lifetime -- hand &spec() to the graph.
class HostedProcessor final : public client::Processor, public NativeEditorHost {
public:
    // Takes ownership of the instance. `desc` fills the spec's identity.
    HostedProcessor(std::unique_ptr<Instance> instance, Description desc);
    ~HostedProcessor() override;

    const client::PluginSpec& spec() const { return spec_; }
    const Description& description() const { return desc_; }

    // client::Processor
    void prepare(double sampleRate, uint32_t maxBlockFrames) override;
    void process(const float* const* in, float* const* out, uint32_t frames,
                 const midi::Buffer& midiIn, midi::Buffer& midiOut) override;
    void saveExtra(std::vector<uint8_t>& out) override;
    bool loadExtra(const uint8_t* data, size_t size) override;
    uint32_t latencySamples() const override;

    // NativeEditorHost
    bool hasNativeEditor() override;
    bool openNativeEditor(const std::string& windowTitle) override;
    void closeNativeEditor() override;
    bool nativeEditorOpen() const override;
    void idle() override;
    double nativeParameterValue(uint32_t paramId) const override;

    // Push graph-side normalized values into the hosted instance immediately.
    // Main-thread callers use this before opening an editor or saving state;
    // process() calls the same path before audio processing.
    void flushClientParameters();

private:
    struct BridgedParam {
        Parameter* param = nullptr;
        uint32_t clientId = 0;
        double lastSent = 0.0; // last value pushed host->plugin
    };

    std::unique_ptr<Instance> instance_;
    Description desc_;
    client::PluginSpec spec_;
    std::vector<BridgedParam> params_;
    double lastBpm_ = 0.0;
    int lastTsNum_ = 0;
    int lastTsDen_ = 0;
};

// Stable client-side id for a host parameter: its string id parsed as a
// number when it is one (VST3 ParamIDs and AU parameter IDs are numeric),
// else a deterministic FNV-1a hash of the string. Exposed for tests and for
// graph code that needs to map ids without an instance.
uint32_t hostedParamClientId(const std::string& hostParamId);

// Load `desc` through the manager and wrap it. Returns nullptr on failure
// (plugin refused to load); `error` gets a short reason when given.
std::unique_ptr<HostedProcessor> makeHostedProcessor(HostManager& manager,
                                                     const Description& desc,
                                                     std::string* error = nullptr);

} // namespace snd::plugin
