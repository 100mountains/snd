# SND Graph Surface UI Brief

Status: retained `widgets::graphSurface(...)` first pass landed with virtual
semantic children for modules, ports, child parts, and cables, plus cable
previews and keyboard traversal across virtual graph items.

Owner-facing goal: SND should provide a professional retained graph surface for
module/plugin pages: pan/zoom canvas, grid, module boxes, ports, cables,
selection, and context menus without downstream apps reinventing graph UI rules.

Technical namespace remains `snd::ui::retained`; public naming can settle later.

## Primitive Split

### GraphSurface

`widgets::graphSurface(...)` is the retained primitive. It is Canvas-backed for
pixels, but owns layout, focus, event routing, graph-local hit testing,
selection/marquee state, pan/zoom state, and context-menu opening.

Caller-owned state:

- Node/module list, port list, cable list, selection, and model mutations.
- `GraphViewport` pan/zoom values.
- Context menu state and callbacks.
- Cable preview/drop callbacks. The caller decides whether two ports can
  connect and owns the actual graph mutation after a validated drop.
- Optional `GraphSurfaceStyle` for backdrop, module chrome, pin shape/colors,
  and cable curve/thickness.
- DSP/plugin graph state. UI helpers must not own or mutate audio graph policy.

GraphSurface-owned UI behavior:

- Convert tree-local pointer events through one viewport transform to graph/world
  coordinates.
- Draw grid, selection rectangle, module boxes, ports, cables, hover/focus
  states, drag affordances, and drop previews through shared paint helpers.
- Route gestures for panning, zooming, marquee selection, module dragging, port
  cable drags, and context menus.
- Keep transient cable preview, focused graph item, hover, active, marquee, and
  pan state in `GraphSurfaceState` without mutating caller-owned graph data.
- Keep context menus anchored with retained menu placement. A recorded pointer
  coordinate without an actual anchored popup is not acceptable.

### Cables

Cables are draw-only objects in the first pass, rendered through
`paint::drawCable(...)`.

- Selectable through GraphSurface hit testing.
- Not individual retained nodes unless accessibility or action needs require it.
- Should carry stable IDs so selection, hover, context actions, and semantics can
  reference them.
- Cable hit-testing must use the same curve family as drawing. If
  `GraphSurfaceStyle::wireDroop` is enabled, both paint and hit-test use the
  drooping cubic.

### ModuleBox / GraphNode

Module boxes are first-class graph items, not anonymous pixels and not only
painted shapes. They live inside GraphSurface's pan/zoom coordinate system, but
their internal UI parts must keep stable IDs, hit regions, and semantics.

Required data:

- Stable ID.
- Title/name.
- Graph/world bounds.
- Selected, bypassed, disabled, and error/warning state.
- Input and output ports.
- Child parts/controls: readouts, meters, bypass, delete/options, status chips,
  and any small inline controls.
- Action hooks for select, move, bypass, open/editor, delete/options, and
  context menu.

Module boxes may be rendered inside the GraphSurface canvas at first for
performance and transform simplicity. That does not make their internals
decorative. The data/API must model title, body, ports, bypass, delete/options,
readouts, meters, and future inline controls as real UI parts with stable
identity.

`paint::drawModuleBox(...)` is chrome paint only: title slab, body, selected,
bypassed, and error treatment. Child controls/readouts remain GraphNode parts
and are painted, hit-tested, and semantically exposed by GraphSurface through
the retained virtual semantic-child provider.

### Module Internals

Internal parts should use the same model vocabulary whether they are rendered as
real retained child nodes or virtual graph parts in a single canvas pass.

Required part categories:

- `Title`: draggable title/body label region.
- `Readout`: CPU, latency, level, or parameter text; readable semantics.
- `Meter`: live level/CPU-style display; value semantics where meaningful.
- `Toggle`: bypass, mute, solo, arm; focusable and actionable.
- `Action`: delete, options, open editor; focusable and actionable.
- `Status`: warning/error/loaded/bypassed chips; not colour-only.
- `Port`: connection endpoints with direction/type metadata.

GraphSurface may draw these parts in one canvas pass, but it must preserve a
typed hit result and semantic representation for each actionable/readable part.
Do not ship module controls as unaddressable painted glyphs.

### Ports

Ports are structured hit regions even if they are not retained nodes initially.

Required data:

- Stable ID and parent module ID.
- Label/accessibility name.
- Direction or side.
- Type metadata when available: audio, MIDI, CV/control, event, parameter.
- Graph/world position or a deterministic position derived from module bounds.
- Connected/available/invalid-drop state.

## Hit Testing

Hit-test order must be explicit and stable:

1. Ports.
2. Module action controls: bypass, delete/options, inline controls.
3. Module readouts/meters/status parts when they expose actions or values.
4. Title bar.
5. Module body.
6. Cable endpoints.
7. Cables.
8. Empty graph surface.

Graph hit testing should return a typed result, for example surface, node body,
node title, node readout, node meter, node toggle, node action, port, cable
endpoint, cable segment, or marquee. Do not force downstream code to decode this
from string IDs.

## Visual Contract

- Surface: restrained dark tool canvas with a scalable grid; grid density should
  respond to zoom without visual shimmer.
- Module boxes: compact rack/software-module feel, readable title, stable port
  columns, and visible internal controls/readouts when present.
- Internal controls: bypass/options/delete/readouts/meters should look native to
  the module chrome while keeping focus, hover, pressed, selected, and disabled
  states distinct.
- Ports: distinct shape/position and hover ring; direction/type must not rely on
  colour alone.
- Cables: anti-aliased curves with clear hover/selected/focused treatment;
  invalid or muted cable states need non-colour cues.
- Selection: selected, hovered, dragging, focused, bypassed, and error states
  must be visually distinct.
- Text must remain legible at normal zoom; tiny zoom levels may collapse to
  simplified labels or icon/state marks.

Use shared paint helpers for module boxes, ports, cables, grid, and selection
overlays so immediate/debug tools and retained GraphSurface share the same paint
language.
Use `GraphSurfaceStyle` for consumer skins that need square pins, custom module
chrome, drooping wires, or flat/grid/green-grid/mosaic backdrops; do not fork
the graph painter for those choices.

## Interaction Contract

- Left click selects the top hit item.
- Drag on a module body moves selected modules through caller callbacks.
- Drag from a port starts a cable preview; drop validation is caller-owned.
  `canConnect` answers whether the current target is valid, `onCablePreview`
  receives the graph-space preview position, and `onConnect` fires only for a
  validated port-to-port drop.
- Click on a module child part performs that part's action or value interaction
  without falling through to module drag.
- Node and cable double-clicks are separate optional callbacks. The consumer
  decides whether they open an editor, delete a route, or do nothing; the graph
  primitive only supplies the stable hit.
- Drag empty surface creates a selection rectangle unless a pan modifier/mode is
  active.
- Pan/zoom are stored in `GraphViewport`; event handlers must not duplicate
  transform math.
- Context menu opens from retained `ContextMenu` events and semantic
  `Action::OpenMenu`, not raw right-button down/up.
- Escape cancels a drag, cable preview, or marquee before closing higher-level
  UI.
- Arrow keys move the virtual graph focus through modules, ports, actionable
  parts, and cables. Enter/Space activates the focused graph item via the same
  callback path as pointer and semantic activation.

Modifier policy should stay conservative and documented before it becomes shared
library behavior.

## Accessibility Contract

The graph must not be one opaque canvas.

Required minimum:

- GraphSurface is a focusable `Role::Canvas` with a useful accessible name.
- Modules are exposed through a virtual semantic provider with stable IDs and
  graph-transformed bounds.
- Module semantics include title/name and selected/bypassed/error/disabled state.
- Readouts/meters/toggles/actions inside modules expose names, values, states,
  and actions; a bypass button must be focusable/actionable, not just a glyph.
- Ports expose labels, direction/type, and connection state.
- Context actions have a keyboard/a11y fallback through `Action::OpenMenu`.
- Keyboard navigation must have a path to modules and their important actions.
- The current virtual graph focus is reflected in semantic child state so
  native accessibility adapters can announce the same item the retained graph
  surface will activate.

Although cables remain draw-only, GraphSurface exposes cable state and context
actions semantically when a cable is keyboard reachable.

## First API Shape To Prove

Names are provisional, but the landed first-pass data shape is:

```cpp
struct GraphViewport {
    Vec2 pan;
    float zoom = 1.0f;
};

struct GraphPort {
    NodeId id;
    std::string label;
    GraphPortDirection direction = GraphPortDirection::Input;
    GraphPortKind kind = GraphPortKind::Unknown;
    Rect bounds; // node-local coordinates
    bool connected = false;
    bool enabled = true;
    bool invalidDrop = false;
};

enum class GraphNodePartKind {
    Title,
    Readout,
    Meter,
    Toggle,
    Action,
    Status,
};

struct GraphNodePart {
    NodeId id;
    std::string label;
    GraphNodePartKind kind = GraphNodePartKind::Readout;
    Rect bounds; // node-local coordinates
    std::string valueText;
    double value = 0.0;
    bool hasValue = false;
    bool enabled = true;
    bool checked = false;
    bool selected = false;
};

struct GraphNode {
    NodeId id;
    std::string title;
    Rect bounds; // graph/world coordinates
    std::vector<GraphPort> inputs;
    std::vector<GraphPort> outputs;
    std::vector<GraphNodePart> parts;
    bool selected = false;
    bool bypassed = false;
    bool disabled = false;
    bool error = false;
};

struct GraphCable {
    NodeId id;
    NodeId fromNode;
    NodeId fromPort;
    NodeId toNode;
    NodeId toPort;
    bool selected = false;
    bool muted = false;
    bool invalid = false;
};

enum class GraphHitKind {
    None,
    Surface,
    NodeBody,
    NodeTitle,
    NodePart,
    Port,
    CableEndpoint,
    Cable,
};

struct GraphHit {
    GraphHitKind kind = GraphHitKind::None;
    NodeId nodeId;
    NodeId partId;
    NodeId portId;
    NodeId cableId;
    Vec2 graphPosition;
    bool output = false;
};

struct GraphSurfaceState {
    GraphViewport viewport;
    GraphHit focused;
    GraphHit hovered;
    GraphHit active;
    bool panning = false;
    bool marqueeActive = false;
    Rect marquee;
    bool cablePreviewActive = false;
    GraphHit cablePreviewStart;
    GraphHit cablePreviewTarget;
    Vec2 cablePreviewPosition;
    bool cablePreviewValid = false;
};

struct GraphSurfaceCallbacks {
    std::function<void(const GraphHit&)> onSelect;
    std::function<void(const GraphHit&)> onActivate;
    std::function<void(const GraphHit&, Vec2 graphPosition, Vec2 surfacePosition)> onContextMenu;
    std::function<void(const GraphHit&, Vec2 graphDelta)> onDrag;
    std::function<bool(const GraphHit& fromPort, const GraphHit& toPort)> canConnect;
    std::function<void(const GraphHit& fromPort, const GraphHit& toPort)> onConnect;
    std::function<void(const GraphHit& fromPort, Vec2 graphPosition)> onCablePreview;
    std::function<void(const GraphViewport&)> onViewportChanged;
    std::function<void(const GraphHit&)> onNodeDoubleClicked;
    std::function<void(const GraphHit&)> onCableDoubleClicked;
    std::function<void(Vec2 graphPosition)> onBackgroundDoubleClick;
};
```

The painter should receive either transformed screen-space points or one named
transform object. Avoid separate transform math in hit testing and painting.

First-pass helper call:

```cpp
auto graph = widgets::graphSurface("graph.surface", "Patch graph",
                                   graphState, graphNodes, graphCables,
                                   callbacks, &renderer, {520.0f, 320.0f},
                                   &contextMenuState, graphStyle);
```

## Tests And Docs

Selftests should cover:

- Viewport graph-to-screen and screen-to-graph conversion.
- Hit-test z order: port beats body, endpoint beats cable, module beats cable.
- Anchored context menu state and semantic `OpenMenu` fallback.
- Selection/marquee state without mutating caller-owned model directly.
- Cable preview/drop validation without mutating caller-owned model directly.
- Arrow-key traversal and semantic focused state for virtual graph items.
- Module/port/child-part/cable semantics and validation of missing accessible
  names.

Documentation should add a concise `UI_PROGRAMMING_GUIDE.md` entry when public
helpers land. The public showcase should eventually show a small graph with two
modules, typed ports, one or two cables, selection, hover/focus states, and a
context-menu target after the primitive is stable.

## Out Of Scope For First Pass

- Automatic graph layout.
- DSP/plugin graph mutation policy.
- Nested subgraphs.
- Cable routing solvers beyond simple readable curves.
- Minimap.
- Typeahead command palette.
- Consumer-specific module styling.
