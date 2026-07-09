// Retained widgets and ImDrawList renderer.
//
// The technical namespace stays snd::ui::retained. This layer is intentionally
// separate from the retained core: it creates common audio/UI nodes and renders
// them through snd::ui::paint so retained and immediate widgets share pixels.
#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "snd/ui_paint.h"
#include "snd/ui_retained.h"

namespace snd::ui::retained {

enum class VisualKind {
    Auto,
    Panel,
    Text,
    Button,
    IconButton,
    Toggle,
    Knob,
    Led,
    Meter,
    Fader,
    Badge,
    SectionHeader,
    ListItem,
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

    VisualKind kind = VisualKind::Auto;
    KnobStyle knobStyle = KnobStyle::Ring;
    IconFont iconFont = IconFont::Material;
    std::string glyph;
    bool bipolar = false;
    ImU32 accent = 0;
    ImU32 face = 0;
    float meterFloorDb = -48.0f;
    float ledRadius = 5.0f;
    float fontScale = 1.0f;
    bool panelFill = false;
    bool panelBorder = false;
    CanvasDraw canvasDraw;
};

class PaintRenderer {
public:
    void setStyle(const NodeId& id, const VisualStyle& style);
    void clearStyle(const NodeId& id);
    void clearStyles();

    const VisualStyle* styleFor(const NodeId& id) const;

    // Render a laid-out retained tree. The Tree overload uses the semantic
    // snapshot so focus/pressed/disabled states match the retained core.
    void render(const Tree& tree, ImDrawList* drawList = nullptr) const;
    void render(const Tree& tree, const ImVec2& origin,
                ImDrawList* drawList = nullptr) const;
    void render(const Node& root, ImDrawList* drawList = nullptr) const;
    void render(const Node& root, const ImVec2& origin,
                ImDrawList* drawList = nullptr) const;

private:
    using SemanticMap = std::unordered_map<NodeId, SemanticNode>;

    void renderNode(const Node& node, const SemanticMap* semantics,
                    const ImVec2& origin,
                    ImDrawList* drawList) const;
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

namespace widgets {

Node::Ptr panel(NodeId id, Layout layout = {}, Insets padding = {});
Node::Ptr row(NodeId id, float gap = 6.0f, Insets padding = {});
Node::Ptr column(NodeId id, float gap = 6.0f, Insets padding = {});
Node::Ptr label(NodeId id, std::string text, PaintRenderer* renderer = nullptr);
Node::Ptr sectionHeader(NodeId id, std::string text, PaintRenderer* renderer = nullptr);
Node::Ptr badge(NodeId id, std::string text, PaintRenderer* renderer = nullptr);
Node::Ptr listItem(NodeId id, std::string text, bool selected = false,
                   std::function<void(Node&)> onActivate = {},
                   PaintRenderer* renderer = nullptr);
Node::Ptr patternGrid(NodeId id, std::string name, bool* cells, int rows, int steps,
                      PaintRenderer* renderer = nullptr,
                      Vec2 size = {240.0f, 96.0f},
                      std::function<int()> playheadStep = {});
Node::Ptr xyPad(NodeId id, std::string name, ValueBinding xBinding,
                ValueBinding yBinding, PaintRenderer* renderer = nullptr,
                Vec2 size = {120.0f, 120.0f});
Node::Ptr keyboard(NodeId id, std::string name, int firstNote, int octaves,
                   std::function<void(uint8_t note, uint8_t velocity)> noteOn,
                   std::function<void(uint8_t note)> noteOff,
                   const bool* lit = nullptr,
                   PaintRenderer* renderer = nullptr,
                   Vec2 size = {280.0f, 72.0f});
Node::Ptr canvas(NodeId id, std::string name, Vec2 intrinsicSize,
                 VisualStyle::CanvasDraw draw,
                 PaintRenderer* renderer = nullptr, bool focusable = false,
                 Role semanticRole = Role::Canvas);

Node::Ptr button(NodeId id, std::string name, std::function<void(Node&)> onActivate = {},
                 PaintRenderer* renderer = nullptr);
Node::Ptr iconButton(NodeId id, std::string name, std::string glyph,
                     std::function<void(Node&)> onActivate = {},
                     PaintRenderer* renderer = nullptr,
                     IconFont font = IconFont::Material);

Node::Ptr toggle(NodeId id, std::string name, ValueBinding binding,
                 PaintRenderer* renderer = nullptr);
Node::Ptr knob(NodeId id, std::string name, ValueBinding binding,
               PaintRenderer* renderer = nullptr,
               KnobStyle style = KnobStyle::Ring, bool bipolar = false,
               float diameter = 56.0f);
Node::Ptr fader(NodeId id, std::string name, ValueBinding binding,
                PaintRenderer* renderer = nullptr, Vec2 size = {26.0f, 120.0f});

Node::Ptr meter(NodeId id, std::string name, double value,
                PaintRenderer* renderer = nullptr, Vec2 size = {10.0f, 80.0f});
Node::Ptr meter(NodeId id, std::string name, ValueBinding binding,
                PaintRenderer* renderer = nullptr, Vec2 size = {10.0f, 80.0f});
Node::Ptr led(NodeId id, std::string name, bool on, bool clickable = false,
              std::function<void(Node&)> onActivate = {},
              PaintRenderer* renderer = nullptr);
Node::Ptr led(NodeId id, std::string name, ValueBinding binding,
              bool clickable = false, PaintRenderer* renderer = nullptr);

} // namespace widgets

} // namespace snd::ui::retained
