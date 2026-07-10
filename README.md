# SND

**A source-available JUCE/iPlug replacement.** SND is a small, in-house
audio+UI foundation library — cross-platform audio I/O, a GUI shell, plugin
hosting, and a write-once plugin SDK. Apps build on SND instead
of on JUCE. Free for any noncommercial use; commercial use needs a licence —
see [License](#license).

## Modules

- `snd::audio` — device playback/capture, file decode (wav/aiff/flac/mp3/ogg),
  streaming reads, encoders (FLAC, MP3), resampling, a gapless `Player`
- `snd::ui` — native window + Dear ImGui frame shell (GLFW/OpenGL underneath),
  frameless windows with app-drawn title bars, multi-window, file
  drag-and-drop, and a themed audio widget set (knobs, meters, faders,
  keyboard, pattern grid, envelope editor, graph surface, menus, …) in both
  immediate and retained modes. The retained tree adds layout, keyboard
  focus, and accessibility semantics; custom painter hooks re-skin controls
  without forking behaviour. See `UI_PROGRAMMING_GUIDE.md`.
- `snd::plugin` — VST3 + AU **hosting** (editor GUIs in native windows, MIDI,
  host transport) and a **client SDK**: write a `Processor` + ImGui editor once
  and build it as VST3, AU, and a standalone app. Multichannel buses, an
  inter-plugin control-bus API, out-of-process crash-safe scanning.
- `snd::midi` — MIDI events + CoreMIDI/ALSA/WinMM device I/O
- `snd::dsp` — real FFT + COLA STFT (PFFFT)
- `snd::state` — a ValueTree-shaped state tree with XML round-trip + undo
- `snd::platform` — native file dialogs, per-OS config dir

Mac, Linux, and Windows are supported. `DESIGN.md` records the architecture,
`PROGRAMMING-GUIDE.md` the how-to, and `AUDIT.md` the current architecture
audit.

## Build & test

```sh
tools/build.sh            # configure + build + run --selftest
tools/build.sh --no-test  # configure + build only
```

Or by hand (same steps — use these on Windows):

```sh
cmake -S . -B build
cmake --build build
./build/snd-example --selftest   # headless checks
./build/snd-example              # window + audio device demo
```

`--selftest` verifies real behaviour, not just compilation: decode+playback,
capture, VST3/AU hosting (parameter behaviour + stable-ID state round-trip),
and the plugin-client SDK.

**There is no CI here, and there won't be** — private-repo Actions minutes are
metered and easy to over-trigger. This repo only builds when someone runs the
script by hand.

## What's vendored vs. original

- **Vendored, unmodified, pinned** (via CMake `FetchContent`): miniaudio,
  Dear ImGui, GLFW, VST3 SDK, Apple AudioUnitSDK, PFFFT, NFD, imgui-knobs,
  libFLAC
- **Original**: everything under `include/snd/` and `src/` — SND's own API
  and the backends behind it

## Dev workflow

See `AGENTS.md` for the multi-agent dev workflow that applies to work in this
repo.

## License

SND is **source-available, not open source**. It is licensed under the
[PolyForm Noncommercial License 1.0.0](LICENSE): free to use, modify, and share
for any **noncommercial** purpose (personal projects, research, education,
non-profits, evaluation).

**Commercial use requires a separate commercial licence from the author.** If
you want to ship SND — or anything built on it — in a product or service for
commercial advantage or monetary compensation, open an issue at
<https://github.com/100mountains/snd> or contact @100mountains on GitHub to
arrange one.

Copyright 2026 100mountains. The dependencies SND fetches keep their own
licences (mostly permissive: ImGui, GLFW, miniaudio, PFFFT, NFD, libFLAC,
imgui-knobs, nanosvg, AudioUnitSDK). One to note: the **VST3 SDK** is
GPLv3-or-Steinberg — if you build VST3 targets, your use of that SDK is governed
by Steinberg's terms, separately from SND's licence.
