// snd::plugin -- headless hosting of third-party audio plugins (VST3 + AU).
//
// Shape mirrors the audited JUCE Format/FormatManager/unified-instance design
// (see docs/research/juce-hosting-audit.md). Headless-first: there is no
// editor-window concept in this API yet. No MIDI: hosted plugins receive an
// empty event list.
//
// Threading contract (matches both formats' specs):
//   - create/prepare/unprepare/saveState/loadState/idle: MAIN thread only
//   - process(): audio thread (or any single thread for offline/batch use)
//   - Parameter::setValue(): any thread; the value is queued, delivered to the
//     plugin's processor at the next process() and to its controller at the
//     next idle(). Call idle() periodically from the main thread.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace snd::plugin {

struct Description {
    std::string format;     // "VST3" or "AU"
    std::string identifier; // format-specific, stable; enough to re-create
    std::string path;       // file path (VST3); empty for registry formats (AU)
    std::string name;
    std::string vendor;
    std::string version;
    std::string category;
};

class Parameter {
public:
    virtual ~Parameter() = default;

    // Stable per-format ID (VST3 ParamID / AU parameter ID as a string).
    // Persist THIS, never the index -- indices shift between plugin versions.
    virtual const std::string& id() const = 0;
    virtual const std::string& name() const = 0;

    virtual double value() const = 0;        // normalized 0..1, cached
    virtual void setValue(double v) = 0;     // thread-safe, queued
    virtual bool automatable() const = 0;
};

class Instance {
public:
    virtual ~Instance() = default;

    virtual const Description& description() const = 0;

    // Main thread. Deinterleaved float32 processing at the given rate/block.
    virtual bool prepare(double sampleRate, uint32_t maxBlockFrames) = 0;
    virtual void unprepare() = 0;

    // Audio thread (or the batch thread). in/out are arrays of channel
    // pointers. Returns false on hard failure.
    virtual bool process(const float* const* in, uint32_t inChannels,
                         float* const* out, uint32_t outChannels,
                         uint32_t frames) = 0;

    // Main thread, periodically (~60Hz while interactive; once per file is
    // fine for batch). Flushes queued parameter changes to the plugin's
    // controller side.
    virtual void idle() = 0;

    virtual const std::vector<Parameter*>& parameters() const = 0;
    virtual Parameter* parameterById(const std::string& id) const = 0;

    // Main thread.
    virtual bool saveState(std::vector<uint8_t>& out) = 0;
    virtual bool loadState(const uint8_t* data, size_t size) = 0;
};

class Format {
public:
    virtual ~Format() = default;

    virtual const char* name() const = 0;

    // Where this format's plugins live on this OS. Empty for registry-based
    // formats (AU), which enumerate through scan("") instead.
    virtual std::vector<std::string> defaultSearchPaths() const = 0;

    // Cheap check: extension/existence only, no loading.
    virtual bool fileMightBePlugin(const std::string& path) const = 0;

    // Enumerate plugins in a file (may load it -- can be slow, can crash on a
    // broken plugin; the manager wraps this with the dead-man's-pedal file).
    // For registry formats, scan("") enumerates everything installed.
    virtual std::vector<Description> scan(const std::string& path) = 0;

    virtual std::unique_ptr<Instance> create(const Description& desc) = 0;
};

class HostManager {
public:
    HostManager();
    ~HostManager();

    void addFormat(std::unique_ptr<Format> format);
    void addDefaultFormats(); // VST3 everywhere; AU on macOS
    const std::vector<std::unique_ptr<Format>>& formats() const;

    // Crash-loop prevention (JUCE's "dead man's pedal"): each plugin file is
    // recorded here before scanning and cleared after success. Files still
    // present from a previous run are skipped and reported as blacklisted.
    void setDeadMansPedalFile(const std::string& path);

    struct ScanResult {
        std::vector<Description> plugins;
        std::vector<std::string> blacklisted; // skipped: crashed a previous scan
        std::vector<std::string> failed;      // wouldn't load/enumerate
    };
    ScanResult scanAll(const std::vector<std::string>& extraPaths = {});

    std::unique_ptr<Instance> create(const Description& desc);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace snd::plugin
