# Roadmap — SND after WaveBob and the Murk GL surface

SND is the backbone: audio I/O, UI, plugin hosting, DSP, and all
platform-specific glue live here behind one API. Two apps ride on it:
**WaveBob** (wave editor — near feature-complete, keeps shipping) and
**bob** (the Murk/Murfy successor). The original phase 1–4 SND build plan and
the Murk GL surface port are complete; this file now tracks residual SND work
and any downstream pressure that should feed back into the foundation.

Historical Murk surface (what the port had to cover): 1 JUCE GUI app +
MurfyMainPlugin (Standalone/VST3/AU) + ~6 module plugins (Pattern, Seq,
PbQuntise, BassController, module-plugin set) + 3 panel-preview dev tools; 57
app source files, **53 of them touching MIDI**; ONNX engine on the side
(JUCE-light). Current state: the Murk-facing surface is done on SND/GL, with
Arrange and Perform pages done.

Sizes are t-shirt (S/M/L), not dates. No CI — `tools/build.sh` by hand, as ever.

## Module map (current)

| Module | Status |
|---|---|
| `snd::audio` | built — devices, Player (gapless loop, RT insert hook), fast load, probe/prefix, resample |
| `snd::dsp` | built — PFFFT, COLA STFT (−140 dB round-trip) |
| `snd::plugin` (hosting) | built — VST3+AU, editor GUIs in native windows, out-of-process scanning |
| `snd::plugin` (client) | built — Processor+params, VST3 wrapper, ImGui editor in IPlugView, standalone, AU wrapper |
| `snd::ui` | built v1 — frameless window, native drag, SVG assets, icon buttons, and audio widgets (knobs, meters, faders, keyboard, pattern grid, envelope, XY pad, lists, drag number, file browser) |
| `snd::midi` | built — Message/Buffer, CoreMIDI/ALSA/WinMM I/O, host wiring (VST3 events + IMidiMapping, AU MusicDevice) |
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

## Track 2 — SND pillars (the bob/Murk enablers)

### ① Widgets — v1 done; Murk GL surface covered
- Arrange and Perform page needs are covered by the current SND/GL surface.
- Remaining polish candidates: list/table helpers and menu/tooltip conventions
  if another real consumer needs them.

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
- ~~client side: MIDI through the plugin SDK processor API~~ done with ②

### ④ App plumbing — DONE 2026-07-07
- ~~snd::state (tree + listeners + transactional undo + ValueTree-shaped
  XML), runOnMain/processMainQueue + frame timers, StreamReader, ThreadPool,
  snd::plugin::Graph w/ automatic latency compensation,
  Instance::latencySamples (VST3+AU), FLAC/MP3 encoders, media extraction
  backends (AVFoundation / Media Foundation / ffmpeg-on-PATH)~~ all landed
  with behaviour selftests
- remaining: imgui-node-editor vendoring for the graph UI (with its first
  consumer); cross-platform AIFF only if a downstream app needs OS-independent
  AIFF beyond miniaudio's normal decode path

## Track 4 — MULTIPLATFORM (owner-declared very important, 2026-07-07)

SND's deps were chosen cross-platform from day one (miniaudio = WASAPI/ALSA,
GLFW, ImGui, NFD, libFLAC, VST3 SDK w/ module_win32/linux, NFD, std-only
state/graph/dsp). All three platforms now build and pass `--selftest`; the
per-OS backends (MIDI, media extraction, editor windows) exist for each.

| Gap | Windows backend | Linux backend |
|---|---|---|
| MIDI devices | WinMM | ALSA sequencer |
| media extraction (video audio) | Media Foundation (OS API) | ffmpeg-on-PATH pipe |
| plugin editor windows (host) | HWND child window | X11 embed |
| ImGui editor in plugins (client) | WGL view | GLX view |
| frameless-window native drag | Win32 native drag | manual drag fallback |
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
- **P3 — Windows: DONE 2026-07-08** on the `bob-winbuild` KVM/libvirt VM on
  sentience, driven headless over WinRM (pywinrm; source synced Mac → sentience
  → HTTP → VM; MSVC / VS Build Tools 2022 + cmake + ninja). First MSVC compile
  needed only NOMINMAX + WIN32_LEAN_AND_MEAN + _USE_MATH_DEFINES +
  PFFFT_STATIC_DEFINE + a uniform /MD runtime. Backends landed + verified in
  `--selftest` (**ALL PASS**, 13 checks): WinMM MIDI (enumerates devices;
  WinMM has no virtual ports, so loopback verifies enumeration), Media
  Foundation extraction (IMFSourceReader → float PCM, WAV round-trip PASS),
  HWND host editor window + WGL client editor view (compile + factory
  verified; full GL render needs real GPU — the VM's GL is 1.1). Also fixed:
  FLAC-encode crash (libFLAC's Windows file layer → std::ofstream stream API),
  VST3 bundle output path (multi-config Debug/ subdir), portable temp dir.
  Device/loopback/MP3 skip on the headless VM (no audio/MIDI device, no LAME).
- Hardware: Linux = owner's PC over SSH (`ssh sentience`); Windows = a
  libvirt VM on the same box, reached over WinRM.

**SND is now cross-platform: macOS, Linux, and Windows all build + pass
`--selftest`.**

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
3. App shell + GL surface — ✅ DONE: the Murk-facing surface is running on
   SND/GL, with Arrange and Perform pages done. Manager/PluginList, graph,
   sample edit, and sequencer/pattern surface work are no longer tracked here
   as future SND blockers; any remaining issues should come back as specific
   SND defects or focused widget/API requests.
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
- **M4** — ✅ DONE: bob app shell and Murk GL surface on SND, including
  Arrange and Perform pages
- **M5** — JUCE deleted from the bob tree

## Still-true operational notes

- macOS packaging: hosting third-party plugins under the hardened runtime
  needs the library-validation entitlement — SND documents/owns this.
- Every capability lands with a behaviour-verifying `--selftest` check, run
  by hand via `tools/build.sh`. No CI, ever.
- Murk (`~/Documents/murk`, renamed in-tree to `Bob/`) remains read-only
  reference material when historical behaviour needs checking; current work
  lands in bob/SND, not in that reference tree.

**How much, in one line:** the old SND buildout and Murk GL surface work are
done; remaining roadmap items should be concrete foundation fixes, downstream
bug reports, or owner-approved cleanup.
