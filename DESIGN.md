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

## Explicitly Deferred: Plugin Hosting (VST3/AU)

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

## Fundamental Model

Not yet established beyond the vendored/original split above. Once SND has
real behaviour, this section should describe its distinct flows (audio
device/graph, UI/interaction, persistence) and state plainly that they stay
separate — matching the same discipline JUCE and miniaudio already apply.

## Current Design Biases

- Fix root causes, not symptoms.
- Keep the audio callback allocation-free and non-blocking.
- Remove dead code.
- Prefer the smallest change that solves the actual problem.
- Never fork a vendored dependency just to rename it — wrap it instead.
- Keep the audio-I/O layer and any future plugin-hosting layer separable, the
  way JUCE itself does.
