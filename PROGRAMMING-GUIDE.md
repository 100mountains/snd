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
