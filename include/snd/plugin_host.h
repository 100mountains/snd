// snd::plugin -- hosting of third-party audio plugins (VST3 + AU).
//
// Shape mirrors the audited JUCE Format/FormatManager/unified-instance design
// (see docs/research/juce-hosting-audit.md). Instances expose audio, MIDI,
// state, latency, transport, parameters, and optional native editor windows
// through one format-neutral API.
//
// Threading contract (matches both formats' specs):
//   - create/prepare/unprepare/saveState/loadState/idle: MAIN thread only
//   - process(): audio thread (or any single thread for offline/batch use)
//   - Parameter::setValue(): any thread; the value is queued, delivered to the
//     plugin's processor at the next process() and to its controller at the
//     next idle(). Call idle() periodically from the main thread.
#pragma once

#include "snd/control.h"
#include "snd/midi.h"

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

    // Same, with MIDI: midiIn events (frame offsets within this block) are
    // delivered to the plugin; events the plugin emits land in midiOut when
    // given. Default ignores MIDI, so effect-only instances need not care.
    virtual bool processMidi(const float* const* in, uint32_t inChannels,
                             float* const* out, uint32_t outChannels,
                             uint32_t frames, const midi::Buffer& midiIn,
                             midi::Buffer* midiOut = nullptr)
    {
        (void)midiIn;
        (void)midiOut;
        return process(in, inChannels, out, outChannels, frames);
    }

    // In-process graph path. Hosted formats use the MIDI path and ignore the
    // Bob/SND control bus; native graph instances may consume and emit both.
    virtual bool processEvents(const float* const* in, uint32_t inChannels,
                               float* const* out, uint32_t outChannels,
                               uint32_t frames, const midi::Buffer& midiIn,
                               midi::Buffer* midiOut,
                               const control::Buffer& controlIn,
                               control::Buffer* controlOut)
    {
        (void)controlIn;
        (void)controlOut;
        return processMidi(in, inChannels, out, outChannels, frames, midiIn,
                           midiOut);
    }

    // Main thread, periodically (~60Hz while interactive; once per file is
    // fine for batch). Flushes queued parameter changes to the plugin's
    // controller side.
    virtual void idle() = 0;

    // Samples of delay this plugin adds (valid after prepare()). Hosts use
    // it to line up parallel paths and offset bounces.
    virtual uint32_t latencySamples() const { return 0; }

    // Musical transport for plugins that sync to host time (tempo LFOs,
    // synced delays). Applies from the next process() call; any thread.
    virtual void setTransport(double tempoBpm, int timeSigNumerator,
                              int timeSigDenominator)
    {
        (void)tempoBpm;
        (void)timeSigNumerator;
        (void)timeSigDenominator;
    }

    virtual const std::vector<Parameter*>& parameters() const = 0;
    virtual Parameter* parameterById(const std::string& id) const = 0;

    // Main thread.
    virtual bool saveState(std::vector<uint8_t>& out) = 0;
    virtual bool loadState(const uint8_t* data, size_t size) = 0;

    // --- The plugin's own editor GUI ---
    // Opens in a floating native window when the format/plugin supplies one.
    // Main thread only. The user closing the window is handled internally --
    // poll editorOpen() for the current state. The editor is closed
    // automatically when the instance is destroyed. Call idle() each frame
    // while an editor is open so parameter changes flow both ways.
    // AU: every plugin has an editor (its own Cocoa view when it ships one,
    // the system generic parameter view otherwise).
    virtual bool hasEditor() = 0;
    virtual bool openEditor(const std::string& windowTitle) = 0; // true if (already) open
    virtual void closeEditor() = 0;
    virtual bool editorOpen() const = 0;
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

// --- Out-of-process scanning ---
// Enumerating a plugin file loads its binary, and a broken plugin can crash
// or hang the whole process. The fix: re-run the app's own executable as a
// one-shot scan worker per plugin. A crash or hang takes down only that
// throwaway child (hangs are killed after a timeout), the plugin simply
// yields no types this scan, the host never dies, and nothing is ever
// permanently blacklisted.
//
// Wiring (two lines in the app):
//   1. at the very top of main(), before any GUI/audio init:
//        if (argc == 5 && std::string(argv[1]) == "--myapp-scan-plugin")
//            return snd::plugin::runScanWorker(argv[2], argv[3], argv[4]);
//   2. before scanning:
//        manager.setScanWorker(snd::platform::executablePath(),
//                              "--myapp-scan-plugin");
//
// Child command line: <exe> <flag> <format-name> <identifier> <out-file>

// CHILD side: enumerate one plugin and write its descriptions to outFile
// (written even when empty). Returns the process exit code.
int runScanWorker(const std::string& formatName, const std::string& identifier,
                  const std::string& outFile);

// PARENT side: enumerate one plugin file in a throwaway child process.
// Empty result = nothing found / crashed / hung / failed to launch; the
// calling process is unharmed in every case.
std::vector<Description> scanViaWorker(const std::string& workerExe,
                                       const std::string& workerFlag,
                                       const std::string& formatName,
                                       const std::string& fileOrIdentifier,
                                       uint32_t timeoutMs = 30000);

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
    // Only used for in-process scans; superseded by setScanWorker().
    void setDeadMansPedalFile(const std::string& path);

    // Route scanAll()'s per-file enumeration through scanViaWorker() (see
    // above). Registry formats (AU) stay in-process: their enumeration reads
    // the OS component registry and never loads plugin code.
    void setScanWorker(const std::string& exePath, const std::string& flag);
    void setScanWorkerTimeout(uint32_t milliseconds); // default 30000 per plugin

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
