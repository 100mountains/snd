// snd::plugin::client -- write a plugin ONCE against this API and ship it as
// VST3 and a standalone app (AU wrapping comes via the VST3 SDK's auwrapper).
// The licensing is the point: no JUCE, no fees -- the VST3 SDK permits free
// closed-source distribution and everything else here is SND's own code.
//
// A plugin is three things:
//   1. a PluginSpec (identity + parameters, all with STABLE ids)
//   2. a Processor subclass (audio/MIDI, optional ImGui editor via drawUi)
//   3. two functions the wrappers find by name:
//        const snd::plugin::client::PluginSpec& sndPluginSpec();
//        std::unique_ptr<snd::plugin::client::Processor> sndCreateProcessor();
//
// Build with the snd_add_plugin() CMake helper. See
// test/plugins/demofilter/ for the complete pattern.
#pragma once

#include "snd/midi.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace snd::plugin::client {

struct ParamSpec {
    uint32_t id = 0; // stable forever: never reuse or renumber
    std::string name;
    double defaultValue = 0.0; // normalized 0..1
    bool automatable = true;
};

struct PluginSpec {
    std::string name;
    std::string vendor;
    std::string version = "1.0.0";
    std::string subCategories = "Fx"; // VST3 subcategory string, e.g. "Fx|Filter"
    uint32_t uid[4] = {0, 0, 0, 0};   // unique per plugin; pick once, keep forever
    bool isInstrument = false;        // no audio input; MIDI drives it
    bool wantsMidi = false;           // receive note events in process()
    bool hasUi = false;               // drawUi() is implemented
    bool uiResizable = false;         // host may resize the editor
    int uiWidth = 480, uiHeight = 320;
    int uiMinWidth = 200, uiMinHeight = 120; // when resizable
    std::vector<ParamSpec> params;
};

// Musical time as the host reports it, refreshed every process() call.
struct Transport {
    double tempoBpm = 120.0;
    int timeSigNumerator = 4, timeSigDenominator = 4;
    double projectQuarterNotes = 0.0; // song position in quarter notes
    double barStartQuarterNotes = 0.0;
    bool playing = false;
};

// Handed to drawUi(): parameter access that keeps the HOST in the loop
// (edits go through begin/perform/endEdit so automation records them).
class UiHost {
public:
    virtual ~UiHost() = default;
    virtual double get(uint32_t id) const = 0;
    virtual void set(uint32_t id, double normalized) = 0;
};

class Processor {
public:
    virtual ~Processor() = default;

    virtual void prepare(double sampleRate, uint32_t maxBlockFrames)
    {
        (void)sampleRate;
        (void)maxBlockFrames;
    }
    virtual void reset() {}

    // Audio thread. `in` is stereo channel pointers (nullptr for
    // instruments), `out` is stereo. `midiIn` carries this block's events
    // when the spec asked for them; anything appended to `midiOut` is
    // delivered to the host as note events.
    virtual void process(const float* const* in, float* const* out,
                         uint32_t frames, const midi::Buffer& midiIn,
                         midi::Buffer& midiOut) = 0;

    // Optional state beyond parameters.
    virtual void saveExtra(std::vector<uint8_t>&) {}
    virtual bool loadExtra(const uint8_t*, size_t) { return true; }

    // Processing latency in samples, for host delay compensation. Queried
    // after prepare() (on activation); dynamic changes mid-flight are not
    // signalled yet.
    virtual uint32_t latencySamples() const { return 0; }

    // ImGui editor body, called per frame while the editor is open (only
    // when spec.hasUi). Draw with ImGui:: / snd::ui::; the surrounding
    // window, GL context and input routing are the wrapper's job.
    virtual void drawUi(UiHost&) {}

    // Host transport, valid inside process() (tempo LFOs, synced delays).
    const Transport& transport() const { return transport_; }

    // wrapper wiring -- not for plugin code
    void _setTransport(const Transport& t) { transport_ = t; }

    // RT-safe normalized parameter read, usable inside process().
    double param(uint32_t id) const
    {
        for (size_t i = 0; i < paramCount_; ++i)
            if (paramIds_[i] == id)
                return paramValues_[i].load(std::memory_order_relaxed);
        return 0.0;
    }

    // wrapper wiring -- not for plugin code
    void _bindParams(const std::atomic<double>* values, const uint32_t* ids,
                     size_t count)
    {
        paramValues_ = values;
        paramIds_ = ids;
        paramCount_ = count;
    }

private:
    const std::atomic<double>* paramValues_ = nullptr;
    const uint32_t* paramIds_ = nullptr;
    size_t paramCount_ = 0;
    Transport transport_;
};

} // namespace snd::plugin::client
