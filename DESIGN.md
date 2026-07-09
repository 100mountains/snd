# Design

Verified against: `fb0b8612c23253c3dc54fbddafd29e873a5a7f9c`.

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

**SND** is a small, source-available, in-house audio+UI foundation
library, in the shape of JUCE or iPlug2, built by wrapping
[miniaudio](https://github.com/mackron/miniaudio) (audio I/O + decoding) and
[Dear ImGui](https://github.com/ocornut/imgui) (UI) behind an original API in
`include/snd/`. It is free for noncommercial use under PolyForm
Noncommercial; commercial use requires a separate licence. Apps are built on
top of it rather than on JUCE.

SND is its own thing, independent of any specific app: decisions here are
driven by what makes SND a coherent, reusable foundation, not narrowed to one
downstream app's requirements. WaveBob and bob are consumers.

Current downstream status, owner-updated 2026-07-09: bob is the active
consumer, and its SND/GL surface is complete, including the Arrange and Perform
pages. Treat those pages as proven consumer pressure for SND's UI/plugin/MIDI
APIs, but keep product-specific behaviour in the downstream app rather than
folding it into the foundation.

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
merely depends on them. Building on these instead of JUCE means SND's own
licensing can be chosen by the project owner without dependency-driven copyleft
or fee obligations.

SND began separate from the legacy JUCE app, whose old tree remains read-only
reference material. The current bob surface now consumes SND through the GL UI
and plugin layers; SND itself remains a from-scratch foundation rather than a
forked retrofit of the old app.

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
why SND keeps device I/O, UI, plugin hosting, and plugin-client wrapping as
separable layers even though all of them now exist in-tree.

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

## UI Architecture

SND has one shared UI toolkit with two authoring models:

- **Immediate mode**: `snd::ui` in `include/snd/ui.h`, built on the existing
  Dear ImGui frame loop, native window shell, embedded icon fonts, SVG helpers,
  and SND audio widgets.
- **Retained mode**: `snd::ui::retained` in `include/snd/ui_retained.h`, a
  headless tree model for stable node IDs, parent/child lifetime, bounds,
  dirty state, focus traversal, hit testing, default event dispatch, custom
  event callbacks, layout, renderer-facing snapshots, and platform-neutral
  semantics. The user-facing name can settle independently; the C++ API stays
  in the retained namespace unless a separate API rename is approved.

These are not separate visual toolkits. Immediate and retained widgets should
share SND paint, style, icon, and semantic vocabulary. The shared draw-only
surface lives under `snd::ui::paint`; retained widgets should map their local
interaction state into those helpers rather than copying visual code.

The retained tree owns only UI-local state: node lifetime, layout bounds,
focus/pressed flags, hit-test state, dirty invalidation, and semantic metadata.
It must not own product model data, plugin parameters, DSP state, undo history,
or audio-thread mutation paths. Value controls use callback-backed
`ValueBinding` adapters so callers remain the owners of their state. When a
bound value changes outside retained event dispatch, callers can invoke
`refreshBoundValues()` on the UI thread to mark affected nodes dirty and refresh
binding-derived states such as toggle checked state.

The first retained layout model is deliberately small: stack, row, column,
padding, gap, alignment, fixed size, intrinsic size, and weighted fill. Do not
add grids, docking, or constraint solving until a real SND consumer proves the
need.

Canvas is the preferred retained primitive for direct-to-screen, custom-drawn,
or live animated regions. A Canvas node owns retained layout, focus, hit-test,
dirty, and semantic space while delegating the pixels inside its bounds to a
custom draw path or renderer style.

The retained layer exposes two snapshots. `nodeSnapshot()` is the
renderer/widget hook:
ordered visible nodes with bounds, role, value, derived states, and
hovered/pressed/focused/disabled interaction flags. `semanticSnapshot()` is the
accessibility hook: roles, names, descriptions, labelled-by relationships,
numeric/text values, states, actions, stable IDs, parent IDs, and bounds.
Custom retained widgets may provide semantics directly. Accessibility adapters
should use `semanticNode(id)`, `performSemanticAction(id, action)`, `value(id)`,
`setValue(id, value)`, `incrementValue(id)`, and `decrementValue(id)` rather
than reaching into renderer state. Custom widgets that advertise semantic
actions use `setOnAction(...)` to implement them. Hidden semantic nodes are not
operable by those adapter-facing calls. Toggle checked state is derived in the
core from live bindings or semantic numeric values, so renderers and
accessibility adapters read the same truth. `validate()` checks basic tree
identity invariants such as empty or duplicate node IDs so renderer and
accessibility adapters can fail early. Native accessibility adapters remain
private future work and must run on the UI/main thread. Retained UI is not
accessibility-complete until at least one native inspector or screen-reader
path is proven.

Dirty state currently means "tree/layout/semantics need repaint or resnapshot."
It is not yet an event-loop scheduling contract. Dirty-driven sleeping,
standalone accessibility objects, and plugin-hosted accessibility must be
designed separately so they do not disrupt host idle, timers, editor embedding,
or audio realtime constraints.

Practical UI authoring guidance belongs in `UI_PROGRAMMING_GUIDE.md`. Keep that
as the canonical guide filename unless the owner explicitly approves a rename.

## Plugin Hosting (VST3 + AU)

`snd::plugin` hosts VST3 and AU plugins, with editor GUIs in native windows.
Shape (from the audits in `docs/research/`):

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
  new truth; a stale queued change must not overwrite it.
- **VST3 backend** wraps the SDK's own hosting utilities (`Module`,
  `PlugProvider`, `HostProcessData`, `ParameterChanges`) rather than
  reimplementing them. Bus-arrangement calls always pass valid pointers even
  for zero buses (known plugin-crash pitfall). SND processes through bus 0;
  extra buses get silence.
- **AU backend** (macOS only, compiled out elsewhere) uses
  AudioToolbox/AudioComponent directly: registry discovery
  (`AudioComponentFindNext`), render-callback input feeding, state via
  `kAudioUnitProperty_ClassInfo` plists. AU v2 effects and instruments; AUv3
  async instantiation is future work.
- **Crash-loop protection**: out-of-process scanning (a child process per
  plugin, killed on a hard timeout) is the default; a dead-man's-pedal file
  (mark-before-load / clear-after-success / skip-if-still-marked) is the
  in-process fallback.
- **MIDI**: hosted plugins receive note/CC events and can emit them, via
  `snd::midi`. Host transport (tempo/bars) is supplied to the plugin.
- **Editor GUIs**: hosted in floating native windows per OS
  (NSView / HWND / X11), with an `IComponentHandler` for parameter edits and
  `IPlugFrame` for resize. The AU side uses the Cocoa/generic view tiers.
- The in-tree demo plugins (`test/plugins/demofilter`, `demosynth`) are SND's
  own VST3/AU builds, so selftest never depends on installed plugins.

## Why This Repo Stays Private

Deliberate, not an oversight: no obligation to keep an API stable for external
consumers, no docs/issue/PR overhead, and freedom to redesign as real usage
(WaveBob and bob) reveals what's actually needed. Going private → public is a
one-click decision that can be made anytime later; the
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

The `snd-example` binary has a `--selftest` mode that exercises the real audio
path headlessly and reports pass/fail with a matching process exit code:
decode+playback, capture, VST3 and AU hosting (parameter behaviour + stable-ID
state round-trip), and the plugin-client SDK. Verification lives in the app
itself as a runnable check, not a separate test framework or remote pipeline.
Every new capability must extend `--selftest` with a real check of the actual
behaviour, not just "did it compile."

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
- Keep audio I/O, UI, plugin hosting, and plugin-client wrapping separable, the
  way JUCE keeps device, GUI, hosting, and plugin-client concerns separate.
- No CI. Verification is `tools/build.sh` and `--selftest`, run by hand.
