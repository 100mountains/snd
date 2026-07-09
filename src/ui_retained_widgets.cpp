#include "snd/ui_retained_widgets.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <unordered_map>
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

std::string nodeValueText(const Node& node, const SemanticNode* sem)
{
    const ValueRange& range = sem ? sem->value : node.semantics().value;
    if (!range.text.empty())
        return range.text;
    if (const ValueBinding* binding = node.valueBinding())
        return formatBindingValue(*binding, readBinding(*binding, range.value));
    if (range.hasNumeric) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", range.value);
        return std::string(buf);
    }
    return {};
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

MouseButton mapMouseButton(ImGuiMouseButton button)
{
    switch (button) {
    case ImGuiMouseButton_Left:
        return MouseButton::Left;
    case ImGuiMouseButton_Right:
        return MouseButton::Right;
    case ImGuiMouseButton_Middle:
        return MouseButton::Middle;
    default:
        return MouseButton::None;
    }
}

void fillModifiers(Event& event, const ImGuiIO& io)
{
    event.shift = io.KeyShift;
    event.ctrl = io.KeyCtrl;
    event.alt = io.KeyAlt;
    event.super = io.KeySuper;
}

Event keyEvent(ImGuiKey key)
{
    Event event;
    event.type = EventType::KeyDown;
    event.key = mapKey(key);
    fillModifiers(event, ImGui::GetIO());
    return event;
}

Event pointerEvent(EventType type, Vec2 position, Vec2 delta, MouseButton button,
                   const ImGuiIO& io)
{
    Event event;
    event.type = type;
    event.position = position;
    event.delta = delta;
    event.button = button;
    fillModifiers(event, io);
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

void setBindingSemantics(Semantics& sem, const ValueBinding& binding)
{
    const double current = readBinding(binding, binding.min);
    sem.value.hasNumeric = true;
    sem.value.min = binding.min;
    sem.value.max = binding.max;
    sem.value.step = binding.step;
    sem.value.value = current;
    sem.value.text = formatBindingValue(binding, current);
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
    sem.value.text = formatBindingValue(*binding, next);
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
        if (event.type == EventType::MouseDown && event.button != MouseButton::Left)
            return false;
        if (event.type != EventType::MouseDown && event.type != EventType::MouseMove)
            return false;
        if (event.type == EventType::MouseMove && !n.pressed())
            return false;
        if (event.type == EventType::MouseMove &&
            event.delta.x == 0.0f && event.delta.y == 0.0f)
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
        if (event.type == EventType::MouseDown) {
            if (event.button != MouseButton::Left)
                return false;
            return true;
        }

        if (event.type == EventType::MouseUp) {
            if (event.button != MouseButton::Left)
                return false;
            return true;
        }

        if (event.type != EventType::MouseMove || !n.pressed())
            return false;

        const ValueBinding* binding = n.valueBinding();
        if (!binding || !binding->get)
            return false;

        const double range = std::abs(binding->max - binding->min);
        if (range <= 0.0)
            return false;

        if (event.delta.x == 0.0f && event.delta.y == 0.0f)
            return false;

        const double fine = event.shift ? 0.25 : 1.0;
        const double next = (event.delta.x - event.delta.y) * range *
                            (fine / 180.0);
        return setBindingValue(n, binding->get() + next);
    });
}

void installDragNumberPointerBehavior(Node& node, double dragSpeed)
{
    node.setOnEvent([dragSpeed](Node& n, const Event& event) {
        if (event.type == EventType::MouseDown) {
            if (event.button != MouseButton::Left)
                return false;
            return true;
        }

        if (event.type == EventType::MouseUp) {
            if (event.button != MouseButton::Left)
                return false;
            return true;
        }

        if (event.type != EventType::MouseMove || !n.pressed())
            return false;

        const ValueBinding* binding = n.valueBinding();
        if (!binding || !binding->get)
            return false;

        const float dx = event.delta.x;
        if (dx == 0.0f)
            return false;

        const double range = std::abs(binding->max - binding->min);
        double speed = 0.01;
        if (dragSpeed > 0.0)
            speed = dragSpeed;
        else if (binding->step > 0.0)
            speed = binding->step;
        else if (range > 0.0)
            speed = range / 100.0;
        if (event.shift)
            speed *= 0.1;
        return setBindingValue(n, binding->get() + (double)dx * speed);
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

bool paintPatternCell(Node& node, const PatternGridBinding& binding, Vec2 point,
                      bool paintOn)
{
    const Rect bounds = node.bounds();
    if (!binding.cells || binding.rows <= 0 || binding.steps <= 0 ||
        bounds.w <= 0.0f || bounds.h <= 0.0f)
        return false;

    const int col = (int)((point.x - bounds.x) / (bounds.w / binding.steps));
    const int row = (int)((point.y - bounds.y) / (bounds.h / binding.rows));
    if (col < 0 || col >= binding.steps || row < 0 || row >= binding.rows)
        return false;

    bool& cell = binding.cells[row * binding.steps + col];
    if (cell == paintOn)
        return false;
    cell = paintOn;
    refreshPatternSemantics(node, binding);
    return true;
}

struct EnvelopeBinding {
    std::vector<EnvPoint>* points = nullptr;
    std::vector<float>* tensions = nullptr;
    int activePoint = -1;
    int activeSegment = -1;
    uint64_t observedHash = 0;
    bool hasObservedHash = false;
};

void ensureEnvelopePoints(EnvelopeBinding& binding)
{
    if (!binding.points)
        return;
    if (binding.points->empty())
        *binding.points = {{0.0f, 0.5f}, {1.0f, 0.5f}};
    if (binding.tensions)
        binding.tensions->resize(binding.points->size(), 0.0f);
    if (binding.activePoint >= (int)binding.points->size())
        binding.activePoint = -1;
    if (binding.activeSegment + 1 >= (int)binding.points->size())
        binding.activeSegment = -1;
}

float envelopeSegmentTension(const EnvelopeBinding& binding, size_t index)
{
    return binding.tensions && index < binding.tensions->size()
               ? (*binding.tensions)[index]
               : 0.0f;
}

uint64_t mixEnvelopeHash(uint64_t hash, int value)
{
    hash ^= (uint32_t)value;
    hash *= 1099511628211ull;
    return hash;
}

uint64_t envelopeStateHash(const EnvelopeBinding& binding)
{
    uint64_t hash = 1469598103934665603ull;
    if (!binding.points)
        return hash;
    hash = mixEnvelopeHash(hash, (int)binding.points->size());
    for (const EnvPoint& point : *binding.points) {
        hash = mixEnvelopeHash(hash, (int)std::lround(point.x * 1000000.0f));
        hash = mixEnvelopeHash(hash, (int)std::lround(point.y * 1000000.0f));
    }
    if (binding.tensions) {
        hash = mixEnvelopeHash(hash, (int)binding.tensions->size());
        for (float tension : *binding.tensions)
            hash = mixEnvelopeHash(hash, (int)std::lround(tension * 1000000.0f));
    }
    return hash;
}

float envelopeValueAt(const EnvelopeBinding& binding, float x)
{
    const auto& points = *binding.points;
    size_t i = 0;
    while (i + 2 < points.size() && x > points[i + 1].x)
        ++i;
    const EnvPoint& a = points[i];
    const EnvPoint& b = points[i + 1];
    const float t = std::clamp((x - a.x) / std::max(1e-6f, b.x - a.x),
                               0.0f, 1.0f);
    return a.y + (b.y - a.y) *
                     paint::envelopeEase(t, envelopeSegmentTension(binding, i));
}

Vec2 envelopePointToScreen(Rect bounds, EnvPoint point)
{
    return {bounds.x + point.x * bounds.w,
            bounds.y + (1.0f - point.y) * bounds.h};
}

EnvPoint envelopePointFromScreen(Rect bounds, Vec2 point)
{
    if (bounds.w <= 0.0f || bounds.h <= 0.0f)
        return {};
    return {std::clamp((point.x - bounds.x) / bounds.w, 0.0f, 1.0f),
            std::clamp(1.0f - (point.y - bounds.y) / bounds.h, 0.0f, 1.0f)};
}

int envelopeHotPoint(EnvelopeBinding& binding, Rect bounds, Vec2 point)
{
    ensureEnvelopePoints(binding);
    if (!binding.points || bounds.w <= 0.0f || bounds.h <= 0.0f)
        return -1;

    constexpr float grab = 8.0f;
    for (int i = 0; i < (int)binding.points->size(); ++i) {
        Vec2 screen = envelopePointToScreen(bounds, (*binding.points)[(size_t)i]);
        if (std::fabs(point.x - screen.x) < grab &&
            std::fabs(point.y - screen.y) < grab) {
            return i;
        }
    }
    return -1;
}

int envelopeHotSegment(EnvelopeBinding& binding, Rect bounds, Vec2 point)
{
    ensureEnvelopePoints(binding);
    if (!binding.points || !binding.tensions || binding.points->size() < 2 ||
        bounds.w <= 0.0f || bounds.h <= 0.0f ||
        point.x < bounds.x || point.x > bounds.x + bounds.w) {
        return -1;
    }

    constexpr float grab = 8.0f;
    const float nx = std::clamp((point.x - bounds.x) / bounds.w, 0.0f, 1.0f);
    const float sy = bounds.y + (1.0f - envelopeValueAt(binding, nx)) * bounds.h;
    if (std::fabs(point.y - sy) >= grab)
        return -1;

    size_t i = 0;
    while (i + 2 < binding.points->size() && nx > (*binding.points)[i + 1].x)
        ++i;
    return (int)i;
}

bool refreshEnvelopeSemantics(Node& node, EnvelopeBinding& binding)
{
    ensureEnvelopePoints(binding);
    const Semantics& current = static_cast<const Node&>(node).semantics();
    Semantics next = current;
    const int count = binding.points ? (int)binding.points->size() : 0;
    next.value.hasNumeric = true;
    next.value.min = 0.0;
    next.value.max = std::max(0, count);
    next.value.step = 1.0;
    next.value.value = count;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d envelope points", count);
    next.value.text = buf;

    const uint64_t hash = envelopeStateHash(binding);
    const bool valueChanged = current.value.hasNumeric != next.value.hasNumeric ||
                              current.value.value != next.value.value ||
                              current.value.min != next.value.min ||
                              current.value.max != next.value.max ||
                              current.value.step != next.value.step ||
                              current.value.text != next.value.text;
    const bool stateChanged = !binding.hasObservedHash ||
                              binding.observedHash != hash;
    binding.observedHash = hash;
    binding.hasObservedHash = true;
    if (valueChanged)
        node.setSemantics(next);
    return valueChanged || stateChanged;
}

bool addEnvelopePoint(Node& node, EnvelopeBinding& binding, Vec2 point)
{
    ensureEnvelopePoints(binding);
    if (!binding.points)
        return false;

    EnvPoint env = envelopePointFromScreen(node.bounds(), point);
    auto& points = *binding.points;
    auto it = std::find_if(points.begin(), points.end(),
                           [&](const EnvPoint& candidate) {
                               return candidate.x > env.x;
                           });
    const auto index = it - points.begin();
    if (binding.tensions)
        binding.tensions->insert(binding.tensions->begin() + index, 0.0f);
    points.insert(it, env);
    binding.activePoint = -1;
    binding.activeSegment = -1;
    refreshEnvelopeSemantics(node, binding);
    node.markDirty();
    return true;
}

bool deleteEnvelopePoint(Node& node, EnvelopeBinding& binding, Vec2 point)
{
    ensureEnvelopePoints(binding);
    if (!binding.points || binding.points->size() <= 2)
        return false;

    const int hot = envelopeHotPoint(binding, node.bounds(), point);
    if (hot <= 0 || hot >= (int)binding.points->size() - 1)
        return false;

    binding.points->erase(binding.points->begin() + hot);
    if (binding.tensions && hot < (int)binding.tensions->size())
        binding.tensions->erase(binding.tensions->begin() + hot);
    binding.activePoint = -1;
    binding.activeSegment = -1;
    refreshEnvelopeSemantics(node, binding);
    node.markDirty();
    return true;
}

bool dragEnvelopePoint(Node& node, EnvelopeBinding& binding, Vec2 point)
{
    ensureEnvelopePoints(binding);
    if (!binding.points || binding.activePoint < 0 ||
        binding.activePoint >= (int)binding.points->size()) {
        return false;
    }

    const Rect bounds = node.bounds();
    if (bounds.w <= 0.0f || bounds.h <= 0.0f)
        return false;

    auto& points = *binding.points;
    auto& env = points[(size_t)binding.activePoint];
    float lo = binding.activePoint == 0 ? 0.0f
                                        : points[(size_t)binding.activePoint - 1].x;
    float hi = binding.activePoint == (int)points.size() - 1
                   ? 1.0f
                   : points[(size_t)binding.activePoint + 1].x;
    if (binding.activePoint == 0)
        hi = 0.0f;
    if (binding.activePoint == (int)points.size() - 1)
        lo = 1.0f;

    const float nextX = std::clamp((point.x - bounds.x) / bounds.w,
                                   std::min(lo, hi), std::max(lo, hi));
    const float nextY = std::clamp(1.0f - (point.y - bounds.y) / bounds.h,
                                   0.0f, 1.0f);
    const bool changed = env.x != nextX || env.y != nextY;
    env.x = nextX;
    env.y = nextY;
    if (changed)
        node.markDirty();
    return changed;
}

bool dragEnvelopeSegment(Node& node, EnvelopeBinding& binding, Vec2 delta)
{
    ensureEnvelopePoints(binding);
    if (!binding.points || !binding.tensions || binding.activeSegment < 0 ||
        binding.activeSegment + 1 >= (int)binding.points->size()) {
        return false;
    }

    const float dy = delta.y;
    if (dy == 0.0f || node.bounds().h <= 0.0f)
        return false;

    const auto& points = *binding.points;
    const float dir = points[(size_t)binding.activeSegment + 1].y >=
                              points[(size_t)binding.activeSegment].y
                          ? 1.0f
                          : -1.0f;
    float& tension = (*binding.tensions)[(size_t)binding.activeSegment];
    const float next = std::clamp(tension + dir * dy * (3.0f / node.bounds().h),
                                  -1.0f, 1.0f);
    const bool changed = tension != next;
    tension = next;
    if (changed)
        node.markDirty();
    return changed;
}

bool handleEnvelopeEvent(Node& node, EnvelopeBinding& binding, const Event& event)
{
    ensureEnvelopePoints(binding);
    if (!binding.points)
        return false;

    if (event.type == EventType::ContextMenu)
        return deleteEnvelopePoint(node, binding, event.position);

    if (event.type == EventType::MouseDown && event.button == MouseButton::Left) {
        const int hot = envelopeHotPoint(binding, node.bounds(), event.position);
        if (event.clickCount >= 2 && hot < 0)
            return addEnvelopePoint(node, binding, event.position);

        binding.activePoint = hot;
        binding.activeSegment = hot < 0
                                    ? envelopeHotSegment(binding, node.bounds(),
                                                         event.position)
                                    : -1;
        return binding.activePoint >= 0 || binding.activeSegment >= 0;
    }

    if (event.type == EventType::MouseMove) {
        if (!node.pressed())
            return false;
        if (event.delta.x == 0.0f && event.delta.y == 0.0f)
            return false;
        if (binding.activePoint >= 0)
            return dragEnvelopePoint(node, binding, event.position);
        if (binding.activeSegment >= 0)
            return dragEnvelopeSegment(node, binding, event.delta);
        return false;
    }

    if (event.type == EventType::MouseUp && event.button == MouseButton::Left) {
        const bool wasActive = binding.activePoint >= 0 || binding.activeSegment >= 0;
        binding.activePoint = -1;
        binding.activeSegment = -1;
        if (wasActive)
            node.markDirty();
        return wasActive;
    }

    return false;
}

float ledExtent(float radius)
{
    const float r = radius > 0.0f ? radius : 5.0f;
    return r * 3.8f;
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
    case VisualKind::ValueRow: {
        const std::string name = nodeName(node, sem);
        const std::string valueText = nodeValueText(node, sem);
        paint::drawValueRow(drawList, ImGui::GetFont(), topLeft(bounds), sizeOf(bounds),
                            name.c_str(), valueText.c_str(), pal, state,
                            style.fontScale > 0.0f ? style.fontScale : 0.90f,
                            node.role() == Role::Slider);
        break;
    }
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
    {
        const std::string name = nodeName(node, sem);
        paint::ButtonPaintArgs args;
        args.drawList = drawList;
        args.font = ImGui::GetFont();
        args.topLeft = topLeft(bounds);
        args.size = sizeOf(bounds);
        args.text = name.c_str();
        args.face = style.face;
        args.palette = &pal;
        args.state = &state;
        args.fontScale = style.fontScale > 0.0f ? style.fontScale : 0.90f;
        paint::drawButtonWithPainter(args, style.buttonPainter);
        break;
    }
    case VisualKind::IconButton: {
        const bool down = state.active || state.selected || checked(node, sem);
        paint::drawTactileIconButton(drawList, iconFont(style.iconFont), topLeft(bounds),
                                     sizeOf(bounds), style.glyph.c_str(), pal, state,
                                     down, style.face);
        break;
    }
    case VisualKind::VectorIconButton:
        paint::drawVectorIconButton(drawList, topLeft(bounds), sizeOf(bounds),
                                    style.vectorIcon, style.accent, pal, state,
                                    style.lit);
        break;
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
        paint::KnobPaintArgs args;
        args.drawList = drawList;
        args.topLeft = p;
        args.size = d;
        args.rawValue = (float)valueOf(node, sem);
        args.normalizedValue = (float)normalizedValue(node, sem);
        args.style = style.knobStyle;
        args.bipolar = style.bipolar;
        args.accent = style.accent;
        args.palette = &pal;
        args.state = &state;
        paint::drawKnobWithPainter(args, style.knobPainter);
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
    static std::unordered_map<const Tree*, Vec2> lastMouseByTree;
    Vec2 moveDelta;
    auto lastIt = lastMouseByTree.find(&tree);
    if (lastIt != lastMouseByTree.end()) {
        moveDelta = {localMouse.x - lastIt->second.x,
                     localMouse.y - lastIt->second.y};
    }
    lastMouseByTree[&tree] = localMouse;

    constexpr ImGuiMouseButton buttons[] = {
        ImGuiMouseButton_Left,
        ImGuiMouseButton_Right,
        ImGuiMouseButton_Middle,
    };
    bool mouseReleased = false;
    bool mouseClicked = false;

    for (ImGuiMouseButton button : buttons) {
        if (ImGui::IsMouseClicked(button)) {
            mouseClicked = true;
            Event event = pointerEvent(EventType::MouseDown, localMouse, {},
                                       mapMouseButton(button), io);
            event.clickCount = ImGui::IsMouseDoubleClicked(button) ? 2 : 1;
            consumed = tree.dispatch(event) || consumed;
        }
        mouseReleased = mouseReleased || ImGui::IsMouseReleased(button);
    }

    if ((io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f) &&
        (tree.root().bounds().contains(localMouse) || tree.hovered() != nullptr ||
         mouseCaptured)) {
        Event event = pointerEvent(EventType::MouseWheel, localMouse, {},
                                   MouseButton::None, io);
        event.wheelDelta = {io.MouseWheelH, io.MouseWheel};
        consumed = tree.dispatch(event) || consumed;
    }

    if (tree.root().bounds().contains(localMouse) ||
        tree.hovered() != nullptr || mouseCaptured || mouseReleased) {
        if (mouseClicked)
            moveDelta = {};
        Event event = pointerEvent(EventType::MouseMove, localMouse,
                                   moveDelta,
                                   MouseButton::None, io);
        consumed = tree.dispatch(event) || consumed;
    }

    for (ImGuiMouseButton button : buttons) {
        if (ImGui::IsMouseReleased(button)) {
            Event event = pointerEvent(EventType::MouseUp, localMouse, {},
                                       mapMouseButton(button), io);
            event.clickCount = ImGui::IsMouseDoubleClicked(button) ? 2 : 1;
            consumed = tree.dispatch(event) || consumed;
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        Event event = pointerEvent(EventType::ContextMenu, localMouse, {},
                                   MouseButton::Right, io);
        event.clickCount = 1;
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

Node::Ptr gradientPanel(NodeId id, Vec2 size,
                        ImU32 topLeftColor, ImU32 topRightColor,
                        ImU32 bottomRightColor, ImU32 bottomLeftColor,
                        PaintRenderer* renderer)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::Group);
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    Semantics sem = named(Role::Group, std::string{});
    sem.hidden = true;
    node->setSemantics(sem);
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Canvas;
        style.canvasDraw = [topLeftColor, topRightColor,
                            bottomRightColor, bottomLeftColor](
                               ImDrawList& dl, const Node&, Rect bounds,
                               const paint::ControlState&) {
            paint::drawGradientPanel(&dl, topLeft(bounds), sizeOf(bounds),
                                     topLeftColor, topRightColor,
                                     bottomRightColor, bottomLeftColor);
        };
        renderer->setStyle(sid, style);
    }
    return node;
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
        if (event.type == EventType::MouseDown && event.button != MouseButton::Left)
            return false;
        if (event.type != EventType::MouseDown && event.type != EventType::MouseMove)
            return false;
        if (event.type == EventType::MouseMove && !n.pressed())
            return false;
        if (event.type == EventType::MouseMove &&
            event.delta.x == 0.0f && event.delta.y == 0.0f)
            return false;
        return paintPatternCell(n, *binding, event.position, !event.alt);
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
        if (event.type == EventType::MouseDown && event.button != MouseButton::Left)
            return false;
        if (event.type != EventType::MouseDown && event.type != EventType::MouseMove)
            return false;
        if (event.type == EventType::MouseMove && !n.pressed())
            return false;
        if (event.type == EventType::MouseMove &&
            event.delta.x == 0.0f && event.delta.y == 0.0f)
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
        if (event.type == EventType::MouseDown) {
            if (event.button != MouseButton::Left)
                return false;
            return playKeyboardFromPoint(n, *binding, event.position);
        }
        if (event.type == EventType::MouseMove) {
            if (!n.pressed())
                return false;
            return playKeyboardFromPoint(n, *binding, event.position);
        }
        if (event.type == EventType::MouseUp) {
            if (event.button != MouseButton::Left)
                return false;
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

Node::Ptr envelopeEditor(NodeId id, std::string name,
                         std::vector<EnvPoint>& points,
                         PaintRenderer* renderer, Vec2 size,
                         std::vector<float>* tensions)
{
    NodeId sid = id;
    auto binding = std::make_shared<EnvelopeBinding>();
    binding->points = &points;
    binding->tensions = tensions;
    ensureEnvelopePoints(*binding);

    auto node = Node::make(std::move(id), Role::Canvas);
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);
    Semantics sem = named(Role::Canvas, name);
    sem.description = "Envelope curve editor";
    node->setSemantics(sem);
    refreshEnvelopeSemantics(*node, *binding);
    node->setOnEvent([binding](Node& n, const Event& event) {
        return handleEnvelopeEvent(n, *binding, event);
    });
    node->setOnRefresh([binding](Node& n) {
        return refreshEnvelopeSemantics(n, *binding);
    });

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Canvas;
        style.canvasDraw = [binding](ImDrawList& dl, const Node&, Rect bounds,
                                     const paint::ControlState& state) {
            ensureEnvelopePoints(*binding);
            if (!binding->points)
                return;
            const ImGuiIO& io = ImGui::GetIO();
            const Vec2 mouse{io.MousePos.x, io.MousePos.y};
            paint::ControlState inner = state;
            inner.focused = false;
            const int hotPoint = state.hovered
                                     ? envelopeHotPoint(*binding, bounds, mouse)
                                     : -1;
            const int hotSegment = state.hovered && hotPoint < 0
                                       ? envelopeHotSegment(*binding, bounds, mouse)
                                       : -1;
            paint::drawEnvelope(&dl, topLeft(bounds), sizeOf(bounds),
                                *binding->points, binding->tensions,
                                hotPoint, binding->activePoint,
                                hotSegment, binding->activeSegment,
                                palette(), inner);
        };
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr valueRow(NodeId id, std::string name, ValueBinding binding,
                   PaintRenderer* renderer, Vec2 size)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::Text);
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    Semantics sem = named(Role::Text, name);
    sem.description = "Read-only value";
    setBindingSemantics(sem, binding);
    node->setSemantics(sem);
    binding.set = {};
    node->setValueBinding(std::move(binding));

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::ValueRow;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr dragNumber(NodeId id, std::string name, ValueBinding binding,
                     PaintRenderer* renderer, Vec2 size, double dragSpeed)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::Slider);
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);
    Semantics sem = named(Role::Slider, name);
    sem.description = "Horizontal drag number field";
    setBindingSemantics(sem, binding);
    node->setSemantics(sem);
    node->setValueBinding(std::move(binding));
    installDragNumberPointerBehavior(*node, dragSpeed);

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::ValueRow;
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
                 PaintRenderer* renderer, paint::ButtonPainter painter)
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
        style.buttonPainter = std::move(painter);
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr animatedButton(NodeId id, std::string name,
                         std::function<void(Node&)> onActivate,
                         PaintRenderer* renderer, Vec2 size,
                         ImU32 top, ImU32 bottom, bool animate)
{
    NodeId sid = id;
    auto node = button(std::move(id), std::move(name), std::move(onActivate), nullptr);
    node->setIntrinsicSize(size);
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Canvas;
        style.canvasDraw = [label = node->semantics().name, top, bottom, animate](
                               ImDrawList& dl, const Node&, Rect bounds,
                               const paint::ControlState& state) {
            const float pulse = animate
                                    ? 0.5f + 0.5f *
                                                   std::sin((float)ImGui::GetTime() * 3.6f)
                                    : 0.0f;
            paint::drawAnimatedButton(&dl, ImGui::GetFont(), topLeft(bounds),
                                      sizeOf(bounds), label.c_str(), top, bottom,
                                      palette(), state, pulse);
        };
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

Node::Ptr iconButton(NodeId id, std::string name, Icon icon,
                     std::function<void(Node&)> onActivate,
                     PaintRenderer* renderer, Vec2 size, ImU32 accent, bool active)
{
    NodeId sid = id;
    auto node = button(std::move(id), std::move(name), std::move(onActivate), nullptr);
    node->setIntrinsicSize(size);
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::VectorIconButton;
        style.vectorIcon = icon;
        style.accent = accent;
        style.lit = active;
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
               float diameter, paint::KnobPainter painter)
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
        style.knobPainter = std::move(painter);
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
              std::function<void(Node&)> onActivate, PaintRenderer* renderer,
              float radius, ImU32 onColor)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), clickable ? Role::Toggle : Role::Text);
    const float extent = ledExtent(radius);
    node->setIntrinsicSize({extent, extent});
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
        style.ledRadius = radius > 0.0f ? radius : 5.0f;
        style.accent = onColor;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr led(NodeId id, std::string name, ValueBinding binding,
              bool clickable, PaintRenderer* renderer,
              float radius, ImU32 onColor)
{
    NodeId sid = id;
    const bool on = binding.get && binding.get() >= (binding.min + binding.max) * 0.5;
    auto node = Node::make(std::move(id), clickable ? Role::Toggle : Role::Text);
    const float extent = ledExtent(radius);
    node->setIntrinsicSize({extent, extent});
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
        style.ledRadius = radius > 0.0f ? radius : 5.0f;
        style.accent = onColor;
        renderer->setStyle(sid, style);
    }
    return node;
}

} // namespace widgets

} // namespace snd::ui::retained
