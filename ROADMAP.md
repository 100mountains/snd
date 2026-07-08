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
| `snd::plugin` (client) | built — Processor+params, VST3 wrapper, ImGui editor in IPlugView, standalone; AU wrapper pending |
| `snd::ui` | built v1 — frameless window, native drag, icon buttons, widget set (Palette, knobs, toggle, LED, dB meter, fader, badge) |
| `snd::midi` | built — Message/Buffer, CoreMIDI I/O, host wiring (VST3 events + IMidiMapping, AU MusicDevice) |
| `snd::platform` | built — dialogs, configDir, executablePath |
| state / graph / plumbing | built — snd::state, Graph w/ latency comp, StreamReader, timers/queue, ThreadPool, FLAC/MP3/media |

## Track 1 — WaveBob: DONE 2026-07-07 (owner-trimmed)

Shipped: FLAC + MP3 export (LAME dlopen'd), video-soundtrack extraction
(AVFoundation, 5.1 → 6-lane multichannel docs + split-to-tabs),
log-frequency spectrogram axis, preview-before-apply in seven dialogs,
rack bounce latency compensation (+ live latency readout), harmonic brush,
persistent paint via per-file session sidecars, pop-out windows.
Owner dropped: parametric EQ, pitch/time-stretch, noise reduction v2.
Remaining candidates: OGG export, loudness-select for the brush.

## Track 2 — SND pillars (the Murk enablers)

### ① Widgets — v1 done; Murk parity still needs
- list/table conventions (thin wrappers over ImGui tables) — S
- MIDI keyboard widget, XY pad, envelope/curve + pattern-grid editors — M/L
- file-browser panel, drag-number entry, menu/tooltip conventions — S/M

### ② Plugin **client** SDK — CORE DONE (M1 hit 2026-07-07)
- ~~Processor base, declarative params, state chunks, VST3 wrapper w/ runtime
  factory, ImGui editor inside IPlugView (own GL context + input per
  instance), standalone shell, snd_add_plugin()~~ done — DemoFilter ships as
  .vst3 + app; hosted + editor verified through SND's own host (selftest 13
  + window probe)
- ~~MIDI-out from client processors, resizable editors (uiResizable +
  min sizes), live input in the standalone shell (SPSC ring off the default
  capture device)~~ done 2026-07-07
- ~~AU client wrapping~~ DONE 2026-07-07 without the Xcode generator: the
  SDK's auwrapper built against Apple's AudioUnitSDK 1.3.0 (Apache-2.0,
  vendored) under plain Makefiles; snd_add_plugin(... AU_TYPE/AU_SUBTYPE/
  AU_MANUFACTURER) emits a .component embedding the .vst3. Verified by
  in-process AudioComponentRegister + our own AU host (selftest 22) --
  no system registry involvement, no auval registrar rescans.

### ③ `snd::midi` — BLESSED 2026-07-07, host side DONE
- ~~event/buffer types, CoreMIDI in/out devices~~ done (virtual-endpoint
  loopback selftest)
- ~~host side: VST3 IEventList + IMidiMapping CC route, AU
  MusicDeviceMIDIEvent, aumu/aumf in the scan~~ done (DLS-synth-sings selftest)
- client side: MIDI through the plugin SDK processor API — lands with ② — S

### ④ App plumbing — DONE 2026-07-07
- ~~snd::state (tree + listeners + transactional undo + ValueTree-shaped
  XML), runOnMain/processMainQueue + frame timers, StreamReader, ThreadPool,
  snd::plugin::Graph w/ automatic latency compensation,
  Instance::latencySamples (VST3+AU), FLAC/MP3 encoders, AVFoundation media
  reads (covers AIFF on mac)~~ all landed with behaviour selftests (20/20)
- remaining: imgui-node-editor vendoring for the graph UI (with its first
  consumer); cross-platform AIFF if Murk ever leaves the Mac

## Track 4 — MULTIPLATFORM (owner-declared very important, 2026-07-07)

SND's deps were chosen cross-platform from day one (miniaudio = WASAPI/ALSA,
GLFW, ImGui, NFD, libFLAC, VST3 SDK w/ module_win32/linux, NFD, std-only
state/graph/dsp). What is genuinely macOS-only today is four .mm files and
the fact that nothing has ever been COMPILED off-Mac.

| Gap | Windows backend | Linux backend |
|---|---|---|
| MIDI devices | WinMM / WinRT MIDI | ALSA sequencer |
| media extraction (video audio) | Media Foundation (OS API) | GStreamer (dlopen'd) or ffmpeg-on-PATH pipe |
| plugin editor windows (host) | HWND child window | X11 embed |
| ImGui editor in plugins (client) | WGL view | GLX view |
| frameless-window native drag | Win32 path partially written | manual drag fallback |
| AU hosting | n/a (AU is Apple-only; VST3 covers) | n/a |

Phases (verification is LOCAL AND MANUAL — no CI, ever):
- **P1 — DONE 2026-07-07 on the owner's Mint 22 box (`ssh sentience`)**:
  SND 16 PASS + 4 honest skips, WaveBob 17/17 ALL PASS, GUI runs on X11,
  real device playback/capture works, SDK plugins build+host as Linux .vst3,
  MP3 encode works off system libmp3lame. Fixes were three transitive
  includes + global PIC. Workflow: rsync from the Mac, run tools/build.sh
  by hand over SSH.
- **P2 — DONE 2026-07-07 (minus editor embedding)**: ALSA sequencer MIDI
  (loopback selftest passes on the box), media extraction via
  ffmpeg-on-PATH pipe (process-isolated like the plugin scanner -- nothing
  linked, nothing vendored; swap for GStreamer if ffmpeg-as-tool ever
  bothers us). Linux scorecard: 19 PASS + 2 AU skips = full parity minus AU.
- **P2b — DONE 2026-07-07**: X11 plugin-editor embedding both sides.
  Host: X11 floating window + Steinberg::Linux::IRunLoop on the plug frame
  (timers + fd handlers serviced from Instance::idle()). Client: GLX ImGui
  view driven by the host's run loop (no threads in foreign processes).
  Verified live on the box: our host opened our plugin's editor on the
  real display. Linux is now at FULL parity minus AU (Apple-only forever)
  and Windows P3.
- **P3 — Windows**: needs a Windows machine/VM the owner controls. MSVC or
  clang-cl build, WinMM MIDI, Media Foundation, HWND/WGL editors, verify the
  already-written CreateProcess scan path + LAME dll loading.
- Hardware: Linux = owner's PC over SSH (`ssh sentience`, powered on by
  request); Windows = owner has a real PC (access method TBD).

## Track 3 — the bob port (Murk renamed "bob" 2026-07-08)

Lives in a NEW repo **bob2** (`github.com/100mountains/bob2`, `~/github/bob2`);
`~/Documents/murk` (renamed in-tree to `Bob/`) stays read-only reference.
Each module is an `snd::plugin::client::Processor` built VST3+AU+standalone via
`snd_add_plugin()`, ImGui editor with `snd::ui`. See `bob2/PORTING.md`.

1. Engine first: DSP + ONNX (AidModel/AidBaker/GrooveModel/FeelModel…) —
   ✅ DONE, ported into the modules that use them (ONNX Runtime 1.27 vendored
   as the `bob_ort` target; groove/feel/NxD models load + run from assets).
2. Module plugins onto the client SDK — ✅ **14 of 15 done, all selftests
   green**: filter, mixer, drummixer, auxreturns, morph (beat-reverb),
   pbquntise, scale, basscontroller, drums, synth, seq, pattern (generative,
   ONNX), sampler, nxd (neural drum, ONNX). Last one **bassmodel** (embeds the
   AID engine + Faust bass voices + shared filter) in progress.
3. App shell + panels: Manager/PluginList panels (host UI already exists in
   SND), GraphEditorPanel → imgui-node-editor, SampleEditView → componentized
   WaveBob edit view, LoopSequencer/pattern editors → new widgets —
   **XL, the long tail** (not started — modules first, shell after)
4. State migration: ValueTree XML → snd state tree (converter or read-compat) — M
5. Parked / unknowns: `android/` build (SND excludes Android by charter),
   `web/` dir, accessibility (ImGui has none — known regression), AAX (never
   asked for)

## Milestone gates

- **M1** — ✅ DONE: SND-built ImGui plugin (DemoFilter) hosted + editor verified
- **M2** — ✅ DONE: MIDI blessed, host wired, client-SDK MIDI-in synth (DemoSynth) + keyboard widget
- **M3** — ✅ DONE (and then some): 14 of 15 bob modules ship on SND as
  VST3+AU+standalone with behaviour-verifying selftests, incl. the ONNX ones
  (pattern groove/feel, nxd neural drum). Last module bassmodel in flight.
- **M4** — bob app shell on SND with first panels; then panel-by-panel
- **M5** — JUCE deleted from the bob tree

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
