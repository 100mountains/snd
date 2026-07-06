# SND

**A JUCE/iPlug replacement attempt.** SND is a small, permissively-licensed,
in-house audio+UI foundation library — cross-platform audio I/O, a GUI shell,
and headless VST3/AU plugin hosting, without JUCE's AGPLv3-or-pay licensing.
Apps (Snoredacity first) build on SND instead of on JUCE.

## What works right now

- `snd::audio` — device playback/capture, file decode (wav/flac/mp3/ogg),
  WAV writing, a simple `Player` (wraps miniaudio)
- `snd::ui` — window + Dear ImGui frame shell (GLFW/OpenGL underneath),
  file drag-and-drop, first custom widgets (`gradientPanel`, `gradientButton`
  drawn via ImDrawList — ImGui itself stays unmodified)
- `snd::plugin` — headless plugin hosting:
  - **VST3** everywhere, built on the VST3 SDK's own hosting utilities
  - **AU** on macOS via AudioToolbox directly
  - format-agnostic API (`Format` / `HostManager` / `Instance` /
    `Parameter`), stable parameter IDs (never indices), spin-lock +
    deferred-parameter threading model per the JUCE audit, dead-man's-pedal
    crash-loop protection for scanning
  - no editor-GUI embedding yet, deliberately; no MIDI, deliberately
- `TestGain` — SND's own minimal VST3 plugin, built as part of the tree so
  hosting tests never depend on what's installed on a machine

See `ROADMAP.md` for phases and `docs/research/` for the JUCE/VST3 audits
this design came from. `DESIGN.md` records the architecture decisions.

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

`--selftest` verifies real behaviour, not just compilation: decodes and plays
a bundled file, records from the input device, loads TestGain and confirms
audio is actually altered by a parameter set via stable ID (and that state
save/restore round-trips), and hosts Apple's AULowpass confirming an 8kHz
sine gets attenuated.

**There is no CI here, and there won't be.** A previous project's GitHub
Actions produced a surprise bill; this repo only builds when someone runs the
script by hand.

## What's vendored vs. original

- **Vendored, unmodified, pinned** (via CMake `FetchContent`): miniaudio
  0.11.25, Dear ImGui v1.92.8, GLFW 3.4, VST3 SDK v3.8.0 (MIT — used for its
  hosting utilities and to build TestGain)
- **Original**: everything under `include/snd/` and `src/` — SND's own API
  and the backends behind it

## Dev workflow

See `AGENTS.md` for the multi-agent dev workflow (genericized from Murk's)
that applies to work in this repo.
