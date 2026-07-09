# Retained UI And Accessibility Brief

Status: approved implementation brief; first retained-core implementation
started 2026-07-09.
Author: Mira, UI Specialist.
Inspected baseline: `fb0b861`.
Architect comments: Calder, 2026-07-09.

This document records the staged path for adding SND's retained-mode UI layer,
making that layer accessible first, then bringing the same accessibility model
back to the existing immediate-mode `snd::ui` widgets. The current C++
namespace remains `snd::ui::retained` unless a separate API rename is approved.

## Software Architect Review Notes

Calder: retained-first is a plausible direction, but this is a significant UI
architecture change and should stay proposal/design work until Lumen and the
owner approve the shape. The main architectural constraint is that SND must not
grow two parallel UI toolkits with duplicated paint, icons, focus rules, and
semantics. The retained layer should be a second authoring/front-end model over
shared style, paint helpers, icon assets, and accessibility semantics.

Suggested work split:

- UI Specialist owns the visual language, widget states, retained interaction
  behaviour, keyboard/focus UX, and accessibility quality bar.
- Software Architect owns the public API boundary, ownership/lifetime model,
  layout scope control, realtime safety review, and `DESIGN.md` updates if the
  proposal is approved.
- Project Lead coordinates sequencing with active icon-library work and any
  downstream bob needs.
- Build Engineer should review once native accessibility adapters add platform
  frameworks, compiler flags, packaging, or entitlement implications.

Architectural guardrails:

- Prefer `snd::ui::retained` in a new header while experimental. Do not crowd
  `include/snd/ui.h` until the API shape has survived a real consumer.
- Extract or introduce shared internal paint/icon/style primitives before
  retained widgets diverge visually from immediate widgets.
- Retained widgets may own focus, hit-testing, local interaction state, and
  semantic nodes. They should not own product model state, DSP state, plugin
  parameters, or audio-thread mutation paths.
- The first layout system should be stack/row/column plus fixed/intrinsic
  sizing. Avoid a constraint solver or broad app framework until a real
  consumer proves the need.
- Accessibility adapters should be driven from stable semantic node handles on
  the UI thread. Snapshot diffing is useful, but native APIs usually need
  durable object identity as well as fresh bounds/state.
- Plugin-hosted accessibility is higher risk than standalone/native SND
  windows. Prove the model in standalone SND windows first, then adapt host
  embedding per platform.
- Coordinate with the new icon library: retained icon buttons should consume
  that library and carry accessible names/actions from the same button API, not
  invent a separate retained icon registry.

## Goal

SND should support two UI authoring models over one visual and semantic toolkit:

- a retained-mode SND UI for applications and plugins that benefit from a widget
  tree, idle repaint, structured focus, and native accessibility;
- the existing immediate-mode ImGui-based `snd::ui` path for fast authoring,
  demos, plugin editors, and consumers that already use the current API.

The long-term promise is not "two unrelated GUIs." The promise is one SND UI
language with two front-ends:

- shared visual vocabulary: palette, typography, gear controls, knobs, LEDs,
  meters, buttons, focus rings, spacing, and hit targets;
- shared accessibility vocabulary: roles, names, values, bounds, states,
  relations, and actions;
- separate interaction/layout models where that is genuinely necessary.

## Non-Goals

- Do not replace Dear ImGui or remove the existing immediate-mode API.
- Do not adopt Qt, GTK, JUCE, or a platform-native widget toolkit for SND's own
  controls.
- Do not move UI work onto the audio thread.
- Do not make bob-specific workflows shared SND policy without owner approval.
- Do not promise pixel identity until both paths share concrete paint helpers
  and screenshot verification.
- Do not make retained UI the default plugin-editor path until the retained
  event loop, host embedding, and accessibility object lifetimes are proven.

## Stage 1: Retained UI Foundation

Build a small SND-native retained UI layer first. This gives SND a stable widget
tree and focus model before native accessibility adapters are added.

Proposed shape:

- Add a retained namespace separate from today's immediate API, for example
  `snd::ui::retained` or `snd::ui2`.
- Keep rendering on the same window/GL/ImDrawList foundation where practical.
- Define a retained widget tree with stable node IDs, bounds, dirty state,
  parent/child relationships, focusability, enabled/disabled state, and basic
  event dispatch.
- Start with a narrow but real widget set:
  - labels and text value displays;
  - buttons, toggle buttons, and icon buttons;
  - sliders or knobs;
  - LEDs and meters;
  - simple panels/rows/columns needed to compose plugin UIs.
- Build the new hardware-style controls here as first-class retained widgets
  where they help prove the abstraction: illuminated push buttons, rocker
  switches, LED buttons, and selector knobs.
- Use Canvas as the retained primitive for direct-to-screen custom drawing or
  live animation: it owns layout, focus, hit testing, dirty state, and
  semantics while delegating pixels inside its bounds to a draw callback or
  renderer style.
- Use shared internal paint helpers from the beginning so retained controls do
  not drift visually from immediate `snd::ui` controls.

Calder comment: I would choose `snd::ui::retained` plus a new public header
such as `include/snd/ui_retained.h` during incubation. `snd::ui2` is short, but
it suggests replacement rather than a second model over the same toolkit. Keep
implementation files separate enough that the immediate path does not pay for
retained machinery unless a consumer opts in.

Calder comment: the "idle repaint" completion signal needs a precise event-loop
contract. Current `snd::ui::Window` is frame-pumped and plugin editors are
host-loop sensitive. A retained layer can track dirty state immediately, but
waiting/sleeping between frames must not break timers, plugin-host idle, Linux
IRunLoop servicing, or host-embedded editor repaint rules. Treat "dirty-driven
rendering" as a Stage 1b behaviour unless the window scheduling design is
approved first.

Calder comment: retained value controls should have an explicit binding model.
For example, a slider/knob can expose callbacks or a small value adapter, but
the retained tree should not become the owner of plugin parameters or app
state. This keeps undo, automation, state persistence, and audio handoff owned
by the caller/plugin wrapper.

Implementation note: bound controls derive current value and toggle checked
state in retained core. If a caller-owned value changes outside retained event
dispatch, the caller should call `refreshBoundValues()` on the UI thread so
dirty state, renderer snapshots, and semantic snapshots stay in sync.
Native accessibility bridges should operate retained controls through the core
semantic API (`semanticNode`, `performSemanticAction`, `value`, `setValue`,
`incrementValue`, and `decrementValue`) instead of consulting renderer state.
Custom retained widgets that advertise semantic actions should implement them
with `setOnAction(...)`.

Architectural questions for Calder:

- Should retained UI live in `include/snd/ui.h`, a new public header, or an
  internal experimental header until the API settles?
- What is the smallest layout system that is useful without becoming a generic
  app framework?
- How should retained UI integrate with plugin-client editor sizing and host
  embedding?
- What manual selftest or headless verification is acceptable before native
  accessibility can be exercised?

Calder answers:

- Use a new header and `snd::ui::retained` while experimental. Promote selected
  common types into `ui.h` only after a real app/plugin has used them.
- Minimum layout: stack, row, column, padding/gap/alignment, fixed size,
  intrinsic size, and maybe weighted fill. No grid/constraint/docking system in
  the first pass.
- Plugin-client sizing should initially treat a retained root like a draw/edit
  surface inside the existing editor view, with explicit min/preferred size.
  Host embedding and native accessibility should be proven after standalone
  window semantics work.
- Before native accessibility, add headless tests for focus traversal, hit-test
  dispatch, semantic tree construction, action dispatch, and dirty invalidation.
  Screenshot checks are useful for shared paint, but they do not replace
  semantic tests.

Stage 1 completion signal:

- A retained demo/editor can create a window, draw a small audio control panel,
  handle mouse and keyboard interaction, repaint without a busy frame loop when
  idle, and use SND's shared visual style.

## Stage 2: Accessibility On Retained UI

Add accessibility to the retained layer first. The retained tree gives native
accessibility code a durable object model, which makes this the lower-risk place
to establish SND's accessibility vocabulary.

Proposed shape:

- Define SND accessibility node data independent of native platform APIs:
  - role: button, toggle, slider, meter, text, group, list item, etc.;
  - accessible name and optional description;
  - value text and numeric range where applicable;
  - bounds in window coordinates;
  - state: focused, focusable, disabled, pressed, checked, selected, expanded;
  - actions: activate, increment, decrement, set value, focus;
  - stable ID and parent/child ordering.
- Map retained widgets to these semantic nodes by default. Accessibility should
  be built into shared widgets, not left to each consumer.
- Add keyboard navigation and visible focus indication as part of accessibility,
  not as a cosmetic follow-up.
- Implement platform adapters incrementally:
  - macOS Accessibility first if that is the fastest proof path;
  - Windows UI Automation next;
  - Linux AT-SPI when the Linux windowing path is ready for it.
- Keep all accessibility mutation and platform calls on the UI/main thread.
  Audio threads communicate only through existing atomics/queues.

Calder comment: make the platform-neutral accessibility model small but public
enough for custom retained widgets. Built-in widgets should generate semantics
by default, but consumers will need a supported way to name custom controls,
mark decorative elements hidden, define labelled-by relationships, and provide
actions/value text.

Calder comment: roles should include an escape hatch such as `custom` only if
the native adapters can still expose a useful fallback role/name/action. A broad
custom-role path can quietly erase accessibility if it is easier than choosing
correct semantics.

Architectural questions for Calder:

- Should the accessibility model be a public SND API, an internal contract, or
  public only for custom retained widgets?
- How much native accessibility should be required before the retained API is
  considered usable?
- How should plugin-hosted editors expose accessibility through host windows on
  each platform?

Calder answers:

- The semantic model should be public for custom retained widgets, but native
  adapter internals should stay private until the lifetime and threading model
  is proven.
- Retained UI can be useful before native adapters are complete only if it has
  keyboard navigation, focus rings, semantic node generation, and headless
  semantic tests. Do not call it accessibility-complete until at least one
  native inspector/screen reader path works.
- Start with standalone SND windows for native accessibility. Plugin-hosted
  editors need separate platform research because the host owns the outer
  window and may impose accessibility or event-routing constraints.

Stage 2 completion signal:

- The retained demo/editor exposes a navigable accessibility tree to at least
  one platform screen reader or platform accessibility inspector, supports
  keyboard focus/action paths, and keeps visual focus states consistent with
  semantic focus.

## Stage 3: Immediate-Mode Accessibility Bridge

Bring the retained accessibility model back into the existing immediate-mode
`snd::ui` widgets. Immediate UI remains immediate visually, but each widget
emits semantic nodes during the frame.

Proposed shape:

- Add an accessibility snapshot builder to each `Window` frame.
- Immediate widgets register semantic nodes as they draw:
  - stable ID from the ImGui ID stack or an explicit accessibility ID;
  - role/name/value/bounds/states/actions;
  - parent scope inferred from explicit SND accessibility groups where needed.
- Diff the current semantic snapshot against the previous frame to keep native
  accessibility adapters stable enough for assistive tech.
- Reuse the Stage 2 accessibility roles, states, actions, and platform adapters.
- Add accessible wrappers or annotations for stock ImGui controls where SND
  examples/plugins need them, while keeping SND-owned widgets accessible by
  default.
- Preserve the current immediate-mode ownership model: app state stays in the
  caller, and widget calls return changes exactly as they do now.

Calder comment: immediate-mode accessibility almost certainly needs explicit
stable accessibility IDs for non-trivial projects. ImGui IDs are useful input,
but they can change when labels, tree shape, hidden panels, or PushID scopes
change. A helper such as `accessibilityId("transport.play")` or an optional
parameter on SND-owned widgets would be safer than relying entirely on inferred
ImGui identity.

Calder comment: keep the immediate bridge opt-in for stock ImGui and on by
default for SND-owned widgets. Trying to make arbitrary raw ImGui controls
fully accessible in one step will expand the scope too far.

Architectural questions for Calder:

- Do immediate widgets need explicit accessibility IDs, or can ImGui IDs be
  made stable enough for real projects?
- How should SND expose grouping, labels-for relationships, and hidden text in
  immediate mode without making the API noisy?
- Which stock ImGui controls must SND wrap or annotate for examples and plugin
  editors to meet a baseline accessibility standard?

Calder answers:

- SND-owned widgets should accept or derive stable accessibility IDs. For raw
  ImGui controls, provide annotations/groups rather than promising automatic
  correctness.
- Use scoped helpers for groups, labels, hidden text, and relationships. Keep
  basic widgets concise, and let complex panels opt into richer metadata.
- Baseline wrappers should follow actual examples/plugins: button, icon button,
  checkbox/toggle, slider/drag number, combo/list, text input if used, and the
  SND audio controls that already exist.

Stage 3 completion signal:

- A current immediate-mode demo/editor exposes the same kind of accessibility
  tree as the retained demo, with keyboard navigation and visible focus for SND
  widgets, while preserving the existing immediate API style.

## Shared Validation

Each stage should include manual local validation rather than CI:

- build-only validation through the existing local build path;
- headless semantic tests in `--selftest` where possible, without requiring a
  window or native accessibility service;
- focused UI examples that can run under the app when explicitly launched by an
  administrator or owner;
- screenshot comparisons for shared paint helpers where pixel consistency
  matters;
- platform accessibility inspector checks for native accessibility adapters;
- regression checks that no UI work, allocation-heavy path, or platform
  accessibility callback reaches realtime audio code.

Calder comment: add negative tests too. A decorative icon should be hidden from
the semantic tree, a disabled button should expose disabled state and reject
activation, and a meter should expose a value without being focusable unless it
has an action.

## Suggested First Review Questions

1. Is retained-first the right order, or should a smaller accessibility model be
   drafted before any retained widget implementation?
2. Should retained UI be experimental/internal until one app or plugin proves
   the API shape?
3. Which platform should be the first native accessibility target?
4. What is the minimum retained layout and widget set that proves the approach
   without turning SND into a broad app framework?
5. Where should this proposal graduate if approved: `DESIGN.md`,
   `UI_PROGRAMMING_GUIDE.md`, a new UI architecture doc, or some combination?

Calder recommendation: if approved, put the durable architecture in
`DESIGN.md` under a UI architecture section, put implementation conventions and
widget authoring rules in `UI_PROGRAMMING_GUIDE.md`, and keep any large
platform accessibility research notes under `docs/research/` until they become
settled design.
