// Retained widgets and shared paint renderers.
//
// The technical namespace stays snd::ui::retained. This layer is intentionally
// separate from the retained core: it creates common audio/UI nodes and renders
// them through snd::ui::paint so retained and immediate widgets share pixels.
#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "snd/ui_paint.h"
#include "snd/ui_retained.h"
#include "snd/ui_retained_matrix.h"

namespace snd::ui {
struct FileBrowserState;
}

namespace snd::ui::retained {

enum class VisualKind {
    Auto,
    Panel,
    Text,
    Button,
    IconButton,
    VectorIconButton,
    OutlineButton,
    OutlineIconButton,
    Segmented,
    TabBar,
    CycleButton,
    LedButton,
    Toggle,
    Checkbox,
    Knob,
    Led,
    Meter,
    Fader,
    Badge,
    SectionHeader,
    ListItem,
    MenuItem,
    ValueRow,
    Canvas,
};

enum class IconFont {
    Material,
    Lucide,
    Text,
};

struct VisualStyle {
    using CanvasDraw = std::function<void(ImDrawList&, const Node&, Rect,
                                          const paint::ControlState&)>;
    using CanvasSurfaceDraw = std::function<void(draw::Surface&, const Node&, Rect,
                                                 const paint::ControlState&,
                                                 const draw::FrameContext&)>;

    VisualKind kind = VisualKind::Auto;
    KnobStyle knobStyle = KnobStyle::Ring;
    IconFont iconFont = IconFont::Material;
    std::string glyph;
    bool bipolar = false;
    Icon vectorIcon = Icon::Play;
    ImU32 accent = 0;
    ImU32 face = 0;
    float meterFloorDb = -48.0f;
    float ledRadius = 5.0f;
    float fontScale = 1.0f;
    paint::KnobPainter knobPainter;
    paint::ButtonPainter buttonPainter;
    paint::OutlineButtonStyle outlineButtonStyle;
    MenuItem menuItem;
    PopupMenuState* popupState = nullptr;
    bool panelFill = false;
    bool panelBorder = false;
    float panelRounding = 0.0f; // house style: square (owner directive)
    bool canvasClip = true;
    bool canvasBorder = true;    // draw the panelBorder frame around a canvas
    bool canvasFocusRing = true; // draw the focus ring when a canvas is focused
    bool lit = false;
    std::vector<std::string> segments; // Segmented / CycleButton option labels
    bool ledBlink = false;             // LedButton: pulse the lit ring
    std::function<KnobMod()> knobMod;  // Knob: live modulation ring source
    CanvasDraw canvasDraw;
    CanvasSurfaceDraw canvasSurfaceDraw;
};

class PaintRenderer {
public:
    void setStyle(const NodeId& id, const VisualStyle& style);
    void clearStyle(const NodeId& id);
    void clearStyles();
    // Drop style entries whose node no longer exists in `tree`. setStyle only
    // ever adds; without this sweep the map keeps every style ever registered
    // for a transient popup/menu or a torn-down screen forever, and the
    // per-frame popup loops walk the corpses. Self-throttled, so it is cheap to
    // call every frame; the render(Tree, ...) overloads call it automatically.
    void gcStyles(const Tree& tree) const;

    const VisualStyle* styleFor(const NodeId& id) const;
    // Call after layout when manually rendering retained trees; drawImGui does
    // this automatically. Keeps open anchored popups aligned for hit/paint/a11y.
    void prepareOpenPopups(Tree& tree) const;
    bool dismissOpenPopupsOutside(Tree& tree, const ImVec2& origin,
                                  const ImVec2& screenPoint) const;

    // Render a laid-out retained tree. The Tree overload uses the semantic
    // snapshot so focus/pressed/disabled states match the retained core.
    // Overlay subtrees (popups, flyout panels) always paint AFTER the main
    // tree, so a menu can never end up under later siblings.
    void render(const Tree& tree, ImDrawList* drawList = nullptr) const;
    void render(const Tree& tree, const ImVec2& origin,
                ImDrawList* drawList = nullptr) const;
    void render(const Node& root, ImDrawList* drawList = nullptr) const;
    void render(const Node& root, const ImVec2& origin,
                ImDrawList* drawList = nullptr) const;
    void render(const Tree& tree, draw::Surface& surface,
                const draw::FrameContext& context = {},
                draw::Vec2 origin = {}) const;
    void render(const Node& root, draw::Surface& surface,
                const draw::FrameContext& context = {},
                draw::Vec2 origin = {}) const;

    // Phase split for hosts that composite their own layer between the tree
    // and its popups (e.g. ImGui-bridged module editors): renderMain paints
    // everything EXCEPT overlay subtrees, renderOverlays paints ONLY them.
    // render() above is exactly renderMain + renderOverlays.
    void renderMain(const Tree& tree, const ImVec2& origin,
                    ImDrawList* drawList) const;
    void renderOverlays(const Tree& tree, const ImVec2& origin,
                        ImDrawList* drawList) const;
    void renderMain(const Tree& tree, draw::Surface& surface,
                    const draw::FrameContext& context = {},
                    draw::Vec2 origin = {}) const;
    void renderOverlays(const Tree& tree, draw::Surface& surface,
                        const draw::FrameContext& context = {},
                        draw::Vec2 origin = {}) const;

private:
    using SemanticMap = std::unordered_map<NodeId, SemanticNode>;

    void renderNode(const Node& node, const SemanticMap* semantics,
                    const ImVec2& origin,
                    ImDrawList* drawList,
                    const draw::FrameContext& context,
                    std::vector<const Node*>* overlayQueue = nullptr) const;
    void renderNode(const Node& node, const SemanticMap* semantics,
                    draw::Vec2 origin, draw::Surface& surface,
                    const draw::FrameContext& context,
                    std::vector<const Node*>* overlayQueue = nullptr) const;
    void renderTooltip(const Tree& tree, const SemanticMap& semantics,
                       const ImVec2& origin, ImDrawList* drawList,
                       const draw::FrameContext& context) const;
    void renderTooltip(const Node& root, const SemanticMap* semantics,
                       const ImVec2& origin, ImDrawList* drawList,
                       const draw::FrameContext& context) const;
    void renderTooltip(const Tree& tree, const SemanticMap& semantics,
                       draw::Vec2 origin, draw::Surface& surface,
                       const draw::FrameContext& context) const;
    void renderTooltip(const Node& root, const SemanticMap* semantics,
                       draw::Vec2 origin, draw::Surface& surface,
                       const draw::FrameContext& context) const;
    // Visible, open overlay subtree roots in tree order (recursing past
    // non-overlay children only; nested overlays surface while their parent
    // overlay renders).
    void collectOverlays(const Node& node,
                         std::vector<const Node*>& out) const;
    VisualStyle resolvedStyle(const Node& node) const;

    // mutable: gcStyles() prunes dead entries from the const render() path, and
    // styleGcTick_ throttles that sweep. Style content is otherwise set only
    // through the non-const setStyle/clearStyle API.
    mutable std::unordered_map<NodeId, VisualStyle> styles_;
    mutable unsigned styleGcTick_ = 0;
    mutable std::string tooltipKey_;
    mutable double tooltipStart_ = 0.0;
};

// Translate current ImGui input into retained events. `origin` is the
// same screen-space origin passed to PaintRenderer::render(tree, origin).
bool dispatchImGuiInput(Tree& tree, const ImVec2& origin);
bool dispatchImGuiInput(Tree& tree, const ImVec2& origin, bool mouseCaptured);

struct ImGuiFrameResult {
    bool valuesRefreshed = false;
    bool inputConsumed = false;
    ImVec2 origin;
    ImVec2 size;
};

// Normal retained-frame bridge for embedding a tree in an ImGui layout:
// refresh caller-owned bound values, layout to `size`, reserve the ImGui item
// rectangle, dispatch current input, then render with shared paint helpers.
ImGuiFrameResult drawImGui(Tree& tree, PaintRenderer& renderer, Vec2 size,
                           ImDrawList* drawList = nullptr);

struct GraphViewport {
    Vec2 pan;
    float zoom = 1.0f;
};

enum class GraphPortDirection {
    Input,
    Output,
};

enum class GraphPortKind {
    Unknown,
    Audio,
    Midi,
    Control,
    Event,
    Parameter,
};

enum class GraphNodePartKind {
    Title,
    Readout,
    Meter,
    Toggle,
    Action,
    Status,
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
    ImU32 color = 0;
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

    bool valid() const { return kind != GraphHitKind::None; }
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
    // port-tooltip hover timing (which pin, and since when)
    std::string tooltipKey;
    double tooltipStart = 0.0;
};

struct GraphSurfaceCallbacks {
    std::function<void(const GraphHit&)> onSelect;
    std::function<void(const GraphHit&)> onActivate;
    // surfacePosition is the pointer in surface-local coordinates (the same
    // space as the node bounds), so consumers can open a popup exactly under
    // the cursor; the bound PopupMenuState is anchored there automatically.
    std::function<void(const GraphHit&, Vec2 graphPosition, Vec2 surfacePosition)>
        onContextMenu;
    std::function<void(const GraphHit&, Vec2 graphDelta)> onDrag;
    std::function<void(const GraphHit&)> onDragFinished;
    std::function<bool(const GraphHit& fromPort, const GraphHit& toPort)> canConnect;
    std::function<void(const GraphHit& fromPort, const GraphHit& toPort)> onConnect;
    std::function<void(const GraphHit& fromPort, Vec2 graphPosition)> onCablePreview;
    std::function<void(const GraphViewport&)> onViewportChanged;
    // Double-click on a module (NodeBody/NodeTitle/NodePart), usually to open
    // an editor or inspector for the graph item.
    std::function<void(const GraphHit&)> onNodeDoubleClicked;
    // Double-click a cable or cable endpoint, usually to remove that route.
    std::function<void(const GraphHit&)> onCableDoubleClicked;
    // Double-click on empty canvas. When unset, the surface centres/fits all
    // modules itself.
    std::function<void(Vec2 graphPosition)> onBackgroundDoubleClick;
};

using GraphSurfaceStyle = paint::GraphSurfaceStyle;

Vec2 graphToScreen(const GraphViewport& viewport, Vec2 graphPoint);
Rect graphToScreen(const GraphViewport& viewport, Rect graphRect);
Vec2 screenToGraph(const GraphViewport& viewport, Vec2 screenPoint);

// Centre/fit every module in view: the union of module bounds expanded by
// 160x120, zoom clamped to 0.45..1.25, centred in surfaceSize. Empty graph
// resets to zoom 1, pan 0. The surface runs this on empty-canvas double-click
// when onBackgroundDoubleClick is unset; callers can invoke it for a "fit"
// menu action.
void fitGraphViewport(GraphSurfaceState& state,
                      const std::vector<GraphNode>& nodes, Vec2 surfaceSize);
GraphHit hitTestGraph(const GraphViewport& viewport,
                      const std::vector<GraphNode>& nodes,
                      const std::vector<GraphCable>& cables,
                      Vec2 screenPoint,
                      GraphSurfaceStyle style = {});

enum class ModalDialogResult {
    None,
    Primary,
    Secondary,
    Cancel,
    Destructive,
    Dismissed,
};

enum class ModalButtonRole {
    Primary,
    Secondary,
    Cancel,
    Destructive,
};

enum class ModalEscapePolicy {
    Ignore,
    Close,
    Cancel,
    Primary,
};

struct ModalDialogState {
    bool open = false;
    ModalDialogResult lastResult = ModalDialogResult::None;
    std::string lastAction;
};

struct ModalDialogButton {
    std::string id;
    std::string label;
    ModalButtonRole role = ModalButtonRole::Secondary;
    bool closes = true;
    std::function<void()> onActivate;
};

struct ModalDialogOptions {
    Vec2 size = {360.0f, 168.0f};
    ModalEscapePolicy escapePolicy = ModalEscapePolicy::Cancel;
    bool closeOnScrimClick = false;
    ImU32 scrimColor = IM_COL32(0, 0, 0, 156);
    ImU32 panelFill = 0;
    ImU32 panelBorder = 0;
    Role role = Role::Dialog;
};

namespace widgets {

Node& attachTooltip(Node& node, std::string text);

Node::Ptr panel(NodeId id, Layout layout = {}, Insets padding = {});
Node::Ptr row(NodeId id, float gap = 6.0f, Insets padding = {});
Node::Ptr column(NodeId id, float gap = 6.0f, Insets padding = {});
Node::Ptr gradientPanel(NodeId id, Vec2 size,
                        ImU32 topLeft, ImU32 topRight,
                        ImU32 bottomRight, ImU32 bottomLeft,
                        PaintRenderer* renderer = nullptr);
Node::Ptr label(NodeId id, std::string text, PaintRenderer* renderer = nullptr);
Node::Ptr sectionHeader(NodeId id, std::string text, PaintRenderer* renderer = nullptr);
Node::Ptr badge(NodeId id, std::string text, PaintRenderer* renderer = nullptr);
Node::Ptr listItem(NodeId id, std::string text, bool selected = false,
                   std::function<void(Node&)> onActivate = {},
                   PaintRenderer* renderer = nullptr);
Node::Ptr selectableList(NodeId id, std::string name,
                         std::vector<std::string> items, int* selected,
                         PaintRenderer* renderer = nullptr,
                         Vec2 size = {180.0f, 120.0f},
                         std::function<void(Node&, int)> onSelect = {});
Node::Ptr fileBrowser(NodeId id, std::string name, FileBrowserState& state,
                      std::string* outPath,
                      PaintRenderer* renderer = nullptr,
                      Vec2 size = {260.0f, 180.0f},
                      const char* extensions = nullptr);
Node::Ptr menuItem(NodeId id, MenuItem item,
                   std::function<void(Node&, const MenuItem&, int)> onSelect = {},
                   PaintRenderer* renderer = nullptr, int index = -1,
                   float width = 180.0f);
Node::Ptr popupMenu(NodeId id, PopupMenuState* state,
                    const std::vector<MenuItem>& items,
                    std::function<void(Node&, const MenuItem&, int)> onSelect = {},
                    PaintRenderer* renderer = nullptr, float width = 180.0f);
Node::Ptr dropdownMenu(NodeId id, std::string name, PopupMenuState& state,
                       const std::vector<MenuItem>& items, int* selectedIndex,
                       std::function<void(Node&, const MenuItem&, int)> onSelect = {},
                       PaintRenderer* renderer = nullptr,
                       Vec2 buttonSize = {160.0f, 28.0f},
                       float menuWidth = 180.0f,
                       paint::OutlineButtonStyle buttonStyle = {});
// ValueBinding overload: the selected index is read/written through the
// binding (rounded to an int), so the dropdown live-follows a value that
// changes underneath it -- a real dropdown replacement for cycleButton on
// longer enum lists.
Node::Ptr dropdownMenu(NodeId id, std::string name, PopupMenuState& state,
                       const std::vector<MenuItem>& items, ValueBinding binding,
                       std::function<void(Node&, const MenuItem&, int)> onSelect = {},
                       PaintRenderer* renderer = nullptr,
                       Vec2 buttonSize = {160.0f, 28.0f},
                       float menuWidth = 180.0f,
                       paint::OutlineButtonStyle buttonStyle = {});
Node::Ptr contextMenuRegion(NodeId id, std::string name, Vec2 intrinsicSize,
                            PopupMenuState& state,
                            std::function<void(Node&, Vec2)> onOpen = {},
                            PaintRenderer* renderer = nullptr,
                            VisualStyle::CanvasDraw draw = {},
                            bool focusable = true,
                            Role semanticRole = Role::Canvas);
Node::Ptr contextMenuRegion(NodeId id, std::string name, Vec2 intrinsicSize,
                            PopupMenuState& state,
                            std::function<void(Node&, Vec2)> onOpen,
                            PaintRenderer* renderer,
                            VisualStyle::CanvasSurfaceDraw draw,
                            bool focusable = true,
                            Role semanticRole = Role::Canvas);
Node::Ptr modalDialog(NodeId id, std::string title, std::string message,
                      ModalDialogState& state,
                      std::vector<ModalDialogButton> buttons,
                      PaintRenderer* renderer = nullptr,
                      ModalDialogOptions options = {});
Node::Ptr alertDialog(NodeId id, std::string title, std::string message,
                      ModalDialogState& state,
                      std::function<void()> onOk = {},
                      PaintRenderer* renderer = nullptr,
                      ModalDialogOptions options = {});
Node::Ptr confirmDialog(NodeId id, std::string title, std::string message,
                        ModalDialogState& state,
                        std::function<void()> onConfirm = {},
                        std::function<void()> onCancel = {},
                        PaintRenderer* renderer = nullptr,
                        ModalDialogOptions options = {});
Node::Ptr patternGrid(NodeId id, std::string name, bool* cells, int rows, int steps,
                      PaintRenderer* renderer = nullptr,
                      Vec2 size = {240.0f, 96.0f},
                      std::function<int()> playheadStep = {},
                      paint::PatternCellPainter cellPainter = {});
Node::Ptr xyPad(NodeId id, std::string name, ValueBinding xBinding,
                ValueBinding yBinding, PaintRenderer* renderer = nullptr,
                Vec2 size = {120.0f, 120.0f},
                paint::XYPadPainter painter = {});
Node::Ptr keyboard(NodeId id, std::string name, int firstNote, int octaves,
                   std::function<void(uint8_t note, uint8_t velocity)> noteOn,
                   std::function<void(uint8_t note)> noteOff,
                   const bool* lit = nullptr,
                   PaintRenderer* renderer = nullptr,
                   Vec2 size = {280.0f, 72.0f});
Node::Ptr envelopeEditor(NodeId id, std::string name,
                         std::vector<EnvPoint>& points,
                         PaintRenderer* renderer = nullptr,
                         Vec2 size = {200.0f, 80.0f},
                         std::vector<float>* tensions = nullptr);
Node::Ptr valueRow(NodeId id, std::string name, ValueBinding binding,
                   PaintRenderer* renderer = nullptr,
                   Vec2 size = {160.0f, 26.0f});
Node::Ptr dragNumber(NodeId id, std::string name, ValueBinding binding,
                     PaintRenderer* renderer = nullptr,
                     Vec2 size = {160.0f, 26.0f},
                     double dragSpeed = 0.0);
Node::Ptr valueField(NodeId id, std::string name, ValueBinding binding,
                     PaintRenderer* renderer = nullptr,
                     Vec2 size = {120.0f, 28.0f},
                     paint::OutlineButtonStyle style = {},
                     double dragSpeed = 0.2);
// panelBorder draws the 1px framed outline around the canvas; pass false for a
// borderless canvas (e.g. a title strip that paints its own chrome).
Node::Ptr canvas(NodeId id, std::string name, Vec2 intrinsicSize,
                 VisualStyle::CanvasDraw draw,
                 PaintRenderer* renderer = nullptr, bool focusable = false,
                 Role semanticRole = Role::Canvas, bool panelBorder = true);
Node::Ptr canvas(NodeId id, std::string name, Vec2 intrinsicSize,
                 VisualStyle::CanvasSurfaceDraw draw,
                 PaintRenderer* renderer = nullptr, bool focusable = false,
                 Role semanticRole = Role::Canvas, bool panelBorder = true);
// Horizontal dual-handle range slider. lo/hi are ValueBindings sharing one
// range (taken from lo.min/lo.max). Drag the nearest handle (opposite end
// clamps it); Left/Right nudge the last-touched handle. Exposes Slider
// semantics.
Node::Ptr rangeSlider(NodeId id, std::string name, ValueBinding lo,
                      ValueBinding hi, PaintRenderer* renderer = nullptr,
                      Vec2 size = {140.0f, 20.0f});

// Progress bar (display only). progress.get() in 0..1; a negative value renders
// an indeterminate animated sweep. Exposes Meter semantics.
Node::Ptr progressBar(NodeId id, std::string name, ValueBinding progress,
                      PaintRenderer* renderer = nullptr,
                      Vec2 size = {160.0f, 10.0f});

// Per-frame sample provider for waveformView: returns {buffer, count} in -1..1;
// the caller owns the buffer, returns {nullptr, 0} when empty.
using WaveformSource = std::function<std::pair<const float*, int>()>;
// Waveform view (display only). `source` is polled each paint; the optional
// `playhead` binding supplies the 0..1 head position (unset = none).
Node::Ptr waveformView(NodeId id, std::string name, WaveformSource source,
                       PaintRenderer* renderer = nullptr,
                       Vec2 size = {240.0f, 60.0f}, ValueBinding playhead = {});

// Spectrum view (display only): `source` returns {magnitudes, bins} (0..1),
// polled each paint and drawn as bottom-anchored bars. Canvas semantics.
Node::Ptr spectrumView(NodeId id, std::string name, WaveformSource source,
                       PaintRenderer* renderer = nullptr,
                       Vec2 size = {240.0f, 80.0f});

// Timeline ruler (display only): bar/beat ticks over [startBeat, endBeat]; the
// optional `playhead` binding supplies the 0..1 head position. Canvas semantics.
Node::Ptr timelineRuler(NodeId id, std::string name, double startBeat,
                        double endBeat, double beatsPerBar,
                        PaintRenderer* renderer = nullptr,
                        Vec2 size = {320.0f, 22.0f}, ValueBinding playhead = {});

// Automation / curve lane. `source` returns the caller-owned point vector,
// mutated in place (drag to move, double-click to add, double/right-click a
// point to remove); onChange fires after each edit. Slider semantics.
using AutoPointSource = std::function<std::vector<AutoPoint>&()>;
Node::Ptr automationLane(NodeId id, std::string name, AutoPointSource source,
                         std::function<void()> onChange = {},
                         PaintRenderer* renderer = nullptr,
                         Vec2 size = {320.0f, 100.0f});

// A draggable divider between panes. Dragging (or Left/Right/Up/Down while
// focused) writes the bound value — the adjacent pane's width (horizontal
// splitter bar, dragged along X) or height — clamped to [binding.min,
// binding.max]. `invert` flips the drag direction for panes docked at the
// far edge (dragging left grows a right-docked panel). The host rebuilds or
// re-lays-out off the bound value; the splitter itself is stateless.
Node::Ptr splitter(NodeId id, std::string name, ValueBinding binding,
                   bool horizontal = true, bool invert = false,
                   PaintRenderer* renderer = nullptr, float thickness = 6.0f);

// A single-line retained text field editing a caller-owned string in place:
// caret + selection, printable keys via the TextInput event, Left/Right
// (Shift extends selection), Home/End, Backspace/Delete, Enter commits
// (fires onCommit), Esc reverts to the value at focus time. Click to focus +
// place the caret. Pure retained -- no ImGui InputText bridge.
Node::Ptr textField(NodeId id, std::string* text,
                    PaintRenderer* renderer = nullptr,
                    Vec2 size = {160.0f, 24.0f}, std::string placeholder = {},
                    std::function<void(const std::string&)> onCommit = {});

// A vertical scroll container: add children to it and they stack as a column
// at their natural height, clipped to the view with a wheel-driven, draggable
// scrollbar when they overflow. `gap`/`padding` apply to the column; the node
// fills its parent by default (give it a fixed Length to size the viewport).
Node::Ptr scrollView(NodeId id, float gap = 0.0f, Insets padding = {},
                     PaintRenderer* renderer = nullptr);

Node::Ptr graphSurface(NodeId id, std::string name, GraphSurfaceState& state,
                       const std::vector<GraphNode>& nodes,
                       const std::vector<GraphCable>& cables,
                       GraphSurfaceCallbacks callbacks = {},
                       PaintRenderer* renderer = nullptr,
                       Vec2 size = {520.0f, 320.0f},
                       PopupMenuState* contextMenu = nullptr,
                       GraphSurfaceStyle style = {});

Node::Ptr button(NodeId id, std::string name, std::function<void(Node&)> onActivate = {},
                 PaintRenderer* renderer = nullptr,
                 paint::ButtonPainter painter = {});
// actOnPress fires onActivate on mouse DOWN (and Enter/Space) for a snappy,
// no-latency feel; the default false fires on release (the click lands only if
// the pointer is still over the button), which is the conventional button
// behaviour. Mirrors outlineIconButton's actOnPress.
Node::Ptr outlineButton(NodeId id, std::string name,
                        std::function<void(Node&)> onActivate = {},
                        PaintRenderer* renderer = nullptr,
                        Vec2 size = {72.0f, 28.0f},
                        paint::OutlineButtonStyle style = {},
                        bool selected = false, bool actOnPress = false);
Node::Ptr animatedButton(NodeId id, std::string name,
                         std::function<void(Node&)> onActivate = {},
                         PaintRenderer* renderer = nullptr,
                         Vec2 size = {110.0f, 52.0f},
                         ImU32 top = 0, ImU32 bottom = 0,
                         bool animate = true);
Node::Ptr gradientButton(NodeId id, std::string name,
                         std::function<void(Node&)> onActivate = {},
                         PaintRenderer* renderer = nullptr,
                         Vec2 size = {110.0f, 52.0f},
                         ImU32 top = 0, ImU32 bottom = 0);
Node::Ptr iconButton(NodeId id, std::string name, std::string glyph,
                     std::function<void(Node&)> onActivate = {},
                     PaintRenderer* renderer = nullptr,
                     IconFont font = IconFont::Material,
                     Vec2 size = {30.0f, 30.0f}, bool lit = false);
// Icon button in the OUTLINE chrome (drawOutlineButton: border-first, fill
// on press, selectedFill while `selected`) with a centred glyph at 70% of
// the button height. The house look for transport rows -- pass the same
// OutlineButtonStyle as the neighbouring text buttons so the family
// matches.
Node::Ptr outlineIconButton(NodeId id, std::string name, std::string glyph,
                            std::function<void(Node&)> onActivate = {},
                            PaintRenderer* renderer = nullptr,
                            Vec2 size = {30.0f, 30.0f},
                            paint::OutlineButtonStyle style = {},
                            bool selected = false,
                            IconFont font = IconFont::Material,
                            bool actOnPress = false);
Node::Ptr iconButton(NodeId id, std::string name, Icon icon,
                     std::function<void(Node&)> onActivate = {},
                     PaintRenderer* renderer = nullptr,
                     Vec2 size = {30.0f, 30.0f},
                     ImU32 accent = 0, bool active = false);
// A transport button: the house OUTLINE chrome of outlineButton with a crisp
// vector transport glyph (Icon::Record/Play/Stop/Pause../Loop) instead of a
// font glyph -- the building block of a transport bar. Same behaviour as
// outlineIconButton (selected = engaged fill; actOnPress fires on mouse down).
Node::Ptr transportButton(NodeId id, std::string name, Icon icon,
                          std::function<void(Node&)> onActivate = {},
                          PaintRenderer* renderer = nullptr,
                          Vec2 size = {36.0f, 18.0f},
                          paint::OutlineButtonStyle style = {},
                          bool selected = false, bool actOnPress = false);

// Pill group of mutually exclusive options. The binding holds the selected
// index (its min/max/step are forced to 0..count-1 step 1; a default format
// reports the selected label). Click a segment, Left/Right when focused.
// size 0 = a width estimated from the labels.
Node::Ptr segmented(NodeId id, std::string name,
                    std::vector<std::string> labels, ValueBinding binding,
                    PaintRenderer* renderer = nullptr,
                    Vec2 size = {0.0f, 24.0f});

// Tab strip for switching pages/views. The binding holds the selected tab
// index as in segmented(). Click picks a tab; Left/Right increment/decrement.
Node::Ptr tabBar(NodeId id, std::string name,
                 std::vector<std::string> labels, ValueBinding binding,
                 PaintRenderer* renderer = nullptr,
                 Vec2 size = {0.0f, 26.0f});

// Multi-state value button: Enter/Space/click advance through the options
// (wrapping); Up/Right and Down/Left step without wrapping. The binding holds
// the option index as segmented() above. size 0 = width from the labels.
Node::Ptr cycleButton(NodeId id, std::string name,
                      std::vector<std::string> labels, ValueBinding binding,
                      PaintRenderer* renderer = nullptr,
                      Vec2 size = {0.0f, 26.0f});

// Tactile key with an integrated status LED ring; the binding is the on/off
// state as toggle(). `blink` pulses the lit ring for armed/pending states.
Node::Ptr ledButton(NodeId id, std::string name, std::string glyph,
                    ValueBinding binding, bool blink = false,
                    PaintRenderer* renderer = nullptr,
                    Vec2 size = {30.0f, 30.0f}, ImU32 ledColor = 0,
                    IconFont font = IconFont::Material);

Node::Ptr toggle(NodeId id, std::string name, ValueBinding binding,
                 PaintRenderer* renderer = nullptr);
Node::Ptr checkbox(NodeId id, std::string name, ValueBinding binding,
                   PaintRenderer* renderer = nullptr,
                   Vec2 size = {0.0f, 22.0f});
Node::Ptr knob(NodeId id, std::string name, ValueBinding binding,
               PaintRenderer* renderer = nullptr,
               KnobStyle style = KnobStyle::Ring, bool bipolar = false,
               float diameter = 56.0f,
               paint::KnobPainter painter = {});
Node::Ptr knobDb(NodeId id, std::string name, ValueBinding binding,
                 double minDb, double maxDb,
                 PaintRenderer* renderer = nullptr,
                 float diameter = 56.0f,
                 paint::KnobPainter painter = {});
// Knob with a live modulation ring: `mod` is polled each rendered frame for
// the depth arc + modulated-position dot drawn over the body.
Node::Ptr knob(NodeId id, std::string name, ValueBinding binding,
               std::function<KnobMod()> mod,
               PaintRenderer* renderer = nullptr,
               KnobStyle style = KnobStyle::Ring, bool bipolar = false,
               float diameter = 56.0f,
               paint::KnobPainter painter = {});
Node::Ptr fader(NodeId id, std::string name, ValueBinding binding,
                PaintRenderer* renderer = nullptr, Vec2 size = {26.0f, 120.0f});

Node::Ptr meter(NodeId id, std::string name, double value,
                PaintRenderer* renderer = nullptr, Vec2 size = {10.0f, 80.0f});
Node::Ptr meter(NodeId id, std::string name, ValueBinding binding,
                PaintRenderer* renderer = nullptr, Vec2 size = {10.0f, 80.0f});
Node::Ptr led(NodeId id, std::string name, bool on, bool clickable = false,
              std::function<void(Node&)> onActivate = {},
              PaintRenderer* renderer = nullptr,
              float radius = 5.0f, ImU32 onColor = 0);
Node::Ptr led(NodeId id, std::string name, ValueBinding binding,
              bool clickable = false, PaintRenderer* renderer = nullptr,
              float radius = 5.0f, ImU32 onColor = 0);

} // namespace widgets

} // namespace snd::ui::retained
