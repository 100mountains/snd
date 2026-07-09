# Declarative UI Tree Proposal

Status: parked proposal for later owner review.
Author: Calder, Software Architect.

## Summary

SND could eventually add a small native declarative UI tree layer inspired by
Flutter, SwiftUI, Jetpack Compose, React Native, QML, XAML, and similar
component-tree toolkits.

The useful shape is a retained component tree plus explicit bindings, layout
intent, theme tokens, variants, and paint hooks. The layer would build down to
SND's retained nodes, shared paint primitives, custom painter hooks, and
accessibility semantics.

This should stay SND-native. It should not become a compatibility project for
another UI framework, and it should not move behaviour or state ownership away
from SND's widgets and caller-owned models.

## Prior Art

Useful references:

- Flutter: widget tree, build functions, themes, and explicit state.
- SwiftUI: value-style view tree, modifiers, environment values, and bindings.
- Jetpack Compose: composable functions, state hoisting, and modifier chains.
- React Native: component tree, explicit props, and native rendering targets.
- QML / Qt Quick: declarative object tree, properties, bindings, and states.
- XAML: component tree, resources, templates, and accessibility-aware controls.
- Slint: native declarative UI descriptions compiled into application code.

The lesson for SND is the tree model: describe structure and visual intent
compactly, then let the toolkit own interaction, rendering, and semantics.

## Goals

- Describe retained UI trees more compactly than hand-written C++ builder code.
- Share visual policy through tokens, tags, variants, and themes instead of
  copying widget styling decisions across consumers.
- Keep behaviour, hit testing, focus traversal, keyboard handling, value
  mapping, semantic actions, and accessibility owned by SND widgets.
- Let custom painter hooks participate as named visual faces without turning
  those faces into default SND policy.
- Keep immediate and retained UI visually aligned by sharing paint primitives,
  palette tokens, and painter contracts.

## Non-Goals

- Do not replace the normal C++ immediate or retained APIs.
- Do not implement broad compatibility with any external UI toolkit.
- Do not add a runtime scripting engine.
- Do not move product/plugin/DSP state ownership into UI descriptions.
- Do not let theme rules change control semantics or accessibility roles.
- Do not introduce consumer-specific brand identity into SND defaults.
- Do not require this layer for normal SND widget use.

## Possible Shape

Start with a small component/theme model rather than a syntax decision. The
first version could be C++ data, JSON, TOML, or a tiny SND-specific text format.
The architectural contract matters more than the surface notation.

Example direction, intentionally illustrative:

```text
tokens {
  accent: #f0be5a;
  panel: #22252e;
  gap.small: 6;
}

component TransportPanel {
  layout: row;
  gap: token(gap.small);
  padding: 8;

  Button play {
    text: "Play";
    variant: transport;
  }

  Knob cutoff {
    label: "Cutoff";
    variant: macro;
    diameter: 56;
  }
}

variant button.transport {
  face: raised;
  accent: token(accent);
}

variant knob.macro {
  style: ring;
}
```

Possible C++ use:

```cpp
snd::ui::retained::Theme theme;
theme.loadText(themeText);

auto tree = snd::ui::retained::build(viewDescription, bindings, actions);
theme.apply(tree, renderer);
```

This is not a proposed API yet. It only shows the intended separation:
structure, theme, bindings, actions, and renderer policy remain distinct.

## Architectural Model

- **Component tree layer:** creates retained nodes with stable IDs, roles,
  accessible names, layout intent, tags, and optional variants.
- **Theme layer:** maps IDs, roles, tags, variants, and states to paint and
  layout tokens. Matching should be simple and deterministic.
- **Binding layer:** connects named values and actions to caller-owned state.
  The document never owns plugin parameters, undo state, MIDI state, or DSP
  data.
- **Paint layer:** consumes resolved theme values through existing `paint::`
  helpers, `VisualStyle`, and custom painter hooks.
- **Accessibility layer:** remains derived from retained node semantics, not
  from theme rules.

## Matching Scope

Keep matching deliberately small:

- Node ID: `transport.play`
- Tag: `transport`
- Role/type: `button`, `knob`, `panel`
- Variant: `primary`, `macro`, `transport`
- State: hovered, active, focused, disabled, selected

Avoid deep ancestry rules, layout-dependent matching, ordering surprises,
inheritance surprises, and runtime rule mutation in the first pass.

## Layout Scope

Reuse the retained layout model before inventing more:

- row / column / stack
- padding / gap / alignment
- fixed, intrinsic, and fill sizing
- simple responsive switches only if real SND surfaces need them

Avoid broad layout machinery until real surfaces prove the need.

## Realtime And Ownership Rules

- Parsing, theme resolution, and tree construction are UI-thread work only.
- No allocation, file I/O, logging, locks, or UI calls may enter audio callbacks.
- Bindings must remain caller-owned and explicit.
- Theme changes may invalidate layout or paint, but must not mutate audio or
  product state.
- Custom painters remain body painters; SND draws focus rings and preserves
  semantic behaviour.

## Open Questions

- Should the first representation be data-only C++ structs, JSON/TOML, or a
  small text format?
- Should this target retained UI only at first, with immediate UI sharing only
  tokens and painter contracts?
- How much matching support is enough before complexity outweighs the benefit?
- Should named painter hooks be referenced by string, enum, or explicit C++
  registration?
- Does SND need hot reload for development, or is static loading enough?
- Should component descriptions support conditionals/repetition, or should C++
  own that composition?

## Suggested Phases

1. **Research spike**
   - Compare Flutter, SwiftUI, Jetpack Compose, React Native, QML, XAML, and
     Slint from SND's point of view.
   - Identify the smallest subset that would help real SND/demo/plugin screens.

2. **Token, tag, and variant model**
   - Add theme tokens and tag/variant metadata to retained nodes if the core
     does not already expose enough.
   - Keep tokens usable by immediate code through normal C++ APIs.

3. **Renderer theme application**
   - Resolve a small theme description into retained `VisualStyle`, layout
     values, palette values, and custom painter choices.
   - Preserve default widget appearance when no theme is supplied.

4. **Declarative structure prototype**
   - Build one small retained panel from a data description.
   - Bind values and actions through explicit caller-provided maps.
   - Validate semantics snapshots and keyboard/focus behaviour.

5. **Format decision**
   - Only after the model works, decide whether SND wants a text syntax, JSON,
     TOML, or C++-only builder data.

## Acceptance Criteria For Any Future Implementation

- Existing C++ immediate and retained APIs remain primary APIs.
- A declarative panel can be inspected as normal retained nodes with normal
  semantic snapshots.
- Theme data can change pixels and layout, but not control meaning or state
  ownership.
- Custom painter hooks remain shared across immediate and retained rendering.
- No large compatibility promise is introduced by accident.

