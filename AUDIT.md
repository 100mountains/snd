# Architecture Audit

This is the Software Architect's durable audit ledger for SND. Open findings
must describe current problems only. Resolved items move to Done with the
commit inspected or fixed.

## Baseline Pass - 2026-07-09

Audited commit: `bb98209e009095769dae5fe8dd64bd1452190c29`
Architect: Calder

### Scope

- Reviewed root docs: `README.md`, `DESIGN.md`, `PROGRAMMING-GUIDE.md`,
  `UI_PROGRAMMING_GUIDE.md`, `ROADMAP.md`, and `docs/research/`.
- Reviewed recent git history, CMake/build surface, public headers, core
  implementations, per-OS plugin editor/media/MIDI backends, and the
  `snd-example --selftest` harness.
- Product modules: none live in this repo yet; the current source tree is the
  SND foundation library plus example/test plugins, so root `DESIGN.md` owns
  the current design coverage.
- Validation: inspection only. The app/selftest was not run because the app
  process rule requires an explicit system-administrator request before
  launching app binaries.

### Coverage Status

- Root `DESIGN.md` exists and is now stamped against the audited commit.
- Root `PROGRAMMING-GUIDE.md` exists; UI-specific implementation guidance lives
  in `UI_PROGRAMMING_GUIDE.md`.
- Root `AUDIT.md` now exists as the required single durable audit ledger.
- No module-level `DESIGN.md` files are required yet because there are no
  product-level modules in this repo.

### Open Findings

#### A1. AU host violates realtime and parameter-queue contracts

Evidence: `include/snd/plugin_host.h` states `Parameter::setValue()` is
thread-safe and queued for processor/controller delivery, but
`src/plugin_host/au/au_format.mm` calls `AudioUnitSetParameter()` directly in
`AUParameter::setValue()`. The AU render path also allocates
`std::vector<float> outStorage(channels_ * frames)` inside `process()`.

Risk: AU hosting can allocate in the audio callback and does not match the
format-neutral parameter contract that VST3 follows. This can create audio
glitches and format-specific behaviour surprises for callers.

Recommended change: preallocate AU render scratch in `prepare()`, clear it
per block without resizing, and either queue AU parameter changes consistently
with VST3 or explicitly narrow the public contract after owner approval. Add a
selftest that exercises AU parameter changes while processing.

#### A3. Root design coverage lags the current SDK/backends

Evidence: `DESIGN.md` accurately covers the original hosting architecture but
barely documents the plugin client SDK, `snd_add_plugin()`, AU wrapping,
multichannel plugin bus constraints, the control-bus status, SVG/UI asset
handling, or the newer Windows/Linux media/MIDI/editor backends. Some of that
truth currently lives only in `ROADMAP.md` or code comments.

Risk: future workers may treat roadmap/changelog text as architecture and miss
the constraints that must be preserved when changing wrappers, graph routing,
or per-OS backends.

Recommended change: add concise root design sections for plugin-client
packaging, bus/channel policy, control-bus ownership, and per-OS backend
choices. Keep `ROADMAP.md` as planning/status only.

#### A4. VST3 host musical time may be one block stale

Evidence: `src/plugin_host/vst3/vst3_format.cpp` calls
`processor_->process(processData_)` before refreshing `processContext_.tempo`,
time signature, `projectTimeMusic`, and `barPositionMusic` for the block.

Risk: tempo-synced hosted VST3 plugins may observe the previous block's musical
position or one-block-late transport changes. The selftest list includes host
transport coverage in history, but the current harness does not obviously
assert per-block musical position.

Recommended change: confirm the intended timing contract, update the VST3
process context before `processor_->process()` if the finding holds, and add a
small test plugin/selftest assertion for tempo and project-quarter position.

### Done This Pass

- Uncommitted against `d09c76b`: the prepared graph now routes explicit audio
  channel ranges across instance-reported bus widths without callback
  allocation; Bob3's headless contract covers a stereo source routed into
  channels 3/4 of a four-channel processor.

- Resolved in `5cf556b`: GraphSurface exposes distinct node/cable
  double-click callbacks; Bob3 uses the cable callback for its owner-defined
  route deletion rule.
- Resolved in `5fc8967`: A2 now uses fixed-capacity MIDI/control block
  buffers, POD control target IDs, typed graph edges, allocation-free graph
  event fan-in, and MIDI/control routing in the graph's prepared topological
  schedule; covered by the graph selftest and Bob3 headless contract test.

- Uncommitted documentation hygiene at audited commit
  `bb98209e009095769dae5fe8dd64bd1452190c29`: recorded the architect working
  name, corrected `README.md` audit-location and MIDI/control wording, stamped
  and corrected `DESIGN.md` licensing language, updated stale `ROADMAP.md`
  status lines, and corrected stale public comments in `include/snd/audio.h`,
  `include/snd/midi.h`, `include/snd/plugin_host.h`, `include/snd/state.h`,
  `src/midi.cpp`, and `src/state.cpp`.
- Uncommitted owner-status documentation update, 2026-07-09: recorded that the
  downstream GL surface is done on SND/GL and that Arrange and Perform pages
  are done. This status is owner-provided downstream context, not inferred from
  the SND source tree.
