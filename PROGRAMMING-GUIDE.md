# Programming Guide

This is the root programming standards document for this project. It records the
practical conventions a developer should follow when adding or changing app
behaviour. Architecture belongs in root `DESIGN.md` and module `DESIGN.md`
files; this document explains how to implement common classes of work in the
style of the app.

Keep this document current whenever a feature establishes a standard pattern
that other code should follow.

UI programming (the `snd::ui` window shell, frame loop, and widgets) has its
own reference: `UI_PROGRAMMING_GUIDE.md`.

## Core Rule

Before adding a new helper, owner, scheduler, style, or data path, look for the
existing app pattern and reuse it when it is still correct. The app should feel
like one coherent codebase, not a collection of unrelated experiments.

During active development, fix stale source data upstream instead of adding
fallbacks or converters in the app. For repo-owned presets, fixtures, and saved
examples, update the saved files unless the project owner explicitly asks for
backward compatibility.

The app is performance-sensitive audio software. Message-thread convenience must
not leak into audio-thread timing, rendering, or state mutation.

## Shared Code Changes

Shared code is high-risk because a local change can silently alter other pages,
modules, or realtime paths.

Before changing shared helpers, shared engine code, common UI styling, or any
function/class used by more than one feature, first identify all current users.

If the requested change is only needed by one module, panel, screen, or workflow,
warn the project owner that changing shared code may affect other behaviour.
Offer a narrower option such as:

- a dedicated module-local helper
- a small wrapper around the shared API
- a new explicit mode or parameter with unchanged defaults
- a separate code path for the new behaviour
- a staged follow-up to generalise the shared code after approval

Do not casually bend shared code to fit one new use case. If shared behaviour is
being deliberately changed, keep the old contract clear, update every affected
caller, and verify the main known users.

When in doubt, preserve shared behaviour and create the smallest dedicated piece
of code needed for the specific feature. Generalise only when at least two real
callers need the same behaviour and the shared abstraction remains simpler than
separate local code.

## Realtime Audio Rules

Audio render paths must be allocation-free in normal operation.

Avoid in audio callbacks:

- file IO
- heap allocation
- resizing buffers
- logging in hot loops
- blocking locks
- message-thread calls
- UI object mutation

Prepare buffers, lookup tables, and scratch storage before the audio thread
needs them. If UI code needs to update audio state, use atomics, prepared
command queues, copied snapshots, or block-boundary application owned by the
audio-owning object.

## Retained UI Rules

SND's retained-mode UI layer currently uses the C++ API
`snd::ui::retained` in `include/snd/ui_retained.h`; do not rename public
symbols for a user-facing nickname without a separate approved API decision.

Keep retained UI work headless and renderer-neutral unless the task is
specifically visual. The tree owns node lifetime, layout bounds, focus,
hover/press state, dirty invalidation, event dispatch, custom local event
hooks, and semantics. It does not own app state, plugin parameters, DSP state,
undo history, persistence, or audio-thread handoff. Bind values through
callbacks or small adapters so the caller remains the state owner.
After a mouse press, custom event hooks on the pressed node continue receiving
mouse-move events until release so custom controls can implement drags without
renderer-local capture state.
Pointer events carry button identity, click count, modifier keys, pointer
delta, wheel delta, and context-menu intent in the retained `Event`.
Left-button press/release keeps the default focus, pressed, capture, and
activation behaviour. Right/middle buttons, wheel, and context-menu events are
delivered to the hit node's custom event hook without triggering built-in
button activation.

Use stable, caller-chosen node IDs. Do not derive durable IDs from visible
labels, child indices, or translated text. Call `validate()` in headless tests
or debug paths for any non-trivial tree; it catches duplicate/empty IDs and
visible meaningful nodes without accessible names.

Every built retained control should expose semantics by default: role,
accessible name, value/range where relevant, disabled/focused/checked state,
and supported actions. Decorative nodes should be renderable but hidden from
`semanticSnapshot()`.
Custom widgets that advertise semantic actions beyond the built-in binding and
activation paths must handle them with `setOnAction(...)`.
Canvas-backed widgets that draw structured internal parts must not expose only
one opaque semantic node. Use `setSemanticChildren(...)` to publish virtual
semantic children with stable IDs, bounds, roles, states, values, and actions,
and use `setOnSemanticAction(...)` to route those virtual child actions back to
the owning retained node. This is the preferred pattern for zoomable/spatial
surfaces such as graphs where real child layout nodes would fight the viewport
transform.

Use `Role::Canvas` for custom-drawn or live animated regions that still need
retained layout, focus, hit testing, dirty state, and semantics. Canvas drawing
must stay on the UI/render thread and read audio-derived values through the
same snapshot/atomic patterns as other UI.

Use `nodeSnapshot()` as the renderer/widget boundary and `semanticSnapshot()`
as the accessibility boundary. Shared visual work should consume
`snd::ui::paint` helpers rather than copying immediate-mode drawing code.
If a caller-owned bound value changes outside retained dispatch, call
`refreshBoundValues()` on the UI thread before deciding whether to repaint or
publish fresh semantics. Custom Canvas/widgets with caller-owned models that
are not representable as a simple `ValueBinding` should install
`setOnRefresh(...)`; the hook is invoked by `refreshBoundValues()` and should
return true only when it refreshed model-derived semantics or node state.
Native accessibility bridges should operate through `semanticNode(id)`,
`performSemanticAction(id, action)`, `value(id)`, `setValue(id, value)`,
`incrementValue(id)`, and `decrementValue(id)` instead of reading renderer
state or calling widget helper internals.

Extend `snd-example --selftest` with headless checks for new retained behaviour
whenever practical. Build-only validation is acceptable when the app process
rule prevents running the binary in the current handoff.

## State And Persistence

Persist durable user/project state, not runtime implementation details.

When saving state:

- Store the user's actual data and references needed to reconstruct the session.
- Avoid serialising transient render buffers, prepared schedules, temporary
  caches, or UI-only drag state.
- Keep source data separate from compiled/derived playback data.
- Make restored state valid before audio rendering resumes.

## Documentation Touch Points

Update root `DESIGN.md` when a change affects app-wide architecture, ownership,
or timing/session design.

Update module `DESIGN.md` files when a user-facing module changes its internal
structure, algorithms, or performance-sensitive behaviour.

Update this `PROGRAMMING-GUIDE.md` when a change establishes or changes a
practical implementation standard that future work should follow.
