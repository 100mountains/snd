// snd::ui::retained -- headless retained UI tree, layout, focus, events, and
// platform-neutral semantics. Rendering stays shared with snd::ui paint helpers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace snd::ui::retained {

using NodeId = std::string;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool contains(Vec2 p) const;
};

struct Insets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    static Insets all(float v);
    static Insets xy(float x, float y);
};

enum class Role {
    None,
    Group,
    Text,
    Button,
    Toggle,
    Slider,
    Meter,
    ListItem,
    Menu,
    MenuItem,
    ComboBox,
    Canvas,
    Custom,
};

enum class SemanticState : uint32_t {
    None = 0,
    Focusable = 1u << 0,
    Focused = 1u << 1,
    Disabled = 1u << 2,
    Pressed = 1u << 3,
    Checked = 1u << 4,
    Selected = 1u << 5,
    Expanded = 1u << 6,
    Hidden = 1u << 7,
    FocusVisible = 1u << 8,
};
using SemanticStates = uint32_t;

constexpr SemanticStates stateMask(SemanticState state)
{
    return static_cast<SemanticStates>(state);
}

constexpr SemanticStates operator|(SemanticState a, SemanticState b)
{
    return stateMask(a) | stateMask(b);
}

constexpr SemanticStates operator|(SemanticStates a, SemanticState b)
{
    return a | stateMask(b);
}

inline SemanticStates& operator|=(SemanticStates& a, SemanticState b)
{
    a = a | b;
    return a;
}

bool hasState(SemanticStates states, SemanticState state);

enum class Action {
    Focus,
    Activate,
    OpenMenu,
    Increment,
    Decrement,
    SetValue,
};

struct ValueRange {
    bool hasNumeric = false;
    double value = 0.0;
    double min = 0.0;
    double max = 1.0;
    double step = 0.01;
    std::string text;
};

struct Semantics {
    Role role = Role::None;
    std::string name;
    std::string description;
    std::string labelledBy;
    ValueRange value;
    SemanticStates states = 0;
    std::vector<Action> actions;
    bool hidden = false;
};

struct SemanticNode {
    NodeId id;
    NodeId parent;
    Rect bounds;
    Role role = Role::None;
    std::string name;
    std::string description;
    std::string labelledBy;
    ValueRange value;
    SemanticStates states = 0;
    std::vector<Action> actions;
};

struct InteractionState {
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
    bool disabled = false;
    bool focusable = false;
    bool checked = false;
    bool selected = false;
    bool expanded = false;
};

struct NodeSnapshot {
    NodeId id;
    NodeId parent;
    Rect bounds;
    Role role = Role::None;
    ValueRange value;
    SemanticStates states = 0;
    InteractionState interaction;
};

enum class ValidationIssueKind {
    EmptyId,
    DuplicateId,
    MissingAccessibleName,
};

struct ValidationIssue {
    ValidationIssueKind kind = ValidationIssueKind::EmptyId;
    NodeId id;
};

enum class LayoutKind {
    Stack,
    Row,
    Column,
};

enum class Align {
    Start,
    Center,
    End,
    Stretch,
};

enum class LengthMode {
    Intrinsic,
    Fixed,
    Fill,
};

struct Length {
    LengthMode mode = LengthMode::Intrinsic;
    float value = 0.0f;
    float weight = 0.0f;

    static Length intrinsic();
    static Length fixed(float value);
    static Length fill(float weight = 1.0f);
};

struct Layout {
    LayoutKind kind = LayoutKind::Column;
    Insets padding;
    float gap = 0.0f;
    Align mainAlign = Align::Start;
    Align crossAlign = Align::Stretch;
};

enum class EventType {
    MouseMove,
    MouseDown,
    MouseUp,
    MouseWheel,
    ContextMenu,
    KeyDown,
    KeyUp,
    TextInput,
};

enum class MouseButton {
    Left,
    Right,
    Middle,
    None,
};

enum class Key {
    Unknown,
    Tab,
    Enter,
    Space,
    Left,
    Right,
    Up,
    Down,
    Escape,
    Backspace,
    Delete,
    Home,
    End,
};

struct Event {
    // Pointer coordinates are tree-local. Use clickCount >= 2 for
    // double-click style gestures; delta, wheelDelta, key, modifiers, and text
    // are adapter-provided.
    //
    // KeyDown/KeyUp carry physical/navigation key intent in key. TextInput is
    // delivered separately to the focused node with committed UTF-8 in text;
    // the string may contain one or more Unicode scalar values from IME or
    // paste. Existing KeyDown text payloads remain valid for compatibility,
    // but new backends should prefer TextInput for character insertion.
    EventType type = EventType::MouseMove;
    Vec2 position;
    Vec2 delta;
    MouseButton button = MouseButton::Left;
    Key key = Key::Unknown;
    std::string text;
    Vec2 wheelDelta;
    int clickCount = 0;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
};

struct ValueBinding {
    std::function<double()> get;
    std::function<void(double)> set;
    std::function<std::string(double)> format;
    double min = 0.0;
    double max = 1.0;
    double step = 0.01;
};

class Node {
public:
    using Ptr = std::unique_ptr<Node>;

    explicit Node(NodeId id, Role role = Role::Group);
    static Ptr make(NodeId id, Role role = Role::Group);

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) = delete;
    Node& operator=(Node&&) = delete;

    const NodeId& id() const { return id_; }
    Role role() const { return role_; }
    void setRole(Role role);

    Node* parent() { return parent_; }
    const Node* parent() const { return parent_; }

    std::size_t childCount() const { return children_.size(); }
    Node* child(std::size_t index);
    const Node* child(std::size_t index) const;
    const std::vector<Ptr>& children() const { return children_; }

    Node& addChild(Ptr child);
    Ptr removeChild(const NodeId& id);
    Node* find(const NodeId& id);
    const Node* find(const NodeId& id) const;

    void setLayout(const Layout& layout);
    const Layout& layout() const { return layout_; }

    void setSize(Length width, Length height);
    const Length& width() const { return width_; }
    const Length& height() const { return height_; }

    void setIntrinsicSize(Vec2 size);
    Vec2 intrinsicSize() const { return intrinsic_; }

    void setBounds(Rect bounds);
    Rect bounds() const { return bounds_; }

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // Overlay nodes are laid out for their own subtree but do not consume
    // parent row/column space. Menus/popups use this for floating children.
    void setOverlay(bool overlay);
    bool overlay() const { return overlay_; }

    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }

    void setFocusable(bool focusable);
    bool focusable() const { return focusable_; }
    bool focused() const { return focused_; }
    bool focusVisible() const { return focusVisible_; }
    bool hovered() const { return hovered_; }
    bool pressed() const { return pressed_; }

    void setSemantics(const Semantics& semantics);
    Semantics& semantics();
    const Semantics& semantics() const { return semantics_; }

    void setOnActivate(std::function<void(Node&)> callback);
    bool activate();
    void setOnAction(std::function<bool(Node&, Action, double)> callback);
    void setOnEvent(std::function<bool(Node&, const Event&)> callback);
    void setOnRefresh(std::function<bool(Node&)> callback);
    // Virtual semantic children are for Canvas/spatial controls whose internal
    // parts are not normal retained layout children.
    void setSemanticChildren(
        std::function<void(const Node&, std::vector<SemanticNode>&)> provider);
    void setOnSemanticAction(
        std::function<bool(Node&, const NodeId&, Action, double)> callback);

    void setValueBinding(ValueBinding binding);
    void clearValueBinding();
    const ValueBinding* valueBinding() const;

    bool perform(Action action, double value = 0.0);

    Node* hitTest(Vec2 point);
    const Node* hitTest(Vec2 point) const;

    void markDirty();
    bool dirty() const { return dirty_; }
    bool subtreeDirty() const;
    void clearDirtyRecursive();

private:
    friend class Tree;

    void setFocused(bool focused, bool focusVisible);
    void setHovered(bool hovered);
    void setPressed(bool pressed);
    bool handleEvent(const Event& event);
    bool refreshBindingState();
    bool refreshExternalState();
    bool provideSemanticChildren(std::vector<SemanticNode>& out) const;
    bool performSemanticChildAction(const NodeId& id, Action action, double value);

    NodeId id_;
    Role role_ = Role::Group;
    Node* parent_ = nullptr;
    std::vector<Ptr> children_;
    Layout layout_;
    Length width_ = Length::intrinsic();
    Length height_ = Length::intrinsic();
    Vec2 intrinsic_;
    Rect bounds_;
    bool visible_ = true;
    bool overlay_ = false;
    bool enabled_ = true;
    bool focusable_ = false;
    bool focused_ = false;
    bool focusVisible_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    bool dirty_ = true;
    Semantics semantics_;
    std::function<void(Node&)> onActivate_;
    std::function<bool(Node&, Action, double)> onAction_;
    std::function<bool(Node&, const Event&)> onEvent_;
    std::function<bool(Node&)> onRefresh_;
    std::function<void(const Node&, std::vector<SemanticNode>&)> semanticChildren_;
    std::function<bool(Node&, const NodeId&, Action, double)> onSemanticAction_;
    std::unique_ptr<ValueBinding> valueBinding_;
    bool hasObservedBinding_ = false;
    ValueRange observedBindingValue_;
    SemanticStates observedBindingStates_ = 0;
};

class Tree {
public:
    explicit Tree(Node::Ptr root);
    static Tree withRoot(NodeId id);

    Node& root() { return *root_; }
    const Node& root() const { return *root_; }

    Node* find(const NodeId& id);
    const Node* find(const NodeId& id) const;

    void layout(Vec2 size);
    Vec2 viewport() const { return viewport_; }

    Node* hitTest(Vec2 point);
    const Node* hitTest(Vec2 point) const;

    bool focus(const NodeId& id);
    bool clearFocus();
    bool focusNext(bool reverse = false);
    Node* focused();
    const Node* focused() const;
    Node* hovered();
    const Node* hovered() const;

    bool dispatch(const Event& event);
    bool performAction(const NodeId& id, Action action, double value = 0.0);
    bool performSemanticAction(const NodeId& id, Action action, double value = 0.0);
    bool value(const NodeId& id, ValueRange& out) const;
    bool setValue(const NodeId& id, double value);
    bool incrementValue(const NodeId& id);
    bool decrementValue(const NodeId& id);

    bool refreshBoundValues();
    std::vector<ValidationIssue> validate() const;
    bool nodeSnapshot(const NodeId& id, NodeSnapshot& out) const;
    std::vector<NodeSnapshot> nodeSnapshot() const;
    bool semanticNode(const NodeId& id, SemanticNode& out) const;
    std::vector<SemanticNode> semanticSnapshot() const;

    bool dirty() const;
    void clearDirty();

private:
    void collectFocusable(std::vector<Node*>& out);
    void collectFocusable(const Node& node, std::vector<const Node*>& out) const;
    void collectNodes(const Node& node, const NodeId& parent,
                      std::vector<NodeSnapshot>& out) const;
    void collectSemantics(const Node& node, const NodeId& parent,
                          std::vector<SemanticNode>& out) const;
    bool setHoveredNode(Node* node);

    Node::Ptr root_;
    Vec2 viewport_;
    NodeId focusedId_;
    NodeId hoveredId_;
    NodeId pressedId_;
};

} // namespace snd::ui::retained
