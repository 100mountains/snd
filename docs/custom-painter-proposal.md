# Custom Painter Hooks For Knobs And Buttons

Status: implemented — first shared hook landed.
Original proposal: Iris, UI Specialist (second thread).
Architect review: Calder.
Project routing: Lumen.

## Goal

Let consumers provide custom knob and button faces without forking SND widget
behaviour. SND keeps owning interaction, hit targets, focus rings, keyboard
handling, value mapping, semantic actions, and accessibility. Consumers only
draw the widget body.

## Decision Direction

- Keep built-in `KnobStyle` presets for common SND looks.
- Keep shared `paint::` primitives public for composed faces.
- Add custom painter hooks as an extension point, not as a replacement for the
  built-in controls.
- Define the painter contract once in the shared paint layer, then adapt both
  immediate and retained UI to it.
- Do not add a named-style registry yet. Start with callbacks and add a registry
  only if repeated real consumers prove the need.

## API Shape To Design

Use argument structs rather than long callback signatures, for example:

```cpp
struct KnobPaintArgs {
    ImDrawList* drawList = nullptr;
    ImVec2 topLeft;
    float size = 0.0f;
    float rawValue = 0.0f;        // raw/current value when available
    float normalizedValue = 0.0f; // 0..1 value used for drawing
    bool bipolar = false;
    ImU32 accent = 0;
    const Palette* palette = nullptr;
    const ControlState* state = nullptr;
};

struct ButtonPaintArgs {
    ImDrawList* drawList = nullptr;
    ImVec2 topLeft;
    ImVec2 size;
    const char* text = "";
    ImU32 face = 0;
    const Palette* palette = nullptr;
    const ControlState* state = nullptr;
};
```

The exact names and fields should be settled during implementation, but the
shape should stay stable and shared between immediate and retained code.

For callback ownership:

- Immediate-mode overloads may use a lightweight non-owning callback shape if
  SND adds one, or a templated callable overload if that is simpler.
- Retained `VisualStyle` needs an owning callback, likely `std::function`,
  because styles live beyond the call site.

## Guardrails

- A custom painter draws the body only.
- SND draws the focus ring and any required accessibility overlay after the
  painter runs. The painter receives a body state with focus suppressed so the
  ring is drawn exactly once by SND.
- A painter must not create ImGui controls, read input, mutate product/plugin
  state, touch audio-thread state, or own model data.
- A painter may read `ControlState` to reflect hover, active, selected, or
  disabled body state, but it must not decide whether the widget is reachable.
- Retained and immediate widgets must use the same `paint` contract so visual
  customization does not split the toolkit.

## Implementation Plan

1. Owner/API approval
   - Confirm the shared paint-layer contract.
   - Decide immediate callback shape: lightweight function-ref, templated
     callable, or `std::function`.
   - Confirm retained uses owning callbacks in `VisualStyle`.

2. Shared paint contract
   - Add painter argument structs and callback aliases in the shared UI/paint
     public surface.
   - Provide default adapter functions that call existing `paint::drawKnob`,
     `paint::drawButton`, and icon-button drawing paths.

3. Immediate widgets
   - Add custom-painter overloads for knob and button-like controls.
   - Preserve existing overloads and default styles.
   - Keep all interaction, keyboard, focus, and value mapping inside SND.
   - Draw SND-owned focus/accessibility overlays after the custom body.

4. Retained widgets
   - Add matching painter fields to retained `VisualStyle`.
   - Route retained knob/button/icon-button rendering through the shared painter
     contract when present.
   - Preserve semantic actions, `ValueBinding`, focus state, snapshots, and
     default paint behaviour.

5. Documentation and examples
   - Update `UI_PROGRAMMING_GUIDE.md` with immediate and retained examples.
   - Document painter guardrails and ownership rules.
   - Add one small demo usage, preferably outside shared library policy if it is
     consumer-specific.

6. Validation
   - Build-only validation at minimum.
   - Extend headless retained selftest only if the hook changes retained
     semantics or event behaviour.
   - Visual parity should be checked manually by the UI owner because this is a
     drawing extension point.

7. Follow-up only if needed
   - Consider a named painter registry after at least two real repeated uses
     need shared names or theme-level lookup.

## Acceptance Criteria

- Existing knobs and buttons behave and draw the same by default.
- A custom-painted knob can be used without reimplementing drag, keyboard,
  focus, semantic value, or accessibility behaviour.
- A custom-painted retained knob/button uses the same painter contract as the
  immediate widget.
- Focus indication remains SND-owned and cannot be removed by a painter.
- No consumer-specific face becomes shared library policy unless the owner
  explicitly approves it.

## Non-Goals

- Do not replace the built-in `KnobStyle` presets.
- Do not add a style registry in the first pass.
- Do not let custom painters own interaction or accessibility.
- Do not move bob-specific visual identity into SND defaults.

## Sequencing Note

This is separate from the retained-mode/Gooey implementation stack. Stage it as
a reviewed proposal/action-plan document first, or as its own implementation
branch later. Do not bundle it into retained-mode implementation commits unless
the owner explicitly asks for that.
