#include "snd/ui_retained_widgets.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

namespace snd::ui::retained {

namespace {

ImVec2 topLeft(Rect r) { return ImVec2(r.x, r.y); }
ImVec2 sizeOf(Rect r) { return ImVec2(r.w, r.h); }
ImVec2 bottomRight(Rect r) { return ImVec2(r.x + r.w, r.y + r.h); }
Rect offset(Rect r, const ImVec2& origin)
{
    r.x += origin.x;
    r.y += origin.y;
    return r;
}

double clamp01(double v)
{
    return std::max(0.0, std::min(1.0, v));
}

double clampValue(double value, double min, double max)
{
    if (min > max)
        std::swap(min, max);
    return std::max(min, std::min(max, value));
}

double lerpValue(double frac, double min, double max)
{
    frac = clamp01(frac);
    if (min > max)
        std::swap(min, max);
    return min + (max - min) * frac;
}

double valueOf(const Node& node, const SemanticNode* sem)
{
    if (const ValueBinding* binding = node.valueBinding()) {
        if (binding->get)
            return binding->get();
    }
    const ValueRange& range = sem ? sem->value : node.semantics().value;
    return range.value;
}

double normalizedValue(const Node& node, const SemanticNode* sem)
{
    const ValueRange& range = sem ? sem->value : node.semantics().value;
    double value = valueOf(node, sem);
    double min = range.min;
    double max = range.max;
    if (const ValueBinding* binding = node.valueBinding()) {
        min = binding->min;
        max = binding->max;
    }
    if (max == min)
        return 0.0;
    if (min > max)
        std::swap(min, max);
    return clamp01((value - min) / (max - min));
}

std::string nodeName(const Node& node, const SemanticNode* sem)
{
    if (sem && !sem->name.empty())
        return sem->name;
    return node.semantics().name;
}

bool checked(const Node& node, const SemanticNode* sem)
{
    const SemanticStates states = sem ? sem->states : node.semantics().states;
    if (hasState(states, SemanticState::Checked) ||
        hasState(states, SemanticState::Selected) ||
        hasState(states, SemanticState::Pressed)) {
        return true;
    }
    if (node.valueBinding())
        return normalizedValue(node, sem) >= 0.5;
    const ValueRange& range = sem ? sem->value : node.semantics().value;
    return range.hasNumeric && range.value >= 0.5;
}

double readBinding(const ValueBinding& binding, double fallback = 0.0)
{
    double value = binding.get ? binding.get() : fallback;
    if (binding.min != binding.max)
        value = clampValue(value, binding.min, binding.max);
    return value;
}

bool writeBinding(const ValueBinding& binding, double value)
{
    if (!binding.set)
        return false;
    const double next = clampValue(value, binding.min, binding.max);
    const double prev = binding.get ? binding.get() : next;
    binding.set(next);
    return std::abs(prev - next) > 0.0000001;
}

std::string formatBindingValue(const ValueBinding& binding, double value)
{
    if (binding.format)
        return binding.format(value);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", value);
    return std::string(buf);
}

paint::ControlState controlState(const Node& node, const SemanticNode* sem,
                                 const ImVec2& origin)
{
    const SemanticStates states = sem ? sem->states : node.semantics().states;
    paint::ControlState state;
    state.disabled = !node.enabled() || hasState(states, SemanticState::Disabled);
    state.focused = hasState(states, SemanticState::Focused);
    state.active = hasState(states, SemanticState::Pressed);
    state.selected = hasState(states, SemanticState::Checked) ||
                     hasState(states, SemanticState::Selected);

    ImGuiIO& io = ImGui::GetIO();
    const Rect b = offset(node.bounds(), origin);
    state.hovered = !state.disabled && io.MousePos.x >= b.x && io.MousePos.y >= b.y &&
                    io.MousePos.x <= b.x + b.w && io.MousePos.y <= b.y + b.h;
    return state;
}

ImFont* iconFont(IconFont font)
{
    switch (font) {
    case IconFont::Lucide:
        return iconFontLucide();
    case IconFont::Text:
        return ImGui::GetFont();
    case IconFont::Material:
    default:
        return iconFontMaterial();
    }
}

Key mapKey(ImGuiKey key)
{
    switch (key) {
    case ImGuiKey_Tab:
        return Key::Tab;
    case ImGuiKey_Enter:
    case ImGuiKey_KeypadEnter:
        return Key::Enter;
    case ImGuiKey_Space:
        return Key::Space;
    case ImGuiKey_LeftArrow:
        return Key::Left;
    case ImGuiKey_RightArrow:
        return Key::Right;
    case ImGuiKey_UpArrow:
        return Key::Up;
    case ImGuiKey_DownArrow:
        return Key::Down;
    case ImGuiKey_Escape:
        return Key::Escape;
    default:
        return Key::Unknown;
    }
}

Event keyEvent(ImGuiKey key)
{
    Event event;
    event.type = EventType::KeyDown;
    event.key = mapKey(key);
    event.shift = ImGui::GetIO().KeyShift;
    return event;
}

void drawText(ImDrawList* dl, ImFont* font, Rect bounds, const std::string& text,
              float scale, ImU32 color, Align align = Align::Center)
{
    if (!dl || !font || text.empty())
        return;
    const float fs = ImGui::GetFontSize() * scale;
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text.c_str());
    float x = bounds.x;
    if (align == Align::Center)
        x += std::max(0.0f, bounds.w - ts.x) * 0.5f;
    else if (align == Align::End)
        x += std::max(0.0f, bounds.w - ts.x);
    const float y = bounds.y + std::max(0.0f, bounds.h - ts.y) * 0.5f;
    dl->AddText(font, fs, ImVec2(x, y), color, text.c_str());
}

Semantics named(Role role, const std::string& name)
{
    Semantics sem;
    sem.role = role;
    sem.name = name;
    return sem;
}

void setCheckedState(Node& node, bool on)
{
    Semantics& sem = node.semantics();
    if (on)
        sem.states |= SemanticState::Checked;
    else
        sem.states &= ~stateMask(SemanticState::Checked);
    if (sem.value.hasNumeric) {
        sem.value.value = on ? sem.value.max : sem.value.min;
        sem.value.text = on ? "On" : "Off";
    }
}

bool setBindingValue(Node& node, double value)
{
    const ValueBinding* binding = node.valueBinding();
    if (!binding || !binding->set)
        return false;

    const double next = clampValue(value, binding->min, binding->max);
    binding->set(next);

    Semantics& sem = node.semantics();
    sem.value.hasNumeric = true;
    sem.value.min = binding->min;
    sem.value.max = binding->max;
    sem.value.step = binding->step;
    sem.value.value = next;
    if (binding->format)
        sem.value.text = binding->format(next);
    return true;
}

bool setBindingNormalized(Node& node, double frac)
{
    const ValueBinding* binding = node.valueBinding();
    if (!binding)
        return false;
    return setBindingValue(node, lerpValue(frac, binding->min, binding->max));
}

void installFaderPointerBehavior(Node& node)
{
    node.setOnEvent([](Node& n, const Event& event) {
        if (event.type != EventType::MouseDown && event.type != EventType::MouseMove)
            return false;
        if (event.type == EventType::MouseMove &&
            (!n.pressed() || !ImGui::IsMouseDown(ImGuiMouseButton_Left)))
            return false;

        const Rect bounds = n.bounds();
        if (bounds.h <= 0.0f)
            return false;

        const double frac = 1.0 - (event.position.y - bounds.y) / bounds.h;
        return setBindingNormalized(n, frac);
    });
}

void installKnobPointerBehavior(Node& node)
{
    node.setOnEvent([](Node& n, const Event& event) {
        if (event.type == EventType::MouseDown)
            return true;

        if (event.type == EventType::MouseUp)
            return true;

        if (event.type != EventType::MouseMove || !n.pressed())
            return false;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            return false;

        const ValueBinding* binding = n.valueBinding();
        if (!binding || !binding->get)
            return false;

        const double range = std::abs(binding->max - binding->min);
        if (range <= 0.0)
            return false;

        const ImGuiIO& io = ImGui::GetIO();
        if (io.MouseDelta.x == 0.0f && io.MouseDelta.y == 0.0f)
            return false;

        const double fine = io.KeyShift ? 0.25 : 1.0;
        const double delta = (io.MouseDelta.x - io.MouseDelta.y) *
                             range * (fine / 180.0);
        return setBindingValue(n, binding->get() + delta);
    });
}

struct XYPadBindings {
    ValueBinding x;
    ValueBinding y;
};

std::string xyValueText(const XYPadBindings& bindings)
{
    const double x = readBinding(bindings.x);
    const double y = readBinding(bindings.y);
    return "X " + formatBindingValue(bindings.x, x) +
           ", Y " + formatBindingValue(bindings.y, y);
}

void refreshXYSemantics(Node& node, const XYPadBindings& bindings)
{
    Semantics& sem = node.semantics();
    sem.value.text = xyValueText(bindings);
}

bool writeXYFromPoint(Node& node, const XYPadBindings& bindings, Vec2 point)
{
    const Rect bounds = node.bounds();
    if (bounds.w <= 0.0f || bounds.h <= 0.0f)
        return false;

    const double x = clamp01((point.x - bounds.x) / bounds.w);
    const double y = clamp01(1.0 - (point.y - bounds.y) / bounds.h);
    const bool changedX = writeBinding(bindings.x, x);
    const bool changedY = writeBinding(bindings.y, y);
    if (changedX || changedY)
        refreshXYSemantics(node, bindings);
    return changedX || changedY;
}

struct PatternGridBinding {
    bool* cells = nullptr;
    int rows = 0;
    int steps = 0;
    std::function<int()> playheadStep;
};

int patternPlayhead(const PatternGridBinding& binding)
{
    return binding.playheadStep ? binding.playheadStep() : -1;
}

int patternActiveCount(const PatternGridBinding& binding)
{
    if (!binding.cells || binding.rows <= 0 || binding.steps <= 0)
        return 0;
    int count = 0;
    for (int i = 0; i < binding.rows * binding.steps; ++i)
        if (binding.cells[i])
            ++count;
    return count;
}

void refreshPatternSemantics(Node& node, const PatternGridBinding& binding)
{
    Semantics& sem = node.semantics();
    sem.value.hasNumeric = true;
    sem.value.min = 0.0;
    sem.value.max = (double)std::max(0, binding.rows * binding.steps);
    sem.value.step = 1.0;
    sem.value.value = (double)patternActiveCount(binding);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d active steps", (int)sem.value.value);
    sem.value.text = buf;
}

bool paintPatternCell(Node& node, const PatternGridBinding& binding, Vec2 point)
{
    const Rect bounds = node.bounds();
    if (!binding.cells || binding.rows <= 0 || binding.steps <= 0 ||
        bounds.w <= 0.0f || bounds.h <= 0.0f)
        return false;

    const int col = (int)((point.x - bounds.x) / (bounds.w / binding.steps));
    const int row = (int)((point.y - bounds.y) / (bounds.h / binding.rows));
    if (col < 0 || col >= binding.steps || row < 0 || row >= binding.rows)
        return false;

    const bool want = !ImGui::GetIO().KeyAlt;
    bool& cell = binding.cells[row * binding.steps + col];
    if (cell == want)
        return false;
    cell = want;
    refreshPatternSemantics(node, binding);
    return true;
}

struct KeyboardBinding {
    int firstNote = 60;
    int octaves = 2;
    std::function<void(uint8_t note, uint8_t velocity)> noteOn;
    std::function<void(uint8_t note)> noteOff;
    const bool* lit = nullptr;
    int mouseNote = -1;
};

int keyboardHit(const KeyboardBinding& binding, Rect bounds, Vec2 point,
                float* hitDepth = nullptr)
{
    const int whites = binding.octaves * 7;
    if (whites <= 0 || bounds.w <= 0.0f || bounds.h <= 0.0f)
        return -1;

    const float lx = point.x - bounds.x;
    const float ly = point.y - bounds.y;
    if (lx < 0.0f || lx >= bounds.w || ly < 0.0f || ly >= bounds.h)
        return -1;

    const float ww = bounds.w / (float)whites;
    const float bw = ww * 0.62f;
    const float bh = bounds.h * 0.60f;
    static const int whiteSemi[7] = {0, 2, 4, 5, 7, 9, 11};
    static const int blackAfterWhite[7] = {1, 1, 0, 1, 1, 1, 0};
    static const int blackSemi[7] = {1, 3, 0, 6, 8, 10, 0};

    int hitNote = -1;
    float depth = 0.0f;
    if (ly < bh) {
        for (int w = 0; w < whites && hitNote < 0; ++w) {
            const int inOct = w % 7;
            if (!blackAfterWhite[inOct])
                continue;
            const float bx = (w + 1) * ww - bw * 0.5f;
            if (lx >= bx && lx < bx + bw) {
                hitNote = binding.firstNote + (w / 7) * 12 + blackSemi[inOct];
                depth = ly / bh;
            }
        }
    }

    if (hitNote < 0) {
        const int w = std::min(whites - 1, (int)(lx / ww));
        hitNote = binding.firstNote + (w / 7) * 12 + whiteSemi[w % 7];
        depth = ly / bounds.h;
    }

    if (hitDepth)
        *hitDepth = depth;
    return hitNote;
}

bool releaseKeyboard(KeyboardBinding& binding)
{
    if (binding.mouseNote < 0)
        return false;
    if (binding.noteOff)
        binding.noteOff((uint8_t)binding.mouseNote);
    binding.mouseNote = -1;
    return true;
}

void refreshKeyboardSemantics(Node& node, const KeyboardBinding& binding)
{
    Semantics& sem = node.semantics();
    if (binding.mouseNote >= 0) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "Note %d", binding.mouseNote);
        sem.value.text = buf;
    } else {
        sem.value.text = "No note";
    }
}

bool playKeyboardFromPoint(Node& node, KeyboardBinding& binding, Vec2 point)
{
    float hitDepth = 0.0f;
    const int want = keyboardHit(binding, node.bounds(), point, &hitDepth);
    if (want == binding.mouseNote)
        return false;

    releaseKeyboard(binding);
    if (want >= 0 && want < 128) {
        if (binding.noteOn) {
            const uint8_t vel =
                (uint8_t)std::clamp(40.0f + hitDepth * 87.0f, 1.0f, 127.0f);
            binding.noteOn((uint8_t)want, vel);
        }
        binding.mouseNote = want;
    }
    refreshKeyboardSemantics(node, binding);
    return true;
}

} // namespace

void PaintRenderer::setStyle(const NodeId& id, const VisualStyle& style)
{
    styles_[id] = style;
}

void PaintRenderer::clearStyle(const NodeId& id)
{
    styles_.erase(id);
}

void PaintRenderer::clearStyles()
{
    styles_.clear();
}

const VisualStyle* PaintRenderer::styleFor(const NodeId& id) const
{
    auto it = styles_.find(id);
    return it == styles_.end() ? nullptr : &it->second;
}

VisualStyle PaintRenderer::resolvedStyle(const Node& node) const
{
    if (const VisualStyle* explicitStyle = styleFor(node.id()))
        return *explicitStyle;

    VisualStyle style;
    switch (node.role()) {
    case Role::Text:
        style.kind = VisualKind::Text;
        break;
    case Role::Button:
        style.kind = VisualKind::Button;
        break;
    case Role::Toggle:
        style.kind = VisualKind::Toggle;
        break;
    case Role::Slider:
        style.kind = VisualKind::Knob;
        break;
    case Role::Meter:
        style.kind = VisualKind::Meter;
        break;
    case Role::ListItem:
        style.kind = VisualKind::ListItem;
        break;
    case Role::Group:
    default:
        style.kind = VisualKind::Auto;
        break;
    }
    return style;
}

void PaintRenderer::render(const Tree& tree, ImDrawList* drawList) const
{
    render(tree, ImVec2(0.0f, 0.0f), drawList);
}

void PaintRenderer::render(const Tree& tree, const ImVec2& origin,
                           ImDrawList* drawList) const
{
    SemanticMap semMap;
    for (const SemanticNode& node : tree.semanticSnapshot())
        semMap[node.id] = node;
    renderNode(tree.root(), &semMap, origin, drawList ? drawList : ImGui::GetWindowDrawList());
}

void PaintRenderer::render(const Node& root, ImDrawList* drawList) const
{
    render(root, ImVec2(0.0f, 0.0f), drawList);
}

void PaintRenderer::render(const Node& root, const ImVec2& origin,
                           ImDrawList* drawList) const
{
    renderNode(root, nullptr, origin, drawList ? drawList : ImGui::GetWindowDrawList());
}

void PaintRenderer::renderNode(const Node& node, const SemanticMap* semantics,
                               const ImVec2& origin,
                               ImDrawList* drawList) const
{
    if (!node.visible())
        return;

    const auto semIt = semantics ? semantics->find(node.id()) : SemanticMap::const_iterator{};
    const SemanticNode* sem = semantics && semIt != semantics->end() ? &semIt->second : nullptr;
    const VisualStyle style = resolvedStyle(node);
    const Palette& pal = palette();
    const Rect bounds = offset(node.bounds(), origin);
    paint::ControlState state = controlState(node, sem, origin);

    switch (style.kind) {
    case VisualKind::Panel:
        if (style.panelFill)
            drawList->AddRectFilled(topLeft(bounds), bottomRight(bounds), pal.frame, 4.0f);
        if (style.panelBorder)
            drawList->AddRect(topLeft(bounds), bottomRight(bounds), pal.frameBright, 4.0f);
        break;
    case VisualKind::Text:
        drawText(drawList, ImGui::GetFont(), bounds, nodeName(node, sem),
                 style.fontScale > 0.0f ? style.fontScale : 1.0f, pal.text, Align::Start);
        break;
    case VisualKind::SectionHeader:
        paint::drawSectionHeader(drawList, ImGui::GetFont(), topLeft(bounds),
                                 nodeName(node, sem).c_str(),
                                 ImGui::GetFontSize() *
                                     (style.fontScale > 0.0f ? style.fontScale * 0.80f : 0.80f),
                                 bounds.w, pal);
        break;
    case VisualKind::Badge:
        paint::drawBadge(drawList, ImGui::GetFont(), topLeft(bounds),
                         nodeName(node, sem).c_str(),
                         ImGui::GetFontSize() *
                             (style.fontScale > 0.0f ? style.fontScale * 0.78f : 0.78f),
                         style.accent, pal);
        break;
    case VisualKind::ListItem:
        paint::drawListItem(drawList, ImGui::GetFont(), topLeft(bounds), sizeOf(bounds),
                            nodeName(node, sem).c_str(), pal, state,
                            style.fontScale > 0.0f ? style.fontScale : 0.90f);
        break;
    case VisualKind::Canvas:
        if (style.panelFill)
            drawList->AddRectFilled(topLeft(bounds), bottomRight(bounds), pal.frame, 3.0f);
        if (style.canvasDraw)
            style.canvasDraw(*drawList, node, bounds, state);
        if (style.panelBorder)
            drawList->AddRect(topLeft(bounds), bottomRight(bounds), pal.frameBright, 3.0f);
        if (state.focused && !state.disabled)
            paint::drawFocusRing(drawList, topLeft(bounds), bottomRight(bounds), pal, 3.0f);
        break;
    case VisualKind::Button:
        paint::drawButton(drawList, ImGui::GetFont(), topLeft(bounds), sizeOf(bounds),
                          nodeName(node, sem).c_str(), pal, state);
        break;
    case VisualKind::IconButton: {
        const bool down = state.active || state.selected || checked(node, sem);
        paint::drawTactileIconButton(drawList, iconFont(style.iconFont), topLeft(bounds),
                                     sizeOf(bounds), style.glyph.c_str(), pal, state,
                                     down, style.face);
        break;
    }
    case VisualKind::Toggle:
        paint::drawToggle(drawList, topLeft(bounds), bounds.w, bounds.h,
                          checked(node, sem) ? 1.0f : 0.0f, pal, state);
        if (!nodeName(node, sem).empty()) {
            Rect textBounds = bounds;
            textBounds.x += bounds.w + 6.0f;
            textBounds.w = 160.0f;
            drawText(drawList, ImGui::GetFont(), textBounds, nodeName(node, sem), 1.0f,
                     state.disabled ? pal.textDim : pal.text, Align::Start);
        }
        break;
    case VisualKind::Knob: {
        const float d = std::min(bounds.w, bounds.h);
        const ImVec2 p(bounds.x + (bounds.w - d) * 0.5f, bounds.y);
        paint::drawKnob(drawList, p, d, (float)normalizedValue(node, sem),
                        style.knobStyle, pal, state, style.bipolar, style.accent);
        const std::string name = nodeName(node, sem);
        if (!name.empty()) {
            Rect labelBounds{bounds.x, bounds.y + d + 2.0f, bounds.w,
                             ImGui::GetFontSize() + 2.0f};
            drawText(drawList, ImGui::GetFont(), labelBounds, name, 0.90f, pal.text);
        }
        break;
    }
    case VisualKind::Led:
        paint::drawLed(drawList,
                       ImVec2(bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f),
                       style.ledRadius > 0.0f ? style.ledRadius : 5.0f,
                       checked(node, sem), pal, state, style.accent);
        break;
    case VisualKind::Meter:
        paint::drawMeter(drawList, topLeft(bounds), sizeOf(bounds),
                         (float)normalizedValue(node, sem),
                         (float)normalizedValue(node, sem),
                         style.meterFloorDb, pal);
        break;
    case VisualKind::Fader:
        paint::drawFader(drawList, topLeft(bounds), sizeOf(bounds),
                         (float)normalizedValue(node, sem), pal, state);
        break;
    case VisualKind::Auto:
    default:
        break;
    }

    for (const auto& child : node.children())
        renderNode(*child, semantics, origin, drawList);
}

bool dispatchImGuiInput(Tree& tree, const ImVec2& origin)
{
    return dispatchImGuiInput(tree, origin, false);
}

bool dispatchImGuiInput(Tree& tree, const ImVec2& origin, bool mouseCaptured)
{
    bool consumed = false;
    ImGuiIO& io = ImGui::GetIO();
    const Vec2 localMouse{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
    const bool mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Event event;
        event.type = EventType::MouseDown;
        event.position = localMouse;
        event.button = MouseButton::Left;
        consumed = tree.dispatch(event) || consumed;
    }

    if (tree.root().bounds().contains(localMouse) ||
        tree.hovered() != nullptr || mouseCaptured || mouseReleased) {
        Event event;
        event.type = EventType::MouseMove;
        event.position = localMouse;
        consumed = tree.dispatch(event) || consumed;
    }

    if (mouseReleased) {
        Event event;
        event.type = EventType::MouseUp;
        event.position = localMouse;
        event.button = MouseButton::Left;
        consumed = tree.dispatch(event) || consumed;
    }

    const bool keyboardScope = tree.focused() != nullptr ||
                               tree.root().bounds().contains(localMouse);
    if (!keyboardScope)
        return consumed;

    constexpr ImGuiKey keys[] = {
        ImGuiKey_Tab,        ImGuiKey_Enter,    ImGuiKey_KeypadEnter,
        ImGuiKey_Space,      ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
        ImGuiKey_UpArrow,    ImGuiKey_DownArrow, ImGuiKey_Escape,
    };
    for (ImGuiKey key : keys) {
        if (ImGui::IsKeyPressed(key, false)) {
            Event event = keyEvent(key);
            if (event.key != Key::Unknown)
                consumed = tree.dispatch(event) || consumed;
        }
    }

    return consumed;
}

ImGuiFrameResult drawImGui(Tree& tree, PaintRenderer& renderer, Vec2 size,
                           ImDrawList* drawList)
{
    ImGuiFrameResult result;
    size.x = std::max(0.0f, size.x);
    size.y = std::max(0.0f, size.y);
    result.valuesRefreshed = tree.refreshBoundValues();
    tree.layout(size);

    result.origin = ImGui::GetCursorScreenPos();
    result.size = ImVec2(size.x, size.y);

    ImGui::PushID(&tree);
    ImGui::InvisibleButton("##snd-retained-tree", result.size);
    const bool mouseCaptured = ImGui::IsItemActive();
    ImGui::PopID();

    result.inputConsumed = dispatchImGuiInput(tree, result.origin, mouseCaptured);
    renderer.render(tree, result.origin, drawList);
    return result;
}

namespace widgets {

Node::Ptr panel(NodeId id, Layout layout, Insets padding)
{
    auto node = Node::make(std::move(id), Role::Group);
    layout.padding = padding;
    node->setLayout(layout);
    node->setSize(Length::fill(), Length::fill());
    return node;
}

Node::Ptr row(NodeId id, float gap, Insets padding)
{
    Layout layout;
    layout.kind = LayoutKind::Row;
    layout.gap = gap;
    layout.padding = padding;
    return panel(std::move(id), layout, padding);
}

Node::Ptr column(NodeId id, float gap, Insets padding)
{
    Layout layout;
    layout.kind = LayoutKind::Column;
    layout.gap = gap;
    layout.padding = padding;
    return panel(std::move(id), layout, padding);
}

Node::Ptr label(NodeId id, std::string text, PaintRenderer* renderer)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::Text);
    node->setIntrinsicSize({120.0f, 20.0f});
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setSemantics(named(Role::Text, text));
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Text;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr sectionHeader(NodeId id, std::string text, PaintRenderer* renderer)
{
    NodeId sid = id;
    auto node = label(std::move(id), std::move(text), nullptr);
    node->setIntrinsicSize({180.0f, 22.0f});
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::SectionHeader;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr badge(NodeId id, std::string text, PaintRenderer* renderer)
{
    NodeId sid = id;
    auto node = label(std::move(id), std::move(text), nullptr);
    node->setIntrinsicSize({56.0f, 18.0f});
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Badge;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr listItem(NodeId id, std::string text, bool selected,
                   std::function<void(Node&)> onActivate, PaintRenderer* renderer)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::ListItem);
    node->setIntrinsicSize({180.0f, 26.0f});
    node->setSize(Length::fill(), Length::intrinsic());
    node->setFocusable(true);
    Semantics sem = named(Role::ListItem, text);
    if (selected)
        sem.states |= SemanticState::Selected;
    node->setSemantics(sem);
    if (onActivate)
        node->setOnActivate(std::move(onActivate));
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::ListItem;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr patternGrid(NodeId id, std::string name, bool* cells, int rows, int steps,
                      PaintRenderer* renderer, Vec2 size,
                      std::function<int()> playheadStep)
{
    NodeId sid = id;
    auto binding = std::make_shared<PatternGridBinding>();
    binding->cells = cells;
    binding->rows = rows;
    binding->steps = steps;
    binding->playheadStep = std::move(playheadStep);

    auto node = Node::make(std::move(id), Role::Canvas);
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);
    Semantics sem = named(Role::Canvas, name);
    sem.description = "Step pattern grid";
    node->setSemantics(sem);
    refreshPatternSemantics(*node, *binding);
    node->setOnEvent([binding](Node& n, const Event& event) {
        if (event.type != EventType::MouseDown && event.type != EventType::MouseMove)
            return false;
        if (event.type == EventType::MouseMove && !n.pressed())
            return false;
        return paintPatternCell(n, *binding, event.position);
    });

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Canvas;
        style.canvasDraw = [binding](ImDrawList& dl, const Node&, Rect bounds,
                                     const paint::ControlState& state) {
            paint::ControlState inner = state;
            inner.focused = false;
            paint::drawPatternGrid(&dl, topLeft(bounds), sizeOf(bounds),
                                   binding->cells, binding->rows, binding->steps,
                                   patternPlayhead(*binding), palette(), inner);
        };
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr xyPad(NodeId id, std::string name, ValueBinding xBinding,
                ValueBinding yBinding, PaintRenderer* renderer, Vec2 size)
{
    NodeId sid = id;
    auto bindings = std::make_shared<XYPadBindings>();
    bindings->x = std::move(xBinding);
    bindings->y = std::move(yBinding);

    auto node = Node::make(std::move(id), Role::Canvas);
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);
    Semantics sem = named(Role::Canvas, name);
    sem.description = "Two-axis XY control";
    sem.value.text = xyValueText(*bindings);
    node->setSemantics(sem);
    node->setOnEvent([bindings](Node& n, const Event& event) {
        if (event.type != EventType::MouseDown && event.type != EventType::MouseMove)
            return false;
        if (event.type == EventType::MouseMove && !n.pressed())
            return false;
        return writeXYFromPoint(n, *bindings, event.position);
    });

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Canvas;
        style.canvasDraw = [bindings](ImDrawList& dl, const Node&, Rect bounds,
                                      const paint::ControlState& state) {
            paint::ControlState inner = state;
            inner.focused = false;
            paint::drawXYPad(&dl, topLeft(bounds), sizeOf(bounds),
                             (float)readBinding(bindings->x),
                             (float)readBinding(bindings->y),
                             palette(), inner);
        };
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr keyboard(NodeId id, std::string name, int firstNote, int octaves,
                   std::function<void(uint8_t note, uint8_t velocity)> noteOn,
                   std::function<void(uint8_t note)> noteOff,
                   const bool* lit, PaintRenderer* renderer, Vec2 size)
{
    NodeId sid = id;
    auto binding = std::make_shared<KeyboardBinding>();
    binding->firstNote = firstNote;
    binding->octaves = octaves;
    binding->noteOn = std::move(noteOn);
    binding->noteOff = std::move(noteOff);
    binding->lit = lit;

    auto node = Node::make(std::move(id), Role::Canvas);
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);
    Semantics sem = named(Role::Canvas, name);
    sem.description = "Piano keyboard";
    sem.value.text = "No note";
    node->setSemantics(sem);
    node->setOnEvent([binding](Node& n, const Event& event) {
        if (event.type == EventType::MouseDown)
            return playKeyboardFromPoint(n, *binding, event.position);
        if (event.type == EventType::MouseMove) {
            if (!n.pressed())
                return false;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const bool released = releaseKeyboard(*binding);
                if (released)
                    refreshKeyboardSemantics(n, *binding);
                return released;
            }
            return playKeyboardFromPoint(n, *binding, event.position);
        }
        if (event.type == EventType::MouseUp) {
            const bool released = releaseKeyboard(*binding);
            if (released)
                refreshKeyboardSemantics(n, *binding);
            return released;
        }
        return false;
    });

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Canvas;
        style.canvasDraw = [binding](ImDrawList& dl, const Node&, Rect bounds,
                                     const paint::ControlState& state) {
            paint::ControlState inner = state;
            inner.focused = false;
            paint::drawKeyboard(&dl, topLeft(bounds), sizeOf(bounds),
                                binding->firstNote, binding->octaves,
                                binding->mouseNote, binding->lit,
                                palette(), inner);
        };
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr canvas(NodeId id, std::string name, Vec2 intrinsicSize,
                 VisualStyle::CanvasDraw draw,
                 PaintRenderer* renderer, bool focusable, Role semanticRole)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::Canvas);
    node->setIntrinsicSize(intrinsicSize);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(focusable);
    node->setSemantics(named(semanticRole, name));
    if (name.empty())
        node->semantics().hidden = true;
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Canvas;
        style.panelBorder = true;
        style.canvasDraw = std::move(draw);
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr button(NodeId id, std::string name, std::function<void(Node&)> onActivate,
                 PaintRenderer* renderer)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::Button);
    node->setIntrinsicSize({72.0f, 28.0f});
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);
    node->setSemantics(named(Role::Button, name));
    if (onActivate)
        node->setOnActivate(std::move(onActivate));
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Button;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr iconButton(NodeId id, std::string name, std::string glyph,
                     std::function<void(Node&)> onActivate,
                     PaintRenderer* renderer, IconFont font)
{
    NodeId sid = id;
    auto node = button(std::move(id), std::move(name), std::move(onActivate), nullptr);
    node->setIntrinsicSize({30.0f, 30.0f});
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::IconButton;
        style.glyph = std::move(glyph);
        style.iconFont = font;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr toggle(NodeId id, std::string name, ValueBinding binding,
                 PaintRenderer* renderer)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::Toggle);
    node->setIntrinsicSize({36.0f, 20.0f});
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);
    Semantics sem = named(Role::Toggle, name);
    sem.value.hasNumeric = true;
    sem.value.min = binding.min;
    sem.value.max = binding.max;
    sem.value.step = binding.step;
    if (binding.get) {
        sem.value.value = binding.get();
        if (sem.value.value >= (binding.min + binding.max) * 0.5)
            sem.states |= SemanticState::Checked;
    }
    node->setSemantics(sem);
    node->setValueBinding(std::move(binding));
    node->setOnActivate([](Node& n) {
        const ValueBinding* b = n.valueBinding();
        if (!b || !b->get || !b->set)
            return;
        const bool next = b->get() < (b->min + b->max) * 0.5;
        b->set(next ? b->max : b->min);
        setCheckedState(n, next);
    });
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Toggle;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr knob(NodeId id, std::string name, ValueBinding binding,
               PaintRenderer* renderer, KnobStyle knobStyle, bool bipolar,
               float diameter)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::Slider);
    node->setIntrinsicSize({diameter, diameter + 20.0f});
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);
    Semantics sem = named(Role::Slider, name);
    sem.value.hasNumeric = true;
    sem.value.min = binding.min;
    sem.value.max = binding.max;
    sem.value.step = binding.step;
    if (binding.get)
        sem.value.value = binding.get();
    node->setSemantics(sem);
    node->setValueBinding(std::move(binding));
    installKnobPointerBehavior(*node);
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Knob;
        style.knobStyle = knobStyle;
        style.bipolar = bipolar;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr fader(NodeId id, std::string name, ValueBinding binding,
                PaintRenderer* renderer, Vec2 size)
{
    NodeId sid = id;
    auto node = knob(std::move(id), std::move(name), std::move(binding), nullptr,
                     KnobStyle::Ring, false, size.x);
    node->setIntrinsicSize(size);
    installFaderPointerBehavior(*node);
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Fader;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr meter(NodeId id, std::string name, double value,
                PaintRenderer* renderer, Vec2 size)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::Meter);
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    Semantics sem = named(Role::Meter, name);
    sem.value.hasNumeric = true;
    sem.value.value = value;
    sem.value.min = 0.0;
    sem.value.max = 1.0;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f%%", clamp01(value) * 100.0);
    sem.value.text = buf;
    node->setSemantics(sem);
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Meter;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr meter(NodeId id, std::string name, ValueBinding binding,
                PaintRenderer* renderer, Vec2 size)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::Meter);
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    Semantics sem = named(Role::Meter, name);
    sem.value.hasNumeric = true;
    sem.value.min = binding.min;
    sem.value.max = binding.max;
    sem.value.step = binding.step;
    if (binding.get) {
        sem.value.value = binding.get();
        if (binding.format)
            sem.value.text = binding.format(sem.value.value);
    }
    node->setSemantics(sem);
    node->setValueBinding(std::move(binding));
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Meter;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr led(NodeId id, std::string name, bool on, bool clickable,
              std::function<void(Node&)> onActivate, PaintRenderer* renderer)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), clickable ? Role::Toggle : Role::Text);
    node->setIntrinsicSize({22.0f, 22.0f});
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(clickable);
    Semantics sem = named(clickable ? Role::Toggle : Role::Text, name);
    sem.value.hasNumeric = true;
    sem.value.value = on ? 1.0 : 0.0;
    sem.value.text = on ? "On" : "Off";
    if (on)
        sem.states |= SemanticState::Checked;
    if (name.empty())
        sem.hidden = true;
    node->setSemantics(sem);
    if (onActivate) {
        node->setOnActivate(std::move(onActivate));
    } else if (clickable) {
        node->setOnActivate([](Node& n) {
            setCheckedState(n, !hasState(n.semantics().states, SemanticState::Checked));
        });
    }
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Led;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr led(NodeId id, std::string name, ValueBinding binding,
              bool clickable, PaintRenderer* renderer)
{
    NodeId sid = id;
    const bool on = binding.get && binding.get() >= (binding.min + binding.max) * 0.5;
    auto node = Node::make(std::move(id), clickable ? Role::Toggle : Role::Text);
    node->setIntrinsicSize({22.0f, 22.0f});
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(clickable);
    Semantics sem = named(clickable ? Role::Toggle : Role::Text, name);
    const double current = binding.get ? binding.get() : binding.min;
    sem.value.hasNumeric = true;
    sem.value.min = binding.min;
    sem.value.max = binding.max;
    sem.value.step = binding.step;
    sem.value.value = current;
    sem.value.text = binding.format ? binding.format(current) : (on ? "On" : "Off");
    if (on)
        sem.states |= SemanticState::Checked;
    if (name.empty())
        sem.hidden = true;
    node->setSemantics(sem);
    node->setValueBinding(std::move(binding));
    if (clickable) {
        node->setOnActivate([](Node& n) {
            const ValueBinding* b = n.valueBinding();
            if (!b || !b->get || !b->set)
                return;
            const bool next = b->get() < (b->min + b->max) * 0.5;
            b->set(next ? b->max : b->min);
            setCheckedState(n, next);
        });
    }
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Led;
        renderer->setStyle(sid, style);
    }
    return node;
}

} // namespace widgets

} // namespace snd::ui::retained
