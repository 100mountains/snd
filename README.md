# SND

**A JUCE/iPlug replacement attempt.** SND is a small, permissively-licensed,
in-house audio+UI foundation library — cross-platform audio I/O and a GUI,
without JUCE's AGPLv3-or-pay licensing. It wraps
[miniaudio](https://github.com/mackron/miniaudio) (audio device I/O +
decoding, public domain / MIT-0) and [Dear ImGui](https://github.com/ocornut/imgui)
(UI, MIT) behind an original API. Snoredacity — and any future app — builds on
SND instead of on JUCE.

This repo's folder/remote is still named `app-template`; SND is the name of
what it's becoming.

## Why this exists

JUCE solves cross-platform audio + UI + (optionally) VST/AU plugin hosting
well, but it ties whatever you build to AGPLv3 or a paid commercial license.
SND exists to get the same job done — cross-platform audio I/O, a UI layer,
and eventually plugin hosting if it's ever needed — on foundations that impose
nothing: miniaudio and Dear ImGui are both about as permissively licensed as
software gets. See `DESIGN.md` for the full reasoning, including why plugin
hosting is deliberately not attempted yet, and how JUCE's own module
boundaries (audio-device layer separate from plugin-hosting layer) inform how
SND is laid out.

This is unrelated to Murk, which keeps its existing JUCE setup untouched.

## Status

Early — this repo currently proves the foundation actually builds and runs:
one placeholder binary that opens an audio device and a blank window, calling
miniaudio and Dear ImGui directly. SND's own wrapper API doesn't exist yet.

## What's vendored vs. original

- **Vendored, unmodified** (never forked, pulled in via CMake `FetchContent`):
  miniaudio, Dear ImGui, GLFW.
- **Original** (SND's own code, once it exists): everything under `include/snd/`
  and `src/` — a thin API wrapping the above with SND's own names and
  ergonomics, not a copy of miniaudio's or ImGui's own surface.

## Build

```sh
cmake -S . -B build
cmake --build build
./build/app-template        # path varies by generator/platform
```

## Dev workflow

See `AGENTS.md` for the multi-agent dev workflow (genericized from Murk's) that applies to work in this repo.
