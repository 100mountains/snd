# Design

This is the current high-level design for this project. It records architectural
decisions, ownership boundaries, and the technical reasons behind them.

This file is not a diary, task list, changelog, bug tracker, feature wishlist, or
place for temporary implementation notes. Add to it only when a decision affects
the structure of the app, the ownership of state, or the standard way a class of
features should be built.

This root design owns application-wide architecture: shared engine, timing,
persistence, and other non-module code. Only user-facing product modules should
have their own module `DESIGN.md` files (see `agents/software-architect.md` for
where those live). Do not create design documents merely because a source-code
folder exists. Practical implementation standards and how-to guidance live in
root `PROGRAMMING-GUIDE.md`.

## What The Program Does

Right now: nothing yet. This repo is a placeholder proving that miniaudio
(audio I/O) and Dear ImGui (UI) build and run together via CMake — see
`README.md`. Replace this section once the app has a real shape.

## Fundamental Model

Not yet established. When the app has real behaviour, this section should
describe the small number of distinct data/control flows in the system (for
example: an audio graph, a UI/interaction layer, and a persistence layer) and
state plainly that they should stay separate.

## Current Design Biases

- Fix root causes, not symptoms.
- Keep the audio callback allocation-free and non-blocking.
- Remove dead code.
- Prefer the smallest change that solves the actual problem.
