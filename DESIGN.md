# Design

This is the current high-level design for this project. It records architectural
decisions, ownership boundaries, and the technical reasons behind them.

This file is not a diary, task list, changelog, bug tracker, feature wishlist, or
place for temporary implementation notes. Add to it only when a decision affects
the structure of the app, the ownership of state, or the standard way a class of
features should be built.

This root design owns application-wide architecture: shared engine, timing,
persistence, and other non-module code. Only user-facing product modules should
have their own module `DESIGN.md` files (see `agents/software-architect.md` for
where those live). Do not create design documents merely because a source-code
folder exists. Practical implementation standards and how-to guidance live in
root `PROGRAMMING-GUIDE.md`.

## What This Repo Is

This is the seed of **SND** — a small, permissively-licensed, in-house audio+UI
foundation library, in the shape of JUCE or iPlug2, built by wrapping
[miniaudio](https://github.com/mackron/miniaudio) (audio I/O + decoding) and
[Dear ImGui](https://github.com/ocornut/imgui) (UI) behind an original API.
Snoredacity, and any future app, gets built on top of it rather than on JUCE.

Right now the repo is only a placeholder proving the foundation actually builds
and runs (`main.cpp` calls miniaudio and Dear ImGui directly — see `README.md`).
SND's own wrapper API (`include/snd/`) doesn't exist yet. This section describes
the intended direction, not current implementation status.

SND is being built as its own thing right now, independent of any specific app.
Snoredacity is the anticipated first consumer, not the thing currently being
worked on — decisions here should be driven by what makes SND a coherent,
reusable foundation, not narrowed to one downstream app's requirements.

## Why Not JUCE

JUCE is dual-licensed: free under AGPLv3 (which obligates any app built on it to
also ship as AGPLv3 open source), or a commercial license — the Indie tier is
$40/month or $800 one-time per developer, for companies under $500k/year
revenue. That's not expensive as commercial licenses go, but it's an obligation
this project doesn't need to take on, given the alternative: build on
dependencies that impose nothing.

miniaudio (by David Reid, aka `mackron` — also the author of the `dr_wav` /
`dr_flac` / `dr_mp3` single-header libraries) is dual-licensed public domain /
MIT-0. Dear ImGui is MIT. Neither requires attribution, permits unrestricted
commercial use and modification, and neither imposes anything on code that
merely depends on them. Building on these instead of JUCE means SND, and
anything built on SND, keeps full licensing freedom by construction.

This is unrelated to Murk, which keeps its own existing, working JUCE setup
untouched — SND is a separate, from-scratch effort, not a replacement being
retrofitted into a shipping app.

## Layering, Modeled On JUCE's Own Module Boundaries

JUCE is not one monolithic thing — it's organized into modules, and its
audio-device layer is architecturally separate from its plugin-hosting layer:

- `juce_audio_devices` — cross-platform device I/O (CoreAudio / WASAPI / ASIO /
  ALSA). This is JUCE's own equivalent of what miniaudio does.
- `juce_audio_basics` / `juce_audio_formats` — buffer types and file codecs.
  miniaudio's built-in decoders already cover much of this.
- `juce_audio_processors` + `juce_audio_plugin_client` — VST3/AU/LV2
  *hosting* (loading and running other people's plugins), and the separate
  glue that lets a processor *become* a plugin binary.
- `juce_gui_basics` / `juce_gui_extra` / `juce_graphics` / `juce_opengl` — the
  GUI layer.

A plain JUCE standalone app never touches the plugin-processor module at all,
and a plugin doesn't own the audio device (the host does, calling it back with
buffers). These are genuinely separable concerns, not entangled ones — which is
why SND can build the audio+UI foundation now without that work being
incompatible with adding plugin hosting later.

## SND's Own Layering (Intended)

- **Vendored, unmodified**: miniaudio, Dear ImGui, GLFW — pulled in via CMake
  `FetchContent`, never forked. This keeps upstream bugfixes flowing, and keeps
  code we didn't write clearly separate from code we did.
- **SND's own layer**: a thin, original API wrapping the above
  (`include/snd/audio.h`, `include/snd/ui.h` — names, types, and ergonomics are
  SND's, not miniaudio's or ImGui's verbatim surface). This is the part that's
  actually ours: fully ours to redesign, extend, or eventually relicense, since
  wrapping a permissively-licensed dependency creates no obligation back to it.

Forking and renaming miniaudio's own source was considered and rejected: the
license already permits using it freely as-is, so forking it would only mean
permanently maintaining a copy of someone else's audio-I/O code, with no
corresponding benefit.

## Plugin Hosting (VST3 + AU) — BUILT, headless v1

`snd::plugin` exists and passes selftest. Shape (from the audits in
`docs/research/`):

- `Format` (per-format base) / `HostManager` (registry + scanning) /
  `Instance` (unified across formats) / `Parameter` — mirrors JUCE's proven
  Format/FormatManager/AudioPluginInstance layering.
- **Stable parameter IDs only**: `Parameter::id()` is the VST3 `ParamID` /
  AU parameter ID as a string. Indices are never exposed — they shift between
  plugin versions and corrupt saved state.
- **Threading** (per the JUCE audit): a spin lock guards process(); parameter
  sets are queued — delivered to the processor at the next `process()` via
  `ParameterChanges` (sample-accurate) and to the controller at the next
  `idle()` on the main thread. Setup/state calls are main-thread-only.
  **State restore clears pending parameter queues** — restored state is the
  new truth; a stale queued change must not overwrite it (selftest caught
  exactly this on first run).
- **VST3 backend** wraps the SDK's own hosting utilities (`Module`,
  `PlugProvider`, `HostProcessData`, `ParameterChanges`) rather than
  reimplementing them. Bus-arrangement calls always pass valid pointers even
  for zero buses (known plugin-crash pitfall). SND processes through bus 0;
  extra buses get silence.
- **AU backend** (macOS only, compiled out elsewhere) uses
  AudioToolbox/AudioComponent directly: registry discovery
  (`AudioComponentFindNext`), render-callback input feeding, state via
  `kAudioUnitProperty_ClassInfo` plists. AU v2 effects only for now; AUv3
  async instantiation is future work.
- **Crash-loop protection**: dead-man's-pedal file around scanning
  (mark-before-load / clear-after-success / skip-if-still-marked). True
  out-of-process isolation remains a later hardening phase.
- **No MIDI** (owner decision): hosted plugins get an empty event list.
- **No editor-GUI embedding yet**: `Instance` has no window concept. When
  wanted, the per-OS embedding (NSView/HWND/X11) belongs in SND, and the AU
  side has three tiers to support (CocoaUI / RequestViewController /
  AUGenericView — see the JUCE audit).
- `TestGain` (test/plugins/testgain) is SND's own single-component VST3 gain
  plugin, built in-tree so selftest never depends on installed plugins.

## Formerly Deferred: Plugin Hosting (original rationale, kept for history)

Not needed for Snoredacity (VST hosting there is a "maybe," not a requirement),
and not attempted yet. When it's actually wanted:

- It's a separate, additive module, the same shape as JUCE's own
  `juce_audio_processors` — sitting alongside the audio-device layer rather
  than requiring it to be rebuilt.
- It's hard but charted territory, not blind research: JUCE's own source,
  Steinberg's published VST3 SDK examples, and existing prior art with more
  permissive licensing than JUCE are all real reference material —
  specifically **iPlug2** (supports AU/VST3/AUv3, GPU-accelerated graphics,
  already integrates Faust) and **DPF/DISTRHO** (ISC license, but explicitly
  lacks AU support, which rules it out for anything needing Apple's format).
- Building it is a real, multi-stage effort in its own right. Scope it as its
  own project when it's actually needed, rather than folding it into the
  audio+UI foundation work.

## Why This Repo Stays Private

Deliberate, not an oversight: no obligation to keep an API stable for external
consumers, no docs/issue/PR overhead, and freedom to redesign as real usage
(Snoredacity, and possibly Murk later) reveals what's actually needed. Going
private → public is a one-click decision that can be made anytime later; the
reverse isn't, so there's no cost to deferring that choice.

## Testing And Verification: No CI, Ever

This repo has no GitHub Actions workflow and none should be added. A prior
project's CI ran automatically far more often than intended and produced a
surprise bill before anyone noticed — private-repo Actions minutes are
metered, and it's easy for a workflow trigger to run far more often than
whoever set it up expected. SND avoids that risk entirely rather than trying
to configure around it: `tools/build.sh` is a local, manually-run, one-shot
script (configure, build, run `--selftest`) — nothing runs unless someone runs
it by hand, on their own machine, on purpose.

The app binary has a `--selftest` mode (see `main.cpp`) that exercises the
real audio path headlessly: decode-and-play a bundled test file, record
briefly from the input device, and report pass/fail with a matching process
exit code. This is the model going forward as SND grows — verification lives
in the app itself as a runnable check, not in a separate test framework or
remote pipeline. New capabilities should extend `--selftest` with a real
check of the actual behaviour (not just "did it compile"), the same way
Murk's own Debug build supports a `--selftest` flag for its own smoke checks.

`--selftest` now covers all four capabilities with behaviour checks:
decode+playback, capture, VST3 hosting (TestGain: RMS actually changes with
the gain parameter; state + stable-ID round-trip), AU hosting (AULowpass
attenuates an 8kHz sine). The selftest binary is `snd-example`.

## Fundamental Model

Three flows, kept separate:

1. **Audio flow** — device callbacks and plugin `process()` calls. Real-time
   rules apply: no allocation, no blocking locks, no I/O. Communication with
   the rest of the app is via atomics and spin-lock-guarded queues only.
2. **Control flow** — main-thread API calls (prepare/unprepare, parameter
   sets, `idle()`, state save/load, scanning). Parameter changes cross into
   the audio flow through queues, never direct calls.
3. **Persistence flow** — opaque state blobs (`saveState`/`loadState`) and
   stable parameter IDs. Restored state invalidates queued control changes.

## Current Design Biases

- Fix root causes, not symptoms.
- Keep the audio callback allocation-free and non-blocking.
- Remove dead code.
- Prefer the smallest change that solves the actual problem.
- Never fork a vendored dependency just to rename it — wrap it instead.
- Keep the audio-I/O layer and any future plugin-hosting layer separable, the
  way JUCE itself does.
- No CI. Verification is `tools/build.sh` and `--selftest`, run by hand.
