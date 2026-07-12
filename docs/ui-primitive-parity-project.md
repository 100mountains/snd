# UI Primitive Parity Project

Status: active architecture checklist. Inspected commit: `b331166`.

Goal: keep SND as one UI toolkit with two authoring front ends. Reusable
primitives should have an immediate API, retained API, shared `paint::drawX`
path, matched input behaviour, retained semantics, docs/examples/tests, and a
parity review whenever either side changes. Mode-specific primitives are allowed
only when documented as such.

## Current Map

Full or near-full parity:

- Buttons: `button`, `outlineButton`, `animatedButton`, `gradientButton`,
  icon button basics, and `transportButton`.
- Choice controls: `segmented`, `tabBar`, `cycleButton`.
- Menus: popup, dropdown, context menu model and retained semantic fallback.
- Continuous/status controls: `knob`, `toggle`, `checkbox`, `led`, `meter`,
  `fader`, `dragNumber`, `knobDb`.
- Audio surfaces: `keyboard`, `patternGrid`, `envelopeEditor`, `xyPad`.
- Chrome/help: `gradientPanel`, `badge`, `sectionHeader`, `tooltip`.
- Lists/files: `selectableList`, `fileBrowser`.

Retained-primary primitives:

- Layout/container primitives: `row`, `column`, `panel`, `canvas`.
- Overlay/dialog primitives: `modalDialog`, `alertDialog`, `confirmDialog`.
- Structured fields/views: `valueRow`, `valueField`, `textField`,
  `scrollView`, `splitter`.
- Larger surfaces: `graphSurface`, `sequencerMatrix`.
- Higher-level retained conveniences such as `outlineIconButton` are allowed
  when they compose already-paired paint/input pieces and do not create a second
  visual system.

Mode-specific, utility, or adapter-only:

- Immediate `Window`, native clipboard, font accessors, texture loading,
  SVG/image decode/upload, and `itemFocusVisible` are host/adapter helpers, not
  retained widget parity targets.
- Retained layout containers do not require fake immediate wrappers; immediate
  mode already has ImGui layout calls.
- Retained dialogs, text fields, scroll views, splitters, graph surfaces, and
  sequencer matrices are retained-primary because they depend on retained
  layout/focus/semantic ownership. Immediate consumers should use stock ImGui
  containers or local tools unless a thin wrapper proves useful.
- `draw::Surface::glDraw(...)` is a retained Canvas/live-surface backend hatch,
  not a primitive parity target and not a reusable widget paint path. It may be
  used for bounded shader/custom regions such as Murk only when the retained
  node still owns layout, input, focus, and semantics.

Paint-wrapper gaps:

- `drawGraphGrid` and `drawModuleBox` have compatibility wrappers, but the
  `ImDrawList*` wrappers inject or hard-code frame data (`timeSeconds`,
  `headerH`). That is acceptable only if documented as adapter convenience.
- Randomise-window helpers (`KnobWindow`, `drawKnobWindow`,
  `drawComboWindow`, `drawPadlock`, and hit tests) are paint-only. Either keep
  them documented as overlay building blocks or promote them into paired
  authoring APIs.
- List/value rows still have wrapper-granularity drift: paint has row painters,
  retained exposes row helpers, immediate exposes higher-level helpers. This is
  acceptable while paired public controls keep behaviour and paint parity.

Open behaviour caveats:

- `knobDb` has a retained convenience API with dB range/formatting; confirm
  whether the immediate 0 dB tick is a required retained visual affordance or a
  style-detail follow-up.
- Immediate `tooltip` may keep ImGui popup mechanics for layering, but its body
  should continue to use the shared tooltip paint path where practical.

## First Cleanup Order

1. Tooltip parity slice landed:
   - retained core carries `Semantics::tooltip` through `SemanticNode`;
   - `paint::drawTooltip` has `draw::Surface&` and `ImDrawList*` overloads;
   - retained rendering draws delayed visual tooltips from `Semantics::tooltip`
     on both render paths;
   - `widgets::attachTooltip(node, text)` is the retained convenience helper;
   - remaining work: immediate tooltip body alignment/wrapping review and any
     visual/manual QA.
2. Paint wrapper audit:
   - add or explicitly exempt one-sided wrappers;
   - keep `draw::Surface` as the primary implementation path.
3. Primitive manifest:
   - keep a short table in the UI guide or this file: full parity,
     retained-primary, immediate-primary, paint-only, adapter-only.
4. Authoring parity decisions:
   - decide whether paint-only randomise overlays should stay building blocks or
     become paired authoring APIs.
5. Coverage:
   - add docs/examples/tests with each parity fix;
   - retained semantics must be verified for every retained user-facing control.

## Tooltip Direction

Preferred shape for the first implementation slice:

- `paint::drawTooltip` remains the single visual body and has an `ImDrawList*`
  compatibility wrapper, with `draw::Surface&` still the primary path.
- Immediate `snd::ui::tooltip(...)` may keep ImGui popup/window mechanics for
  layering, but its colours, padding, and text layout should be aligned with
  `paint::drawTooltip`.
- Retained visual tooltips should be explicit opt-in on a node/style, not
  automatic rendering of every `Semantics::description`; existing descriptions
  often describe control type rather than user help.
- Retained core exposes `Semantics::tooltip` and carries it through
  `SemanticNode`; `widgets::attachTooltip(node, text)` is the convenience
  helper. Use `Semantics::description` separately for accessible descriptive
  text.
- Graph port tooltips can remain graph-specific because ports are virtual parts,
  but they should reuse the same tooltip timing/painter path.

## Ownership

- Software Architect: parity contract, public API review, durable docs,
  mode-specific exemptions, and realtime/state-boundary checks.
- UI Specialist: shared paint helpers, immediate wrappers, retained widget
  implementation, examples, and visual/input parity.

Do not start broad UI rewrites from this checklist. Take one primitive family at
a time and leave unrelated in-flight UI work alone.
