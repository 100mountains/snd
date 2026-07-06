# SND Build Plan

SND is the backbone library for all apps that follow: audio I/O, UI, plugin
hosting, DSP, and every piece of platform-specific glue live in SND behind one
API. Apps built on it contain no `#ifdef __APPLE__`-style code of their own.
Headless operation is a first-class requirement throughout, not a retrofit.
No MIDI anywhere.

Module map (target shape):

| Module | Contents | Status |
|---|---|---|
| `snd::audio` | device I/O, file decode/encode (miniaudio underneath) | **built** (Device/CaptureDevice/Player/load/saveWav) |
| `snd::ui` | window/GL/ImGui shell, custom widget layer (gradients etc.) | **built** (Window shell, gradientPanel/gradientButton) |
| `snd::plugin` | VST3 + AU hosting, headless-first | **built** (phases 1+2 done, selftest passing) |
| `snd::dsp` | FFT, spectral tools (needed for spectral editing) | later — check Murk's existing FFT code first |
| `snd::platform` | native file dialogs, paths, OS packaging glue | later |

Reference material: `docs/research/` (JUCE hosting audit = architecture to
mirror and pitfalls to avoid; VST3 SDK audit = the hosting utilities we build
on).

## Phase 1 — VST3 hosting (headless)

1. Vendor the VST3 SDK via `FetchContent`, pinned tag, unmodified — same
   pattern as miniaudio/imgui/glfw. Reuse its hosting utilities
   (`Module`, `PlugProvider`, `HostProcessData`, `ParameterChanges`,
   `HostApplication`) rather than reimplementing them.
2. Public API in `include/snd/plugin_host.h`:
   - `snd::PluginFormat` (base) / `snd::PluginHostManager` (registry) —
     mirrors JUCE's proven Format/FormatManager shape
   - `snd::PluginDescription` (scan result)
   - `snd::PluginInstance` — unified across formats: `prepare()`, `process()`,
     parameter list, state save/load, no editor-window concept in v1
   - `snd::PluginParameter` — keyed by **stable per-format parameter ID**,
     never by index (indices shift between plugin versions and corrupt saved
     state)
3. VST3 backend in `src/plugin_host/vst3/`:
   - scanning: per-OS default search paths inside the backend; every `.vst3`'s
     factory must be loaded and iterated (no cheaper shell detection exists)
   - instantiation: handle single-component and dual-component plugins;
     connect component↔controller on one thread only
   - processing: spin lock guards audio-thread access; parameter changes queue
     and flush to the plugin at ~60 Hz on the message thread — the audio
     thread never calls parameter APIs directly
   - setup/teardown/state: message-thread only (spec requirement; plugins
     crash otherwise)
   - hardening from the JUCE audit baked in from the start: never pass a null
     bus-arrangement pointer (dummy zero arrangement instead); re-`prepare()`
     process data on any sample-rate/block-size change; tolerate plugins that
     request restarts from worker threads; no bypass parameter guaranteed
   - crash safety: dead-man's-pedal scan file (mark before load, clear after
     success, blacklist on crash). Out-of-process isolation is a later
     hardening phase.
4. Test plugin: build one minimal known-good VST3 (adapted from the SDK's own
   samples) that SND controls, so tests never depend on what's installed.
5. Extend `--selftest`: load the test plugin, process audio through it, verify
   the output actually changed, save/restore state, confirm a parameter
   round-trips by stable ID. Replaces the current "skipped" VST line.

## Phase 2 — AU hosting (macOS only)

6. AU backend in `src/plugin_host/au/`, compiled out on Windows/Linux, using
   AudioToolbox/AudioComponent directly (no third-party SDK):
   - discovery via the OS component registry (`AudioComponentFindNext`), not
     directory scanning
   - AU v2 instantiates synchronously; AUv3 must instantiate async — the
     manager handles both paths
   - same `snd::PluginInstance` surface as VST3; state via the AU state APIs
7. `--selftest` AU check using a built-in Apple unit (e.g. an Apple-supplied
   effect), so no custom AU needs building.

## Phase 3 — SND wrapper API for audio + UI

8. Promote the placeholder into the real library: `include/snd/audio.h`
   (device open/close, decode, playback/capture) and `include/snd/ui.h`
   (window/frame lifecycle, widget helpers) wrapping miniaudio/ImGui with
   SND's own names. `main.cpp` becomes an example app consuming only `snd::`
   APIs. CMake project/targets renamed from `app-template` to `snd`.
9. Custom widget layer starts here (gradient panels etc., drawn via
   `ImDrawList` on top of stock ImGui — ImGui itself stays unmodified).

## Phase 4 — the wave editor app (own repo, first SND consumer)

Two-mode design:
- **Organize mode**: whole-window file browser/manager for WAV libraries —
  batch cut-to-zero-crossing across files (drum-sample prep), batch rename,
  tagging/foldering. Hover shows how far behind a snapped start point the
  original was.
- **Edit mode**: waveform-dominant window, right-click tools, readouts;
  the Audacity-derived function set (cut/trim/fade/normalize, sync-lock
  style multi-track alignment where relevant); spectral editing once
  `snd::dsp` exists.
- **Headless batch**: run a plugin chain over many files from the CLI using
  `snd::plugin` — no GUI needed.

Editor feature list gets its own planning pass when phase 4 starts; the
Audacity function map from the earlier research is the seed.

## Later / hardening

- Out-of-process plugin scanning and (maybe) hosting isolation
- Plugin editor-window embedding (NSView/HWND/X11) inside `snd::ui`
- `snd::dsp`: FFT choice (check Murk's FFT provenance first — if it's
  JUCE `juce_dsp` it can't be reused; if Faust-generated or original, it can),
  spectral edit operations
- `snd::platform`: native file dialogs (NFD or equivalent), standard paths
- macOS packaging: hosting third-party plugins under the hardened runtime
  needs the library-validation entitlement — SND documents/owns this, not
  each app

## Verification (every phase)

- `tools/build.sh` — one-shot local build + `--selftest`, run by hand. No CI.
- `--selftest` grows a real check per capability: behaviour verified (output
  changed, state round-tripped), not just "it compiled".
- Murk is reference-only: `git -C ~/Documents/murk status` stays untouched by
  any SND work.
