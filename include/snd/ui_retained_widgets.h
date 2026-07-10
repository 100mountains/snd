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

namespace snd::ui::retained {

enum class VisualKind {
    Auto,
    Panel,
    Text,
    Button,
    IconButton,
    VectorIconButton,
    OutlineButton,
    Segmented,
    CycleButton,
    LedButton,
    Toggle,
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
    bool canvasClip = true;
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

    const VisualStyle* styleFor(const NodeId& id) const;
    // Call after layout when manually rendering retained trees; drawImGui does
    // this automatically. Keeps open anchored popups aligned for hit/paint/a11y.
    void prepareOpenPopups(Tree& tree) const;
    bool dismissOpenPopupsOutside(Tree& tree, const ImVec2& origin,
                                  const ImVec2& screenPoint) const;

    // Render a laid-out retained tree. The Tree overload uses the semantic
    // snapshot so focus/pressed/disabled states match the retained core.
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

private:
    using SemanticMap = std::unordered_map<NodeId, SemanticNode>;

    void renderNode(const Node& node, const SemanticMap* semantics,
                    const ImVec2& origin,
                    ImDrawList* drawList,
                    const draw::FrameContext& context) const;
    void renderNode(const Node& node, const SemanticMap* semantics,
                    draw::Vec2 origin, draw::Surface& surface,
                    const draw::FrameContext& context) const;
    VisualStyle resolvedStyle(const Node& node) const;

    std::unordered_map<NodeId, VisualStyle> styles_;
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
};

struct GraphSurfaceCallbacks {
    std::function<void(const GraphHit&)> onSelect;
    std::function<void(const GraphHit&)> onActivate;
    std::function<void(const GraphHit&, Vec2 graphPosition)> onContextMenu;
    std::function<void(const GraphHit&, Vec2 graphDelta)> onDrag;
    std::function<bool(const GraphHit& fromPort, const GraphHit& toPort)> canConnect;
    std::function<void(const GraphHit& fromPort, const GraphHit& toPort)> onConnect;
    std::function<void(const GraphHit& fromPort, Vec2 graphPosition)> onCablePreview;
    std::function<void(const GraphViewport&)> onViewportChanged;
};

using GraphSurfaceStyle = paint::GraphSurfaceStyle;

Vec2 graphToScreen(const GraphViewport& viewport, Vec2 graphPoint);
Rect graphToScreen(const GraphViewport& viewport, Rect graphRect);
Vec2 screenToGraph(const GraphViewport& viewport, Vec2 screenPoint);
GraphHit hitTestGraph(const GraphViewport& viewport,
                      const std::vector<GraphNode>& nodes,
                      const std::vector<GraphCable>& cables,
                      Vec2 screenPoint,
                      GraphSurfaceStyle style = {});

namespace widgets {

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
Node::Ptr canvas(NodeId id, std::string name, Vec2 intrinsicSize,
                 VisualStyle::CanvasDraw draw,
                 PaintRenderer* renderer = nullptr, bool focusable = false,
                 Role semanticRole = Role::Canvas);
Node::Ptr canvas(NodeId id, std::string name, Vec2 intrinsicSize,
                 VisualStyle::CanvasSurfaceDraw draw,
                 PaintRenderer* renderer = nullptr, bool focusable = false,
                 Role semanticRole = Role::Canvas);
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
Node::Ptr outlineButton(NodeId id, std::string name,
                        std::function<void(Node&)> onActivate = {},
                        PaintRenderer* renderer = nullptr,
                        Vec2 size = {72.0f, 28.0f},
                        paint::OutlineButtonStyle style = {},
                        bool selected = false);
Node::Ptr animatedButton(NodeId id, std::string name,
                         std::function<void(Node&)> onActivate = {},
                         PaintRenderer* renderer = nullptr,
                         Vec2 size = {110.0f, 52.0f},
                         ImU32 top = 0, ImU32 bottom = 0,
                         bool animate = true);
Node::Ptr iconButton(NodeId id, std::string name, std::string glyph,
                     std::function<void(Node&)> onActivate = {},
                     PaintRenderer* renderer = nullptr,
                     IconFont font = IconFont::Material);
Node::Ptr iconButton(NodeId id, std::string name, Icon icon,
                     std::function<void(Node&)> onActivate = {},
                     PaintRenderer* renderer = nullptr,
                     Vec2 size = {30.0f, 30.0f},
                     ImU32 accent = 0, bool active = false);

// Pill group of mutually exclusive options. The binding holds the selected
// index (its min/max/step are forced to 0..count-1 step 1; a default format
// reports the selected label). Click a segment, Left/Right when focused.
// size 0 = a width estimated from the labels.
Node::Ptr segmented(NodeId id, std::string name,
                    std::vector<std::string> labels, ValueBinding binding,
                    PaintRenderer* renderer = nullptr,
                    Vec2 size = {0.0f, 24.0f});

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
Node::Ptr knob(NodeId id, std::string name, ValueBinding binding,
               PaintRenderer* renderer = nullptr,
               KnobStyle style = KnobStyle::Ring, bool bipolar = false,
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
