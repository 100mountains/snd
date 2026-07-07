# Roadmap — SND to WaveBob-done and Murk-off-JUCE

SND is the backbone: audio I/O, UI, plugin hosting, DSP, and all
platform-specific glue live here behind one API. Two apps ride on it:
**WaveBob** (wave editor — near feature-complete, keeps shipping) and
**Murk/Murfy** (the migration target). The original phase 1–4 build plan that
used to live in this file is complete (see git history); this is what's left.

Murk's measured surface (what "done" means): 1 JUCE GUI app + MurfyMainPlugin
(Standalone/VST3/AU) + ~6 module plugins (Pattern, Seq, PbQuntise,
BassController, module-plugin set) + 3 panel-preview dev tools; 57 app source
files, **53 of them touching MIDI**; ONNX engine on the side (JUCE-light).

Sizes are t-shirt (S/M/L), not dates. No CI — `tools/build.sh` by hand, as ever.

## Module map (current)

| Module | Status |
|---|---|
| `snd::audio` | built — devices, Player (gapless loop, RT insert hook), fast load, probe/prefix, resample |
| `snd::dsp` | built — PFFFT, COLA STFT (−140 dB round-trip) |
| `snd::plugin` (hosting) | built — VST3+AU, editor GUIs in native windows, out-of-process scanning |
| `snd::plugin` (client) | **missing — pillar ②** |
| `snd::ui` | built v1 — frameless window, native drag, icon buttons, widget set (Palette, knobs, toggle, LED, dB meter, fader, badge) |
| `snd::midi` | **missing — pillar ③, needs charter blessing** |
| `snd::platform` | built — dialogs, configDir, executablePath |
| state tree / graph / streaming formats | **missing — pillar ④** |

## Track 1 — WaveBob to feature-complete (independent; keeps shipping)

| # | Item | Size |
|---|------|------|
| 1 | Save formats: FLAC + OGG encode, vendored permissive encoders (mp3 = LAME is LGPL — decide or skip) | M |
| 2 | Log-frequency spectrogram axis (+ freq zoom) | S |
| 3 | Preview/audition before Apply (effect + plugin dialogs) | M |
| 4 | Parametric EQ (promote the biquad toolkit from `master.h` into `snd::dsp`) | M |
| 5 | Pitch shift / time stretch — vendor signalsmith-stretch (MIT) | M |
| 6 | Noise reduction v2 (temporal smoothing over profile subtract) | M |
| 7 | Rack latency compensation (query plugin latency; align bounce + live dry path) | M |
| 8 | Spectral paint extras: harmonic brush, persist marks, loudness-select | M |
| 9 | Separate-windows mode (needs SND multi-window) | L |
| 10 | Per-file session sidecar (view/selection/paint marks) | S |

## Track 2 — SND pillars (the Murk enablers)

### ① Widgets — v1 done; Murk parity still needs
- list/table conventions (thin wrappers over ImGui tables) — S
- MIDI keyboard widget, XY pad, envelope/curve + pattern-grid editors — M/L
- file-browser panel, drag-number entry, menu/tooltip conventions — S/M

### ② Plugin **client** SDK — the big unlock, start next
- `snd::plugin::client`: Processor base, declarative params (stable IDs), state chunks — M
- VST3 wrapper: factory macro + SingleComponentEffect bridge (TestGain generalized) — M
- **ImGui editor inside IPlugView** — GL context in the host's NSView, input
  routing, resize, multiple instances — L, the hard part
- AU nearly free via the VST3 SDK's `auwrapper` — S/M
- Standalone wrapper = the existing SND app shell — S
- `snd_add_plugin()` CMake helper — S
- Dogfood gate (**M1**): an SND-built plugin with an ImGui GUI running inside
  WaveBob's rack

### ③ `snd::midi` — DECISION GATE: reverses the "no MIDI anywhere" charter line.
Owner must bless before work starts; there is no Murk-on-SND without it
(53/57 files). WaveBob simply won't link it.
- event/buffer types, CoreMIDI in/out devices — M
- host side: VST3 IEventList + AU MusicDeviceMIDIEvent (our event lists are
  deliberately empty today) — M
- client side: MIDI through the plugin SDK processor API — S/M

### ④ App plumbing
- state tree (ValueTree replacement: tree + listeners + undo + serialize;
  Murk uses it in 25 files) — M/L
- timers + run-on-main-thread queue — S
- formats: AIFF read, streaming/seeking reader (the sampler can't full-load
  every kit), encoders shared with Track 1 — M
- processor **graph** with latency compensation (AudioProcessorGraph
  replacement; vendor imgui-node-editor for the UI when needed) — L
- thread pool / background jobs — S

## Track 3 — the Murk port (starts after ②③ and most of ④)

1. Engine first: DSP + ONNX (AidModel/AidBaker/AdtRunner…) — mostly
   JUCE-light, std:: swaps — M
2. Module plugins one at a time onto the client SDK, smallest first — L cumulative
3. App shell + panels: Manager/PluginList panels (host UI already exists in
   SND), GraphEditorPanel → imgui-node-editor, SampleEditView → componentized
   WaveBob edit view, LoopSequencer/pattern editors → new widgets —
   **XL, the long tail**
4. State migration: ValueTree XML → snd state tree (converter or read-compat) — M
5. Parked / unknowns: `android/` build (SND excludes Android by charter),
   `web/` dir, accessibility (ImGui has none — known regression), AAX (never
   asked for)

## Milestone gates

- **M1** — plugin SDK dogfood: SND-built ImGui plugin runs in WaveBob's rack
- **M2** — MIDI blessed + wired host and client; keyboard widget live
- **M3** — first Murfy module plugin ships on SND with state compat
- **M4** — Murfy app shell on SND with first panels; then panel-by-panel
- **M5** — JUCE deleted from the murk tree

## Still-true operational notes

- macOS packaging: hosting third-party plugins under the hardened runtime
  needs the library-validation entitlement — SND documents/owns this.
- Every capability lands with a behaviour-verifying `--selftest` check, run
  by hand via `tools/build.sh`. No CI, ever.
- Murk (`~/Documents/murk`) is reference/requirements until Track 3 — nothing
  under that path gets modified.

**How much, in one line:** Track 1 ≈ ten S/M items; Track 2 is the heavy
middle (② and ④ dominate); Track 3 is the long tail, dominated by panel/UI
porting, with MIDI threaded through everything.
