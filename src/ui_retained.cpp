#include "snd/ui_retained.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

namespace snd::ui::retained {

namespace {

constexpr float kEpsilon = 0.0001f;

bool nearlyEqual(float a, float b)
{
    return std::fabs(a - b) <= kEpsilon;
}

bool sameRect(Rect a, Rect b)
{
    return nearlyEqual(a.x, b.x) && nearlyEqual(a.y, b.y) &&
           nearlyEqual(a.w, b.w) && nearlyEqual(a.h, b.h);
}

float clampNonNegative(float v)
{
    return std::max(0.0f, v);
}

double clampValue(double value, double min, double max)
{
    if (min > max)
        std::swap(min, max);
    return std::max(min, std::min(max, value));
}

bool isInteractive(const Node& node)
{
    return node.visible() && node.enabled();
}

Role effectiveRole(const Node& node)
{
    return node.semantics().role != Role::None ? node.semantics().role : node.role();
}

ValueRange currentValue(const Node& node);

SemanticStates derivedStates(const Node& node)
{
    SemanticStates states = node.semantics().states;
    if (effectiveRole(node) == Role::Toggle) {
        const ValueRange value = currentValue(node);
        if (value.hasNumeric) {
            double min = value.min;
            double max = value.max;
            if (min > max)
                std::swap(min, max);
            if (value.value >= (min + max) * 0.5)
                states |= SemanticState::Checked;
            else
                states &= ~stateMask(SemanticState::Checked);
        }
    }
    if (node.focusable())
        states |= SemanticState::Focusable;
    if (node.focused())
        states |= SemanticState::Focused;
    if (node.focusVisible())
        states |= SemanticState::FocusVisible;
    if (!node.enabled())
        states |= SemanticState::Disabled;
    if (node.pressed())
        states |= SemanticState::Pressed;
    return states;
}

ValueRange currentValue(const Node& node)
{
    ValueRange value = node.semantics().value;
    if (const ValueBinding* binding = node.valueBinding()) {
        value.hasNumeric = true;
        value.min = binding->min;
        value.max = binding->max;
        value.step = binding->step;
        if (binding->get)
            value.value = binding->get();
        if (binding->format)
            value.text = binding->format(value.value);
    }
    return value;
}

bool sameValueRange(const ValueRange& a, const ValueRange& b)
{
    return a.hasNumeric == b.hasNumeric &&
           std::abs(a.value - b.value) < 0.0000001 &&
           std::abs(a.min - b.min) < 0.0000001 &&
           std::abs(a.max - b.max) < 0.0000001 &&
           std::abs(a.step - b.step) < 0.0000001 &&
           a.text == b.text;
}

InteractionState interactionState(const Node& node, SemanticStates states)
{
    InteractionState interaction;
    interaction.hovered = node.hovered();
    interaction.pressed = node.pressed();
    interaction.focused = node.focused();
    interaction.disabled = !node.enabled();
    interaction.focusable = node.focusable();
    interaction.checked = hasState(states, SemanticState::Checked);
    interaction.selected = hasState(states, SemanticState::Selected);
    interaction.expanded = hasState(states, SemanticState::Expanded);
    return interaction;
}

bool hasAction(const std::vector<Action>& actions, Action action)
{
    return std::find(actions.begin(), actions.end(), action) != actions.end();
}

void appendAction(std::vector<Action>& actions, Action action)
{
    if (!hasAction(actions, action))
        actions.push_back(action);
}

bool hasSettableBinding(const Node& node)
{
    const ValueBinding* binding = node.valueBinding();
    return binding && binding->set;
}

bool hasReadableSettableBinding(const Node& node)
{
    const ValueBinding* binding = node.valueBinding();
    return binding && binding->get && binding->set;
}

bool hasToggleBinding(const Node& node)
{
    return effectiveRole(node) == Role::Toggle && hasReadableSettableBinding(node);
}

void appendDerivedActions(const Node& node, std::vector<Action>& actions)
{
    if (node.focusable() && node.enabled())
        appendAction(actions, Action::Focus);
    if (hasToggleBinding(node))
        appendAction(actions, Action::Activate);
    if (hasSettableBinding(node))
        appendAction(actions, Action::SetValue);
    if (hasReadableSettableBinding(node)) {
        appendAction(actions, Action::Increment);
        appendAction(actions, Action::Decrement);
    }
}

Node* findActiveModal(Node& node)
{
    if (!node.visible())
        return nullptr;
    const auto& children = node.children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (Node* modal = findActiveModal(**it))
            return modal;
    }
    return node.modal() ? &node : nullptr;
}

const Node* findActiveModal(const Node& node)
{
    if (!node.visible())
        return nullptr;
    const auto& children = node.children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (const Node* modal = findActiveModal(**it))
            return modal;
    }
    return node.modal() ? &node : nullptr;
}

bool containsNode(const Node& root, const Node& target)
{
    if (&root == &target)
        return true;
    for (const auto& child : root.children())
        if (containsNode(*child, target))
            return true;
    return false;
}

Node* firstFocusable(Node& node)
{
    if (!node.visible() || !node.enabled())
        return nullptr;
    if (node.focusable())
        return &node;
    for (const auto& child : node.children())
        if (Node* next = firstFocusable(*child))
            return next;
    return nullptr;
}

float lengthValue(const Length& length, float intrinsic, float fillSize)
{
    switch (length.mode) {
    case LengthMode::Fixed:
        return clampNonNegative(length.value);
    case LengthMode::Fill:
        return clampNonNegative(fillSize);
    case LengthMode::Intrinsic:
    default:
        return clampNonNegative(intrinsic);
    }
}

float mainIntrinsic(const Node& node, bool row)
{
    return row ? node.intrinsicSize().x : node.intrinsicSize().y;
}

float crossIntrinsic(const Node& node, bool row)
{
    return row ? node.intrinsicSize().y : node.intrinsicSize().x;
}

const Length& mainLength(const Node& node, bool row)
{
    return row ? node.width() : node.height();
}

const Length& crossLength(const Node& node, bool row)
{
    return row ? node.height() : node.width();
}

float alignOffset(float available, float used, Align align)
{
    float spare = std::max(0.0f, available - used);
    switch (align) {
    case Align::Center:
        return spare * 0.5f;
    case Align::End:
        return spare;
    case Align::Start:
    case Align::Stretch:
    default:
        return 0.0f;
    }
}

Rect contentRect(const Node& node)
{
    Rect b = node.bounds();
    const Insets& p = node.layout().padding;
    b.x += p.left;
    b.y += p.top;
    b.w = clampNonNegative(b.w - p.left - p.right);
    b.h = clampNonNegative(b.h - p.top - p.bottom);
    return b;
}

void layoutNode(Node& node);

void layoutStack(Node& node)
{
    Rect c = contentRect(node);
    for (const auto& childPtr : node.children()) {
        Node& child = *childPtr;
        if (!child.visible())
            continue;

        float w = lengthValue(child.width(), child.intrinsicSize().x, c.w);
        float h = lengthValue(child.height(), child.intrinsicSize().y, c.h);
        if (node.layout().mainAlign == Align::Stretch &&
            child.width().mode == LengthMode::Intrinsic)
            w = c.w;
        if (node.layout().crossAlign == Align::Stretch &&
            child.height().mode == LengthMode::Intrinsic)
            h = c.h;

        child.setBounds({c.x + alignOffset(c.w, w, node.layout().mainAlign),
                         c.y + alignOffset(c.h, h, node.layout().crossAlign),
                         w, h});
        layoutNode(child);
    }
}

void layoutLinear(Node& node, bool row)
{
    Rect c = contentRect(node);
    const float mainAvail = row ? c.w : c.h;
    const float crossAvail = row ? c.h : c.w;

    std::vector<Node*> visible;
    visible.reserve(node.childCount());
    std::vector<Node*> overlays;
    overlays.reserve(node.childCount());
    for (const auto& childPtr : node.children()) {
        if (!childPtr->visible())
            continue;
        if (childPtr->overlay()) {
            overlays.push_back(childPtr.get());
            continue;
        }
        visible.push_back(childPtr.get());
    }

    const float totalGap =
        visible.empty() ? 0.0f : clampNonNegative(node.layout().gap) * (visible.size() - 1);

    float fixedMain = 0.0f;
    float totalWeight = 0.0f;
    for (Node* child : visible) {
        const Length& len = mainLength(*child, row);
        if (len.mode == LengthMode::Fill) {
            totalWeight += std::max(0.0f, len.weight);
        } else {
            fixedMain += lengthValue(len, mainIntrinsic(*child, row), 0.0f);
        }
    }

    const float availableForFill =
        std::max(0.0f, mainAvail - totalGap - fixedMain);
    const float usedMain =
        fixedMain + totalGap + (totalWeight > 0.0f ? availableForFill : 0.0f);
    float cursor = (row ? c.x : c.y) +
                   (totalWeight > 0.0f ? 0.0f
                                        : alignOffset(mainAvail, usedMain,
                                                      node.layout().mainAlign));

    for (Node* child : visible) {
        const Length& m = mainLength(*child, row);
        const Length& cr = crossLength(*child, row);

        float mainSize = 0.0f;
        if (m.mode == LengthMode::Fill) {
            float weight = std::max(0.0f, m.weight);
            mainSize = totalWeight > 0.0f ? availableForFill * (weight / totalWeight)
                                          : 0.0f;
        } else {
            mainSize = lengthValue(m, mainIntrinsic(*child, row), 0.0f);
        }

        float crossSize = 0.0f;
        if (cr.mode == LengthMode::Fill ||
            (node.layout().crossAlign == Align::Stretch &&
             cr.mode == LengthMode::Intrinsic)) {
            crossSize = crossAvail;
        } else {
            crossSize = lengthValue(cr, crossIntrinsic(*child, row), crossAvail);
        }

        const float crossOrigin = (row ? c.y : c.x) +
                                  alignOffset(crossAvail, crossSize,
                                              node.layout().crossAlign);

        if (row)
            child->setBounds({cursor, crossOrigin, mainSize, crossSize});
        else
            child->setBounds({crossOrigin, cursor, crossSize, mainSize});

        layoutNode(*child);
        cursor += mainSize + clampNonNegative(node.layout().gap);
    }

    for (Node* child : overlays) {
        float w = lengthValue(child->width(), child->intrinsicSize().x, c.w);
        float h = lengthValue(child->height(), child->intrinsicSize().y, c.h);
        if (row)
            child->setBounds({c.x, c.y, w, h});
        else
            child->setBounds({c.x, c.y, w, h});
        layoutNode(*child);
    }
}

// A vertical scroll container: children stack as a column at their NATURAL
// height (overflowing the node), the whole column is offset up by the clamped
// scroll position, and content/view heights are recorded so the widget can
// draw + drive a scrollbar. Render clips to the node's rect (PaintRenderer).
void layoutScroll(Node& node)
{
    const Rect c = contentRect(node);
    constexpr float kBarGutter = 10.0f; // space reserved for the scrollbar

    // total natural content height (fixed/intrinsic child heights + gaps)
    float contentH = 0.0f;
    int visibleCount = 0;
    for (const auto& childPtr : node.children()) {
        if (!childPtr->visible() || childPtr->overlay())
            continue;
        contentH += lengthValue(childPtr->height(), childPtr->intrinsicSize().y, 0.0f);
        ++visibleCount;
    }
    if (visibleCount > 1)
        contentH += node.layout().gap * (float)(visibleCount - 1);
    contentH += node.layout().padding.top + node.layout().padding.bottom;

    const float viewH = node.bounds().h;
    const bool overflow = contentH > viewH + 0.5f;
    node.setLayoutMetrics(contentH, viewH);

    // clamp the scroll position into range
    const float maxScroll = std::max(0.0f, contentH - viewH);
    if (node.scrollY() < 0.0f)
        node.setScrollY(0.0f);
    if (node.scrollY() > maxScroll)
        node.setScrollY(maxScroll);

    const float innerW = c.w - (overflow ? kBarGutter : 0.0f);
    float cursor = c.y - node.scrollY();
    for (const auto& childPtr : node.children()) {
        Node& child = *childPtr;
        if (!child.visible() || child.overlay())
            continue;
        const float h =
            lengthValue(child.height(), child.intrinsicSize().y, 0.0f);
        float w = lengthValue(child.width(), child.intrinsicSize().x, innerW);
        if (node.layout().crossAlign == Align::Stretch &&
            child.width().mode == LengthMode::Intrinsic)
            w = innerW;
        child.setBounds({c.x + alignOffset(innerW, w, node.layout().crossAlign),
                         cursor, w, h});
        layoutNode(child);
        cursor += h + node.layout().gap;
    }

    // overlays (menus/popups) anchor to the view, not the scrolled content
    for (const auto& childPtr : node.children()) {
        if (!childPtr->visible() || !childPtr->overlay())
            continue;
        Node& child = *childPtr;
        const float w = lengthValue(child.width(), child.intrinsicSize().x, c.w);
        const float h = lengthValue(child.height(), child.intrinsicSize().y, c.h);
        child.setBounds({c.x, c.y, w, h});
        layoutNode(child);
    }
}

void layoutNode(Node& node)
{
    if (node.scroll()) {
        layoutScroll(node);
        return;
    }
    switch (node.layout().kind) {
    case LayoutKind::Stack:
        layoutStack(node);
        break;
    case LayoutKind::Row:
        layoutLinear(node, true);
        break;
    case LayoutKind::Column:
        layoutLinear(node, false);
        break;
    }
}

} // namespace

bool Rect::contains(Vec2 p) const
{
    return p.x >= x && p.y >= y && p.x <= x + w && p.y <= y + h;
}

Insets Insets::all(float v)
{
    return {v, v, v, v};
}

Insets Insets::xy(float x, float y)
{
    return {x, y, x, y};
}

bool hasState(SemanticStates states, SemanticState state)
{
    return (states & stateMask(state)) != 0;
}

Length Length::intrinsic()
{
    return {};
}

Length Length::fixed(float value)
{
    Length out;
    out.mode = LengthMode::Fixed;
    out.value = value;
    return out;
}

Length Length::fill(float weight)
{
    Length out;
    out.mode = LengthMode::Fill;
    out.weight = weight > 0.0f ? weight : 1.0f;
    return out;
}

Node::Node(NodeId id, Role role) : id_(std::move(id)), role_(role) {}

Node::Ptr Node::make(NodeId id, Role role)
{
    return std::make_unique<Node>(std::move(id), role);
}

void Node::setRole(Role role)
{
    if (role_ == role)
        return;
    role_ = role;
    markDirty();
}

Node* Node::child(std::size_t index)
{
    return index < children_.size() ? children_[index].get() : nullptr;
}

const Node* Node::child(std::size_t index) const
{
    return index < children_.size() ? children_[index].get() : nullptr;
}

Node& Node::addChild(Ptr child)
{
    if (!child)
        return *this;
    child->parent_ = this;
    Node& ref = *child;
    children_.push_back(std::move(child));
    markDirty();
    return ref;
}

Node::Ptr Node::removeChild(const NodeId& id)
{
    auto it = std::find_if(children_.begin(), children_.end(),
                           [&](const Ptr& child) { return child->id() == id; });
    if (it == children_.end())
        return nullptr;

    Ptr out = std::move(*it);
    out->parent_ = nullptr;
    children_.erase(it);
    markDirty();
    return out;
}

Node* Node::find(const NodeId& id)
{
    if (id_ == id)
        return this;
    for (const auto& child : children_)
        if (Node* found = child->find(id))
            return found;
    return nullptr;
}

const Node* Node::find(const NodeId& id) const
{
    if (id_ == id)
        return this;
    for (const auto& child : children_)
        if (const Node* found = child->find(id))
            return found;
    return nullptr;
}

void Node::setLayout(const Layout& layout)
{
    layout_ = layout;
    markDirty();
}

void Node::setSize(Length width, Length height)
{
    width_ = width;
    height_ = height;
    markDirty();
}

void Node::setIntrinsicSize(Vec2 size)
{
    if (nearlyEqual(intrinsic_.x, size.x) && nearlyEqual(intrinsic_.y, size.y))
        return;
    intrinsic_ = size;
    markDirty();
}

void Node::setBounds(Rect bounds)
{
    if (sameRect(bounds_, bounds))
        return;
    bounds_ = bounds;
    markDirty();
}

void Node::setVisible(bool visible)
{
    if (visible_ == visible)
        return;
    visible_ = visible;
    markDirty();
}

void Node::setOverlay(bool overlay)
{
    if (overlay_ == overlay)
        return;
    overlay_ = overlay;
    markDirty();
}

void Node::setModal(bool modal)
{
    if (modal_ == modal)
        return;
    modal_ = modal;
    markDirty();
}

void Node::setScroll(bool scroll)
{
    if (scroll_ == scroll)
        return;
    scroll_ = scroll;
    markDirty();
}

void Node::setEnabled(bool enabled)
{
    if (enabled_ == enabled)
        return;
    enabled_ = enabled;
    markDirty();
}

void Node::setCursorStyle(CursorStyle style)
{
    if (cursorStyle_ == style)
        return;
    cursorStyle_ = style;
    markDirty();
}

void Node::setFocusable(bool focusable)
{
    if (focusable_ == focusable)
        return;
    focusable_ = focusable;
    markDirty();
}

void Node::setSemantics(const Semantics& semantics)
{
    semantics_ = semantics;
    markDirty();
}

Semantics& Node::semantics()
{
    markDirty();
    return semantics_;
}

void Node::setOnActivate(std::function<void(Node&)> callback)
{
    onActivate_ = std::move(callback);
    markDirty();
}

void Node::setOnEvent(std::function<bool(Node&, const Event&)> callback)
{
    onEvent_ = std::move(callback);
    markDirty();
}

void Node::setOnAction(std::function<bool(Node&, Action, double)> callback)
{
    onAction_ = std::move(callback);
    markDirty();
}

void Node::setOnRefresh(std::function<bool(Node&)> callback)
{
    onRefresh_ = std::move(callback);
    markDirty();
}

void Node::setSemanticChildren(
    std::function<void(const Node&, std::vector<SemanticNode>&)> provider)
{
    semanticChildren_ = std::move(provider);
    markDirty();
}

void Node::setOnSemanticAction(
    std::function<bool(Node&, const NodeId&, Action, double)> callback)
{
    onSemanticAction_ = std::move(callback);
    markDirty();
}

bool Node::activate()
{
    if (!isInteractive(*this))
        return false;

    if (onActivate_) {
        onActivate_(*this);
        refreshBindingState();
        markDirty();
        return true;
    }

    if (hasToggleBinding(*this)) {
        const double current = valueBinding_->get();
        const double min = std::min(valueBinding_->min, valueBinding_->max);
        const double max = std::max(valueBinding_->min, valueBinding_->max);
        const bool nextOn = current < (min + max) * 0.5;
        valueBinding_->set(nextOn ? valueBinding_->max : valueBinding_->min);
        refreshBindingState();
        markDirty();
        return true;
    }

    if (onAction_ && onAction_(*this, Action::Activate, 0.0)) {
        refreshBindingState();
        markDirty();
        return true;
    }

    return false;
}

void Node::setValueBinding(ValueBinding binding)
{
    valueBinding_ = std::make_unique<ValueBinding>(std::move(binding));
    hasObservedBinding_ = false;
    refreshBindingState();
    markDirty();
}

void Node::clearValueBinding()
{
    if (!valueBinding_)
        return;
    valueBinding_.reset();
    hasObservedBinding_ = false;
    observedBindingValue_ = {};
    observedBindingStates_ = 0;
    markDirty();
}

const ValueBinding* Node::valueBinding() const
{
    return valueBinding_.get();
}

bool Node::perform(Action action, double value)
{
    if (!isInteractive(*this))
        return false;

    if (action == Action::Activate)
        return activate();

    if (!valueBinding_ || !valueBinding_->set) {
        if (onAction_ && onAction_(*this, action, value)) {
            refreshBindingState();
            markDirty();
            return true;
        }
        return false;
    }

    double next = value;
    if (action == Action::Increment || action == Action::Decrement) {
        if (!valueBinding_->get) {
            if (onAction_ && onAction_(*this, action, value)) {
                refreshBindingState();
                markDirty();
                return true;
            }
            return false;
        }
        const double step = valueBinding_->step > 0.0 ? valueBinding_->step : 0.01;
        next = valueBinding_->get() + (action == Action::Increment ? step : -step);
    } else if (action != Action::SetValue) {
        if (onAction_ && onAction_(*this, action, value)) {
            refreshBindingState();
            markDirty();
            return true;
        }
        return false;
    }

    valueBinding_->set(clampValue(next, valueBinding_->min, valueBinding_->max));
    refreshBindingState();
    markDirty();
    return true;
}

namespace {

void collectOverlayRoots(const Node& node, std::vector<const Node*>& out)
{
    if (!node.visible())
        return;

    for (const auto& child : node.children()) {
        if (!child->visible())
            continue;
        if (child->overlay())
            out.push_back(child.get());
        else
            collectOverlayRoots(*child, out);
    }
}

const Node* hitTestMainSubtree(const Node& node, Vec2 point)
{
    if (!node.visible())
        return nullptr;

    const bool scrollGated = node.scroll() && !node.bounds().contains(point);
    for (auto it = node.children().rbegin(); it != node.children().rend(); ++it) {
        if ((*it)->overlay() || scrollGated)
            continue;
        if (const Node* hit = hitTestMainSubtree(**it, point))
            return hit;
    }
    return node.bounds().contains(point) ? &node : nullptr;
}

const Node* hitTestInPaintOrder(const Node& root, Vec2 point)
{
    std::vector<const Node*> overlays;
    collectOverlayRoots(root, overlays);
    for (std::size_t index = 0; index < overlays.size(); ++index)
        collectOverlayRoots(*overlays[index], overlays);

    for (auto it = overlays.rbegin(); it != overlays.rend(); ++it)
        if (const Node* hit = hitTestMainSubtree(**it, point))
            return hit;

    return hitTestMainSubtree(root, point);
}

} // namespace

Node* Node::hitTest(Vec2 point)
{
    return const_cast<Node*>(static_cast<const Node*>(this)->hitTest(point));
}

const Node* Node::hitTest(Vec2 point) const
{
    return hitTestInPaintOrder(*this, point);
}

void Node::markDirty()
{
    dirty_ = true;
    if (parent_)
        parent_->markDirty();
}

bool Node::subtreeDirty() const
{
    if (dirty_)
        return true;
    for (const auto& child : children_)
        if (child->subtreeDirty())
            return true;
    return false;
}

void Node::clearDirtyRecursive()
{
    dirty_ = false;
    for (auto& child : children_)
        child->clearDirtyRecursive();
}

void Node::setFocused(bool focused, bool focusVisible)
{
    if (focused_ == focused && focusVisible_ == (focused && focusVisible))
        return;
    focused_ = focused;
    focusVisible_ = focused && focusVisible;
    markDirty();
}

void Node::setHovered(bool hovered)
{
    if (hovered_ == hovered)
        return;
    hovered_ = hovered;
    markDirty();
}

void Node::setPressed(bool pressed)
{
    if (pressed_ == pressed)
        return;
    pressed_ = pressed;
    markDirty();
}

bool Node::handleEvent(const Event& event)
{
    return onEvent_ ? onEvent_(*this, event) : false;
}

bool Node::refreshBindingState()
{
    if (!valueBinding_) {
        hasObservedBinding_ = false;
        observedBindingValue_ = {};
        observedBindingStates_ = 0;
        return false;
    }

    const ValueRange value = currentValue(*this);
    const SemanticStates states = derivedStates(*this) & stateMask(SemanticState::Checked);
    if (!hasObservedBinding_) {
        observedBindingValue_ = value;
        observedBindingStates_ = states;
        hasObservedBinding_ = true;
        return false;
    }

    if (sameValueRange(observedBindingValue_, value) &&
        observedBindingStates_ == states) {
        return false;
    }

    observedBindingValue_ = value;
    observedBindingStates_ = states;
    markDirty();
    return true;
}

bool Node::refreshExternalState()
{
    if (!onRefresh_)
        return false;
    if (!onRefresh_(*this))
        return false;
    markDirty();
    return true;
}

bool Node::provideSemanticChildren(std::vector<SemanticNode>& out) const
{
    if (!semanticChildren_)
        return false;
    const std::size_t before = out.size();
    semanticChildren_(*this, out);
    return out.size() != before;
}

bool Node::performSemanticChildAction(const NodeId& id, Action action, double value)
{
    if (onSemanticAction_ && onSemanticAction_(*this, id, action, value))
        return true;
    for (const auto& child : children_)
        if (child->performSemanticChildAction(id, action, value))
            return true;
    return false;
}

Tree::Tree(Node::Ptr root) : root_(std::move(root))
{
    if (!root_)
        root_ = Node::make("root");
}

Tree Tree::withRoot(NodeId id)
{
    return Tree(Node::make(std::move(id)));
}

Node* Tree::find(const NodeId& id)
{
    return root_ ? root_->find(id) : nullptr;
}

const Node* Tree::find(const NodeId& id) const
{
    return root_ ? root_->find(id) : nullptr;
}

void Tree::layout(Vec2 size)
{
    viewport_ = size;
    if (!root_)
        return;
    root_->setBounds({0.0f, 0.0f, clampNonNegative(size.x), clampNonNegative(size.y)});
    layoutNode(*root_);
}

Node* Tree::hitTest(Vec2 point)
{
    if (!root_)
        return nullptr;
    if (Node* modal = findActiveModal(*root_))
        return modal->hitTest(point);
    return root_->hitTest(point);
}

const Node* Tree::hitTest(Vec2 point) const
{
    if (!root_)
        return nullptr;
    if (const Node* modal = findActiveModal(*root_))
        return modal->hitTest(point);
    return root_->hitTest(point);
}

bool Tree::focus(const NodeId& id)
{
    return focus(id, true);
}

bool Tree::focus(const NodeId& id, bool focusVisible)
{
    Node* next = find(id);
    if (!next || !next->focusable_ || !isInteractive(*next))
        return false;
    if (Node* modal = root_ ? findActiveModal(*root_) : nullptr) {
        if (!containsNode(*modal, *next))
            return false;
    }

    if (focusedId_ == id) {
        next->setFocused(true, focusVisible); // may promote/demote visibility
        return true;
    }

    if (Node* prev = focused())
        prev->setFocused(false, false);
    focusedId_ = id;
    next->setFocused(true, focusVisible);
    return true;
}

bool Tree::clearFocus()
{
    if (focusedId_.empty())
        return false;
    if (Node* prev = focused())
        prev->setFocused(false, false);
    focusedId_.clear();
    return true;
}

void Tree::collectFocusable(std::vector<Node*>& out)
{
    if (!root_)
        return;
    std::function<void(Node&)> collect = [&](Node& node) {
        if (!node.visible_ || !node.enabled_)
            return;
        if (node.focusable_)
            out.push_back(&node);
        for (const auto& child : node.children_)
            collect(*child);
    };
    if (Node* modal = findActiveModal(*root_))
        collect(*modal);
    else
        collect(*root_);
}

void Tree::collectFocusable(const Node& node, std::vector<const Node*>& out) const
{
    if (!node.visible_ || !node.enabled_)
        return;
    if (node.focusable_)
        out.push_back(&node);
    for (const auto& child : node.children_)
        collectFocusable(*child, out);
}

bool Tree::focusNext(bool reverse)
{
    std::vector<Node*> nodes;
    collectFocusable(nodes);
    if (nodes.empty())
        return false;

    auto current = std::find_if(nodes.begin(), nodes.end(),
                                [&](Node* node) { return node->id() == focusedId_; });
    std::size_t index = 0;
    if (current == nodes.end()) {
        index = reverse ? nodes.size() - 1 : 0;
    } else if (reverse) {
        index = current == nodes.begin()
                    ? nodes.size() - 1
                    : static_cast<std::size_t>(std::distance(nodes.begin(), current) - 1);
    } else {
        index = (static_cast<std::size_t>(std::distance(nodes.begin(), current)) + 1) %
                nodes.size();
    }

    return focus(nodes[index]->id());
}

Node* Tree::focused()
{
    return focusedId_.empty() ? nullptr : find(focusedId_);
}

const Node* Tree::focused() const
{
    return focusedId_.empty() ? nullptr : find(focusedId_);
}

Node* Tree::hovered()
{
    return hoveredId_.empty() ? nullptr : find(hoveredId_);
}

const Node* Tree::pressed() const
{
    return pressedId_.empty() ? nullptr : find(pressedId_);
}

bool Tree::cancelPress()
{
    if (pressedId_.empty())
        return false;
    if (Node* pressed = find(pressedId_)) {
        pressed->setPressed(false);
        // Deliver a terminating MouseUp so gesture state kept inside the node's
        // own setOnEvent handler (drags, rubber-band selections) unwinds exactly
        // as a real release would. Without it the press only clears at the tree
        // level while the handler keeps dragging on later hover moves -- the
        // "sticks to the cursor" bug when an OS window grabs the real release.
        // This is a cancel, not a click, so it bypasses the activate-on-release
        // path in dispatch() and never fires Action::Activate.
        if (isInteractive(*pressed)) {
            Event up;
            up.type = EventType::MouseUp;
            up.button = MouseButton::Left;
            up.position = lastPointer_;
            pressed->handleEvent(up);
        }
    }
    pressedId_.clear();
    return true;
}

const Node* Tree::hovered() const
{
    return hoveredId_.empty() ? nullptr : find(hoveredId_);
}

bool Tree::setHoveredNode(Node* node)
{
    NodeId nextId = node && node->visible() ? node->id() : NodeId{};
    if (hoveredId_ == nextId)
        return false;

    if (Node* prev = hovered())
        prev->setHovered(false);
    hoveredId_ = std::move(nextId);
    if (Node* next = hovered())
        next->setHovered(true);
    return true;
}

bool Tree::dispatch(const Event& event)
{
    if (!root_)
        return false;

    if (event.type == EventType::MouseMove || event.type == EventType::MouseDown ||
        event.type == EventType::MouseUp || event.type == EventType::MouseWheel ||
        event.type == EventType::ContextMenu)
        lastPointer_ = event.position;

    if (event.type == EventType::MouseMove) {
        Node* target = hitTest(event.position);
        bool changed = setHoveredNode(target);
        Node* receiver = pressedId_.empty() ? target : find(pressedId_);
        bool handled = receiver && isInteractive(*receiver) && receiver->handleEvent(event);
        return changed || handled;
    }

    if (event.type == EventType::MouseDown && event.button == MouseButton::Left) {
        Node* target = hitTest(event.position);
        setHoveredNode(target);
        if (!target || !isInteractive(*target))
            return false;
        pressedId_ = target->id();
        target->setPressed(true);
        if (target->focusable_) {
            if (Node* prev = focused())
                prev->setFocused(false, false);
            focusedId_ = target->id();
            target->setFocused(true, false);
        }
        // the press bubbles like the wheel: the deepest hit gets first refusal,
        // then each ancestor, so a container catches presses that land on space
        // its children ignored (a title bar stays draggable by the gaps between
        // its controls). Press/focus stay on the hit node either way.
        for (Node* n = target; n != nullptr; n = n->parent())
            if (isInteractive(*n) && n->handleEvent(event))
                break;
        return true;
    }

    if (event.type == EventType::MouseUp && event.button == MouseButton::Left) {
        Node* pressed = pressedId_.empty() ? nullptr : find(pressedId_);
        Node* target = hitTest(event.position);
        setHoveredNode(target);
        if (pressed)
            pressed->setPressed(false);
        pressedId_.clear();
        Node* receiver = pressed ? pressed : target;
        bool handled = receiver && isInteractive(*receiver) && receiver->handleEvent(event);
        if (pressed && target == pressed)
            return pressed->perform(Action::Activate) || handled;
        return pressed != nullptr || handled;
    }

    if (event.type == EventType::MouseWheel) {
        // the wheel bubbles: the deepest hit gets first refusal, then each
        // ancestor, so a scroll container catches it even when the pointer is
        // over one of its (non-scrolling) children.
        Node* target = hitTest(event.position);
        setHoveredNode(target);
        for (Node* n = target; n != nullptr; n = n->parent())
            if (isInteractive(*n) && n->handleEvent(event))
                return true;
        return false;
    }

    if (event.type == EventType::MouseDown ||
        event.type == EventType::MouseUp ||
        event.type == EventType::ContextMenu) {
        Node* target = hitTest(event.position);
        setHoveredNode(target);
        return target && isInteractive(*target) && target->handleEvent(event);
    }

    const bool keyOrTextEvent = event.type == EventType::KeyDown ||
                                event.type == EventType::KeyUp ||
                                event.type == EventType::TextInput;
    if (!keyOrTextEvent)
        return false;

    Node* modal = root_ ? findActiveModal(*root_) : nullptr;
    if (modal) {
        Node* current = focused();
        if (!current || !containsNode(*modal, *current)) {
            if (current)
                current->setFocused(false, false);
            focusedId_.clear();
            if (Node* first = firstFocusable(*modal))
                focus(first->id());
        }
    }

    if (event.type == EventType::KeyDown && event.key == Key::Tab)
        return focusNext(event.shift);
    if (event.type == EventType::TextInput && event.text.empty())
        return false;

    Node* current = focused();
    if (current) {
        if (current->handleEvent(event))
            return true;

        if (event.type == EventType::KeyDown) {
            switch (event.key) {
            case Key::Enter:
            case Key::Space:
                if (current->perform(Action::Activate))
                    return true;
                break;
            case Key::Down:
                if (effectiveRole(*current) == Role::ListItem ||
                    effectiveRole(*current) == Role::MenuItem)
                    return focusNext(false);
                if (current->perform(Action::Decrement))
                    return true;
                break;
            case Key::Up:
                if (effectiveRole(*current) == Role::ListItem ||
                    effectiveRole(*current) == Role::MenuItem)
                    return focusNext(true);
                if (current->perform(Action::Increment))
                    return true;
                break;
            case Key::Right:
                if (current->perform(Action::Increment))
                    return true;
                break;
            case Key::Left:
                if (current->perform(Action::Decrement))
                    return true;
                break;
            default:
                break;
            }
        }
    }

    if (modal && event.type == EventType::KeyDown && event.key == Key::Escape &&
        isInteractive(*modal) && modal->handleEvent(event))
        return true;

    if (modal) {
        if (modal != current && isInteractive(*modal) && modal->handleEvent(event))
            return true;
        return true;
    }

    // Global accelerators: a KeyDown/TextInput that no focused widget claimed
    // (including when nothing is focused) falls through to the root node's
    // onEvent, so an app can register shortcut keys -- transport, delete --
    // that fire regardless of which widget holds focus. Pointer focus and the
    // focused widget's own keys still get first refusal above.
    if ((event.type == EventType::KeyDown || event.type == EventType::TextInput) &&
        root_ && root_.get() != current)
        return root_->handleEvent(event);
    return false;
}

bool Tree::performAction(const NodeId& id, Action action, double value)
{
    if (action == Action::Focus)
        return focus(id);

    Node* node = find(id);
    if (node) {
        if (Node* modal = root_ ? findActiveModal(*root_) : nullptr) {
            if (!containsNode(*modal, *node))
                return false;
        }
        return node->perform(action, value);
    }
    return root_ && root_->performSemanticChildAction(id, action, value);
}

bool Tree::performSemanticAction(const NodeId& id, Action action, double value)
{
    SemanticNode node;
    if (!semanticNode(id, node) || !hasAction(node.actions, action))
        return false;
    if (action == Action::Focus && !find(id) && !node.parent.empty()) {
        NodeId focusId = node.parent;
        while (!focusId.empty() && !find(focusId)) {
            SemanticNode parentNode;
            if (!semanticNode(focusId, parentNode) || parentNode.parent == focusId)
                break;
            focusId = parentNode.parent;
        }
        if (!focusId.empty())
            focus(focusId);
        return root_ && root_->performSemanticChildAction(id, action, value);
    }
    return performAction(id, action, value);
}

bool Tree::value(const NodeId& id, ValueRange& out) const
{
    SemanticNode node;
    if (!semanticNode(id, node) || !node.value.hasNumeric)
        return false;
    out = node.value;
    return true;
}

bool Tree::setValue(const NodeId& id, double next)
{
    return performSemanticAction(id, Action::SetValue, next);
}

bool Tree::incrementValue(const NodeId& id)
{
    return performSemanticAction(id, Action::Increment);
}

bool Tree::decrementValue(const NodeId& id)
{
    return performSemanticAction(id, Action::Decrement);
}

bool Tree::refreshBoundValues()
{
    if (!root_)
        return false;

    bool changed = false;
    std::function<void(Node&)> visit = [&](Node& node) {
        changed = node.refreshBindingState() || changed;
        changed = node.refreshExternalState() || changed;
        for (const auto& child : node.children_)
            visit(*child);
    };
    visit(*root_);
    return changed;
}

void Tree::collectSemantics(const Node& node, const NodeId& parent,
                            std::vector<SemanticNode>& out) const
{
    if (!node.visible_)
        return;

    const Semantics& sem = node.semantics_;
    if (sem.hidden || hasState(sem.states, SemanticState::Hidden))
        return;

    SemanticNode current;
    current.id = node.id_;
    current.parent = parent;
    current.bounds = node.bounds_;
    current.role = effectiveRole(node);
    current.name = sem.name;
    current.description = sem.description;
    current.tooltip = sem.tooltip;
    current.labelledBy = sem.labelledBy;
    current.value = currentValue(node);
    current.states = derivedStates(node);
    current.actions = sem.actions;

    if (node.onActivate_)
        appendAction(current.actions, Action::Activate);
    appendDerivedActions(node, current.actions);

    out.push_back(current);
    const NodeId currentParent = out.back().id;
    const std::size_t virtualStart = out.size();
    node.provideSemanticChildren(out);
    for (std::size_t i = virtualStart; i < out.size(); ++i) {
        if (out[i].parent.empty())
            out[i].parent = currentParent;
    }
    for (const auto& child : node.children_)
        collectSemantics(*child, currentParent, out);
}

void Tree::collectNodes(const Node& node, const NodeId& parent,
                        std::vector<NodeSnapshot>& out) const
{
    if (!node.visible_)
        return;

    NodeSnapshot current;
    current.id = node.id_;
    current.parent = parent;
    current.bounds = node.bounds_;
    current.role = effectiveRole(node);
    current.value = currentValue(node);
    current.states = derivedStates(node);
    current.interaction = interactionState(node, current.states);

    out.push_back(current);
    const NodeId currentParent = out.back().id;
    for (const auto& child : node.children_)
        collectNodes(*child, currentParent, out);
}

std::vector<NodeSnapshot> Tree::nodeSnapshot() const
{
    std::vector<NodeSnapshot> out;
    if (root_)
        collectNodes(*root_, {}, out);
    return out;
}

bool Tree::nodeSnapshot(const NodeId& id, NodeSnapshot& out) const
{
    for (const NodeSnapshot& node : nodeSnapshot()) {
        if (node.id == id) {
            out = node;
            return true;
        }
    }
    return false;
}

std::vector<ValidationIssue> Tree::validate() const
{
    std::vector<ValidationIssue> issues;
    auto addIssue = [&](ValidationIssueKind kind, const NodeId& id) {
        const auto duplicate = std::find_if(
            issues.begin(), issues.end(), [&](const ValidationIssue& issue) {
                return issue.kind == kind && issue.id == id;
            });
        if (duplicate == issues.end())
            issues.push_back({kind, id});
    };
    std::set<NodeId> seen;
    if (!root_)
        return issues;

    std::function<void(const Node&)> visit = [&](const Node& node) {
        if (node.id().empty()) {
            addIssue(ValidationIssueKind::EmptyId, {});
        } else if (!seen.insert(node.id()).second) {
            addIssue(ValidationIssueKind::DuplicateId, node.id());
        }
        const Semantics& sem = node.semantics();
        const Role role = effectiveRole(node);
        const bool semanticHidden = sem.hidden || hasState(sem.states, SemanticState::Hidden);
        if (node.visible() && !semanticHidden && role != Role::None &&
            role != Role::Group && sem.name.empty()) {
            addIssue(ValidationIssueKind::MissingAccessibleName, node.id());
        }
        for (const auto& child : node.children())
            visit(*child);
    };
    visit(*root_);
    std::set<NodeId> semanticSeen;
    std::vector<SemanticNode> allSemantics;
    collectSemantics(*root_, {}, allSemantics);
    for (const SemanticNode& node : allSemantics) {
        if (node.id.empty()) {
            addIssue(ValidationIssueKind::EmptyId, {});
        } else if (!semanticSeen.insert(node.id).second) {
            addIssue(ValidationIssueKind::DuplicateId, node.id);
        }
        if (!seen.count(node.id) && node.role != Role::None &&
            node.role != Role::Group && node.name.empty()) {
            addIssue(ValidationIssueKind::MissingAccessibleName, node.id);
        }
    }
    return issues;
}

std::vector<SemanticNode> Tree::semanticSnapshot() const
{
    std::vector<SemanticNode> out;
    if (root_) {
        if (const Node* modal = findActiveModal(*root_))
            collectSemantics(*modal, {}, out);
        else
            collectSemantics(*root_, {}, out);
    }
    return out;
}

bool Tree::semanticNode(const NodeId& id, SemanticNode& out) const
{
    for (const SemanticNode& node : semanticSnapshot()) {
        if (node.id == id) {
            out = node;
            return true;
        }
    }
    return false;
}

bool Tree::dirty() const
{
    return root_ && root_->subtreeDirty();
}

void Tree::clearDirty()
{
    if (root_)
        root_->clearDirtyRecursive();
}

} // namespace snd::ui::retained
