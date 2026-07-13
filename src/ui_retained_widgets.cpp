#include "snd/ui_retained_widgets.h"

#include "snd/ui.h"
#include "ui_draw_imgui.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <system_error>
#include <unordered_set>
#include <unordered_map>
#include <utility>

namespace snd::ui::retained {

namespace {

ImVec2 topLeft(Rect r) { return ImVec2(r.x, r.y); }
ImVec2 sizeOf(Rect r) { return ImVec2(r.w, r.h); }
ImVec2 bottomRight(Rect r) { return ImVec2(r.x + r.w, r.y + r.h); }
draw::Vec2 topLeftDraw(Rect r) { return {r.x, r.y}; }
draw::Vec2 sizeOfDraw(Rect r) { return {r.w, r.h}; }
draw::Vec2 bottomRightDraw(Rect r) { return {r.x + r.w, r.y + r.h}; }
Rect offset(Rect r, const ImVec2& origin)
{
    r.x += origin.x;
    r.y += origin.y;
    return r;
}
Rect offset(Rect r, draw::Vec2 origin)
{
    r.x += origin.x;
    r.y += origin.y;
    return r;
}

constexpr double kTooltipDelaySeconds = 0.4;

bool tooltipDelayReady(std::string& currentKey, double& startSeconds,
                       const std::string& nextKey, double nowSeconds)
{
    if (nextKey.empty()) {
        currentKey.clear();
        startSeconds = nowSeconds;
        return false;
    }
    if (currentKey != nextKey) {
        currentKey = nextKey;
        startSeconds = nowSeconds;
        return false;
    }
    if (nowSeconds < startSeconds)
        startSeconds = nowSeconds;
    return nowSeconds - startSeconds >= kTooltipDelaySeconds;
}

const SemanticNode* semanticForNode(
    const Node& node,
    const std::unordered_map<NodeId, SemanticNode>* semantics)
{
    if (!semantics)
        return nullptr;
    const auto it = semantics->find(node.id());
    return it != semantics->end() ? &it->second : nullptr;
}

const std::string* tooltipForNode(
    const Node& node,
    const std::unordered_map<NodeId, SemanticNode>* semantics)
{
    if (!node.visible() || !node.enabled())
        return nullptr;

    const SemanticNode* sem = semanticForNode(node, semantics);
    if (semantics && !sem)
        return nullptr;
    const Semantics& local = node.semantics();
    if (local.hidden || hasState(local.states, SemanticState::Hidden))
        return nullptr;
    if (sem && hasState(sem->states, SemanticState::Hidden))
        return nullptr;
    if (sem && hasState(sem->states, SemanticState::Disabled))
        return nullptr;

    const std::string& text = sem ? sem->tooltip : local.tooltip;
    return text.empty() ? nullptr : &text;
}

const Node* tooltipNodeForHover(
    const Node* hovered,
    const std::unordered_map<NodeId, SemanticNode>* semantics)
{
    for (const Node* node = hovered; node != nullptr; node = node->parent()) {
        if (tooltipForNode(*node, semantics))
            return node;
    }
    return nullptr;
}

draw::Vec2 tooltipAnchor(const Node& node, draw::Vec2 origin,
                         const draw::FrameContext& context)
{
    if (context.pointerValid)
        return context.pointer;
    const Rect bounds = offset(node.bounds(), origin);
    return {bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f};
}

draw::Vec2 tooltipClipMax(const Node& root, draw::Vec2 origin)
{
    const Rect bounds = offset(root.bounds(), origin);
    if (bounds.w <= 0.0f || bounds.h <= 0.0f)
        return {};
    return {bounds.x + bounds.w, bounds.y + bounds.h};
}

std::string tooltipKeyForNode(const Node& node, const std::string& text)
{
    return node.id().empty() ? text : node.id() + "\n" + text;
}

// Vertical scrollbar geometry for a scroll container: the thumb rect on the
// right edge of `viewRect`. Shared by the render (drawScrollbar) and the
// widget's drag handling so a press lands exactly on what's drawn.
constexpr float kScrollBarW = 6.0f;
constexpr float kScrollBarInset = 2.0f;
Rect scrollbarThumbRect(Rect viewRect, float contentH, float viewH,
                        float scrollY)
{
    const float trackY = viewRect.y + 2.0f;
    const float trackH = std::max(0.0f, viewRect.h - 4.0f);
    const float thumbH =
        contentH > 0.0f ? std::max(24.0f, trackH * viewH / contentH) : trackH;
    const float maxScroll = std::max(0.0f, contentH - viewH);
    const float frac = maxScroll > 0.0f ? scrollY / maxScroll : 0.0f;
    const float thumbY = trackY + frac * std::max(0.0f, trackH - thumbH);
    const float x = viewRect.x + viewRect.w - kScrollBarInset - kScrollBarW;
    return {x, thumbY, kScrollBarW, thumbH};
}

void drawScrollbar(draw::Surface& surface, Rect viewRect, float contentH,
                   float viewH, float scrollY)
{
    const Palette& pal = palette();
    const float x = viewRect.x + viewRect.w - kScrollBarInset - kScrollBarW;
    surface.fillRect({x, viewRect.y + 2.0f},
                     {x + kScrollBarW, viewRect.y + viewRect.h - 2.0f},
                     paint::withAlpha(pal.frameBright, 0x22), 3.0f);
    const Rect t = scrollbarThumbRect(viewRect, contentH, viewH, scrollY);
    surface.fillRect({t.x, t.y}, {t.x + t.w, t.y + t.h},
                     paint::withAlpha(pal.frameBright, 0x99), 3.0f);
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
    state.focused = hasState(states, SemanticState::FocusVisible);
    state.active = hasState(states, SemanticState::Pressed);
    state.selected = hasState(states, SemanticState::Checked) ||
                     hasState(states, SemanticState::Selected);

    ImGuiIO& io = ImGui::GetIO();
    const Rect b = offset(node.bounds(), origin);
    state.hovered = !state.disabled && io.MousePos.x >= b.x && io.MousePos.y >= b.y &&
                    io.MousePos.x <= b.x + b.w && io.MousePos.y <= b.y + b.h;
    return state;
}

paint::ControlState controlState(const Node& node, const SemanticNode* sem,
                                 draw::Vec2 origin,
                                 const draw::FrameContext& context)
{
    const SemanticStates states = sem ? sem->states : node.semantics().states;
    paint::ControlState state;
    state.disabled = !node.enabled() || hasState(states, SemanticState::Disabled);
    state.focused = hasState(states, SemanticState::FocusVisible);
    state.active = hasState(states, SemanticState::Pressed);
    state.selected = hasState(states, SemanticState::Checked) ||
                     hasState(states, SemanticState::Selected);

    const Rect b = offset(node.bounds(), origin);
    state.hovered = !state.disabled && context.pointerValid &&
                    context.pointer.x >= b.x && context.pointer.y >= b.y &&
                    context.pointer.x <= b.x + b.w &&
                    context.pointer.y <= b.y + b.h;
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

draw::FontRef iconFontRef(IconFont font, const draw::FrameContext& context)
{
    switch (font) {
    case IconFont::Lucide:
        return context.iconFontLucide.handle ? context.iconFontLucide : context.font;
    case IconFont::Text:
    case IconFont::Material:
    default:
        return context.font;
    }
}

draw::FrameContext imGuiFrameContext()
{
    draw::FrameContext context;
    context.font = draw::fontRef(ImGui::GetFont());
    context.iconFontLucide = draw::fontRef(iconFontLucide());
    context.fontSizePx = ImGui::GetFontSize();
    context.timeSeconds = ImGui::GetTime();
    const ImGuiIO& io = ImGui::GetIO();
    context.pointer = {io.MousePos.x, io.MousePos.y};
    context.pointerValid = true;
    return context;
}

draw::FrameContext normalizedFrameContext(draw::FrameContext context)
{
    if (context.fontSizePx <= 0.0f)
        context.fontSizePx = 13.0f;
    return context;
}

bool focusableMenuChild(const Node* node)
{
    return node && node->visible() && node->enabled() && node->focusable() &&
           (node->role() == Role::ListItem || node->role() == Role::MenuItem);
}

int firstFocusableMenuChild(const Node& menu)
{
    for (std::size_t i = 0; i < menu.childCount(); ++i)
        if (focusableMenuChild(menu.child(i)))
            return (int)i;
    return -1;
}

int checkedMenuChild(const Node& menu)
{
    for (std::size_t i = 0; i < menu.childCount(); ++i) {
        const Node* child = menu.child(i);
        if (focusableMenuChild(child) &&
            hasState(child->semantics().states, SemanticState::Checked))
            return (int)i;
    }
    return -1;
}

int nextFocusableMenuChild(const Node& menu, int current, int direction)
{
    if (menu.childCount() == 0)
        return -1;
    if (current < 0 || current >= (int)menu.childCount())
        current = direction < 0 ? 0 : -1;
    for (std::size_t step = 0; step < menu.childCount(); ++step) {
        current += direction;
        if (current < 0)
            current = (int)menu.childCount() - 1;
        else if (current >= (int)menu.childCount())
            current = 0;
        if (focusableMenuChild(menu.child((std::size_t)current)))
            return current;
    }
    return -1;
}

int focusedMenuChild(const Node& menu, const Node* focused)
{
    if (!focused)
        return -1;
    for (std::size_t i = 0; i < menu.childCount(); ++i) {
        const Node* child = menu.child(i);
        if (child && child->id() == focused->id() && focusableMenuChild(child))
            return (int)i;
    }
    return -1;
}

std::string lowercaseAscii(std::string text)
{
    for (char& ch : text)
        ch = (char)std::tolower((unsigned char)ch);
    return text;
}

bool startsWithAscii(const std::string& text, const std::string& prefix)
{
    return text.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), text.begin());
}

int typeaheadMenuChild(const Node& menu, int current, const std::string& query)
{
    if (query.empty() || menu.childCount() == 0)
        return -1;
    const std::string q = lowercaseAscii(query);
    if (current < 0 || current >= (int)menu.childCount())
        current = -1;
    for (std::size_t step = 0; step < menu.childCount(); ++step) {
        current += 1;
        if (current >= (int)menu.childCount())
            current = 0;
        const Node* child = menu.child((std::size_t)current);
        if (!focusableMenuChild(child))
            continue;
        if (startsWithAscii(lowercaseAscii(child->semantics().name), q))
            return current;
    }
    return -1;
}

std::string menuPathKey(const MenuItem& item, int index)
{
    if (!item.id.empty())
        return item.id;
    if (!item.label.empty())
        return item.label;
    char suffix[32];
    std::snprintf(suffix, sizeof suffix, "item%d", index);
    return suffix;
}

std::string menuPath(const std::string& parentPath, const MenuItem& item, int index)
{
    const std::string key = menuPathKey(item, index);
    return parentPath.empty() ? key : parentPath + "/" + key;
}

// Open `path` and close every open submenu that is not one of its ancestors
// (hover moving to another parent closes the old flyout, like a real menu).
void openSubmenuExclusive(PopupMenuState* state, const std::string& path);
// Close every open submenu that is not `parentPath` itself or an ancestor of
// it (hovering a leaf closes flyouts deeper than its own menu level).
void closeSubmenusBelow(PopupMenuState* state, const std::string& parentPath);

bool submenuOpen(const PopupMenuState* state, const std::string& path)
{
    if (!state)
        return false;
    return std::find(state->openSubmenuPath.begin(), state->openSubmenuPath.end(),
                     path) != state->openSubmenuPath.end();
}

bool pathIsAncestorOrSelf(const std::string& candidate, const std::string& path)
{
    if (candidate.size() > path.size())
        return false;
    if (path.compare(0, candidate.size(), candidate) != 0)
        return false;
    return candidate.size() == path.size() || path[candidate.size()] == '/';
}

void openSubmenuExclusive(PopupMenuState* state, const std::string& path)
{
    if (!state)
        return;
    auto& paths = state->openSubmenuPath;
    paths.erase(std::remove_if(paths.begin(), paths.end(),
                               [&](const std::string& openPath) {
                                   return !pathIsAncestorOrSelf(openPath, path);
                               }),
                paths.end());
    if (std::find(paths.begin(), paths.end(), path) == paths.end())
        paths.push_back(path);
}

void closeSubmenusBelow(PopupMenuState* state, const std::string& parentPath)
{
    if (!state)
        return;
    auto& paths = state->openSubmenuPath;
    paths.erase(std::remove_if(paths.begin(), paths.end(),
                               [&](const std::string& openPath) {
                                   return !pathIsAncestorOrSelf(openPath,
                                                                parentPath);
                               }),
                paths.end());
}

void setSubmenuOpen(PopupMenuState* state, const std::string& path, bool open)
{
    if (!state)
        return;
    auto& paths = state->openSubmenuPath;
    auto it = std::find(paths.begin(), paths.end(), path);
    if (open) {
        if (it == paths.end())
            paths.push_back(path);
        return;
    }
    if (it != paths.end())
        paths.erase(it);
    paths.erase(std::remove_if(paths.begin(), paths.end(),
                               [&](const std::string& openPath) {
                                   return openPath.size() > path.size() &&
                                          openPath.compare(0, path.size(), path) == 0 &&
                                          openPath[path.size()] == '/';
                               }),
                paths.end());
}

bool parentSubmenuVisible(const PopupMenuState* state, const std::string& parentPath)
{
    return parentPath.empty() || submenuOpen(state, parentPath);
}

void translateSubtree(Node& node, Vec2 delta)
{
    Rect b = node.bounds();
    b.x += delta.x;
    b.y += delta.y;
    node.setBounds(b);
    for (std::size_t i = 0; i < node.childCount(); ++i)
        if (Node* child = node.child(i))
            translateSubtree(*child, delta);
}

void anchorPopupSubtree(Tree& tree, Node& popup, PopupMenuState& state)
{
    Rect b = popup.bounds();
    float x = state.position.x;
    float y = state.position.y;
    const Vec2 viewport = tree.viewport();
    if (viewport.x > 0.0f && b.w > 0.0f)
        x = std::clamp(x, 0.0f, std::max(0.0f, viewport.x - b.w));
    if (viewport.y > 0.0f && b.h > 0.0f)
        y = std::clamp(y, 0.0f, std::max(0.0f, viewport.y - b.h));
    translateSubtree(popup, {x - b.x, y - b.y});
}

void anchorOverlayPopup(Tree& tree, Node& popup)
{
    if (!popup.overlay() || !popup.parent())
        return;
    const Node* parent = popup.parent();
    Rect anchor = parent->bounds();
    for (std::size_t i = 0; i < parent->childCount(); ++i) {
        const Node* child = parent->child(i);
        if (!child || child == &popup)
            break;
        if (child->visible() && !child->overlay()) {
            anchor = child->bounds();
        }
    }

    Rect b = popup.bounds();
    float x = anchor.x;
    float y = anchor.y + anchor.h + 2.0f;
    const Vec2 viewport = tree.viewport();
    if (viewport.x > 0.0f && b.w > 0.0f)
        x = std::clamp(x, 0.0f, std::max(0.0f, viewport.x - b.w));
    if (viewport.y > 0.0f && b.h > 0.0f)
        y = std::clamp(y, 0.0f, std::max(0.0f, viewport.y - b.h));
    translateSubtree(popup, {x - b.x, y - b.y});
}

void syncPopupFocus(Tree& tree, Node& popup, PopupMenuState& state)
{
    const Node* focused = tree.focused();
    // Focus inside a flyout panel: leave that level's navigation alone.
    if (focused && focused->parent() && focused->parent() != &popup &&
        popup.find(focused->id()) != nullptr)
        return;

    const int focusedIndex = focusedMenuChild(popup, focused);
    const bool highlightValid =
        state.highlightedIndex >= 0 &&
        state.highlightedIndex < (int)popup.childCount() &&
        focusableMenuChild(popup.child((std::size_t)state.highlightedIndex));

    if (focusedIndex >= 0) {
        if (highlightValid && state.highlightedIndex != focusedIndex) {
            // the keyboard moved the highlight; move visible focus with it
            if (Node* child = popup.child((std::size_t)state.highlightedIndex))
                tree.focus(child->id(), true);
        } else if (highlightValid && !focused->focusVisible()) {
            // the invisible anchor was picked by the first key press
            tree.focus(focused->id(), true);
        } else if (!highlightValid && focused->focusVisible()) {
            state.highlightedIndex = focusedIndex;
        }
        return;
    }

    if (highlightValid) {
        if (Node* child = popup.child((std::size_t)state.highlightedIndex))
            tree.focus(child->id(), true);
        return;
    }

    // Menus open with no highlighted row: focus the first row only as an
    // invisible keyboard anchor so Up/Down/Escape route into the menu.
    const int anchor = firstFocusableMenuChild(popup);
    if (anchor >= 0)
        if (Node* child = popup.child((std::size_t)anchor))
            tree.focus(child->id(), false);
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
    case ImGuiKey_Backspace:
        return Key::Backspace;
    case ImGuiKey_Delete:
        return Key::Delete;
    case ImGuiKey_Home:
        return Key::Home;
    case ImGuiKey_End:
        return Key::End;
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
    // ImGui swaps Cmd<->Ctrl on macOS (ConfigMacOSXBehaviors, imgui.cpp
    // AddKeyEvent). SND events carry the PHYSICAL keys on every backend --
    // super is always the Cmd/Win key, matching the raw-GLFW GL path -- so
    // un-swap here. Without this, cmd+wheel scrolled and CTRL zoomed.
    event.ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
    event.alt = io.KeyAlt;
    event.super = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeySuper;
}

void appendUtf8(std::string& out, uint32_t codepoint)
{
    if (codepoint <= 0x7Fu) {
        out.push_back((char)codepoint);
    } else if (codepoint <= 0x7FFu) {
        out.push_back((char)(0xC0u | (codepoint >> 6)));
        out.push_back((char)(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0xFFFFu) {
        if (codepoint >= 0xD800u && codepoint <= 0xDFFFu)
            return;
        out.push_back((char)(0xE0u | (codepoint >> 12)));
        out.push_back((char)(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back((char)(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0x10FFFFu) {
        out.push_back((char)(0xF0u | (codepoint >> 18)));
        out.push_back((char)(0x80u | ((codepoint >> 12) & 0x3Fu)));
        out.push_back((char)(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back((char)(0x80u | (codepoint & 0x3Fu)));
    }
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

void drawText(draw::Surface& surface, draw::FontRef font, float baseFontSize,
              Rect bounds, const std::string& text, float scale, ImU32 color,
              Align align = Align::Center)
{
    if (text.empty() || baseFontSize <= 0.0f)
        return;
    const float fs = baseFontSize * scale;
    draw::Vec2 ts = surface.measureText(font, fs, text.c_str());
    float x = bounds.x;
    if (align == Align::Center)
        x += std::max(0.0f, bounds.w - ts.x) * 0.5f;
    else if (align == Align::End)
        x += std::max(0.0f, bounds.w - ts.x);
    const float y = bounds.y + std::max(0.0f, bounds.h - ts.y) * 0.5f;
    surface.text(font, fs, {x, y}, color, text.c_str());
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

bool setSelectedState(Node& node, bool selected)
{
    const Semantics& current = static_cast<const Node&>(node).semantics();
    const bool wasSelected = hasState(current.states, SemanticState::Selected);
    if (wasSelected == selected)
        return false;

    Semantics next = current;
    if (selected)
        next.states |= SemanticState::Selected;
    else
        next.states &= ~stateMask(SemanticState::Selected);
    node.setSemantics(next);
    return true;
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

        if (event.shift) {
            // fine adjust: relative drag at 10% rate instead of jump-to-position
            if (event.type == EventType::MouseDown)
                return true; // claim the press without jumping the cap
            const ValueBinding* binding = n.valueBinding();
            if (!binding || !binding->get)
                return false;
            const double range = binding->max - binding->min;
            return setBindingValue(n, binding->get() -
                                          (double)event.delta.y * range * 0.1 /
                                              bounds.h);
        }

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

        const double fine = event.shift ? 0.1 : 1.0; // Shift = 10x slower
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

using ValueFieldBuffer = std::array<char, 64>;

struct ValueFieldEdit {
    ValueFieldBuffer buffer{};
    std::size_t cursor = 0;
    bool selectAll = false;
};

std::unordered_map<NodeId, ValueFieldEdit>& valueFieldEdits()
{
    static std::unordered_map<NodeId, ValueFieldEdit> edits;
    return edits;
}

ValueFieldBuffer& valueFieldBuffer(const NodeId& id)
{
    return valueFieldEdits()[id].buffer;
}

std::unordered_set<NodeId>& activeValueFields()
{
    static std::unordered_set<NodeId> active;
    return active;
}

std::unordered_set<NodeId>& focusValueFields()
{
    static std::unordered_set<NodeId> focus;
    return focus;
}

void beginValueFieldEdit(Node& node)
{
    ValueFieldEdit& edit = valueFieldEdits()[node.id()];
    edit.buffer.fill(0);
    std::string text = nodeValueText(node, nullptr);
    if (text.empty()) {
        if (const ValueBinding* binding = node.valueBinding())
            text = formatBindingValue(*binding, readBinding(*binding));
    }
    std::snprintf(edit.buffer.data(), edit.buffer.size(), "%s", text.c_str());
    edit.cursor = std::strlen(edit.buffer.data());
    edit.selectAll = true;
    activeValueFields().insert(node.id());
    focusValueFields().insert(node.id());
    node.semantics().value.text = edit.buffer.data();
}

void cancelValueFieldEdit(const NodeId& id)
{
    activeValueFields().erase(id);
    focusValueFields().erase(id);
}

bool commitValueFieldEdit(const NodeId& id, const ValueBinding& binding)
{
    auto it = valueFieldEdits().find(id);
    if (it == valueFieldEdits().end())
        return false;
    char* end = nullptr;
    const double parsed = std::strtod(it->second.buffer.data(), &end);
    activeValueFields().erase(id);
    focusValueFields().erase(id);
    if (end == it->second.buffer.data() || !binding.set)
        return false;
    return writeBinding(binding, parsed);
}

bool commitValueFieldEdit(Node& node)
{
    const ValueBinding* binding = node.valueBinding();
    if (!binding)
        return false;
    const bool changed = commitValueFieldEdit(node.id(), *binding);
    setBindingSemantics(node.semantics(), *binding);
    return changed;
}

bool valueFieldEditing(const NodeId& id)
{
    return activeValueFields().find(id) != activeValueFields().end();
}

void writeValueFieldText(const NodeId& id, const std::string& text,
                         std::size_t cursor, bool selectAll = false)
{
    ValueFieldEdit& edit = valueFieldEdits()[id];
    edit.buffer.fill(0);
    std::snprintf(edit.buffer.data(), edit.buffer.size(), "%s", text.c_str());
    edit.cursor = std::min(cursor, std::strlen(edit.buffer.data()));
    edit.selectAll = selectAll;
}

bool insertValueFieldText(Node& node, const std::string& inserted)
{
    if (inserted.empty())
        return false;
    ValueFieldEdit& edit = valueFieldEdits()[node.id()];
    std::string text = edit.buffer.data();
    if (edit.selectAll) {
        text.clear();
        edit.cursor = 0;
        edit.selectAll = false;
    }
    edit.cursor = std::min(edit.cursor, text.size());
    const std::size_t capacity = edit.buffer.size() - 1;
    const std::size_t available = capacity > text.size() ? capacity - text.size() : 0;
    if (available == 0)
        return true;
    const std::string clipped = inserted.substr(0, available);
    text.insert(edit.cursor, clipped);
    writeValueFieldText(node.id(), text, edit.cursor + clipped.size());
    node.semantics().value.text = valueFieldBuffer(node.id()).data();
    return true;
}

bool eraseValueFieldSelectionOrChar(Node& node, bool beforeCursor)
{
    ValueFieldEdit& edit = valueFieldEdits()[node.id()];
    std::string text = edit.buffer.data();
    if (edit.selectAll) {
        text.clear();
        writeValueFieldText(node.id(), text, 0);
        node.semantics().value.text = valueFieldBuffer(node.id()).data();
        return true;
    }
    edit.cursor = std::min(edit.cursor, text.size());
    if (beforeCursor) {
        if (edit.cursor == 0)
            return true;
        text.erase(edit.cursor - 1, 1);
        writeValueFieldText(node.id(), text, edit.cursor - 1);
    } else {
        if (edit.cursor >= text.size())
            return true;
        text.erase(edit.cursor, 1);
        writeValueFieldText(node.id(), text, edit.cursor);
    }
    node.semantics().value.text = valueFieldBuffer(node.id()).data();
    return true;
}

bool moveValueFieldCursor(Node& node, int direction)
{
    ValueFieldEdit& edit = valueFieldEdits()[node.id()];
    const std::size_t len = std::strlen(edit.buffer.data());
    edit.selectAll = false;
    if (direction < 0)
        edit.cursor = edit.cursor > 0 ? edit.cursor - 1 : 0;
    else if (direction > 0)
        edit.cursor = std::min(len, edit.cursor + 1);
    return true;
}

void installValueFieldPointerBehavior(Node& node, double dragSpeed)
{
    node.setOnEvent([dragSpeed](Node& n, const Event& event) {
        if (event.type == EventType::MouseDown) {
            if (event.button != MouseButton::Left)
                return false;
            if (event.clickCount >= 2) {
                beginValueFieldEdit(n);
                return true;
            }
            return true;
        }

        if (event.type == EventType::MouseUp) {
            if (event.button != MouseButton::Left)
                return false;
            return true;
        }

        if (event.type == EventType::KeyDown) {
            if (valueFieldEditing(n.id())) {
                if (!event.text.empty())
                    return insertValueFieldText(n, event.text);
                switch (event.key) {
                case Key::Escape:
                    cancelValueFieldEdit(n.id());
                    return true;
                case Key::Enter:
                    commitValueFieldEdit(n);
                    return true;
                case Key::Left:
                    return moveValueFieldCursor(n, -1);
                case Key::Right:
                    return moveValueFieldCursor(n, 1);
                case Key::Backspace:
                    return eraseValueFieldSelectionOrChar(n, true);
                case Key::Delete:
                    return eraseValueFieldSelectionOrChar(n, false);
                case Key::Home:
                    valueFieldEdits()[n.id()].cursor = 0;
                    valueFieldEdits()[n.id()].selectAll = false;
                    return true;
                case Key::End: {
                    ValueFieldEdit& edit = valueFieldEdits()[n.id()];
                    edit.cursor = std::strlen(edit.buffer.data());
                    edit.selectAll = false;
                    return true;
                }
                default:
                    return true;
                }
            }
            if (event.key == Key::Enter || event.key == Key::Space) {
                beginValueFieldEdit(n);
                return true;
            }
        }

        if (event.type == EventType::TextInput && valueFieldEditing(n.id()))
            return insertValueFieldText(n, event.text);

        if (event.type != EventType::MouseMove || !n.pressed() ||
            valueFieldEditing(n.id()))
            return false;

        const ValueBinding* binding = n.valueBinding();
        if (!binding || !binding->get)
            return false;

        const float dx = event.delta.x;
        if (dx == 0.0f)
            return false;

        const double range = std::abs(binding->max - binding->min);
        double speed = dragSpeed > 0.0 ? dragSpeed
                     : binding->step > 0.0 ? binding->step
                     : range > 0.0 ? range / 100.0
                                   : 0.01;
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

namespace {
void collectNodeIds(const Node& node, std::unordered_set<NodeId>& out)
{
    out.insert(node.id());
    for (const auto& child : node.children())
        if (child)
            collectNodeIds(*child, out);
}
} // namespace

void PaintRenderer::gcStyles(const Tree& tree) const
{
    // Sweep on a cadence: dead entries only appear when nodes are destroyed
    // (screen switch, menu/popup teardown), which is rare relative to frames,
    // so a periodic pass keeps this off the hot path. Tree::find is an O(nodes)
    // walk, so collect every live id in one traversal and prune in one pass
    // rather than find() per entry.
    if ((++styleGcTick_ % 64u) != 0 || styles_.empty())
        return;
    std::unordered_set<NodeId> live;
    live.reserve(styles_.size() * 2);
    collectNodeIds(tree.root(), live);
    for (auto it = styles_.begin(); it != styles_.end();)
        it = live.count(it->first) ? std::next(it) : styles_.erase(it);
}

const VisualStyle* PaintRenderer::styleFor(const NodeId& id) const
{
    auto it = styles_.find(id);
    return it == styles_.end() ? nullptr : &it->second;
}

namespace {

// Place every open flyout panel ("<rowId>.sub") beside its parent row:
// right edge - 2, top-aligned with the row (menu padding compensated),
// flipped to the left when it would leave the viewport. Parent panels are
// anchored before their nested panels (DFS), so chains land correctly.
void anchorOpenSubmenuPanels(Tree& tree, Node& panel)
{
    for (std::size_t i = 0; i < panel.childCount(); ++i) {
        Node* sub = panel.child(i);
        if (!sub || !sub->visible())
            continue;
        const NodeId& id = sub->id();
        constexpr const char* kSuffix = ".sub";
        if (id.size() > 4 && id.compare(id.size() - 4, 4, kSuffix) == 0) {
            if (const Node* row = tree.find(id.substr(0, id.size() - 4))) {
                const Rect rb = row->bounds();
                const Rect sb = sub->bounds();
                const Vec2 viewport = tree.viewport();
                float x = rb.x + rb.w - 2.0f;
                if (viewport.x > 0.0f && x + sb.w > viewport.x)
                    x = std::max(0.0f, rb.x - sb.w + 2.0f); // flip left
                float y = rb.y - 4.0f; // row sits 4px inside the panel pad
                if (viewport.y > 0.0f && sb.h > 0.0f)
                    y = std::clamp(y, 0.0f,
                                   std::max(0.0f, viewport.y - sb.h));
                translateSubtree(*sub, {x - sb.x, y - sb.y});
            }
            anchorOpenSubmenuPanels(tree, *sub);
        }
    }
}

} // namespace

void PaintRenderer::prepareOpenPopups(Tree& tree) const
{
    for (const auto& entry : styles_) {
        PopupMenuState* state = entry.second.popupState;
        if (!state || !state->open)
            continue;
        Node* node = tree.find(entry.first);
        if (!node || !node->visible())
            continue;
        if (state->anchorToPosition)
            anchorPopupSubtree(tree, *node, *state);
        else
            anchorOverlayPopup(tree, *node);
        anchorOpenSubmenuPanels(tree, *node);
        syncPopupFocus(tree, *node, *state);
    }
}

bool PaintRenderer::dismissOpenPopupsOutside(Tree& tree, const ImVec2& origin,
                                             const ImVec2& screenPoint) const
{
    bool hasOpenPopup = false;
    bool insideOpenPopup = false;
    for (const auto& entry : styles_) {
        PopupMenuState* state = entry.second.popupState;
        if (!state || !state->open || !state->closeOnOutsideClick)
            continue;
        const Node* node = tree.find(entry.first);
        if (!node || !node->visible())
            continue;
        hasOpenPopup = true;
        const Vec2 local{screenPoint.x - origin.x, screenPoint.y - origin.y};
        if (node->hitTest(local) != nullptr) { // covers flyout panels too
            insideOpenPopup = true;
            break;
        }
    }

    if (!hasOpenPopup || insideOpenPopup)
        return false;

    bool dismissed = false;
    for (const auto& entry : styles_) {
        PopupMenuState* state = entry.second.popupState;
        if (!state || !state->open || !state->closeOnOutsideClick)
            continue;
        state->open = false;
        state->highlightedIndex = -1;
        if (Node* node = tree.find(entry.first))
            node->setVisible(false);
        dismissed = true;
    }
    return dismissed;
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
    case Role::MenuItem:
        style.kind = VisualKind::ListItem;
        break;
    case Role::ComboBox:
        style.kind = VisualKind::OutlineButton;
        break;
    case Role::Menu:
    case Role::Dialog:
    case Role::Alert:
        style.kind = VisualKind::Panel;
        style.panelFill = true;
        style.panelBorder = true;
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
    gcStyles(tree);
    SemanticMap semMap;
    for (const SemanticNode& node : tree.semanticSnapshot())
        semMap[node.id] = node;
    ImDrawList* dl = drawList ? drawList : ImGui::GetWindowDrawList();
    const auto context = imGuiFrameContext();
    std::vector<const Node*> overlays;
    renderNode(tree.root(), &semMap, origin, dl, context, &overlays);
    for (std::size_t i = 0; i < overlays.size(); ++i) // grows as nested defer
        renderNode(*overlays[i], &semMap, origin, dl, context, &overlays);
    renderTooltip(tree, semMap, origin, dl, context);
}

void PaintRenderer::render(const Node& root, ImDrawList* drawList) const
{
    render(root, ImVec2(0.0f, 0.0f), drawList);
}

void PaintRenderer::render(const Node& root, const ImVec2& origin,
                           ImDrawList* drawList) const
{
    ImDrawList* dl = drawList ? drawList : ImGui::GetWindowDrawList();
    const auto context = imGuiFrameContext();
    std::vector<const Node*> overlays;
    renderNode(root, nullptr, origin, dl, context, &overlays);
    for (std::size_t i = 0; i < overlays.size(); ++i)
        renderNode(*overlays[i], nullptr, origin, dl, context, &overlays);
    renderTooltip(root, nullptr, origin, dl, context);
}

void PaintRenderer::render(const Tree& tree, draw::Surface& surface,
                           const draw::FrameContext& context,
                           draw::Vec2 origin) const
{
    gcStyles(tree);
    SemanticMap semMap;
    for (const SemanticNode& node : tree.semanticSnapshot())
        semMap[node.id] = node;
    const auto ctx = normalizedFrameContext(context);
    std::vector<const Node*> overlays;
    renderNode(tree.root(), &semMap, origin, surface, ctx, &overlays);
    for (std::size_t i = 0; i < overlays.size(); ++i)
        renderNode(*overlays[i], &semMap, origin, surface, ctx, &overlays);
    renderTooltip(tree, semMap, origin, surface, ctx);
}

void PaintRenderer::render(const Node& root, draw::Surface& surface,
                           const draw::FrameContext& context,
                           draw::Vec2 origin) const
{
    const auto ctx = normalizedFrameContext(context);
    std::vector<const Node*> overlays;
    renderNode(root, nullptr, origin, surface, ctx, &overlays);
    for (std::size_t i = 0; i < overlays.size(); ++i)
        renderNode(*overlays[i], nullptr, origin, surface, ctx, &overlays);
    renderTooltip(root, nullptr, origin, surface, ctx);
}

void PaintRenderer::renderMain(const Tree& tree, const ImVec2& origin,
                               ImDrawList* drawList) const
{
    SemanticMap semMap;
    for (const SemanticNode& node : tree.semanticSnapshot())
        semMap[node.id] = node;
    std::vector<const Node*> deferred; // discarded: renderOverlays re-collects
    renderNode(tree.root(), &semMap, origin,
               drawList ? drawList : ImGui::GetWindowDrawList(),
               imGuiFrameContext(), &deferred);
}

void PaintRenderer::renderOverlays(const Tree& tree, const ImVec2& origin,
                                   ImDrawList* drawList) const
{
    SemanticMap semMap;
    for (const SemanticNode& node : tree.semanticSnapshot())
        semMap[node.id] = node;
    ImDrawList* dl = drawList ? drawList : ImGui::GetWindowDrawList();
    const auto context = imGuiFrameContext();
    std::vector<const Node*> overlays;
    collectOverlays(tree.root(), overlays);
    for (std::size_t i = 0; i < overlays.size(); ++i)
        renderNode(*overlays[i], &semMap, origin, dl, context, &overlays);
    renderTooltip(tree, semMap, origin, dl, context);
}

void PaintRenderer::renderMain(const Tree& tree, draw::Surface& surface,
                               const draw::FrameContext& context,
                               draw::Vec2 origin) const
{
    SemanticMap semMap;
    for (const SemanticNode& node : tree.semanticSnapshot())
        semMap[node.id] = node;
    std::vector<const Node*> deferred;
    renderNode(tree.root(), &semMap, origin, surface,
               normalizedFrameContext(context), &deferred);
}

void PaintRenderer::renderOverlays(const Tree& tree, draw::Surface& surface,
                                   const draw::FrameContext& context,
                                   draw::Vec2 origin) const
{
    SemanticMap semMap;
    for (const SemanticNode& node : tree.semanticSnapshot())
        semMap[node.id] = node;
    const auto ctx = normalizedFrameContext(context);
    std::vector<const Node*> overlays;
    collectOverlays(tree.root(), overlays);
    for (std::size_t i = 0; i < overlays.size(); ++i)
        renderNode(*overlays[i], &semMap, origin, surface, ctx, &overlays);
    renderTooltip(tree, semMap, origin, surface, ctx);
}

void PaintRenderer::renderTooltip(const Tree& tree, const SemanticMap& semantics,
                                  const ImVec2& origin, ImDrawList* drawList,
                                  const draw::FrameContext& context) const
{
    const Node* hovered = tree.hovered();
    if (!hovered && context.pointerValid) {
        hovered = tree.hitTest(
            {context.pointer.x - origin.x, context.pointer.y - origin.y});
    }
    const Node* node = tooltipNodeForHover(hovered, &semantics);
    const std::string* text = node ? tooltipForNode(*node, &semantics) : nullptr;
    if (!text) {
        tooltipDelayReady(tooltipKey_, tooltipStart_, {}, context.timeSeconds);
        return;
    }

    const std::string key = tooltipKeyForNode(*node, *text);
    if (!tooltipDelayReady(tooltipKey_, tooltipStart_, key, context.timeSeconds))
        return;

    const draw::Vec2 drawOrigin{origin.x, origin.y};
    const draw::Vec2 anchor = tooltipAnchor(*node, drawOrigin, context);
    const draw::Vec2 clipMax = tooltipClipMax(tree.root(), drawOrigin);
    paint::drawTooltip(drawList, draw::imFont(context.font),
                       context.fontSizePx > 0.0f ? context.fontSizePx : 13.0f,
                       ImVec2(anchor.x, anchor.y), text->c_str(), palette(),
                       ImVec2(clipMax.x, clipMax.y));
}

void PaintRenderer::renderTooltip(const Node& root, const SemanticMap* semantics,
                                  const ImVec2& origin, ImDrawList* drawList,
                                  const draw::FrameContext& context) const
{
    const Node* hovered = nullptr;
    if (context.pointerValid) {
        hovered = root.hitTest(
            {context.pointer.x - origin.x, context.pointer.y - origin.y});
    }
    const Node* node = tooltipNodeForHover(hovered, semantics);
    const std::string* text = node ? tooltipForNode(*node, semantics) : nullptr;
    if (!text) {
        tooltipDelayReady(tooltipKey_, tooltipStart_, {}, context.timeSeconds);
        return;
    }

    const std::string key = tooltipKeyForNode(*node, *text);
    if (!tooltipDelayReady(tooltipKey_, tooltipStart_, key, context.timeSeconds))
        return;

    const draw::Vec2 drawOrigin{origin.x, origin.y};
    const draw::Vec2 anchor = tooltipAnchor(*node, drawOrigin, context);
    const draw::Vec2 clipMax = tooltipClipMax(root, drawOrigin);
    paint::drawTooltip(drawList, draw::imFont(context.font),
                       context.fontSizePx > 0.0f ? context.fontSizePx : 13.0f,
                       ImVec2(anchor.x, anchor.y), text->c_str(), palette(),
                       ImVec2(clipMax.x, clipMax.y));
}

void PaintRenderer::renderTooltip(const Tree& tree, const SemanticMap& semantics,
                                  draw::Vec2 origin, draw::Surface& surface,
                                  const draw::FrameContext& context) const
{
    const Node* hovered = tree.hovered();
    if (!hovered && context.pointerValid) {
        hovered = tree.hitTest(
            {context.pointer.x - origin.x, context.pointer.y - origin.y});
    }
    const Node* node = tooltipNodeForHover(hovered, &semantics);
    const std::string* text = node ? tooltipForNode(*node, &semantics) : nullptr;
    if (!text) {
        tooltipDelayReady(tooltipKey_, tooltipStart_, {}, context.timeSeconds);
        return;
    }

    const std::string key = tooltipKeyForNode(*node, *text);
    if (!tooltipDelayReady(tooltipKey_, tooltipStart_, key, context.timeSeconds))
        return;

    paint::drawTooltip(surface, context.font,
                       context.fontSizePx > 0.0f ? context.fontSizePx : 13.0f,
                       tooltipAnchor(*node, origin, context), text->c_str(),
                       palette(), tooltipClipMax(tree.root(), origin));
}

void PaintRenderer::renderTooltip(const Node& root, const SemanticMap* semantics,
                                  draw::Vec2 origin, draw::Surface& surface,
                                  const draw::FrameContext& context) const
{
    const Node* hovered = nullptr;
    if (context.pointerValid) {
        hovered = root.hitTest(
            {context.pointer.x - origin.x, context.pointer.y - origin.y});
    }
    const Node* node = tooltipNodeForHover(hovered, semantics);
    const std::string* text = node ? tooltipForNode(*node, semantics) : nullptr;
    if (!text) {
        tooltipDelayReady(tooltipKey_, tooltipStart_, {}, context.timeSeconds);
        return;
    }

    const std::string key = tooltipKeyForNode(*node, *text);
    if (!tooltipDelayReady(tooltipKey_, tooltipStart_, key, context.timeSeconds))
        return;

    paint::drawTooltip(surface, context.font,
                       context.fontSizePx > 0.0f ? context.fontSizePx : 13.0f,
                       tooltipAnchor(*node, origin, context), text->c_str(),
                       palette(), tooltipClipMax(root, origin));
}

void PaintRenderer::collectOverlays(const Node& node,
                                    std::vector<const Node*>& out) const
{
    if (!node.visible())
        return;
    const VisualStyle style = resolvedStyle(node);
    if (style.popupState && !style.popupState->open)
        return;
    for (const auto& child : node.children()) {
        if (!child->visible())
            continue;
        if (child->overlay())
            out.push_back(child.get());
        else
            collectOverlays(*child, out);
    }
}

void PaintRenderer::renderNode(const Node& node, const SemanticMap* semantics,
                               const ImVec2& origin,
                               ImDrawList* drawList,
                               const draw::FrameContext& context,
                               std::vector<const Node*>* overlayQueue) const
{
    if (!node.visible())
        return;

    const auto semIt = semantics ? semantics->find(node.id()) : SemanticMap::const_iterator{};
    const SemanticNode* sem = semantics && semIt != semantics->end() ? &semIt->second : nullptr;
    const VisualStyle style = resolvedStyle(node);
    const Palette& pal = palette();
    const Rect bounds = offset(node.bounds(), origin);
    draw::ImGuiSurface surface(drawList);
    ImFont* font = draw::imFont(context.font);
    const float fontSize = context.fontSizePx;
    paint::ControlState state =
        controlState(node, sem, {origin.x, origin.y}, context);
    if (style.popupState && !style.popupState->open)
        return;

    switch (style.kind) {
    case VisualKind::Panel:
        if (style.panelFill)
            drawList->AddRectFilled(topLeft(bounds), bottomRight(bounds), pal.frame,
                                    style.panelRounding);
        if (style.panelBorder)
            drawList->AddRect(topLeft(bounds), bottomRight(bounds), pal.frameBright,
                              style.panelRounding);
        break;
    case VisualKind::Text:
        drawText(drawList, font, bounds, nodeName(node, sem),
                 style.fontScale > 0.0f ? style.fontScale : 1.0f, pal.text, Align::Start);
        break;
    case VisualKind::SectionHeader:
        paint::drawSectionHeader(drawList, font, topLeft(bounds),
                                 nodeName(node, sem).c_str(),
                                 fontSize *
                                     (style.fontScale > 0.0f ? style.fontScale * 0.80f : 0.80f),
                                 bounds.w, pal);
        break;
    case VisualKind::Badge:
        paint::drawBadge(drawList, font, topLeft(bounds),
                         nodeName(node, sem).c_str(),
                         fontSize *
                             (style.fontScale > 0.0f ? style.fontScale * 0.78f : 0.78f),
                         style.accent, pal);
        break;
    case VisualKind::ListItem:
        paint::drawListItem(drawList, font, topLeft(bounds), sizeOf(bounds),
                            nodeName(node, sem).c_str(), pal, state,
                            style.fontScale > 0.0f ? style.fontScale : 0.90f);
        break;
    case VisualKind::MenuItem: {
        MenuItem item = style.menuItem;
        if (item.label.empty())
            item.label = nodeName(node, sem);
        if (hasState(sem ? sem->states : node.semantics().states,
                     SemanticState::Checked))
            item.checked = true;
        item.enabled = !state.disabled;
        paint::drawMenuItem(drawList, font, iconFont(style.iconFont),
                            topLeft(bounds), sizeOf(bounds), item, pal, state,
                            style.fontScale > 0.0f ? style.fontScale : 0.90f);
        break;
    }
    case VisualKind::ValueRow: {
        const std::string name = nodeName(node, sem);
        const std::string valueText = nodeValueText(node, sem);
        paint::drawValueRow(drawList, font, topLeft(bounds), sizeOf(bounds),
                            name.c_str(), valueText.c_str(), pal, state,
                            style.fontScale > 0.0f ? style.fontScale : 0.90f,
                            node.role() == Role::Slider);
        break;
    }
    case VisualKind::Canvas:
        // square by default (panelRounding), like the rest of the house --
        // the hardcoded 3px here kept putting rounded rings/fills around
        // clickable canvases (the bar readout, value fields)
        if (style.panelFill)
            drawList->AddRectFilled(topLeft(bounds), bottomRight(bounds), pal.frame,
                                    style.panelRounding);
        if (style.canvasDraw) {
            if (style.canvasClip)
                drawList->PushClipRect(topLeft(bounds), bottomRight(bounds), true);
            style.canvasDraw(*drawList, node, bounds, state);
            if (style.canvasClip)
                drawList->PopClipRect();
        } else if (style.canvasSurfaceDraw) {
            if (style.canvasClip)
                surface.pushClip(topLeftDraw(bounds), bottomRightDraw(bounds), true);
            style.canvasSurfaceDraw(surface, node, bounds, state, context);
            if (style.canvasClip)
                surface.popClip();
        }
        if (style.panelBorder)
            drawList->AddRect(topLeft(bounds), bottomRight(bounds), pal.frameBright,
                              style.panelRounding);
        if (style.canvasFocusRing && state.focused && !state.disabled)
            paint::drawFocusRing(drawList, topLeft(bounds), bottomRight(bounds),
                                 pal, style.panelRounding);
        break;
    case VisualKind::Button:
    {
        const std::string name = nodeName(node, sem);
        paint::ButtonPaintArgs args;
        args.drawList = drawList;
        args.font = font;
        args.topLeft = topLeft(bounds);
        args.size = sizeOf(bounds);
        args.text = name.c_str();
        args.face = style.face;
        args.palette = &pal;
        args.state = &state;
        args.fontScale = style.fontScale > 0.0f ? style.fontScale : 0.90f;
        args.fontRef = context.font;
        args.fontSizePx = fontSize * args.fontScale;
        args.surface = &surface;
        paint::drawButtonWithPainter(args, style.buttonPainter);
        break;
    }
    case VisualKind::OutlineButton: {
        const std::string name = nodeName(node, sem);
        paint::drawOutlineButton(drawList, font, topLeft(bounds),
                                 sizeOf(bounds), name.c_str(), pal, state,
                                 style.outlineButtonStyle);
        break;
    }
    case VisualKind::IconButton: {
        const bool down = state.active || state.selected || style.lit ||
                          checked(node, sem);
        paint::drawTactileIconButton(drawList, iconFont(style.iconFont), topLeft(bounds),
                                     sizeOf(bounds), style.glyph.c_str(), pal, state,
                                     down, style.face);
        break;
    }
    case VisualKind::OutlineIconButton: {
        // outline chrome + a centred glyph at 70% height: drawOutlineButton
        // already centres any string, so hand it the glyph in the icon font
        paint::ControlState st = state;
        st.selected = st.selected || style.lit ||
                      checked(node, sem);
        draw::ImGuiSurface s(drawList);
        paint::drawOutlineButton(s, draw::fontRef(iconFont(style.iconFont)),
                                 bounds.h * 0.70f, topLeftDraw(bounds),
                                 sizeOfDraw(bounds), style.glyph.c_str(), pal,
                                 st, style.outlineButtonStyle);
        break;
    }
    case VisualKind::VectorIconButton:
        paint::drawVectorIconButton(drawList, topLeft(bounds), sizeOf(bounds),
                                    style.vectorIcon, style.accent, pal, state,
                                    style.lit);
        break;
    case VisualKind::Segmented: {
        const int count = (int)style.segments.size();
        std::vector<const char*> labels;
        labels.reserve(style.segments.size());
        for (const std::string& s : style.segments)
            labels.push_back(s.c_str());
        const int selected = std::clamp((int)std::lround(valueOf(node, sem)),
                                        0, std::max(0, count - 1));
        int hoverIdx = -1;
        if (count > 0 && (state.hovered || state.active) && bounds.w > 0.0f &&
            context.pointerValid) {
            const float lx = context.pointer.x - bounds.x;
            if (lx >= 0.0f && lx <= bounds.w)
                hoverIdx = std::clamp((int)(lx / (bounds.w / (float)count)),
                                      0, count - 1);
        }
        paint::drawSegmented(drawList, font, topLeft(bounds),
                             sizeOf(bounds), labels.data(), count, selected,
                             hoverIdx, pal, state,
                             style.fontScale > 0.0f ? style.fontScale * 0.90f
                                                    : 0.90f);
        break;
    }
    case VisualKind::TabBar: {
        const int count = (int)style.segments.size();
        std::vector<const char*> labels;
        labels.reserve(style.segments.size());
        for (const std::string& s : style.segments)
            labels.push_back(s.c_str());
        const int selected = std::clamp((int)std::lround(valueOf(node, sem)),
                                        0, std::max(0, count - 1));
        int hoverIdx = -1;
        if (count > 0 && (state.hovered || state.active) && bounds.w > 0.0f &&
            context.pointerValid) {
            const float lx = context.pointer.x - bounds.x;
            if (lx >= 0.0f && lx <= bounds.w)
                hoverIdx = std::clamp((int)(lx / (bounds.w / (float)count)),
                                      0, count - 1);
        }
        paint::drawTabBar(drawList, font, topLeft(bounds),
                          sizeOf(bounds), labels.data(), count, selected,
                          hoverIdx, pal, state,
                          style.fontScale > 0.0f ? style.fontScale * 0.90f
                                                 : 0.90f);
        break;
    }
    case VisualKind::CycleButton: {
        const int count = (int)style.segments.size();
        const int index = std::clamp((int)std::lround(valueOf(node, sem)),
                                     0, std::max(0, count - 1));
        std::string text = nodeValueText(node, sem);
        if (text.empty() && index < count)
            text = style.segments[(size_t)index];
        paint::drawCycleButton(drawList, font, topLeft(bounds),
                               sizeOf(bounds), text.c_str(), index, count, pal,
                               state,
                               style.fontScale > 0.0f ? style.fontScale * 0.90f
                                                      : 0.90f);
        break;
    }
    case VisualKind::LedButton: {
        const bool on = checked(node, sem);
        float level = on ? 1.0f : 0.0f;
        if (on && style.ledBlink) // arm-blink for pending states
            level = 0.35f + 0.65f * (0.5f + 0.5f *
                                     std::sin((float)context.timeSeconds * 6.0f));
        const bool down = state.active || on;
        paint::drawLedButton(drawList, iconFont(style.iconFont), topLeft(bounds),
                             sizeOf(bounds), style.glyph.c_str(), level, pal,
                             state, down, style.accent, style.face);
        break;
    }
    case VisualKind::Toggle:
        paint::drawToggle(drawList, topLeft(bounds), bounds.w, bounds.h,
                          checked(node, sem) ? 1.0f : 0.0f, pal, state);
        if (!nodeName(node, sem).empty()) {
            Rect textBounds = bounds;
            textBounds.x += bounds.w + 6.0f;
            textBounds.w = 160.0f;
            drawText(drawList, font, textBounds, nodeName(node, sem), 1.0f,
                     state.disabled ? pal.textDim : pal.text, Align::Start);
        }
        break;
    case VisualKind::Checkbox: {
        const std::string name = nodeName(node, sem);
        paint::drawCheckbox(drawList, font, topLeft(bounds), sizeOf(bounds),
                            name.c_str(), checked(node, sem), pal, state,
                            style.fontScale > 0.0f ? style.fontScale * 0.90f
                                                   : 0.90f);
        break;
    }
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
        args.surface = &surface;
        if (style.knobMod)
            args.mod = style.knobMod();
        paint::drawKnobWithPainter(args, style.knobPainter);
        const std::string name = nodeName(node, sem);
        if (!name.empty()) {
            Rect labelBounds{bounds.x, bounds.y + d + 2.0f, bounds.w,
                             fontSize + 2.0f};
            drawText(drawList, font, labelBounds, name, 0.90f, pal.text);
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

    const bool scrollClip = node.scroll();
    if (scrollClip)
        drawList->PushClipRect(topLeft(bounds), bottomRight(bounds), true);
    for (const auto& child : node.children()) {
        if (overlayQueue && child->overlay() && child->visible()) {
            overlayQueue->push_back(child.get()); // painted after the main pass
            continue;
        }
        renderNode(*child, semantics, origin, drawList, context, overlayQueue);
    }
    if (scrollClip) {
        drawList->PopClipRect();
        if (node.contentHeight() > node.viewHeight() + 0.5f) {
            draw::ImGuiSurface sbSurface(drawList);
            drawScrollbar(sbSurface, bounds, node.contentHeight(),
                          node.viewHeight(), node.scrollY());
        }
    }
}

void PaintRenderer::renderNode(const Node& node, const SemanticMap* semantics,
                               draw::Vec2 origin, draw::Surface& surface,
                               const draw::FrameContext& context,
                               std::vector<const Node*>* overlayQueue) const
{
    if (!node.visible())
        return;

    const auto semIt = semantics ? semantics->find(node.id()) : SemanticMap::const_iterator{};
    const SemanticNode* sem = semantics && semIt != semantics->end() ? &semIt->second : nullptr;
    const VisualStyle style = resolvedStyle(node);
    const Palette& pal = palette();
    const Rect bounds = offset(node.bounds(), origin);
    paint::ControlState state = controlState(node, sem, origin, context);
    if (style.popupState && !style.popupState->open)
        return;

    const draw::FontRef font = context.font;
    const float fontSize = context.fontSizePx;

    switch (style.kind) {
    case VisualKind::Panel:
        if (style.panelFill)
            surface.fillRect(topLeftDraw(bounds), bottomRightDraw(bounds), pal.frame,
                             style.panelRounding);
        if (style.panelBorder)
            surface.strokeRect(topLeftDraw(bounds), bottomRightDraw(bounds),
                               pal.frameBright, style.panelRounding);
        break;
    case VisualKind::Text:
        drawText(surface, font, fontSize, bounds, nodeName(node, sem),
                 style.fontScale > 0.0f ? style.fontScale : 1.0f, pal.text,
                 Align::Start);
        break;
    case VisualKind::SectionHeader:
        paint::drawSectionHeader(surface, font, topLeftDraw(bounds),
                                 nodeName(node, sem).c_str(),
                                 fontSize * (style.fontScale > 0.0f
                                                 ? style.fontScale * 0.80f
                                                 : 0.80f),
                                 bounds.w, pal);
        break;
    case VisualKind::Badge:
        paint::drawBadge(surface, font, topLeftDraw(bounds),
                         nodeName(node, sem).c_str(),
                         fontSize * (style.fontScale > 0.0f
                                         ? style.fontScale * 0.78f
                                         : 0.78f),
                         style.accent, pal);
        break;
    case VisualKind::ListItem:
        paint::drawListItem(surface, font,
                            fontSize * (style.fontScale > 0.0f ? style.fontScale : 0.90f),
                            topLeftDraw(bounds), sizeOfDraw(bounds),
                            nodeName(node, sem).c_str(), pal, state);
        break;
    case VisualKind::MenuItem: {
        MenuItem item = style.menuItem;
        if (item.label.empty())
            item.label = nodeName(node, sem);
        if (hasState(sem ? sem->states : node.semantics().states,
                     SemanticState::Checked))
            item.checked = true;
        item.enabled = !state.disabled;
        paint::drawMenuItem(surface, font, iconFontRef(style.iconFont, context),
                            fontSize * (style.fontScale > 0.0f ? style.fontScale : 0.90f),
                            topLeftDraw(bounds), sizeOfDraw(bounds), item, pal, state);
        break;
    }
    case VisualKind::ValueRow: {
        const std::string name = nodeName(node, sem);
        const std::string valueText = nodeValueText(node, sem);
        paint::drawValueRow(surface, font,
                            fontSize * (style.fontScale > 0.0f ? style.fontScale : 0.90f),
                            topLeftDraw(bounds), sizeOfDraw(bounds),
                            name.c_str(), valueText.c_str(), pal, state,
                            node.role() == Role::Slider);
        break;
    }
    case VisualKind::Canvas:
        if (style.panelFill)
            surface.fillRect(topLeftDraw(bounds), bottomRightDraw(bounds), pal.frame,
                             style.panelRounding);
        if (style.canvasSurfaceDraw) {
            if (style.canvasClip)
                surface.pushClip(topLeftDraw(bounds), bottomRightDraw(bounds), true);
            style.canvasSurfaceDraw(surface, node, bounds, state, context);
            if (style.canvasClip)
                surface.popClip();
        }
        if (style.panelBorder)
            surface.strokeRect(topLeftDraw(bounds), bottomRightDraw(bounds),
                               pal.frameBright,
                               style.panelRounding);
        if (style.canvasFocusRing && state.focused && !state.disabled)
            paint::drawFocusRing(surface, topLeftDraw(bounds), bottomRightDraw(bounds),
                                 pal, style.panelRounding);
        break;
    case VisualKind::Button: {
        const std::string name = nodeName(node, sem);
        if (style.buttonPainter) {
            paint::ButtonPaintArgs args;
            args.topLeft = topLeft(bounds);
            args.size = sizeOf(bounds);
            args.text = name.c_str();
            args.face = style.face;
            args.palette = &pal;
            args.state = &state;
            args.fontScale = style.fontScale > 0.0f ? style.fontScale : 0.90f;
            args.surface = &surface;
            args.fontRef = font;
            args.fontSizePx = fontSize * args.fontScale;
            paint::drawButtonWithPainter(args, style.buttonPainter);
        } else {
            paint::drawButton(surface, font,
                              fontSize * (style.fontScale > 0.0f
                                              ? style.fontScale
                                              : 0.90f),
                              topLeftDraw(bounds), sizeOfDraw(bounds), name.c_str(),
                              pal, state);
        }
        break;
    }
    case VisualKind::OutlineButton: {
        const std::string name = nodeName(node, sem);
        const float scale = style.outlineButtonStyle.fontScale > 0.0f
                                ? style.outlineButtonStyle.fontScale
                                : 0.90f;
        paint::drawOutlineButton(surface, font, fontSize * scale,
                                 topLeftDraw(bounds), sizeOfDraw(bounds),
                                 name.c_str(), pal, state,
                                 style.outlineButtonStyle);
        break;
    }
    case VisualKind::IconButton: {
        const bool down = state.active || state.selected || style.lit ||
                          checked(node, sem);
        paint::drawTactileIconButton(surface, iconFontRef(style.iconFont, context),
                                     topLeftDraw(bounds), sizeOfDraw(bounds),
                                     style.glyph.c_str(), pal, state, down,
                                     style.face);
        break;
    }
    case VisualKind::OutlineIconButton: {
        paint::ControlState st = state;
        st.selected = st.selected || style.lit ||
                      checked(node, sem);
        paint::drawOutlineButton(surface, iconFontRef(style.iconFont, context),
                                 bounds.h * 0.70f, topLeftDraw(bounds),
                                 sizeOfDraw(bounds), style.glyph.c_str(), pal,
                                 st, style.outlineButtonStyle);
        break;
    }
    case VisualKind::VectorIconButton:
        paint::drawVectorIconButton(surface, topLeftDraw(bounds), sizeOfDraw(bounds),
                                    style.vectorIcon, style.accent, pal, state,
                                    style.lit);
        break;
    case VisualKind::Segmented: {
        const int count = (int)style.segments.size();
        std::vector<const char*> labels;
        labels.reserve(style.segments.size());
        for (const std::string& s : style.segments)
            labels.push_back(s.c_str());
        const int selected = std::clamp((int)std::lround(valueOf(node, sem)),
                                        0, std::max(0, count - 1));
        int hoverIdx = -1;
        if (count > 0 && (state.hovered || state.active) && bounds.w > 0.0f &&
            context.pointerValid) {
            const float lx = context.pointer.x - bounds.x;
            if (lx >= 0.0f && lx <= bounds.w)
                hoverIdx = std::clamp((int)(lx / (bounds.w / (float)count)),
                                      0, count - 1);
        }
        paint::drawSegmented(surface, font,
                             fontSize * (style.fontScale > 0.0f
                                             ? style.fontScale * 0.90f
                                             : 0.90f),
                             topLeftDraw(bounds), sizeOfDraw(bounds), labels.data(),
                             count, selected, hoverIdx, pal, state);
        break;
    }
    case VisualKind::TabBar: {
        const int count = (int)style.segments.size();
        std::vector<const char*> labels;
        labels.reserve(style.segments.size());
        for (const std::string& s : style.segments)
            labels.push_back(s.c_str());
        const int selected = std::clamp((int)std::lround(valueOf(node, sem)),
                                        0, std::max(0, count - 1));
        int hoverIdx = -1;
        if (count > 0 && (state.hovered || state.active) && bounds.w > 0.0f &&
            context.pointerValid) {
            const float lx = context.pointer.x - bounds.x;
            if (lx >= 0.0f && lx <= bounds.w)
                hoverIdx = std::clamp((int)(lx / (bounds.w / (float)count)),
                                      0, count - 1);
        }
        paint::drawTabBar(surface, font,
                          fontSize * (style.fontScale > 0.0f
                                          ? style.fontScale * 0.90f
                                          : 0.90f),
                          topLeftDraw(bounds), sizeOfDraw(bounds), labels.data(),
                          count, selected, hoverIdx, pal, state);
        break;
    }
    case VisualKind::CycleButton: {
        const int count = (int)style.segments.size();
        const int index = std::clamp((int)std::lround(valueOf(node, sem)),
                                     0, std::max(0, count - 1));
        std::string text = nodeValueText(node, sem);
        if (text.empty() && index < count)
            text = style.segments[(size_t)index];
        paint::drawCycleButton(surface, font,
                               fontSize * (style.fontScale > 0.0f
                                               ? style.fontScale * 0.90f
                                               : 0.90f),
                               topLeftDraw(bounds), sizeOfDraw(bounds),
                               text.c_str(), index, count, pal, state);
        break;
    }
    case VisualKind::LedButton: {
        const bool on = checked(node, sem);
        float level = on ? 1.0f : 0.0f;
        if (on && style.ledBlink)
            level = 0.35f + 0.65f *
                                (0.5f + 0.5f * std::sin((float)context.timeSeconds * 6.0f));
        const bool down = state.active || on;
        paint::drawLedButton(surface, iconFontRef(style.iconFont, context),
                             topLeftDraw(bounds), sizeOfDraw(bounds),
                             style.glyph.c_str(), level, pal, state, down,
                             style.accent, style.face);
        break;
    }
    case VisualKind::Toggle:
        paint::drawToggle(surface, topLeftDraw(bounds), bounds.w, bounds.h,
                          checked(node, sem) ? 1.0f : 0.0f, pal, state);
        if (!nodeName(node, sem).empty()) {
            Rect textBounds = bounds;
            textBounds.x += bounds.w + 6.0f;
            textBounds.w = 160.0f;
            drawText(surface, font, fontSize, textBounds, nodeName(node, sem), 1.0f,
                     state.disabled ? pal.textDim : pal.text, Align::Start);
        }
        break;
    case VisualKind::Checkbox: {
        const std::string name = nodeName(node, sem);
        paint::drawCheckbox(surface, font,
                            fontSize * (style.fontScale > 0.0f
                                            ? style.fontScale * 0.90f
                                            : 0.90f),
                            topLeftDraw(bounds), sizeOfDraw(bounds),
                            name.c_str(), checked(node, sem), pal, state);
        break;
    }
    case VisualKind::Knob: {
        const float d = std::min(bounds.w, bounds.h);
        const draw::Vec2 p{bounds.x + (bounds.w - d) * 0.5f, bounds.y};
        paint::KnobPaintArgs args;
        args.topLeft = ImVec2(p.x, p.y);
        args.size = d;
        args.rawValue = (float)valueOf(node, sem);
        args.normalizedValue = (float)normalizedValue(node, sem);
        args.style = style.knobStyle;
        args.bipolar = style.bipolar;
        args.accent = style.accent;
        args.palette = &pal;
        args.state = &state;
        args.surface = &surface;
        if (style.knobMod)
            args.mod = style.knobMod();
        paint::drawKnobWithPainter(args, style.knobPainter);
        const std::string name = nodeName(node, sem);
        if (!name.empty()) {
            Rect labelBounds{bounds.x, bounds.y + d + 2.0f, bounds.w,
                             fontSize + 2.0f};
            drawText(surface, font, fontSize, labelBounds, name, 0.90f, pal.text);
        }
        break;
    }
    case VisualKind::Led:
        paint::drawLed(surface,
                       {bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f},
                       style.ledRadius > 0.0f ? style.ledRadius : 5.0f,
                       checked(node, sem), pal, state, style.accent);
        break;
    case VisualKind::Meter:
        paint::drawMeter(surface, topLeftDraw(bounds), sizeOfDraw(bounds),
                         (float)normalizedValue(node, sem),
                         (float)normalizedValue(node, sem),
                         style.meterFloorDb, pal);
        break;
    case VisualKind::Fader:
        paint::drawFader(surface, topLeftDraw(bounds), sizeOfDraw(bounds),
                         (float)normalizedValue(node, sem), pal, state);
        break;
    case VisualKind::Auto:
    default:
        break;
    }

    const bool scrollClip = node.scroll();
    if (scrollClip)
        surface.pushClip(topLeftDraw(bounds), bottomRightDraw(bounds), true);
    for (const auto& child : node.children()) {
        if (overlayQueue && child->overlay() && child->visible()) {
            overlayQueue->push_back(child.get()); // painted after the main pass
            continue;
        }
        renderNode(*child, semantics, origin, surface, context, overlayQueue);
    }
    if (scrollClip) {
        surface.popClip();
        if (node.contentHeight() > node.viewHeight() + 0.5f)
            drawScrollbar(surface, bounds, node.contentHeight(),
                          node.viewHeight(), node.scrollY());
    }
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
        // Entering/leaving relative-cursor mode (Window::setMouseCaptured)
        // warps the OS cursor, which shows up here as one enormous jump.
        // Ignore any single-frame delta bigger than half the viewport so a
        // capture toggle can't spike the dragged value.
        if (std::abs(moveDelta.x) > io.DisplaySize.x * 0.5f ||
            std::abs(moveDelta.y) > io.DisplaySize.y * 0.5f)
            moveDelta = {0.0f, 0.0f};
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

    // A press ends on MouseUp -- unless an OS window spawned mid-gesture (e.g.
    // double-clicking a graph node opens its editor) grabbed the release,
    // leaving the tree pressed so it keeps dragging the node on later moves
    // ("it sticks to the cursor"). Reconcile against the pointer state already
    // in io: with no button held, no press can be live, so drop any stale one.
    if (!io.MouseDown[0] && !io.MouseDown[1] && !io.MouseDown[2])
        tree.cancelPress();

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        // Context menus open on right mouse-down, not on release.
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
        ImGuiKey_Backspace,  ImGuiKey_Delete,   ImGuiKey_Home,
        ImGuiKey_End,
    };
    for (ImGuiKey key : keys) {
        if (ImGui::IsKeyPressed(key, false)) {
            Event event = keyEvent(key);
            if (event.key != Key::Unknown)
                consumed = tree.dispatch(event) || consumed;
        }
    }
    for (ImWchar ch : io.InputQueueCharacters) {
        if (ch < 32 || io.KeyCtrl || io.KeyAlt || io.KeySuper)
            continue;
        Event event;
        event.type = EventType::TextInput;
        fillModifiers(event, io);
        appendUtf8(event.text, (uint32_t)ch);
        if (event.text.empty())
            continue;
        consumed = tree.dispatch(event) || consumed;
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
    renderer.prepareOpenPopups(tree);

    result.origin = ImGui::GetCursorScreenPos();
    result.size = ImVec2(size.x, size.y);

    ImGui::PushID(&tree);
    ImGui::InvisibleButton("##snd-retained-tree", result.size);
    const bool mouseCaptured = ImGui::IsItemActive();
    ImGui::PopID();

    const ImGuiIO& io = ImGui::GetIO();
    const bool popupDismissed =
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) &&
        renderer.dismissOpenPopupsOutside(tree, result.origin, io.MousePos);
    result.inputConsumed = popupDismissed ||
                           dispatchImGuiInput(tree, result.origin, mouseCaptured);
    if (result.inputConsumed) {
        result.valuesRefreshed = tree.refreshBoundValues() || result.valuesRefreshed;
        tree.layout(size);
        renderer.prepareOpenPopups(tree);
    }
    renderer.render(tree, result.origin, drawList);
    return result;
}

namespace {

float retainedMenuItemHeight(const MenuItem& item)
{
    return item.separator ? 7.0f : 24.0f;
}

float retainedVisibleMenuHeight(const std::vector<MenuItem>& items,
                                const PopupMenuState* state,
                                const std::string& parentPath = {})
{
    // Flyout submenus live in their own side panels; a menu panel's height
    // is just its direct rows.
    (void)state;
    (void)parentPath;
    float total = 8.0f;
    for (const MenuItem& item : items)
        total += retainedMenuItemHeight(item);
    return total;
}

std::string menuChildId(const NodeId& parentId, const MenuItem& item, int index)
{
    if (!item.id.empty())
        return parentId + "." + item.id;
    char suffix[32];
    std::snprintf(suffix, sizeof suffix, ".item%d", index);
    return parentId + suffix;
}

Node::Ptr makeMenuItemNode(NodeId id, MenuItem item,
                           std::function<void(Node&, const MenuItem&, int)> onSelect,
                           PaintRenderer* renderer, int index, float width,
                           PopupMenuState* state,
                           std::string path, std::string parentPath,
                           int depth, int focusIndex);

void addMenuItemNodes(Node& menu, const NodeId& sid,
                      const std::vector<MenuItem>& items,
                      std::function<void(Node&, const MenuItem&, int)> onSelect,
                      PaintRenderer* renderer, float width, PopupMenuState* state,
                      const std::string& parentPath = {}, int depth = 0)
{
    for (int i = 0; i < (int)items.size(); ++i) {
        const MenuItem& item = items[(std::size_t)i];
        const std::string path = menuPath(parentPath, item, i);
        const int focusIndex = (int)menu.childCount();
        const NodeId rowId = menuChildId(sid, item, focusIndex);
        menu.addChild(makeMenuItemNode(rowId, item,
                                       onSelect, renderer, i, width, state,
                                       path, parentPath, depth, focusIndex));
        if (!item.children.empty()) {
            // Real flyout: the submenu is its own overlay panel, anchored to
            // the right of its parent row by prepareOpenPopups, not an
            // inline accordion in the same column.
            auto sub = widgets::column(rowId + ".sub", 0.0f, Insets::all(4.0f));
            sub->setOverlay(true);
            sub->setSize(Length::fixed(width + 8.0f), Length::intrinsic());
            sub->setIntrinsicSize({width + 8.0f,
                                   retainedVisibleMenuHeight(item.children,
                                                             state)});
            Semantics subSem = named(Role::Menu, item.label + " submenu");
            sub->setSemantics(subSem);
            const std::string subPath = path;
            sub->setVisible(state && state->open && submenuOpen(state, subPath));
            sub->setOnRefresh([state, subPath](Node& n) {
                const bool wasVisible = n.visible();
                n.setVisible(state && state->open &&
                             submenuOpen(state, subPath));
                return wasVisible != n.visible();
            });
            addMenuItemNodes(*sub, rowId + ".sub", item.children, onSelect,
                             renderer, width, state, path, depth + 1);
            if (renderer) {
                VisualStyle subStyle;
                subStyle.kind = VisualKind::Panel;
                subStyle.panelFill = true;
                subStyle.panelBorder = true;
                subStyle.panelRounding = 0.0f;
                renderer->setStyle(rowId + ".sub", subStyle);
            }
            menu.addChild(std::move(sub));
        }
    }
}

std::string selectedMenuLabel(const std::string& fallback,
                              const std::vector<MenuItem>& items,
                              const int* selectedIndex)
{
    if (selectedIndex && *selectedIndex >= 0 &&
        *selectedIndex < (int)items.size()) {
        const MenuItem& item = items[(std::size_t)*selectedIndex];
        if (!item.separator && !item.label.empty())
            return item.label;
    }
    return fallback;
}

void setDropdownButtonSemantics(Node& node, const std::string& name, bool open)
{
    Semantics sem = named(Role::ComboBox, name);
    if (open)
        sem.states |= SemanticState::Expanded;
    sem.value.text = name;
    sem.actions.push_back(Action::OpenMenu);
    node.setSemantics(sem);
}

void setMenuVisibleFromState(Node& node, PopupMenuState* state)
{
    if (state) {
        node.setVisible(state->open);
        if (!state->open) {
            state->highlightedIndex = -1;
            state->openSubmenuPath.clear();
            state->typeahead.clear();
        }
    }
}

void closeMenuFromNode(Node& node, PopupMenuState* state)
{
    if (state) {
        state->open = false;
        state->highlightedIndex = -1;
        state->openSubmenuPath.clear();
        state->typeahead.clear();
    }
    if (Node* menu = node.parent())
        menu->setVisible(false);
}

Node::Ptr makeMenuItemNode(NodeId id, MenuItem item,
                           std::function<void(Node&, const MenuItem&, int)> onSelect,
                           PaintRenderer* renderer, int index, float width,
                           PopupMenuState* state,
                           std::string path = {}, std::string parentPath = {},
                           int depth = 0, int focusIndex = -1)
{
    if (focusIndex < 0)
        focusIndex = index;
    NodeId sid = id;
    auto node = Node::make(std::move(id), item.separator ? Role::Text : Role::MenuItem);
    node->setIntrinsicSize({width, retainedMenuItemHeight(item)});
    node->setSize(Length::fixed(width), Length::intrinsic());
    node->setFocusable(!item.separator && item.enabled);
    node->setEnabled(!item.separator && item.enabled);

    Semantics sem = named(item.separator ? Role::Text : Role::MenuItem, item.label);
    if (item.separator) {
        sem.hidden = true;
        sem.states |= SemanticState::Hidden;
    }
    if (!item.enabled)
        sem.states |= SemanticState::Disabled;
    if (item.checked)
        sem.states |= SemanticState::Checked;
    if (!item.children.empty()) {
        sem.actions.push_back(Action::OpenMenu);
        if (submenuOpen(state, path))
            sem.states |= SemanticState::Expanded;
    }
    node->setSemantics(sem);
    node->setVisible(!state || (state->open && parentSubmenuVisible(state, parentPath)));
    node->setOnRefresh([state, path, parentPath, item](Node& n) {
        const bool wasVisible = n.visible();
        const bool visible = !state || (state->open && parentSubmenuVisible(state, parentPath));
        n.setVisible(visible);
        Semantics next = n.semantics();
        const bool expanded = submenuOpen(state, path);
        const bool wasExpanded = hasState(next.states, SemanticState::Expanded);
        if (item.children.empty()) {
            if (wasVisible == visible)
                return false;
            return true;
        }
        if (expanded)
            next.states |= SemanticState::Expanded;
        else
            next.states &= ~stateMask(SemanticState::Expanded);
        if (wasExpanded != expanded)
            n.setSemantics(next);
        return wasVisible != visible || wasExpanded != expanded;
    });

    if (!item.separator && item.enabled) {
        node->setOnActivate([item, index, onSelect, state, path](Node& n) {
            if (!item.children.empty()) {
                if (submenuOpen(state, path))
                    setSubmenuOpen(state, path, false);
                else
                    openSubmenuExclusive(state, path);
                n.markDirty();
                return;
            }
            if (onSelect)
                onSelect(n, item, index);
            closeMenuFromNode(n, state);
        });
        node->setOnAction([state, path, item](Node& n, Action action, double) {
            if (action != Action::OpenMenu || item.children.empty())
                return false;
            openSubmenuExclusive(state, path);
            n.markDirty();
            return true;
        });
        node->setOnEvent([state, index, focusIndex, path, parentPath,
                          item](Node& n, const Event& event) {
            if (event.type == EventType::MouseMove) {
                // hover opens flyouts like a real menu; hovering a sibling
                // (leaf or other parent) closes deeper flyouts
                if (state && state->open) {
                    if (!item.children.empty()) {
                        if (!submenuOpen(state, path)) {
                            openSubmenuExclusive(state, path);
                            n.markDirty();
                        }
                    } else if (!state->openSubmenuPath.empty()) {
                        const std::size_t before = state->openSubmenuPath.size();
                        closeSubmenusBelow(state, parentPath);
                        if (state->openSubmenuPath.size() != before)
                            n.markDirty();
                    }
                }
                return false; // never consume hover
            }
            if (event.type != EventType::KeyDown)
                return false;
            if (!event.text.empty() && state) {
                Node* menu = n.parent();
                if (!menu)
                    return false;
                state->typeahead = event.text;
                const int next = typeaheadMenuChild(*menu, focusIndex, state->typeahead);
                if (next >= 0) {
                    state->highlightedIndex = next;
                    return true;
                }
            }
            if (event.key == Key::Escape) {
                closeMenuFromNode(n, state);
                return true;
            }
            if (event.key == Key::Right && !item.children.empty() && state) {
                openSubmenuExclusive(state, path);
                n.markDirty();
                return true;
            }
            if (event.key == Key::Left && state && !state->openSubmenuPath.empty()) {
                state->openSubmenuPath.pop_back();
                n.markDirty();
                return true;
            }
            if ((event.key == Key::Down || event.key == Key::Up) && state) {
                const Node* menu = n.parent();
                if (!menu)
                    return false;
                // nothing highlighted yet: the first key press lights this
                // row (the invisible anchor) instead of skipping past it
                const int next =
                    state->highlightedIndex < 0
                        ? focusIndex
                        : nextFocusableMenuChild(
                              *menu, focusIndex, event.key == Key::Down ? 1 : -1);
                if (next >= 0) {
                    state->highlightedIndex = next;
                    return true;
                }
            }
            return false;
        });
    }

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::MenuItem;
        style.menuItem = item;
        renderer->setStyle(sid, style);
    }
    return node;
}

Vec2 add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 sub(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 mul(Vec2 v, float s) { return {v.x * s, v.y * s}; }

Vec2 rectCenter(Rect r)
{
    return {r.x + r.w * 0.5f, r.y + r.h * 0.5f};
}

Rect nodeLocalRect(const GraphNode& node, Rect local)
{
    local.x += node.bounds.x;
    local.y += node.bounds.y;
    return local;
}

const GraphNode* findGraphNode(const std::vector<GraphNode>& nodes,
                               const NodeId& id)
{
    auto it = std::find_if(nodes.begin(), nodes.end(),
                           [&](const GraphNode& node) { return node.id == id; });
    return it == nodes.end() ? nullptr : &*it;
}

const GraphPort* findGraphPort(const GraphNode& node, const NodeId& id,
                               bool* output = nullptr)
{
    auto outIt = std::find_if(node.outputs.begin(), node.outputs.end(),
                              [&](const GraphPort& port) { return port.id == id; });
    if (outIt != node.outputs.end()) {
        if (output)
            *output = true;
        return &*outIt;
    }
    auto inIt = std::find_if(node.inputs.begin(), node.inputs.end(),
                             [&](const GraphPort& port) { return port.id == id; });
    if (inIt != node.inputs.end()) {
        if (output)
            *output = false;
        return &*inIt;
    }
    return nullptr;
}

bool portCenterGraph(const std::vector<GraphNode>& nodes, const NodeId& nodeId,
                     const NodeId& portId, Vec2& out)
{
    const GraphNode* node = findGraphNode(nodes, nodeId);
    if (!node)
        return false;
    const GraphPort* port = findGraphPort(*node, portId);
    if (!port)
        return false;
    out = add({node->bounds.x, node->bounds.y}, rectCenter(port->bounds));
    return true;
}

float distance(Vec2 a, Vec2 b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

float distanceToSegment(Vec2 p, Vec2 a, Vec2 b)
{
    const Vec2 ab = sub(b, a);
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    if (len2 <= 0.0001f)
        return distance(p, a);
    const Vec2 ap = sub(p, a);
    const float t = std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f);
    return distance(p, {a.x + ab.x * t, a.y + ab.y * t});
}

Vec2 cubic(Vec2 a, Vec2 b, Vec2 c, Vec2 d, float t)
{
    const float u = 1.0f - t;
    const float uu = u * u;
    const float tt = t * t;
    return {
        a.x * uu * u + 3.0f * b.x * uu * t + 3.0f * c.x * u * tt + d.x * tt * t,
        a.y * uu * u + 3.0f * b.y * uu * t + 3.0f * c.y * u * tt + d.y * tt * t,
    };
}

void graphCableControls(Vec2 from, Vec2 to, const GraphSurfaceStyle& style,
                        Vec2& c1, Vec2& c2)
{
    const float dx = std::max(style.wireDroop ? 40.0f : 38.0f,
                              std::abs(to.x - from.x) *
                                  (style.wireDroop ? 0.50f : 0.52f));
    const float sag = style.wireDroop
                          ? std::min(80.0f, 22.0f + std::abs(to.x - from.x) * 0.18f)
                          : 0.0f;
    c1 = {from.x + dx, from.y + sag};
    c2 = {to.x - dx, to.y + sag};
}

float distanceToCable(Vec2 p, Vec2 from, Vec2 to, const GraphSurfaceStyle& style)
{
    Vec2 c1;
    Vec2 c2;
    graphCableControls(from, to, style, c1, c2);
    float best = std::numeric_limits<float>::max();
    Vec2 prev = from;
    for (int i = 1; i <= 24; ++i) {
        const Vec2 cur = cubic(from, c1, c2, to, (float)i / 24.0f);
        best = std::min(best, distanceToSegment(p, prev, cur));
        prev = cur;
    }
    return best;
}

GraphHit makeGraphHit(GraphHitKind kind, Vec2 graphPosition)
{
    GraphHit hit;
    hit.kind = kind;
    hit.graphPosition = graphPosition;
    return hit;
}

bool sameGraphHit(const GraphHit& a, const GraphHit& b)
{
    return a.kind == b.kind &&
           a.nodeId == b.nodeId &&
           a.partId == b.partId &&
           a.portId == b.portId &&
           a.cableId == b.cableId &&
           a.output == b.output;
}

bool graphPortHit(const GraphHit& hit)
{
    return hit.kind == GraphHitKind::Port && !hit.nodeId.empty() && !hit.portId.empty();
}

bool defaultGraphConnectionAllowed(const GraphHit& from, const GraphHit& to)
{
    if (!graphPortHit(from) || !graphPortHit(to))
        return false;
    if (from.nodeId == to.nodeId && from.portId == to.portId)
        return false;
    return from.output != to.output;
}

bool graphConnectionAllowed(const GraphHit& from, const GraphHit& to,
                            const GraphSurfaceCallbacks& callbacks)
{
    if (!defaultGraphConnectionAllowed(from, to))
        return false;
    return callbacks.canConnect ? callbacks.canConnect(from, to) : true;
}

Vec2 graphPortAnchor(const std::vector<GraphNode>& nodes, const GraphHit& hit)
{
    Vec2 out = hit.graphPosition;
    if (graphPortHit(hit))
        portCenterGraph(nodes, hit.nodeId, hit.portId, out);
    return out;
}

void clearGraphCablePreview(GraphSurfaceState& state)
{
    state.cablePreviewActive = false;
    state.cablePreviewStart = {};
    state.cablePreviewTarget = {};
    state.cablePreviewPosition = {};
    state.cablePreviewValid = false;
}

void updateGraphCablePreview(GraphSurfaceState& state,
                             const std::vector<GraphNode>& nodes,
                             const std::vector<GraphCable>& cables,
                             Vec2 localPosition,
                             const GraphSurfaceStyle& style,
                             const GraphSurfaceCallbacks& callbacks)
{
    if (!state.cablePreviewActive)
        return;
    state.cablePreviewPosition = screenToGraph(state.viewport, localPosition);
    GraphHit target = hitTestGraph(state.viewport, nodes, cables, localPosition, style);
    if (!graphPortHit(target) ||
        !graphConnectionAllowed(state.cablePreviewStart, target, callbacks)) {
        state.cablePreviewTarget = {};
        state.cablePreviewValid = false;
    } else {
        state.cablePreviewTarget = target;
        state.cablePreviewValid = true;
    }
    if (callbacks.onCablePreview)
        callbacks.onCablePreview(state.cablePreviewStart, state.cablePreviewPosition);
}

std::vector<GraphHit> graphKeyboardHits(const std::vector<GraphNode>& nodes,
                                        const std::vector<GraphCable>& cables)
{
    std::vector<GraphHit> out;
    for (const GraphNode& node : nodes) {
        GraphHit module = makeGraphHit(GraphHitKind::NodeBody, rectCenter(node.bounds));
        module.nodeId = node.id;
        out.push_back(module);

        auto appendPort = [&](const GraphPort& port, bool output) {
            Vec2 center = add({node.bounds.x, node.bounds.y}, rectCenter(port.bounds));
            GraphHit hit = makeGraphHit(GraphHitKind::Port, center);
            hit.nodeId = node.id;
            hit.portId = port.id;
            hit.output = output;
            out.push_back(hit);
        };
        for (const GraphPort& port : node.inputs)
            appendPort(port, false);
        for (const GraphPort& port : node.outputs)
            appendPort(port, true);

        for (const GraphNodePart& part : node.parts) {
            if (part.kind != GraphNodePartKind::Toggle &&
                part.kind != GraphNodePartKind::Action)
                continue;
            GraphHit hit = makeGraphHit(GraphHitKind::NodePart,
                                        add({node.bounds.x, node.bounds.y},
                                            rectCenter(part.bounds)));
            hit.nodeId = node.id;
            hit.partId = part.id;
            out.push_back(hit);
        }
    }

    for (const GraphCable& cable : cables) {
        Vec2 from;
        Vec2 to;
        if (!portCenterGraph(nodes, cable.fromNode, cable.fromPort, from) ||
            !portCenterGraph(nodes, cable.toNode, cable.toPort, to))
            continue;
        GraphHit hit = makeGraphHit(GraphHitKind::Cable,
                                    {(from.x + to.x) * 0.5f,
                                     (from.y + to.y) * 0.5f});
        hit.cableId = cable.id;
        out.push_back(hit);
    }
    return out;
}

int graphKeyboardIndex(const std::vector<GraphHit>& hits, const GraphHit& current)
{
    if (!current.valid())
        return -1;
    for (int i = 0; i < (int)hits.size(); ++i)
        if (sameGraphHit(hits[(std::size_t)i], current))
            return i;
    return -1;
}

bool moveGraphKeyboardFocus(GraphSurfaceState& state,
                            const std::vector<GraphNode>& nodes,
                            const std::vector<GraphCable>& cables,
                            int direction)
{
    std::vector<GraphHit> hits = graphKeyboardHits(nodes, cables);
    if (hits.empty())
        return false;

    GraphHit current = state.focused.valid() ? state.focused
                     : state.active.valid()  ? state.active
                     : state.hovered;
    int index = graphKeyboardIndex(hits, current);
    if (index < 0)
        index = direction < 0 ? 0 : -1;
    index += direction;
    if (index < 0)
        index = (int)hits.size() - 1;
    else if (index >= (int)hits.size())
        index = 0;

    state.focused = hits[(std::size_t)index];
    state.hovered = state.focused;
    state.active = state.focused;
    return true;
}

void applyGraphFocusState(SemanticNode& sem, const GraphHit& current,
                          const GraphHit& hit)
{
    if (sameGraphHit(current, hit))
        sem.states |= SemanticState::Focused;
}

bool partKindInPass(GraphNodePartKind kind, int pass)
{
    if (pass == 0)
        return kind == GraphNodePartKind::Toggle ||
               kind == GraphNodePartKind::Action;
    if (pass == 1)
        return kind == GraphNodePartKind::Readout ||
               kind == GraphNodePartKind::Meter ||
               kind == GraphNodePartKind::Status;
    return kind == GraphNodePartKind::Title;
}

std::string graphHitLabel(const GraphHit& hit)
{
    switch (hit.kind) {
    case GraphHitKind::NodeBody: return "Module " + hit.nodeId;
    case GraphHitKind::NodeTitle: return "Module title " + hit.nodeId;
    case GraphHitKind::NodePart: return "Module control " + hit.nodeId + "." + hit.partId;
    case GraphHitKind::Port: return "Port " + hit.nodeId + "." + hit.portId;
    case GraphHitKind::CableEndpoint: return "Cable endpoint " + hit.cableId;
    case GraphHitKind::Cable: return "Cable " + hit.cableId;
    case GraphHitKind::Surface: return "Graph surface";
    case GraphHitKind::None:
    default: return "Nothing";
    }
}

Semantics graphSurfaceSemantics(const std::string& name,
                                const GraphSurfaceState& state,
                                const std::vector<GraphNode>& nodes,
                                const std::vector<GraphCable>& cables)
{
    Semantics sem = named(Role::Canvas, name.empty() ? "Graph" : name);
    sem.description = "Module graph surface";
    sem.actions.push_back(Action::OpenMenu);
    sem.value.text = std::to_string(nodes.size()) + " modules, " +
                     std::to_string(cables.size()) + " cables, hover " +
                     graphHitLabel(state.hovered);
    return sem;
}

bool refreshGraphSurfaceSemantics(Node& node, const std::string& name,
                                  const GraphSurfaceState& state,
                                  const std::vector<GraphNode>& nodes,
                                  const std::vector<GraphCable>& cables)
{
    const Semantics next = graphSurfaceSemantics(name, state, nodes, cables);
    const Semantics& current = static_cast<const Node&>(node).semantics();
    if (current.name == next.name &&
        current.description == next.description &&
        current.value.text == next.value.text &&
        current.actions.size() == next.actions.size())
        return false;
    node.setSemantics(next);
    return true;
}

void pushAction(std::vector<Action>& actions, Action action)
{
    if (std::find(actions.begin(), actions.end(), action) == actions.end())
        actions.push_back(action);
}

std::string graphModuleSemanticId(const NodeId& surfaceId, const NodeId& nodeId)
{
    return surfaceId + ".module." + nodeId;
}

std::string graphPartSemanticId(const NodeId& surfaceId, const NodeId& nodeId,
                                const NodeId& partId)
{
    return graphModuleSemanticId(surfaceId, nodeId) + ".part." + partId;
}

std::string graphPortSemanticId(const NodeId& surfaceId, const NodeId& nodeId,
                                const NodeId& portId)
{
    return graphModuleSemanticId(surfaceId, nodeId) + ".port." + portId;
}

std::string graphCableSemanticId(const NodeId& surfaceId, const NodeId& cableId)
{
    return surfaceId + ".cable." + cableId;
}

Rect graphRectToTree(const Rect& surfaceBounds, const GraphViewport& viewport,
                     Rect graphRect)
{
    Rect out = graphToScreen(viewport, graphRect);
    out.x += surfaceBounds.x;
    out.y += surfaceBounds.y;
    return out;
}

Rect cableSemanticBounds(const Rect& surfaceBounds, const GraphViewport& viewport,
                         Vec2 fromGraph, Vec2 toGraph)
{
    const Vec2 a = graphToScreen(viewport, fromGraph);
    const Vec2 b = graphToScreen(viewport, toGraph);
    const float minX = std::min(a.x, b.x);
    const float minY = std::min(a.y, b.y);
    const float maxX = std::max(a.x, b.x);
    const float maxY = std::max(a.y, b.y);
    const float pad = 8.0f;
    return {surfaceBounds.x + minX - pad, surfaceBounds.y + minY - pad,
            std::max(16.0f, maxX - minX + pad * 2.0f),
            std::max(16.0f, maxY - minY + pad * 2.0f)};
}

std::string graphPortKindText(GraphPortKind kind)
{
    switch (kind) {
    case GraphPortKind::Audio: return "audio";
    case GraphPortKind::Midi: return "MIDI";
    case GraphPortKind::Control: return "control";
    case GraphPortKind::Event: return "event";
    case GraphPortKind::Parameter: return "parameter";
    case GraphPortKind::Unknown:
    default: return "unknown";
    }
}

Role graphPartRole(GraphNodePartKind kind)
{
    switch (kind) {
    case GraphNodePartKind::Toggle: return Role::Toggle;
    case GraphNodePartKind::Action: return Role::Button;
    case GraphNodePartKind::Meter: return Role::Meter;
    case GraphNodePartKind::Readout:
    case GraphNodePartKind::Status:
    case GraphNodePartKind::Title:
    default: return Role::Text;
    }
}

std::string graphPartName(const GraphNode& node, const GraphNodePart& part)
{
    if (!part.label.empty())
        return node.title.empty() ? part.label : node.title + " " + part.label;
    if (!part.valueText.empty())
        return node.title.empty() ? part.valueText : node.title + " " + part.valueText;
    return node.title.empty() ? part.id : node.title + " " + part.id;
}

void appendGraphSemanticChildren(const Node& surface,
                                 const GraphSurfaceState& state,
                                 const std::vector<GraphNode>& nodes,
                                 const std::vector<GraphCable>& cables,
                                 std::vector<SemanticNode>& out)
{
    const Rect surfaceBounds = surface.bounds();
    for (const GraphNode& graphNode : nodes) {
        const NodeId moduleId = graphModuleSemanticId(surface.id(), graphNode.id);
        SemanticNode module;
        module.id = moduleId;
        module.parent = surface.id();
        module.bounds = graphRectToTree(surfaceBounds, state.viewport, graphNode.bounds);
        module.role = Role::Group;
        module.name = graphNode.title.empty() ? graphNode.id : graphNode.title;
        module.description = "Graph module";
        module.states |= SemanticState::Focusable;
        GraphHit moduleHit = makeGraphHit(GraphHitKind::NodeBody,
                                          rectCenter(graphNode.bounds));
        moduleHit.nodeId = graphNode.id;
        applyGraphFocusState(module, state.focused, moduleHit);
        if (graphNode.selected)
            module.states |= SemanticState::Selected;
        if (graphNode.disabled)
            module.states |= SemanticState::Disabled;
        std::vector<std::string> flags;
        if (graphNode.bypassed)
            flags.push_back("bypassed");
        if (graphNode.error)
            flags.push_back("error");
        if (!flags.empty()) {
            module.value.text = flags.front();
            for (std::size_t i = 1; i < flags.size(); ++i)
                module.value.text += ", " + flags[i];
        }
        pushAction(module.actions, Action::Focus);
        pushAction(module.actions, Action::Activate);
        pushAction(module.actions, Action::OpenMenu);
        out.push_back(module);

        auto appendPort = [&](const GraphPort& port) {
            SemanticNode sem;
            sem.id = graphPortSemanticId(surface.id(), graphNode.id, port.id);
            sem.parent = moduleId;
            sem.bounds = graphRectToTree(surfaceBounds, state.viewport,
                                         nodeLocalRect(graphNode, port.bounds));
            sem.role = Role::Button;
            const bool output = port.direction == GraphPortDirection::Output;
            sem.name = (output ? "Output " : "Input ") +
                       (port.label.empty() ? port.id : port.label);
            sem.description = graphPortKindText(port.kind) + " port";
            sem.states |= SemanticState::Focusable;
            GraphHit portHit = makeGraphHit(GraphHitKind::Port,
                                            add({graphNode.bounds.x,
                                                 graphNode.bounds.y},
                                                rectCenter(port.bounds)));
            portHit.nodeId = graphNode.id;
            portHit.portId = port.id;
            portHit.output = output;
            applyGraphFocusState(sem, state.focused, portHit);
            if (!port.enabled)
                sem.states |= SemanticState::Disabled;
            if (port.connected)
                sem.states |= SemanticState::Selected;
            sem.value.text = port.connected ? "Connected" : "Not connected";
            pushAction(sem.actions, Action::Focus);
            pushAction(sem.actions, Action::Activate);
            pushAction(sem.actions, Action::OpenMenu);
            out.push_back(sem);
        };
        for (const GraphPort& port : graphNode.inputs)
            appendPort(port);
        for (const GraphPort& port : graphNode.outputs)
            appendPort(port);

        for (const GraphNodePart& part : graphNode.parts) {
            SemanticNode sem;
            sem.id = graphPartSemanticId(surface.id(), graphNode.id, part.id);
            sem.parent = moduleId;
            sem.bounds = graphRectToTree(surfaceBounds, state.viewport,
                                         nodeLocalRect(graphNode, part.bounds));
            sem.role = graphPartRole(part.kind);
            sem.name = graphPartName(graphNode, part);
            sem.description = "Graph module part";
            GraphHit partHit = makeGraphHit(part.kind == GraphNodePartKind::Title
                                                ? GraphHitKind::NodeTitle
                                                : GraphHitKind::NodePart,
                                            add({graphNode.bounds.x,
                                                 graphNode.bounds.y},
                                                rectCenter(part.bounds)));
            partHit.nodeId = graphNode.id;
            partHit.partId = part.id;
            applyGraphFocusState(sem, state.focused, partHit);
            if (!part.enabled)
                sem.states |= SemanticState::Disabled;
            if (part.checked)
                sem.states |= SemanticState::Checked;
            if (part.selected)
                sem.states |= SemanticState::Selected;
            if (part.hasValue) {
                sem.value.hasNumeric = true;
                sem.value.min = 0.0;
                sem.value.max = 1.0;
                sem.value.step = 0.01;
                sem.value.value = std::clamp(part.value, 0.0, 1.0);
            }
            sem.value.text = !part.valueText.empty()
                                 ? part.valueText
                                 : (part.checked ? "On" : "");
            if (part.kind == GraphNodePartKind::Toggle ||
                part.kind == GraphNodePartKind::Action) {
                sem.states |= SemanticState::Focusable;
                pushAction(sem.actions, Action::Focus);
                pushAction(sem.actions, Action::Activate);
                pushAction(sem.actions, Action::OpenMenu);
            }
            out.push_back(sem);
        }
    }

    for (const GraphCable& cable : cables) {
        Vec2 from;
        Vec2 to;
        if (!portCenterGraph(nodes, cable.fromNode, cable.fromPort, from) ||
            !portCenterGraph(nodes, cable.toNode, cable.toPort, to))
            continue;
        SemanticNode sem;
        sem.id = graphCableSemanticId(surface.id(), cable.id);
        sem.parent = surface.id();
        sem.bounds = cableSemanticBounds(surfaceBounds, state.viewport, from, to);
        sem.role = Role::Custom;
        sem.name = cable.id.empty() ? "Cable" : "Cable " + cable.id;
        sem.description = "Graph cable from " + cable.fromNode + "." +
                          cable.fromPort + " to " + cable.toNode + "." +
                          cable.toPort;
        sem.states |= SemanticState::Focusable;
        GraphHit cableHit = makeGraphHit(GraphHitKind::Cable,
                                         {(from.x + to.x) * 0.5f,
                                          (from.y + to.y) * 0.5f});
        cableHit.cableId = cable.id;
        applyGraphFocusState(sem, state.focused, cableHit);
        if (cable.selected)
            sem.states |= SemanticState::Selected;
        if (cable.muted || cable.invalid)
            sem.states |= SemanticState::Disabled;
        sem.value.text = cable.invalid ? "Invalid" : cable.muted ? "Muted" : "Connected";
        pushAction(sem.actions, Action::Focus);
        pushAction(sem.actions, Action::Activate);
        pushAction(sem.actions, Action::OpenMenu);
        out.push_back(sem);
    }
}

GraphHit graphHitForSemanticId(const NodeId& surfaceId, const NodeId& semanticId,
                               const std::vector<GraphNode>& nodes,
                               const std::vector<GraphCable>& cables)
{
    for (const GraphNode& node : nodes) {
        if (semanticId == graphModuleSemanticId(surfaceId, node.id)) {
            GraphHit hit = makeGraphHit(GraphHitKind::NodeBody, rectCenter(node.bounds));
            hit.nodeId = node.id;
            return hit;
        }
        for (const GraphPort& port : node.inputs) {
            if (semanticId == graphPortSemanticId(surfaceId, node.id, port.id)) {
                GraphHit hit = makeGraphHit(GraphHitKind::Port,
                                            add({node.bounds.x, node.bounds.y},
                                                rectCenter(port.bounds)));
                hit.nodeId = node.id;
                hit.portId = port.id;
                hit.output = false;
                return hit;
            }
        }
        for (const GraphPort& port : node.outputs) {
            if (semanticId == graphPortSemanticId(surfaceId, node.id, port.id)) {
                GraphHit hit = makeGraphHit(GraphHitKind::Port,
                                            add({node.bounds.x, node.bounds.y},
                                                rectCenter(port.bounds)));
                hit.nodeId = node.id;
                hit.portId = port.id;
                hit.output = true;
                return hit;
            }
        }
        for (const GraphNodePart& part : node.parts) {
            if (semanticId == graphPartSemanticId(surfaceId, node.id, part.id)) {
                GraphHit hit = makeGraphHit(part.kind == GraphNodePartKind::Title
                                                ? GraphHitKind::NodeTitle
                                                : GraphHitKind::NodePart,
                                            add({node.bounds.x, node.bounds.y},
                                                rectCenter(part.bounds)));
                hit.nodeId = node.id;
                hit.partId = part.id;
                return hit;
            }
        }
    }
    for (const GraphCable& cable : cables) {
        if (semanticId == graphCableSemanticId(surfaceId, cable.id)) {
            Vec2 from;
            Vec2 to;
            if (!portCenterGraph(nodes, cable.fromNode, cable.fromPort, from) ||
                !portCenterGraph(nodes, cable.toNode, cable.toPort, to))
                return {};
            GraphHit hit = makeGraphHit(GraphHitKind::Cable,
                                        {(from.x + to.x) * 0.5f,
                                         (from.y + to.y) * 0.5f});
            hit.cableId = cable.id;
            return hit;
        }
    }
    return {};
}

Vec2 graphHitTreeAnchor(const Node& surface, const GraphSurfaceState& state,
                        const GraphHit& hit)
{
    const Vec2 local = graphToScreen(state.viewport, hit.graphPosition);
    const Rect b = surface.bounds();
    return {b.x + local.x, b.y + local.y};
}

ImU32 graphPortColor(const GraphPort& port, const GraphSurfaceStyle& style,
                     const Palette& pal)
{
    auto use = [](ImU32 preferred, ImU32 fallback) {
        return (preferred & 0xFF000000u) != 0 ? preferred : fallback;
    };
    switch (port.kind) {
    case GraphPortKind::Audio:
        return use(style.pinAudio, IM_COL32(0x7f, 0xd1, 0xae, 255));
    case GraphPortKind::Midi:
        return use(style.pinMidi, IM_COL32(0xd9, 0x8a, 0xd9, 255));
    case GraphPortKind::Control:
    case GraphPortKind::Parameter:
        return use(style.pinControl, IM_COL32(0xe0, 0xb0, 0x20, 255));
    case GraphPortKind::Event:
    case GraphPortKind::Unknown:
    default:
        return pal.frameBright;
    }
}

// Which node edge a pin's node-local rect sits flush against: 0 none,
// 1 left, 2 right. Pins that straddle the edge match neither and keep the
// classic full-outline look; flush-inside pins render as
// sockets (see drawGraphPort).
int portFlushEdge(const GraphNode& node, const GraphPort& port)
{
    constexpr float eps = 0.5f;
    if (std::abs(port.bounds.x) <= eps)
        return 1;
    if (std::abs(port.bounds.x + port.bounds.w - node.bounds.w) <= eps)
        return 2;
    return 0;
}

void drawGraphPort(ImDrawList& dl, Rect bounds, const GraphPort& port,
                   const paint::ControlState& state,
                   const GraphSurfaceStyle& style, int flushEdge)
{
    // Kind colour reduced 1.5, black 0.45 outline, white 0.18 inner outline
    // reduced 2; square or round per skin. A flush pin (square only) opens
    // through the node border: its fill runs to the edge, covering the
    // border segment beneath, and outline + inner lip skip that side, so the
    // wire reads as plugging into a socket.
    const Palette& pal = palette();
    ImU32 col = port.invalidDrop ? pal.meterHot
                                 : graphPortColor(port, style, pal);
    if (!port.enabled)
        col = paint::mix(col, pal.textDim, 0.65f);
    (void)state;
    if (!style.squarePins)
        flushEdge = 0;
    const ImVec2 a(bounds.x + (flushEdge == 1 ? 0.0f : 1.5f),
                   bounds.y + 1.5f);
    const ImVec2 b(bounds.x + bounds.w - (flushEdge == 2 ? 0.0f : 1.5f),
                   bounds.y + bounds.h - 1.5f);
    const ImU32 outline = IM_COL32(0, 0, 0, 115);
    const ImU32 inner = IM_COL32(255, 255, 255, 46);
    if (style.squarePins) {
        dl.AddRectFilled(a, b, col, 0.0f);
        if (flushEdge == 0) {
            dl.AddRect(a, b, outline, 0.0f);
            dl.AddRect(ImVec2(a.x + 2.0f, a.y + 2.0f),
                       ImVec2(b.x - 2.0f, b.y - 2.0f), inner, 0.0f);
        } else {
            // owner: the socket keeps its "slightly darker border box" on the
            // three closed sides (visible on any node fill), open outside
            const ImU32 frame = paint::mix(col, IM_COL32(0, 0, 0, 255), 0.45f);
            const auto sides = [&](ImVec2 lo, ImVec2 hi, ImU32 c, float t) {
                dl.AddLine(lo, ImVec2(hi.x, lo.y), c, t);
                dl.AddLine(ImVec2(lo.x, hi.y), hi, c, t);
                if (flushEdge != 1)
                    dl.AddLine(lo, ImVec2(lo.x, hi.y), c, t);
                if (flushEdge != 2)
                    dl.AddLine(ImVec2(hi.x, lo.y), hi, c, t);
            };
            sides(a, b, frame, 1.5f);
            sides(ImVec2(a.x + (flushEdge == 1 ? 0.0f : 2.0f), a.y + 2.0f),
                  ImVec2(b.x - (flushEdge == 2 ? 0.0f : 2.0f), b.y - 2.0f),
                  inner, 1.0f);
        }
    } else {
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        const float r = std::max(2.0f, (b.x - a.x) * 0.5f);
        dl.AddCircleFilled(c, r, col);
        dl.AddCircle(c, r, outline);
        dl.AddCircle(c, r - 2.0f, inner);
    }
}

void drawGraphPort(draw::Surface& surface, Rect bounds, const GraphPort& port,
                   const paint::ControlState& state,
                   const GraphSurfaceStyle& style, int flushEdge)
{
    const Palette& pal = palette();
    ImU32 col = port.invalidDrop ? pal.meterHot
                                 : graphPortColor(port, style, pal);
    if (!port.enabled)
        col = paint::mix(col, pal.textDim, 0.65f);
    (void)state;
    if (!style.squarePins)
        flushEdge = 0;
    const draw::Vec2 a{bounds.x + (flushEdge == 1 ? 0.0f : 1.5f),
                       bounds.y + 1.5f};
    const draw::Vec2 b{bounds.x + bounds.w - (flushEdge == 2 ? 0.0f : 1.5f),
                       bounds.y + bounds.h - 1.5f};
    const ImU32 outline = IM_COL32(0, 0, 0, 115);
    const ImU32 inner = IM_COL32(255, 255, 255, 46);
    if (style.squarePins) {
        surface.fillRect(a, b, col, 0.0f);
        if (flushEdge == 0) {
            surface.strokeRect(a, b, outline, 0.0f);
            surface.strokeRect({a.x + 2.0f, a.y + 2.0f},
                               {b.x - 2.0f, b.y - 2.0f}, inner, 0.0f);
        } else {
            // owner: darker border box on the three closed sides, open outside
            const ImU32 frame = paint::mix(col, IM_COL32(0, 0, 0, 255), 0.45f);
            const auto sides = [&](draw::Vec2 lo, draw::Vec2 hi, ImU32 c,
                                   float t) {
                surface.line(lo, {hi.x, lo.y}, c, t);
                surface.line({lo.x, hi.y}, hi, c, t);
                if (flushEdge != 1)
                    surface.line(lo, {lo.x, hi.y}, c, t);
                if (flushEdge != 2)
                    surface.line({hi.x, lo.y}, hi, c, t);
            };
            sides(a, b, frame, 1.5f);
            sides({a.x + (flushEdge == 1 ? 0.0f : 2.0f), a.y + 2.0f},
                  {b.x - (flushEdge == 2 ? 0.0f : 2.0f), b.y - 2.0f}, inner,
                  1.0f);
        }
    } else {
        const draw::Vec2 c{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
        const float r = std::max(2.0f, (b.x - a.x) * 0.5f);
        surface.fillCircle(c, r, col);
        surface.strokeCircle(c, r, outline);
        surface.strokeCircle(c, r - 2.0f, inner);
    }
}

void drawGraphPart(ImDrawList& dl, ImFont* font, Rect bounds,
                   const GraphNodePart& part, const paint::ControlState& state)
{
    const Palette& pal = palette();
    paint::ControlState inner = state;
    inner.disabled = inner.disabled || !part.enabled;
    inner.selected = inner.selected || part.selected || part.checked;
    const ImVec2 p = topLeft(bounds);
    const ImVec2 s = sizeOf(bounds);
    if (part.kind == GraphNodePartKind::Meter) {
        paint::drawMeter(&dl, p, s, (float)std::clamp(part.value, 0.0, 1.0),
                         (float)std::clamp(part.value, 0.0, 1.0), -48.0f, pal);
        return;
    }
    if (part.kind == GraphNodePartKind::Toggle) {
        paint::drawToggle(&dl, p, s.x, s.y, part.checked ? 1.0f : 0.0f, pal, inner);
        return;
    }
    if (part.kind == GraphNodePartKind::Action) {
        paint::OutlineButtonStyle style;
        style.hoverBorder = pal.accent;
        style.activeFill = paint::withAlpha(pal.accent, 0x28);
        paint::drawOutlineButton(&dl, font, p, s,
                                 part.label.empty() ? "..." : part.label.c_str(),
                                 pal, inner, style);
        return;
    }
    if (part.kind == GraphNodePartKind::Status) {
        paint::drawBadge(&dl, font, p,
                         part.label.empty() ? part.valueText.c_str() : part.label.c_str(),
                         ImGui::GetFontSize() * 0.72f,
                         part.checked ? pal.accent : 0, pal);
        return;
    }
    paint::drawValueRow(&dl, font, p, s, part.label.c_str(), part.valueText.c_str(),
                        pal, inner, 0.78f, false);
}

void drawGraphPart(draw::Surface& surface, draw::FontRef font, float fontSizePx,
                   Rect bounds, const GraphNodePart& part,
                   const paint::ControlState& state)
{
    const Palette& pal = palette();
    paint::ControlState inner = state;
    inner.disabled = inner.disabled || !part.enabled;
    inner.selected = inner.selected || part.selected || part.checked;
    const draw::Vec2 p = topLeftDraw(bounds);
    const draw::Vec2 s = sizeOfDraw(bounds);
    if (part.kind == GraphNodePartKind::Meter) {
        paint::drawMeter(surface, p, s, (float)std::clamp(part.value, 0.0, 1.0),
                         (float)std::clamp(part.value, 0.0, 1.0), -48.0f, pal);
        return;
    }
    if (part.kind == GraphNodePartKind::Toggle) {
        paint::drawToggle(surface, p, s.x, s.y,
                          part.checked ? 1.0f : 0.0f, pal, inner);
        return;
    }
    if (part.kind == GraphNodePartKind::Action) {
        paint::OutlineButtonStyle style;
        style.hoverBorder = pal.accent;
        style.activeFill = paint::withAlpha(pal.accent, 0x28);
        paint::drawOutlineButton(surface, font, fontSizePx,
                                 p, s,
                                 part.label.empty() ? "..." : part.label.c_str(),
                                 pal, inner, style);
        return;
    }
    if (part.kind == GraphNodePartKind::Status) {
        paint::drawBadge(surface, font, p,
                         part.label.empty() ? part.valueText.c_str() : part.label.c_str(),
                         fontSizePx * 0.72f,
                         part.checked ? pal.accent : 0, pal);
        return;
    }
    paint::drawValueRow(surface, font, fontSizePx * 0.78f, p, s,
                        part.label.c_str(), part.valueText.c_str(),
                        pal, inner, false);
}

} // namespace

Vec2 graphToScreen(const GraphViewport& viewport, Vec2 graphPoint)
{
    const float z = std::max(0.05f, viewport.zoom);
    return {viewport.pan.x + graphPoint.x * z,
            viewport.pan.y + graphPoint.y * z};
}

Rect graphToScreen(const GraphViewport& viewport, Rect graphRect)
{
    const Vec2 p = graphToScreen(viewport, Vec2{graphRect.x, graphRect.y});
    const float z = std::max(0.05f, viewport.zoom);
    return {p.x, p.y, graphRect.w * z, graphRect.h * z};
}

Vec2 screenToGraph(const GraphViewport& viewport, Vec2 screenPoint)
{
    const float z = std::max(0.05f, viewport.zoom);
    return {(screenPoint.x - viewport.pan.x) / z,
            (screenPoint.y - viewport.pan.y) / z};
}

// Connector tooltip: while a pin is hovered for a moment, draw its
// GraphPort::label. Timing lives in GraphSurfaceState (which pin,
// since when) so the delay survives across frames. Shared by both render
// paths; call it LAST so the tooltip sits over the graph.
void drawGraphPortTooltip(draw::Surface& surface, GraphSurfaceState& state,
                          const std::vector<GraphNode>& nodes,
                          const GraphSurfaceStyle& graphStyle, Rect bounds,
                          const draw::FrameContext& context)
{
    if (!graphStyle.portTooltips ||
        state.hovered.kind != GraphHitKind::Port) {
        state.tooltipKey.clear();
        return;
    }
    // resolve the hovered port's label
    const std::string* label = nullptr;
    for (const GraphNode& n : nodes) {
        if (n.id != state.hovered.nodeId)
            continue;
        for (const GraphPort& p : n.inputs)
            if (p.id == state.hovered.portId)
                label = &p.label;
        for (const GraphPort& p : n.outputs)
            if (p.id == state.hovered.portId)
                label = &p.label;
        break;
    }
    if (!label || label->empty()) {
        state.tooltipKey.clear();
        return;
    }
    const std::string key = state.hovered.nodeId + "/" + state.hovered.portId;
    if (!tooltipDelayReady(state.tooltipKey, state.tooltipStart, key,
                           context.timeSeconds))
        return;

    draw::Vec2 at;
    if (context.pointerValid) {
        at = context.pointer;
    } else {
        const Vec2 pin = graphToScreen(state.viewport, state.hovered.graphPosition);
        at = {bounds.x + pin.x, bounds.y + pin.y};
    }
    paint::drawTooltip(surface, context.font,
                       context.fontSizePx > 0.0f ? context.fontSizePx : 13.0f,
                       at, label->c_str(), palette(),
                       {bounds.x + bounds.w, bounds.y + bounds.h});
}

GraphHit hitTestGraph(const GraphViewport& viewport,
                      const std::vector<GraphNode>& nodes,
                      const std::vector<GraphCable>& cables,
                      Vec2 screenPoint,
                      GraphSurfaceStyle style)
{
    const Vec2 graphPoint = screenToGraph(viewport, screenPoint);

    for (auto nodeIt = nodes.rbegin(); nodeIt != nodes.rend(); ++nodeIt) {
        const GraphNode& node = *nodeIt;
        auto testPort = [&](const GraphPort& port, bool output) -> GraphHit {
            const Rect screen = graphToScreen(viewport, nodeLocalRect(node, port.bounds));
            if (!screen.contains(screenPoint))
                return {};
            GraphHit hit = makeGraphHit(GraphHitKind::Port, graphPoint);
            hit.nodeId = node.id;
            hit.portId = port.id;
            hit.output = output;
            return hit;
        };
        for (auto it = node.outputs.rbegin(); it != node.outputs.rend(); ++it) {
            GraphHit hit = testPort(*it, true);
            if (hit.valid())
                return hit;
        }
        for (auto it = node.inputs.rbegin(); it != node.inputs.rend(); ++it) {
            GraphHit hit = testPort(*it, false);
            if (hit.valid())
                return hit;
        }
    }

    for (int pass = 0; pass < 3; ++pass) {
        for (auto nodeIt = nodes.rbegin(); nodeIt != nodes.rend(); ++nodeIt) {
            const GraphNode& node = *nodeIt;
            for (auto partIt = node.parts.rbegin(); partIt != node.parts.rend(); ++partIt) {
                const GraphNodePart& part = *partIt;
                if (!partKindInPass(part.kind, pass))
                    continue;
                const Rect screen = graphToScreen(viewport, nodeLocalRect(node, part.bounds));
                if (!screen.contains(screenPoint))
                    continue;
                GraphHit hit = makeGraphHit(part.kind == GraphNodePartKind::Title
                                                ? GraphHitKind::NodeTitle
                                                : GraphHitKind::NodePart,
                                            graphPoint);
                hit.nodeId = node.id;
                hit.partId = part.id;
                return hit;
            }
        }
    }

    for (auto nodeIt = nodes.rbegin(); nodeIt != nodes.rend(); ++nodeIt) {
        const GraphNode& node = *nodeIt;
        const Rect screen = graphToScreen(viewport, node.bounds);
        if (!screen.contains(screenPoint))
            continue;
        GraphHit hit = makeGraphHit(GraphHitKind::NodeBody, graphPoint);
        hit.nodeId = node.id;
        return hit;
    }

    for (auto cableIt = cables.rbegin(); cableIt != cables.rend(); ++cableIt) {
        const GraphCable& cable = *cableIt;
        Vec2 from;
        Vec2 to;
        if (!portCenterGraph(nodes, cable.fromNode, cable.fromPort, from) ||
            !portCenterGraph(nodes, cable.toNode, cable.toPort, to))
            continue;
        const Vec2 fromScreen = graphToScreen(viewport, from);
        const Vec2 toScreen = graphToScreen(viewport, to);
        if (distance(screenPoint, fromScreen) <= 7.0f ||
            distance(screenPoint, toScreen) <= 7.0f) {
            GraphHit hit = makeGraphHit(GraphHitKind::CableEndpoint, graphPoint);
            hit.cableId = cable.id;
            hit.output = distance(screenPoint, fromScreen) <= 7.0f;
            return hit;
        }
        const float tolerance = std::max(6.0f, style.wireThickness + 4.0f);
        if (distanceToCable(screenPoint, fromScreen, toScreen, style) <= tolerance) {
            GraphHit hit = makeGraphHit(GraphHitKind::Cable, graphPoint);
            hit.cableId = cable.id;
            return hit;
        }
    }

    return makeGraphHit(GraphHitKind::Surface, graphPoint);
}

void fitGraphViewport(GraphSurfaceState& state,
                      const std::vector<GraphNode>& nodes, Vec2 surfaceSize)
{
    if (nodes.empty()) {
        state.viewport.zoom = 1.0f;
        state.viewport.pan = {};
        return;
    }
    float x0 = nodes.front().bounds.x, y0 = nodes.front().bounds.y;
    float x1 = x0 + nodes.front().bounds.w, y1 = y0 + nodes.front().bounds.h;
    for (const GraphNode& node : nodes) {
        x0 = std::min(x0, node.bounds.x);
        y0 = std::min(y0, node.bounds.y);
        x1 = std::max(x1, node.bounds.x + node.bounds.w);
        y1 = std::max(y1, node.bounds.y + node.bounds.h);
    }
    // Expand 160x120, zoom 0.45..1.25, centre in the surface.
    x0 -= 160.0f; x1 += 160.0f;
    y0 -= 120.0f; y1 += 120.0f;
    const float bw = std::max(1.0f, x1 - x0);
    const float bh = std::max(1.0f, y1 - y0);
    const float fitX = surfaceSize.x > 0.0f ? surfaceSize.x / bw : 1.0f;
    const float fitY = surfaceSize.y > 0.0f ? surfaceSize.y / bh : 1.0f;
    const float zoom = std::clamp(std::min(fitX, fitY), 0.45f, 1.25f);
    state.viewport.zoom = zoom;
    state.viewport.pan = {surfaceSize.x * 0.5f - (x0 + bw * 0.5f) * zoom,
                          surfaceSize.y * 0.5f - (y0 + bh * 0.5f) * zoom};
}

namespace widgets {

Node& attachTooltip(Node& node, std::string text)
{
    if (static_cast<const Node&>(node).semantics().tooltip == text)
        return node;
    Semantics sem = static_cast<const Node&>(node).semantics();
    sem.tooltip = std::move(text);
    node.setSemantics(sem);
    return node;
}

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

Node::Ptr scrollView(NodeId id, float gap, Insets padding,
                     PaintRenderer* renderer)
{
    (void)renderer; // the scrollbar is drawn by the render, not a style
    auto node = column(std::move(id), gap, padding);
    node->setScroll(true);

    struct ScrollDrag {
        bool active = false;
        float grabOffset = 0.0f; // pointer.y - thumb.y at grab
    };
    auto drag = std::make_shared<ScrollDrag>();
    node->setOnEvent([drag](Node& n, const Event& e) {
        const float maxScroll =
            std::max(0.0f, n.contentHeight() - n.viewHeight());
        if (maxScroll <= 0.0f)
            return false;
        const Rect b = n.bounds();
        switch (e.type) {
        case EventType::MouseWheel:
            if (e.wheelDelta.y != 0.0f) {
                n.setScrollY(std::clamp(n.scrollY() - e.wheelDelta.y * 40.0f,
                                        0.0f, maxScroll));
                return true;
            }
            return false;
        case EventType::MouseDown: {
            if (e.button != MouseButton::Left)
                return false;
            const Rect t = scrollbarThumbRect(b, n.contentHeight(),
                                               n.viewHeight(), n.scrollY());
            // grab the thumb, or page toward a click elsewhere on the track
            if (t.contains(e.position)) {
                drag->active = true;
                drag->grabOffset = e.position.y - t.y;
                return true;
            }
            if (e.position.x >= b.x + b.w - kScrollBarInset - kScrollBarW - 2.0f) {
                n.setScrollY(std::clamp(
                    n.scrollY() + (e.position.y < t.y ? -1.0f : 1.0f) *
                                      n.viewHeight() * 0.9f,
                    0.0f, maxScroll));
                return true;
            }
            return false;
        }
        case EventType::MouseMove: {
            if (!drag->active)
                return false;
            const float trackH = std::max(1.0f, b.h - 4.0f);
            const float thumbH =
                std::max(24.0f, trackH * n.viewHeight() / n.contentHeight());
            const float span = std::max(1.0f, trackH - thumbH);
            const float thumbY = e.position.y - drag->grabOffset;
            const float frac = std::clamp((thumbY - (b.y + 2.0f)) / span,
                                          0.0f, 1.0f);
            n.setScrollY(frac * maxScroll);
            return true;
        }
        case EventType::MouseUp:
            if (drag->active) {
                drag->active = false;
                return true;
            }
            return false;
        default:
            return false;
        }
    });
    return node;
}

Node::Ptr splitter(NodeId id, std::string name, ValueBinding binding,
                   bool horizontal, bool invert, PaintRenderer* renderer,
                   float thickness)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::Group);
    node->setFocusable(true);
    node->setSemantics(named(Role::Group, std::move(name)));
    node->setIntrinsicSize({thickness, thickness});
    if (horizontal)
        node->setSize(Length::fixed(thickness), Length::fill());
    else
        node->setSize(Length::fill(), Length::fixed(thickness));

    if (renderer) {
        const auto paintBar = [horizontal](draw::Surface& surface, Rect bounds,
                                           const paint::ControlState& state) {
            // a plain 1px line (owner: no thick handle); the node itself
            // stays a few px wide so it is still easy to grab
            const Palette& pal = palette();
            const ImU32 col =
                state.active || state.hovered
                    ? pal.accent
                    : paint::withAlpha(pal.frameBright, 0x60);
            const float cx = std::round(bounds.x + bounds.w * 0.5f);
            const float cy = std::round(bounds.y + bounds.h * 0.5f);
            if (horizontal)
                surface.line({cx, bounds.y}, {cx, bounds.y + bounds.h}, col,
                             1.0f);
            else
                surface.line({bounds.x, cy}, {bounds.x + bounds.w, cy}, col,
                             1.0f);
            if (state.focused && !state.disabled)
                paint::drawFocusRing(surface, topLeftDraw(bounds),
                                     bottomRightDraw(bounds), pal, 0.0f);
        };
        VisualStyle style;
        style.kind = VisualKind::Canvas;
        style.canvasSurfaceDraw = [paintBar](draw::Surface& s, const Node&,
                                             Rect b,
                                             const paint::ControlState& st,
                                             const draw::FrameContext&) {
            paintBar(s, b, st);
        };
        style.canvasDraw = [paintBar](ImDrawList& dl, const Node&, Rect b,
                                      const paint::ControlState& st) {
            draw::ImGuiSurface surface(&dl);
            paintBar(surface, b, st);
        };
        renderer->setStyle(sid, style);
    }

    struct DragState {
        double startValue = 0.0;
        Vec2 startPos{};
        bool dragging = false;
    };
    auto drag = std::make_shared<DragState>();
    node->setOnEvent([binding, horizontal, invert, drag](Node& n,
                                                         const Event& e) {
        const auto clamped = [&](double v) {
            return std::clamp(v, binding.min, binding.max);
        };
        switch (e.type) {
        case EventType::MouseDown:
            if (e.button != MouseButton::Left || !binding.get)
                return false;
            drag->dragging = true;
            drag->startValue = binding.get();
            drag->startPos = e.position;
            return true;
        case EventType::MouseMove: {
            if (!drag->dragging || !n.pressed())
                return false;
            const float d = horizontal ? e.position.x - drag->startPos.x
                                       : e.position.y - drag->startPos.y;
            if (binding.set)
                binding.set(clamped(drag->startValue +
                                    (invert ? -(double)d : (double)d)));
            return true;
        }
        case EventType::MouseUp:
            if (e.button == MouseButton::Left && drag->dragging) {
                drag->dragging = false;
                return true;
            }
            return false;
        case EventType::KeyDown: {
            if (!binding.get || !binding.set)
                return false;
            const double kb = binding.step >= 1.0 ? binding.step : 8.0;
            double dir = 0.0;
            if (e.key == (horizontal ? Key::Left : Key::Up))
                dir = -1.0;
            else if (e.key == (horizontal ? Key::Right : Key::Down))
                dir = 1.0;
            if (dir == 0.0)
                return false;
            binding.set(clamped(binding.get() + (invert ? -dir : dir) * kb));
            return true;
        }
        default:
            return false;
        }
    });
    return node;
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
        style.canvasSurfaceDraw = [topLeftColor, topRightColor,
                                   bottomRightColor, bottomLeftColor](
                                      draw::Surface& surface, const Node&,
                                      Rect bounds, const paint::ControlState&,
                                      const draw::FrameContext&) {
            paint::drawGradientPanel(surface, topLeftDraw(bounds), sizeOfDraw(bounds),
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

namespace {

bool refreshSelectableListSelection(Node& list, const int* selected)
{
    bool changed = false;
    for (std::size_t i = 0; i < list.childCount(); ++i) {
        Node* child = list.child(i);
        if (!child || child->role() != Role::ListItem)
            continue;
        changed = setSelectedState(*child, selected && *selected == (int)i) || changed;
    }
    return changed;
}

bool setNodeName(Node& node, const std::string& name)
{
    const Semantics& current = static_cast<const Node&>(node).semantics();
    if (current.name == name)
        return false;
    Semantics next = current;
    next.name = name;
    node.setSemantics(next);
    return true;
}

struct FileBrowserEntry {
    std::string name;
    std::string path;
    bool directory = false;
};

std::string fileBrowserHomeDirectory()
{
    const char* home = std::getenv("HOME");
#if defined(_WIN32)
    if (!home)
        home = std::getenv("USERPROFILE");
#endif
    return home ? std::string(home) : std::string(".");
}

void ensureFileBrowserDirectory(FileBrowserState& state)
{
    if (state.dir.empty())
        state.dir = fileBrowserHomeDirectory();
}

bool fileBrowserMatchesExtensions(const std::filesystem::path& path,
                                  const char* extensions)
{
    if (!extensions)
        return true;
    std::string ext = path.extension().string();
    if (!ext.empty() && ext[0] == '.')
        ext.erase(0, 1);
    for (char& c : ext)
        c = (char)std::tolower((unsigned char)c);

    std::string list = extensions;
    std::size_t pos = 0;
    while (pos != std::string::npos) {
        const std::size_t comma = list.find(',', pos);
        const std::string one =
            list.substr(pos, comma == std::string::npos
                                 ? std::string::npos
                                 : comma - pos);
        if (one == ext)
            return true;
        pos = comma == std::string::npos ? comma : comma + 1;
    }
    return false;
}

std::vector<FileBrowserEntry> fileBrowserEntries(const std::string& dir,
                                                 const char* extensions)
{
    namespace fs = std::filesystem;
    std::vector<fs::directory_entry> dirs;
    std::vector<fs::directory_entry> files;

    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (!name.empty() && name[0] == '.')
            continue;

        std::error_code typeEc;
        if (it->is_directory(typeEc) && !typeEc) {
            dirs.push_back(*it);
        } else if (!typeEc && fileBrowserMatchesExtensions(it->path(), extensions)) {
            files.push_back(*it);
        }
    }

    const auto byName = [](const fs::directory_entry& a,
                           const fs::directory_entry& b) {
        return a.path().filename() < b.path().filename();
    };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);

    std::vector<FileBrowserEntry> entries;
    entries.reserve(dirs.size() + files.size());
    for (const auto& d : dirs) {
        entries.push_back({d.path().filename().string(), d.path().string(), true});
    }
    for (const auto& f : files) {
        entries.push_back({f.path().filename().string(), f.path().string(), false});
    }
    return entries;
}

std::string fileBrowserEntryId(const NodeId& rootId,
                               const FileBrowserEntry& entry)
{
    constexpr std::uint64_t kOffset = 1469598103934665603ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash = kOffset;
    const auto feed = [&](const std::string& text) {
        for (unsigned char c : text) {
            hash ^= c;
            hash *= kPrime;
        }
    };
    feed(entry.directory ? std::string("dir:") : std::string("file:"));
    feed(entry.path);

    char suffix[17];
    std::snprintf(suffix, sizeof suffix, "%016llx",
                  (unsigned long long)hash);
    return rootId + ".entry." + suffix;
}

bool refreshFileBrowserSelection(
    Node& list,
    const std::shared_ptr<const std::vector<FileBrowserEntry>>& entries,
    const FileBrowserState& state)
{
    bool changed = false;
    const std::size_t count = std::min(list.childCount(), entries->size());
    for (std::size_t i = 0; i < count; ++i) {
        Node* child = list.child(i);
        if (!child || child->role() != Role::ListItem)
            continue;
        const FileBrowserEntry& entry = (*entries)[i];
        changed = setSelectedState(*child,
                                   !entry.directory &&
                                       state.selected == entry.path) ||
                  changed;
    }
    return changed;
}

void refreshFileBrowserSelectionFor(Node& itemNode,
                                    const std::shared_ptr<const std::vector<FileBrowserEntry>>& entries,
                                    const FileBrowserState& state)
{
    if (Node* parent = itemNode.parent())
        refreshFileBrowserSelection(*parent, entries, state);
}

void selectFileBrowserEntry(FileBrowserState& state,
                            const FileBrowserEntry& entry)
{
    if (!entry.directory)
        state.selected = entry.path;
}

void activateFileBrowserEntry(FileBrowserState& state, std::string* outPath,
                              const FileBrowserEntry& entry)
{
    if (entry.directory) {
        state.dir = entry.path;
        state.selected.clear();
        return;
    }

    state.selected = entry.path;
    if (outPath)
        *outPath = entry.path;
}

void addFileBrowserActivationSemantics(Node& node)
{
    Semantics sem = static_cast<const Node&>(node).semantics();
    pushAction(sem.actions, Action::Activate);
    node.setSemantics(sem);
}

} // namespace

Node::Ptr selectableList(NodeId id, std::string name,
                         std::vector<std::string> items, int* selected,
                         PaintRenderer* renderer, Vec2 size,
                         std::function<void(Node&, int)> onSelect)
{
    if (size.x <= 0.0f)
        size.x = 180.0f;
    if (size.y <= 0.0f)
        size.y = 120.0f;

    NodeId sid = id;
    auto node = scrollView(std::move(id), 2.0f, Insets::all(4.0f), renderer);
    node->setIntrinsicSize(size);
    node->setSize(Length::fixed(size.x), Length::fixed(size.y));
    node->setSemantics(named(Role::Group, std::move(name)));

    for (int i = 0; i < (int)items.size(); ++i) {
        const NodeId itemId = sid + ".item." + std::to_string(i);
        auto item = listItem(itemId, std::move(items[(std::size_t)i]),
                             selected && *selected == i,
                             [selected, onSelect, i](Node& itemNode) {
                                 if (selected)
                                     *selected = i;
                                 if (Node* parent = itemNode.parent())
                                     refreshSelectableListSelection(*parent, selected);
                                 if (onSelect)
                                     onSelect(itemNode, i);
                             },
                             renderer);
        node->addChild(std::move(item));
    }

    node->setOnRefresh([selected](Node& n) {
        return refreshSelectableListSelection(n, selected);
    });

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Panel;
        style.panelFill = true;
        style.panelBorder = true;
        style.panelRounding = 0.0f;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr fileBrowser(NodeId id, std::string name, FileBrowserState& state,
                      std::string* outPath, PaintRenderer* renderer,
                      Vec2 size, const char* extensions)
{
    namespace fs = std::filesystem;
    if (size.x <= 0.0f)
        size.x = 260.0f;
    if (size.y <= 0.0f)
        size.y = 180.0f;

    ensureFileBrowserDirectory(state);

    NodeId sid = id;
    const NodeId headerId = sid + ".header";
    const NodeId upId = sid + ".up";
    const NodeId dirId = sid + ".dir";
    const NodeId listId = sid + ".files";
    constexpr float headerHeight = 24.0f;
    const float listHeight = std::max(24.0f, size.y - headerHeight - 4.0f);
    auto entries = std::make_shared<const std::vector<FileBrowserEntry>>(
        fileBrowserEntries(state.dir, extensions));

    auto root = column(std::move(id), 4.0f);
    root->setIntrinsicSize(size);
    root->setSize(Length::fixed(size.x), Length::fixed(size.y));
    root->setSemantics(named(Role::Group, name));

    auto header = row(headerId, 6.0f);
    header->setIntrinsicSize({size.x, headerHeight});
    header->setSize(Length::fixed(size.x), Length::fixed(headerHeight));
    header->setSemantics(named(Role::Group, std::string{}));

    auto up = outlineButton(
        upId, "^ up",
        [&state](Node&) {
            const fs::path parent = fs::path(state.dir).parent_path();
            if (!parent.empty())
                state.dir = parent.string();
            state.selected.clear();
        },
        renderer, {54.0f, headerHeight});

    auto dirLabel = label(dirId, state.dir, renderer);
    dirLabel->setIntrinsicSize({std::max(24.0f, size.x - 60.0f),
                                headerHeight});
    dirLabel->setSize(Length::fill(), Length::fixed(headerHeight));
    dirLabel->setOnRefresh([&state](Node& n) {
        return setNodeName(n, state.dir);
    });

    header->addChild(std::move(up));
    header->addChild(std::move(dirLabel));

    auto list = scrollView(listId, 2.0f, Insets::all(4.0f), renderer);
    list->setIntrinsicSize({size.x, listHeight});
    list->setSize(Length::fixed(size.x), Length::fixed(listHeight));
    list->setSemantics(named(Role::Group, name.empty() ? "Files"
                                                       : name + " files"));

    for (const FileBrowserEntry& entry : *entries) {
        const std::string itemLabel =
            entry.directory ? "[dir] " + entry.name : entry.name;
        auto item = listItem(
            fileBrowserEntryId(sid, entry), itemLabel,
            !entry.directory && state.selected == entry.path,
            {},
            renderer);
        addFileBrowserActivationSemantics(*item);
        auto suppressPointerActivate = std::make_shared<bool>(false);
        auto handledPointerDoubleClick = std::make_shared<bool>(false);
        item->setOnEvent(
            [&state, outPath, entries, entry, suppressPointerActivate,
             handledPointerDoubleClick](Node& itemNode, const Event& event) {
                if (event.button != MouseButton::Left)
                    return false;

                if (event.type == EventType::MouseDown) {
                    *suppressPointerActivate = true;
                    *handledPointerDoubleClick = event.clickCount >= 2;
                    if (*handledPointerDoubleClick) {
                        activateFileBrowserEntry(state, outPath, entry);
                        refreshFileBrowserSelectionFor(itemNode, entries, state);
                        return true;
                    }
                    return false;
                }

                if (event.type != EventType::MouseUp)
                    return false;

                if (!itemNode.bounds().contains(event.position)) {
                    *suppressPointerActivate = false;
                    *handledPointerDoubleClick = false;
                    return false;
                }

                if (event.clickCount >= 2) {
                    if (!*handledPointerDoubleClick) {
                        activateFileBrowserEntry(state, outPath, entry);
                        refreshFileBrowserSelectionFor(itemNode, entries, state);
                    }
                    *handledPointerDoubleClick = false;
                    *suppressPointerActivate = true;
                    return true;
                }

                selectFileBrowserEntry(state, entry);
                refreshFileBrowserSelectionFor(itemNode, entries, state);
                return false;
            });
        item->setOnAction(
            [&state, outPath, entries, entry, suppressPointerActivate,
             handledPointerDoubleClick](Node& itemNode, Action action, double) {
                if (action != Action::Activate)
                    return false;

                if (*suppressPointerActivate) {
                    *suppressPointerActivate = false;
                    *handledPointerDoubleClick = false;
                    return true;
                }

                activateFileBrowserEntry(state, outPath, entry);
                refreshFileBrowserSelectionFor(itemNode, entries, state);
                return true;
            });
        list->addChild(std::move(item));
    }

    list->setOnRefresh([entries, &state](Node& n) {
        return refreshFileBrowserSelection(n, entries, state);
    });

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Panel;
        style.panelFill = true;
        style.panelBorder = true;
        style.panelRounding = 0.0f;
        renderer->setStyle(listId, style);
    }

    root->addChild(std::move(header));
    root->addChild(std::move(list));
    return root;
}

Node::Ptr menuItem(NodeId id, MenuItem item,
                   std::function<void(Node&, const MenuItem&, int)> onSelect,
                   PaintRenderer* renderer, int index, float width)
{
    return makeMenuItemNode(std::move(id), std::move(item), std::move(onSelect),
                            renderer, index, width, nullptr);
}

Node::Ptr popupMenu(NodeId id, PopupMenuState* state,
                    const std::vector<MenuItem>& items,
                    std::function<void(Node&, const MenuItem&, int)> onSelect,
                    PaintRenderer* renderer, float width)
{
    NodeId sid = id;
    auto node = column(std::move(id), 0.0f, Insets::all(4.0f));
    node->setSize(Length::fixed(width + 8.0f), Length::intrinsic());
    node->setOverlay(true);
    Semantics sem = named(Role::Menu, "Menu");
    node->setSemantics(sem);
    setMenuVisibleFromState(*node, state);
    node->setIntrinsicSize({width + 8.0f, retainedVisibleMenuHeight(items, state)});
    node->setOnRefresh([state, items, width](Node& n) {
        const bool wasVisible = n.visible();
        setMenuVisibleFromState(n, state);
        const Vec2 before = n.intrinsicSize();
        const Vec2 next{width + 8.0f, retainedVisibleMenuHeight(items, state)};
        n.setIntrinsicSize(next);
        return wasVisible != n.visible() ||
               before.x != next.x || before.y != next.y;
    });

    addMenuItemNodes(*node, sid, items, onSelect, renderer, width, state);

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Panel;
        style.panelFill = true;
        style.panelBorder = true;
        style.panelRounding = 0.0f; // menus are square
        style.popupState = state;
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr dropdownMenu(NodeId id, std::string name, PopupMenuState& state,
                       const std::vector<MenuItem>& items, int* selectedIndex,
                       std::function<void(Node&, const MenuItem&, int)> onSelect,
                       PaintRenderer* renderer, Vec2 buttonSize, float menuWidth,
                       paint::OutlineButtonStyle buttonStyle)
{
    NodeId sid = id;
    const NodeId buttonId = sid + ".button";
    const NodeId menuId = sid + ".menu";
    auto root = column(std::move(id), 2.0f);
    root->setSize(Length::intrinsic(), Length::intrinsic());
    root->setIntrinsicSize(buttonSize);
    Semantics rootSem = named(Role::Group, name);
    root->setSemantics(rootSem);

    auto button = outlineButton(buttonId, selectedMenuLabel(name, items, selectedIndex),
                                {}, renderer, buttonSize, buttonStyle);
    setDropdownButtonSemantics(*button, selectedMenuLabel(name, items, selectedIndex),
                               state.open);
    button->setOnActivate([&state, menuId, selectedIndex](Node& n) {
        state.open = !state.open;
        state.anchorToPosition = false;
        state.highlightedIndex = state.open && selectedIndex ? *selectedIndex : -1;
        setDropdownButtonSemantics(n, n.semantics().name, state.open);
        if (Node* parent = n.parent())
            if (Node* menu = parent->find(menuId))
                menu->setVisible(state.open);
    });
    button->setOnAction([&state, menuId, selectedIndex](Node& n, Action action, double) {
        if (action != Action::OpenMenu)
            return false;
        state.open = true;
        state.anchorToPosition = false;
        state.highlightedIndex = selectedIndex ? *selectedIndex : -1;
        setDropdownButtonSemantics(n, n.semantics().name, state.open);
        if (Node* parent = n.parent())
            if (Node* menu = parent->find(menuId))
                menu->setVisible(state.open);
        return true;
    });
    button->setOnRefresh([&state, selectedIndex, items, name](Node& n) {
        const std::string label = selectedMenuLabel(name, items, selectedIndex);
        const bool changed = n.semantics().name != label ||
                             hasState(n.semantics().states,
                                      SemanticState::Expanded) != state.open;
        if (changed)
            setDropdownButtonSemantics(n, label, state.open);
        return changed;
    });

    std::vector<MenuItem> resolved = items;
    if (selectedIndex && *selectedIndex >= 0 &&
        *selectedIndex < (int)resolved.size())
        resolved[(std::size_t)*selectedIndex].checked = true;

    auto menu = popupMenu(menuId, &state, resolved,
                          [selectedIndex, onSelect](Node& n, const MenuItem& item, int index) {
                              if (selectedIndex)
                                  *selectedIndex = index;
                              if (onSelect)
                                  onSelect(n, item, index);
                          },
                          renderer, menuWidth);
    root->addChild(std::move(button));
    root->addChild(std::move(menu));
    return root;
}

Node::Ptr dropdownMenu(NodeId id, std::string name, PopupMenuState& state,
                       const std::vector<MenuItem>& items, ValueBinding binding,
                       std::function<void(Node&, const MenuItem&, int)> onSelect,
                       PaintRenderer* renderer, Vec2 buttonSize, float menuWidth,
                       paint::OutlineButtonStyle buttonStyle)
{
    // A heap int mirrors the binding: the int* impl reads it for the label,
    // this overload keeps it synced from binding.get() each frame and writes
    // binding.set() on pick.
    auto idx = std::make_shared<int>(
        binding.get ? (int)std::lround(binding.get()) : 0);
    auto user = std::move(onSelect);
    auto onPick = [idx, binding, user](Node& n, const MenuItem& it,
                                       int i) mutable {
        *idx = i;
        if (binding.set)
            binding.set((double)i);
        if (user)
            user(n, it, i);
    };
    auto root = dropdownMenu(std::move(id), std::move(name), state, items,
                             idx.get(), std::move(onPick), renderer, buttonSize,
                             menuWidth, buttonStyle);
    // sync the mirror from the binding before the button's own onRefresh reads
    // it (refresh is top-down, so the root runs first)
    root->setOnRefresh([idx, binding](Node&) {
        if (!binding.get)
            return false;
        const int v = (int)std::lround(binding.get());
        if (v == *idx)
            return false;
        *idx = v;
        return true;
    });
    return root;
}

Node::Ptr contextMenuRegion(NodeId id, std::string name, Vec2 intrinsicSize,
                            PopupMenuState& state,
                            std::function<void(Node&, Vec2)> onOpen,
                            PaintRenderer* renderer,
                            VisualStyle::CanvasDraw draw,
                            bool focusable, Role semanticRole)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), semanticRole);
    node->setIntrinsicSize(intrinsicSize);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(focusable);
    Semantics sem = named(semanticRole, name);
    sem.actions.push_back(Action::OpenMenu);
    node->setSemantics(sem);
    node->setOnEvent([&state, onOpen](Node& n, const Event& event) {
        if (event.type != EventType::ContextMenu)
            return false;
        state.open = true;
        state.anchorToPosition = true;
        state.highlightedIndex = -1;
        state.position = ImVec2(event.position.x, event.position.y);
        if (onOpen)
            onOpen(n, event.position);
        return true;
    });
    node->setOnAction([&state, onOpen](Node& n, Action action, double) {
        if (action != Action::OpenMenu)
            return false;
        state.open = true;
        state.anchorToPosition = true;
        state.highlightedIndex = -1;
        const Rect b = n.bounds();
        Vec2 pos{b.x + b.w * 0.5f, b.y + b.h * 0.5f};
        state.position = ImVec2(pos.x, pos.y);
        if (onOpen)
            onOpen(n, pos);
        return true;
    });

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Canvas;
        style.panelBorder = !draw;
        style.canvasDraw = std::move(draw);
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr contextMenuRegion(NodeId id, std::string name, Vec2 intrinsicSize,
                            PopupMenuState& state,
                            std::function<void(Node&, Vec2)> onOpen,
                            PaintRenderer* renderer,
                            VisualStyle::CanvasSurfaceDraw draw,
                            bool focusable, Role semanticRole)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), semanticRole);
    node->setIntrinsicSize(intrinsicSize);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(focusable);
    Semantics sem = named(semanticRole, name);
    sem.actions.push_back(Action::OpenMenu);
    node->setSemantics(sem);
    node->setOnEvent([&state, onOpen](Node& n, const Event& event) {
        if (event.type != EventType::ContextMenu)
            return false;
        state.open = true;
        state.anchorToPosition = true;
        state.highlightedIndex = -1;
        state.position = ImVec2(event.position.x, event.position.y);
        if (onOpen)
            onOpen(n, event.position);
        return true;
    });
    node->setOnAction([&state, onOpen](Node& n, Action action, double) {
        if (action != Action::OpenMenu)
            return false;
        state.open = true;
        state.anchorToPosition = true;
        state.highlightedIndex = -1;
        const Rect b = n.bounds();
        Vec2 pos{b.x + b.w * 0.5f, b.y + b.h * 0.5f};
        state.position = ImVec2(pos.x, pos.y);
        if (onOpen)
            onOpen(n, pos);
        return true;
    });

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Canvas;
        style.panelBorder = !draw;
        style.canvasSurfaceDraw = std::move(draw);
        renderer->setStyle(sid, style);
    }
    return node;
}

namespace {

ModalDialogResult resultForButtonRole(ModalButtonRole role)
{
    switch (role) {
    case ModalButtonRole::Primary: return ModalDialogResult::Primary;
    case ModalButtonRole::Secondary: return ModalDialogResult::Secondary;
    case ModalButtonRole::Cancel: return ModalDialogResult::Cancel;
    case ModalButtonRole::Destructive: return ModalDialogResult::Destructive;
    }
    return ModalDialogResult::None;
}

paint::OutlineButtonStyle modalButtonStyle(ModalButtonRole role)
{
    const Palette& pal = palette();
    paint::OutlineButtonStyle style;
    style.rounding = 0.0f;
    style.border = paint::withAlpha(pal.frameBright, 0xbb);
    style.hoverBorder = pal.accent;
    style.activeBorder = pal.accent;
    style.activeFill = paint::withAlpha(pal.accent, 0x33);
    style.text = pal.text;
    if (role == ModalButtonRole::Primary) {
        style.border = pal.accent;
        style.activeFill = paint::withAlpha(pal.accent, 0x44);
    } else if (role == ModalButtonRole::Destructive) {
        style.border = IM_COL32(190, 74, 70, 220);
        style.hoverBorder = IM_COL32(238, 92, 86, 255);
        style.activeBorder = IM_COL32(238, 92, 86, 255);
        style.activeFill = IM_COL32(238, 92, 86, 52);
        style.text = IM_COL32(255, 188, 184, 255);
    }
    return style;
}

void dismissModal(ModalDialogState& state, ModalDialogResult result,
                  std::string action)
{
    state.lastResult = result;
    state.lastAction = std::move(action);
    state.open = false;
}

void activateModalButton(ModalDialogState& state,
                         const ModalDialogButton& button)
{
    state.lastResult = resultForButtonRole(button.role);
    state.lastAction = button.id;
    if (button.closes)
        state.open = false;
    if (button.onActivate)
        button.onActivate();
}

bool activateFirstModalRole(ModalDialogState& state,
                            const std::vector<ModalDialogButton>& buttons,
                            ModalButtonRole primary,
                            ModalButtonRole fallback)
{
    auto findRole = [&](ModalButtonRole role) {
        return std::find_if(buttons.begin(), buttons.end(),
                            [&](const ModalDialogButton& b) {
                                return b.role == role;
                            });
    };
    auto it = findRole(primary);
    if (it == buttons.end())
        it = findRole(fallback);
    if (it == buttons.end())
        return false;
    activateModalButton(state, *it);
    return true;
}

void syncModalRootFromState(Node& node, const ModalDialogState& state)
{
    for (Node* n = &node; n != nullptr; n = n->parent()) {
        if (n->modal() || n->overlay()) {
            n->setVisible(state.open);
            n->setModal(state.open);
            return;
        }
    }
}

} // namespace

Node::Ptr modalDialog(NodeId id, std::string title, std::string message,
                      ModalDialogState& state,
                      std::vector<ModalDialogButton> buttons,
                      PaintRenderer* renderer,
                      ModalDialogOptions options)
{
    NodeId sid = id;
    const NodeId dialogId = sid + ".dialog";
    auto buttonDefs =
        std::make_shared<std::vector<ModalDialogButton>>(std::move(buttons));

    Layout overlayLayout;
    overlayLayout.kind = LayoutKind::Stack;
    overlayLayout.mainAlign = Align::Center;
    overlayLayout.crossAlign = Align::Center;
    auto overlay = Node::make(std::move(id), Role::Group);
    overlay->setLayout(overlayLayout);
    overlay->setSize(Length::fill(), Length::fill());
    overlay->setIntrinsicSize(options.size);
    overlay->setOverlay(true);
    overlay->setVisible(state.open);
    overlay->setModal(state.open);
    overlay->setSemantics(named(Role::Group, std::string{}));

    overlay->setOnRefresh([&state](Node& n) {
        const bool changed = n.visible() != state.open ||
                             n.modal() != state.open;
        n.setVisible(state.open);
        n.setModal(state.open);
        return changed;
    });

    overlay->setOnEvent([&state, buttonDefs, dialogId, options](Node& n,
                                                                const Event& e) {
        if (!state.open)
            return false;
        if (e.type == EventType::KeyDown && e.key == Key::Escape) {
            switch (options.escapePolicy) {
            case ModalEscapePolicy::Ignore:
                return true;
            case ModalEscapePolicy::Primary:
                if (activateFirstModalRole(state, *buttonDefs,
                                           ModalButtonRole::Primary,
                                           ModalButtonRole::Secondary)) {
                    syncModalRootFromState(n, state);
                    return true;
                }
                dismissModal(state, ModalDialogResult::Dismissed, "escape");
                syncModalRootFromState(n, state);
                return true;
            case ModalEscapePolicy::Cancel:
                if (activateFirstModalRole(state, *buttonDefs,
                                           ModalButtonRole::Cancel,
                                           ModalButtonRole::Secondary)) {
                    syncModalRootFromState(n, state);
                    return true;
                }
                dismissModal(state, ModalDialogResult::Dismissed, "escape");
                syncModalRootFromState(n, state);
                return true;
            case ModalEscapePolicy::Close:
                dismissModal(state, ModalDialogResult::Dismissed, "escape");
                syncModalRootFromState(n, state);
                return true;
            }
        }

        if (e.type == EventType::MouseMove ||
            e.type == EventType::MouseWheel ||
            e.type == EventType::ContextMenu ||
            e.type == EventType::MouseDown ||
            e.type == EventType::MouseUp) {
            if (e.type == EventType::MouseUp && e.button == MouseButton::Left &&
                options.closeOnScrimClick) {
                const Node* dialog = n.find(dialogId);
                if (!dialog || !dialog->bounds().contains(e.position)) {
                    dismissModal(state, ModalDialogResult::Dismissed, "scrim");
                    syncModalRootFromState(n, state);
                }
            }
            return true;
        }
        return false;
    });

    if (renderer) {
        VisualStyle scrim;
        scrim.kind = VisualKind::Canvas;
        scrim.panelBorder = false;
        scrim.canvasClip = false;
        scrim.canvasFocusRing = false;
        scrim.canvasSurfaceDraw =
            [color = options.scrimColor](draw::Surface& surface,
                                         const Node&, Rect bounds,
                                         const paint::ControlState&,
                                         const draw::FrameContext&) {
                surface.fillRect(topLeftDraw(bounds), bottomRightDraw(bounds),
                                 color, 0.0f);
            };
        renderer->setStyle(sid, scrim);
    }

    Layout dialogLayout;
    dialogLayout.kind = LayoutKind::Column;
    dialogLayout.padding = Insets::all(16.0f);
    dialogLayout.gap = 10.0f;
    dialogLayout.crossAlign = Align::Stretch;
    auto dialog = Node::make(dialogId, options.role);
    dialog->setLayout(dialogLayout);
    dialog->setSize(Length::fixed(options.size.x), Length::fixed(options.size.y));
    dialog->setIntrinsicSize(options.size);
    dialog->setFocusable(true);
    Semantics dialogSem = named(options.role, title);
    dialogSem.description = message;
    dialog->setSemantics(dialogSem);

    if (renderer) {
        VisualStyle panelStyle;
        panelStyle.kind = VisualKind::Canvas;
        panelStyle.canvasClip = false;
        panelStyle.panelBorder = false;
        panelStyle.canvasFocusRing = true;
        panelStyle.canvasSurfaceDraw =
            [fill = options.panelFill, border = options.panelBorder](
                draw::Surface& surface, const Node&, Rect bounds,
                const paint::ControlState&, const draw::FrameContext&) {
                const Palette& pal = palette();
                surface.fillRect(topLeftDraw(bounds), bottomRightDraw(bounds),
                                 fill ? fill : pal.frame, 0.0f);
                surface.strokeRect(topLeftDraw(bounds), bottomRightDraw(bounds),
                                   border ? border : pal.frameBright, 0.0f);
            };
        renderer->setStyle(dialogId, panelStyle);
    }

    auto titleNode = label(sid + ".title", std::move(title), renderer);
    titleNode->setSize(Length::fill(), Length::fixed(22.0f));
    titleNode->setIntrinsicSize({std::max(40.0f, options.size.x - 32.0f), 22.0f});

    auto messageNode = label(sid + ".message", std::move(message), renderer);
    messageNode->setSize(Length::fill(), Length::fill());
    messageNode->setIntrinsicSize({std::max(40.0f, options.size.x - 32.0f), 52.0f});

    Layout buttonLayout;
    buttonLayout.kind = LayoutKind::Row;
    buttonLayout.gap = 8.0f;
    buttonLayout.mainAlign = Align::End;
    buttonLayout.crossAlign = Align::Center;
    auto buttonRow = Node::make(sid + ".buttons", Role::Group);
    buttonRow->setLayout(buttonLayout);
    buttonRow->setSize(Length::fill(), Length::fixed(30.0f));
    buttonRow->setIntrinsicSize({std::max(40.0f, options.size.x - 32.0f), 30.0f});
    buttonRow->setSemantics(named(Role::Group, std::string{}));

    for (std::size_t i = 0; i < buttonDefs->size(); ++i) {
        ModalDialogButton& def = (*buttonDefs)[i];
        if (def.id.empty())
            def.id = "button" + std::to_string(i);
        const NodeId buttonId = sid + "." + def.id;
        const float buttonW =
            std::max(74.0f, 18.0f + (float)def.label.size() * 7.0f);
        auto buttonNode = outlineButton(
            buttonId, def.label,
            [&state, buttonDefs, i](Node& n) {
                activateModalButton(state, (*buttonDefs)[i]);
                syncModalRootFromState(n, state);
            },
            renderer, {buttonW, 28.0f}, modalButtonStyle(def.role));
        buttonRow->addChild(std::move(buttonNode));
    }

    dialog->addChild(std::move(titleNode));
    dialog->addChild(std::move(messageNode));
    dialog->addChild(std::move(buttonRow));
    overlay->addChild(std::move(dialog));
    return overlay;
}

Node::Ptr alertDialog(NodeId id, std::string title, std::string message,
                      ModalDialogState& state,
                      std::function<void()> onOk,
                      PaintRenderer* renderer,
                      ModalDialogOptions options)
{
    options.role = Role::Alert;
    options.escapePolicy = ModalEscapePolicy::Primary;
    std::vector<ModalDialogButton> buttons = {
        {"ok", "OK", ModalButtonRole::Primary, true, std::move(onOk)},
    };
    return modalDialog(std::move(id), std::move(title), std::move(message),
                       state, std::move(buttons), renderer, options);
}

Node::Ptr confirmDialog(NodeId id, std::string title, std::string message,
                        ModalDialogState& state,
                        std::function<void()> onConfirm,
                        std::function<void()> onCancel,
                        PaintRenderer* renderer,
                        ModalDialogOptions options)
{
    options.role = Role::Dialog;
    options.escapePolicy = ModalEscapePolicy::Cancel;
    std::vector<ModalDialogButton> buttons = {
        {"cancel", "Cancel", ModalButtonRole::Cancel, true, std::move(onCancel)},
        {"confirm", "Confirm", ModalButtonRole::Primary, true,
         std::move(onConfirm)},
    };
    return modalDialog(std::move(id), std::move(title), std::move(message),
                       state, std::move(buttons), renderer, options);
}

Node::Ptr patternGrid(NodeId id, std::string name, bool* cells, int rows, int steps,
                      PaintRenderer* renderer, Vec2 size,
                      std::function<int()> playheadStep,
                      paint::PatternCellPainter cellPainter)
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
        paint::PatternCellPainter surfaceCellPainter = cellPainter;
        style.canvasDraw = [binding, cellPainter = std::move(cellPainter)](
                               ImDrawList& dl, const Node&, Rect bounds,
                               const paint::ControlState& state) {
            paint::ControlState inner = state;
            inner.focused = false;
            paint::drawPatternGrid(&dl, topLeft(bounds), sizeOf(bounds),
                                   binding->cells, binding->rows, binding->steps,
                                   patternPlayhead(*binding), palette(), inner,
                                   cellPainter);
        };
        style.canvasSurfaceDraw =
            [binding, cellPainter = std::move(surfaceCellPainter)](
                draw::Surface& surface, const Node&, Rect bounds,
                const paint::ControlState& state, const draw::FrameContext&) {
                paint::ControlState inner = state;
                inner.focused = false;
                paint::drawPatternGrid(surface, topLeftDraw(bounds), sizeOfDraw(bounds),
                                       binding->cells, binding->rows, binding->steps,
                                       patternPlayhead(*binding), palette(), inner,
                                       cellPainter);
            };
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr xyPad(NodeId id, std::string name, ValueBinding xBinding,
                ValueBinding yBinding, PaintRenderer* renderer, Vec2 size,
                paint::XYPadPainter painter)
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
        paint::XYPadPainter surfacePainter = painter;
        style.canvasDraw = [bindings, painter = std::move(painter)](
                               ImDrawList& dl, const Node&, Rect bounds,
                               const paint::ControlState& state) {
            paint::ControlState inner = state;
            inner.focused = false;
            paint::XYPadPaintArgs args;
            args.drawList = &dl;
            args.topLeft = topLeft(bounds);
            args.size = sizeOf(bounds);
            args.x = (float)readBinding(bindings->x);
            args.y = (float)readBinding(bindings->y);
            args.palette = &palette();
            args.state = &inner;
            paint::drawXYPadWithPainter(args, painter);
        };
        style.canvasSurfaceDraw =
            [bindings, painter = std::move(surfacePainter)](
                draw::Surface& surface, const Node&, Rect bounds,
                const paint::ControlState& state, const draw::FrameContext&) {
                paint::ControlState inner = state;
                inner.focused = false;
                paint::XYPadPaintArgs args;
                args.topLeft = topLeft(bounds);
                args.size = sizeOf(bounds);
                args.x = (float)readBinding(bindings->x);
                args.y = (float)readBinding(bindings->y);
                args.palette = &palette();
                args.state = &inner;
                args.surface = &surface;
                paint::drawXYPadWithPainter(args, painter);
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
        style.canvasSurfaceDraw =
            [binding](draw::Surface& surface, const Node&, Rect bounds,
                      const paint::ControlState& state, const draw::FrameContext&) {
                paint::ControlState inner = state;
                inner.focused = false;
                paint::drawKeyboard(surface, topLeftDraw(bounds), sizeOfDraw(bounds),
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
        style.canvasSurfaceDraw =
            [binding](draw::Surface& surface, const Node&, Rect bounds,
                      const paint::ControlState& state,
                      const draw::FrameContext& context) {
                ensureEnvelopePoints(*binding);
                if (!binding->points)
                    return;
                const Vec2 pointer{context.pointer.x, context.pointer.y};
                paint::ControlState inner = state;
                inner.focused = false;
                const int hotPoint = state.hovered && context.pointerValid
                                         ? envelopeHotPoint(*binding, bounds, pointer)
                                         : -1;
                const int hotSegment = state.hovered && context.pointerValid &&
                                               hotPoint < 0
                                           ? envelopeHotSegment(*binding, bounds, pointer)
                                           : -1;
                paint::drawEnvelope(surface, topLeftDraw(bounds), sizeOfDraw(bounds),
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

Node::Ptr valueField(NodeId id, std::string name, ValueBinding binding,
                     PaintRenderer* renderer, Vec2 size,
                     paint::OutlineButtonStyle fieldStyle, double dragSpeed)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::Slider);
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);
    Semantics sem = named(Role::Slider, name);
    sem.description = "Editable numeric value field";
    setBindingSemantics(sem, binding);
    node->setSemantics(sem);
    node->setValueBinding(binding);
    installValueFieldPointerBehavior(*node, dragSpeed);

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Canvas;
        style.canvasDraw = [binding, fieldStyle](ImDrawList& dl, const Node& node,
                                                 Rect bounds,
                                                 const paint::ControlState& state) mutable {
            const NodeId& id = node.id();
            if (valueFieldEditing(id)) {
                ValueFieldBuffer& buf = valueFieldBuffer(id);
                ImGui::SetCursorScreenPos(ImVec2(bounds.x + 1.0f, bounds.y + 1.0f));
                ImGui::SetNextItemWidth(std::max(8.0f, bounds.w - 2.0f));
                ImGui::PushID(id.c_str());
                if (focusValueFields().erase(id) > 0)
                    ImGui::SetKeyboardFocusHere();
                const bool commit =
                    ImGui::InputText("##value", buf.data(), buf.size(),
                                     ImGuiInputTextFlags_EnterReturnsTrue |
                                         ImGuiInputTextFlags_AutoSelectAll);
                const bool cancel = ImGui::IsItemActive() &&
                                    ImGui::IsKeyPressed(ImGuiKey_Escape, false);
                const bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
                ImGui::PopID();
                if (cancel)
                    cancelValueFieldEdit(id);
                else if (commit || deactivated)
                    commitValueFieldEdit(id, binding);
                return;
            }

            paint::ControlState inner = state;
            const std::string valueText = nodeValueText(node, nullptr);
            paint::drawOutlineButton(&dl, ImGui::GetFont(), topLeft(bounds),
                                     sizeOf(bounds), valueText.c_str(), palette(),
                                     inner, fieldStyle);
        };
        style.canvasSurfaceDraw =
            [binding, fieldStyle](draw::Surface& surface, const Node& node,
                                  Rect bounds, const paint::ControlState& state,
                                  const draw::FrameContext& context) mutable {
                paint::ControlState inner = state;
                const NodeId& id = node.id();
                std::string valueText;
                if (valueFieldEditing(id)) {
                    ValueFieldBuffer& buf = valueFieldBuffer(id);
                    valueText = buf.data();
                } else {
                    valueText = nodeValueText(node, nullptr);
                }
                const float scale = fieldStyle.fontScale > 0.0f
                                        ? fieldStyle.fontScale
                                        : 0.90f;
                paint::drawOutlineButton(surface, context.font,
                                         context.fontSizePx * scale,
                                         topLeftDraw(bounds), sizeOfDraw(bounds),
                                         valueText.c_str(), palette(), inner,
                                         fieldStyle);
                (void)binding;
            };
        renderer->setStyle(sid, style);
    }
    return node;
}

namespace {

// Per-node text-field edit state (caret/selection + the value at focus, for
// Esc). Keyed by NodeId so it survives tree rebuilds.
struct TextFieldEdit {
    int cursor = 0;
    int anchor = 0; // == cursor when there's no selection
    std::string preEdit;
};
std::unordered_map<NodeId, TextFieldEdit>& textFieldEdits()
{
    static std::unordered_map<NodeId, TextFieldEdit> m;
    return m;
}
int clampCaret(int c, const std::string& s)
{
    return std::clamp(c, 0, (int)s.size());
}
// caret byte-index nearest to a local x (0 = field left inset), measuring
// prefixes with the current ImGui font (available in onEvent + render).
int caretFromX(const std::string& s, float localX, float fontSize)
{
    ImFont* f =
        ImGui::GetCurrentContext() ? ImGui::GetFont() : nullptr;
    if (!f)
        return (int)s.size(); // no live frame: caret at end
    float best = 1e9f;
    int bestI = 0;
    for (int i = 0; i <= (int)s.size(); ++i) {
        const ImVec2 w = f->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, s.c_str(),
                                          s.c_str() + i);
        const float d = std::abs(w.x - localX);
        if (d < best) { best = d; bestI = i; }
    }
    return bestI;
}

} // namespace

Node::Ptr textField(NodeId id, std::string* text, PaintRenderer* renderer,
                    Vec2 size, std::string placeholder,
                    std::function<void(const std::string&)> onCommit)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::Slider); // text-entry control
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);
    Semantics sem = named(Role::Slider, placeholder.empty() ? "Text field"
                                                            : placeholder);
    sem.description = "Editable text field";
    node->setSemantics(sem);

    constexpr float kPadX = 7.0f;

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Canvas;
        style.canvasClip = true;
        style.canvasFocusRing = false; // the field draws its own focus border
        style.canvasSurfaceDraw =
            [sid, text, placeholder](draw::Surface& surface, const Node&,
                                     Rect bounds, const paint::ControlState& st,
                                     const draw::FrameContext& context) {
                const Palette& pal = palette();
                const draw::Vec2 tl{bounds.x, bounds.y};
                const draw::Vec2 br{bounds.x + bounds.w, bounds.y + bounds.h};
                surface.fillRect(tl, br, pal.frame, 0.0f);
                surface.strokeRect(tl, br,
                                   st.focused ? pal.accent : pal.frameBright, 0.0f);
                const float fs =
                    context.fontSizePx > 0.0f ? context.fontSizePx : 13.0f;
                const float ty = std::round(bounds.y + (bounds.h - fs) * 0.5f);
                const float tx = bounds.x + kPadX;
                surface.pushClip({bounds.x + 2.0f, bounds.y}, {br.x - 2.0f, br.y},
                                 true);
                const std::string& s = *text;
                if (s.empty() && !st.focused && !placeholder.empty()) {
                    surface.text(context.font, fs, {tx, ty}, pal.textDim,
                                 placeholder.c_str());
                } else {
                    const auto it = textFieldEdits().find(sid);
                    if (st.focused && it != textFieldEdits().end() &&
                        it->second.cursor != it->second.anchor) {
                        const int a =
                            std::min(it->second.cursor, it->second.anchor);
                        const int b =
                            std::max(it->second.cursor, it->second.anchor);
                        const draw::Vec2 wa = surface.measureText(
                            context.font, fs, s.substr(0, (size_t)a).c_str());
                        const draw::Vec2 wb = surface.measureText(
                            context.font, fs, s.substr(0, (size_t)b).c_str());
                        surface.fillRect({tx + wa.x, ty}, {tx + wb.x, ty + fs},
                                         paint::withAlpha(pal.accent, 0x55),
                                         0.0f);
                    }
                    if (!s.empty())
                        surface.text(context.font, fs, {tx, ty}, pal.text,
                                     s.c_str());
                    if (st.focused && it != textFieldEdits().end() &&
                        std::fmod(context.timeSeconds, 1.0) < 0.55) {
                        const draw::Vec2 wc = surface.measureText(
                            context.font, fs,
                            s.substr(0, (size_t)clampCaret(it->second.cursor, s))
                                .c_str());
                        const float cx = std::round(tx + wc.x);
                        surface.line({cx, ty}, {cx, ty + fs}, pal.text, 1.0f);
                    }
                }
                surface.popClip();
            };
        renderer->setStyle(sid, style);
    }

    node->setOnEvent([sid, text, onCommit, kPadX](Node& n, const Event& e) {
        TextFieldEdit& ed = textFieldEdits()[sid];
        const auto selMin = [&] { return std::min(ed.cursor, ed.anchor); };
        const auto selMax = [&] { return std::max(ed.cursor, ed.anchor); };
        const auto deleteSel = [&] {
            if (ed.cursor == ed.anchor)
                return false;
            const int a = selMin(), b = selMax();
            text->erase((size_t)a, (size_t)(b - a));
            ed.cursor = ed.anchor = a;
            return true;
        };
        switch (e.type) {
        case EventType::MouseDown: {
            if (e.button != MouseButton::Left)
                return false;
            ed.preEdit = *text;
            const float fs =
                ImGui::GetCurrentContext() ? ImGui::GetFontSize() : 13.0f;
            const float localX = e.position.x - (n.bounds().x + kPadX);
            ed.cursor = ed.anchor =
                caretFromX(*text, std::max(0.0f, localX), fs);
            return true;
        }
        case EventType::TextInput:
            if (e.text.empty())
                return false;
            deleteSel();
            text->insert((size_t)clampCaret(ed.cursor, *text), e.text);
            ed.cursor = clampCaret(ed.cursor + (int)e.text.size(), *text);
            ed.anchor = ed.cursor;
            return true;
        case EventType::KeyDown:
            switch (e.key) {
            case Key::Left:
                ed.cursor = clampCaret(ed.cursor - 1, *text);
                if (!e.shift) ed.anchor = ed.cursor;
                return true;
            case Key::Right:
                ed.cursor = clampCaret(ed.cursor + 1, *text);
                if (!e.shift) ed.anchor = ed.cursor;
                return true;
            case Key::Home:
                ed.cursor = 0;
                if (!e.shift) ed.anchor = 0;
                return true;
            case Key::End:
                ed.cursor = (int)text->size();
                if (!e.shift) ed.anchor = ed.cursor;
                return true;
            case Key::Backspace:
                if (!deleteSel() && ed.cursor > 0) {
                    text->erase((size_t)(ed.cursor - 1), 1);
                    ed.cursor = ed.anchor = ed.cursor - 1;
                }
                return true;
            case Key::Delete:
                if (!deleteSel() && ed.cursor < (int)text->size())
                    text->erase((size_t)ed.cursor, 1);
                return true;
            case Key::Enter:
                if (onCommit) onCommit(*text);
                return true;
            case Key::Escape:
                *text = ed.preEdit;
                ed.cursor = ed.anchor = clampCaret(ed.cursor, *text);
                return true;
            default:
                return false;
            }
        default:
            return false;
        }
    });
    return node;
}

Node::Ptr canvas(NodeId id, std::string name, Vec2 intrinsicSize,
                 VisualStyle::CanvasDraw draw,
                 PaintRenderer* renderer, bool focusable, Role semanticRole,
                 bool panelBorder)
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
        style.panelBorder = panelBorder;
        style.canvasDraw = std::move(draw);
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr canvas(NodeId id, std::string name, Vec2 intrinsicSize,
                 VisualStyle::CanvasSurfaceDraw draw,
                 PaintRenderer* renderer, bool focusable, Role semanticRole,
                 bool panelBorder)
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
        style.panelBorder = panelBorder;
        style.canvasSurfaceDraw = std::move(draw);
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr graphSurface(NodeId id, std::string name, GraphSurfaceState& state,
                       const std::vector<GraphNode>& nodes,
                       const std::vector<GraphCable>& cables,
                       GraphSurfaceCallbacks callbacks,
                       PaintRenderer* renderer, Vec2 size,
                       PopupMenuState* contextMenu,
                       GraphSurfaceStyle graphStyle)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::Canvas);
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);
    node->setSemantics(graphSurfaceSemantics(name, state, nodes, cables));
    node->setOnRefresh([&state, &nodes, &cables, name](Node& n) {
        return refreshGraphSurfaceSemantics(n, name, state, nodes, cables);
    });
    node->setSemanticChildren([&state, &nodes, &cables](
                                  const Node& n, std::vector<SemanticNode>& out) {
        appendGraphSemanticChildren(n, state, nodes, cables, out);
    });
    node->setOnSemanticAction([sid, &state, &nodes, &cables, contextMenu, callbacks](
                                  Node& n, const NodeId& semanticId,
                                  Action action, double) mutable {
        GraphHit hit = graphHitForSemanticId(sid, semanticId, nodes, cables);
        if (!hit.valid())
            return false;
        state.hovered = hit;
        if (action == Action::Focus) {
            state.focused = hit;
            state.active = hit;
            return true;
        }
        if (action == Action::OpenMenu) {
            state.focused = hit;
            const Vec2 anchor = graphHitTreeAnchor(n, state, hit);
            if (contextMenu) {
                contextMenu->open = true;
                contextMenu->anchorToPosition = true;
                contextMenu->highlightedIndex = -1;
                contextMenu->position = ImVec2(anchor.x, anchor.y);
            }
            if (callbacks.onContextMenu) {
                const Rect nb = n.bounds();
                callbacks.onContextMenu(hit, hit.graphPosition,
                                        {anchor.x - nb.x, anchor.y - nb.y});
            }
            return true;
        }
        if (action == Action::Activate) {
            state.focused = hit;
            if (callbacks.onActivate)
                callbacks.onActivate(hit);
            return true;
        }
        return false;
    });
    node->setOnAction([&state, &nodes, &cables, contextMenu, callbacks, graphStyle](
                          Node& n, Action action, double) mutable {
        if (action != Action::OpenMenu)
            return false;
        const Rect b = n.bounds();
        const Vec2 pos{b.x + b.w * 0.5f, b.y + b.h * 0.5f};
        const Vec2 local{pos.x - b.x, pos.y - b.y};
        state.hovered = hitTestGraph(state.viewport, nodes, cables, local, graphStyle);
        state.focused = state.hovered;
        if (contextMenu) {
            contextMenu->open = true;
            contextMenu->anchorToPosition = true;
            contextMenu->highlightedIndex = -1;
            contextMenu->position = ImVec2(pos.x, pos.y);
        }
        if (callbacks.onContextMenu)
            callbacks.onContextMenu(state.hovered, state.hovered.graphPosition,
                                    local);
        return true;
    });
    node->setOnEvent([&state, &nodes, &cables, contextMenu, callbacks, graphStyle](
                         Node& n, const Event& event) mutable {
        const Rect b = n.bounds();
        const Vec2 local{event.position.x - b.x, event.position.y - b.y};
        auto updateHover = [&] {
            state.hovered = hitTestGraph(state.viewport, nodes, cables, local, graphStyle);
            return state.hovered;
        };

        if (event.type == EventType::MouseMove) {
            if (state.cablePreviewActive) {
                updateGraphCablePreview(state, nodes, cables, local, graphStyle,
                                        callbacks);
                return true;
            }
            if (state.panning) {
                state.viewport.pan.x += event.delta.x;
                state.viewport.pan.y += event.delta.y;
                if (callbacks.onViewportChanged)
                    callbacks.onViewportChanged(state.viewport);
                return true;
            }
            if (state.marqueeActive) {
                const Vec2 g = screenToGraph(state.viewport, local);
                state.marquee.w = g.x - state.marquee.x;
                state.marquee.h = g.y - state.marquee.y;
                return true;
            }
            updateHover();
            if (n.pressed() && state.active.valid() &&
                state.active.kind != GraphHitKind::Surface &&
                (event.delta.x != 0.0f || event.delta.y != 0.0f)) {
                if (callbacks.onDrag) {
                    const float z = std::max(0.05f, state.viewport.zoom);
                    callbacks.onDrag(state.active,
                                     {event.delta.x / z, event.delta.y / z});
                }
                return true;
            }
            return state.hovered.valid();
        }

        // Viewport navigation (owner keys): cmd+wheel zooms at the cursor,
        // cmd+arrows step-scroll, cmd/alt+left-drag or middle-drag pans.
        // PLAIN wheel deliberately does nothing -- bare scrolling shoved the
        // whole canvas around (owner).
        if (event.type == EventType::MouseWheel && event.super &&
            event.wheelDelta.y != 0.0f) {
            const Vec2 before = screenToGraph(state.viewport, local);
            const float prevZoom = std::max(0.05f, state.viewport.zoom);
            const float factor = std::pow(1.12f, event.wheelDelta.y);
            state.viewport.zoom = std::clamp(prevZoom * factor, 0.20f, 4.0f);
            state.viewport.pan.x = local.x - before.x * state.viewport.zoom;
            state.viewport.pan.y = local.y - before.y * state.viewport.zoom;
            if (callbacks.onViewportChanged)
                callbacks.onViewportChanged(state.viewport);
            return true;
        }

        if (event.type == EventType::KeyDown && event.super) {
            const float step = 60.0f;
            Vec2 d{};
            if (event.key == Key::Left)
                d.x = step; // view left = content right
            else if (event.key == Key::Right)
                d.x = -step;
            else if (event.key == Key::Up)
                d.y = step;
            else if (event.key == Key::Down)
                d.y = -step;
            if (d.x != 0.0f || d.y != 0.0f) {
                state.viewport.pan.x += d.x;
                state.viewport.pan.y += d.y;
                if (callbacks.onViewportChanged)
                    callbacks.onViewportChanged(state.viewport);
                return true;
            }
        }

        if (event.type == EventType::MouseDown &&
            (event.button == MouseButton::Middle ||
             (event.button == MouseButton::Left &&
              (event.alt || event.super)))) {
            state.panning = true;
            state.active = makeGraphHit(GraphHitKind::Surface,
                                        screenToGraph(state.viewport, local));
            return true;
        }

        if (event.type == EventType::MouseDown && event.button == MouseButton::Left) {
            state.active = updateHover();
            state.focused = state.active;
            if (event.clickCount >= 2) {
                // Double-click module surfaces and empty canvas without
                // starting marquee or cable gestures.
                if (state.active.kind == GraphHitKind::NodeBody ||
                    state.active.kind == GraphHitKind::NodeTitle ||
                    state.active.kind == GraphHitKind::NodePart) {
                    // Opening an editor may spawn an OS window that grabs the
                    // matching MouseUp, leaving this surface pressed -- then a
                    // later MouseMove drags the node (it "sticks to the
                    // mouse"). Drop the drag target now so no drag can start.
                    if (callbacks.onNodeDoubleClicked)
                        callbacks.onNodeDoubleClicked(state.active);
                    state.active = {};
                    state.panning = false;
                    state.marqueeActive = false;
                    return true;
                }
                if (state.active.kind == GraphHitKind::Cable ||
                    state.active.kind == GraphHitKind::CableEndpoint) {
                    if (callbacks.onCableDoubleClicked)
                        callbacks.onCableDoubleClicked(state.active);
                    state.active = {};
                    return true;
                }
                if (state.active.kind == GraphHitKind::Surface ||
                    state.active.kind == GraphHitKind::None) {
                    const Vec2 g = screenToGraph(state.viewport, local);
                    if (callbacks.onBackgroundDoubleClick) {
                        callbacks.onBackgroundDoubleClick(g);
                    } else {
                        state.panning = false;
                        fitGraphViewport(state, nodes, {b.w, b.h});
                        if (callbacks.onViewportChanged)
                            callbacks.onViewportChanged(state.viewport);
                    }
                    return true;
                }
            }
            if (graphPortHit(state.active)) {
                state.cablePreviewActive = true;
                state.cablePreviewStart = state.active;
                state.cablePreviewPosition = state.active.graphPosition;
                state.cablePreviewTarget = {};
                state.cablePreviewValid = false;
                if (callbacks.onCablePreview)
                    callbacks.onCablePreview(state.cablePreviewStart,
                                             state.cablePreviewPosition);
            }
            if (state.active.kind == GraphHitKind::Surface ||
                state.active.kind == GraphHitKind::None) {
                state.marqueeActive = true;
                const Vec2 g = screenToGraph(state.viewport, local);
                state.marquee = {g.x, g.y, 0.0f, 0.0f};
            }
            if (callbacks.onSelect)
                callbacks.onSelect(state.active);
            return true;
        }

        if (event.type == EventType::MouseUp) {
            if (event.button == MouseButton::Middle) {
                state.panning = false;
                return true;
            }
            if (event.button == MouseButton::Left) {
                if (state.cablePreviewActive) {
                    updateGraphCablePreview(state, nodes, cables, local, graphStyle,
                                            callbacks);
                    if (state.cablePreviewValid && callbacks.onConnect)
                        callbacks.onConnect(state.cablePreviewStart,
                                            state.cablePreviewTarget);
                    clearGraphCablePreview(state);
                    return true;
                }
                const bool wasPanning = state.panning;
                const bool wasMarquee = state.marqueeActive;
                state.panning = false;
                state.marqueeActive = false;
                if (!wasPanning && !wasMarquee && state.active.valid() &&
                    callbacks.onActivate)
                    callbacks.onActivate(state.active);
                return true;
            }
        }

        if (event.type == EventType::ContextMenu) {
            state.hovered = updateHover();
            state.focused = state.hovered;
            if (contextMenu) {
                contextMenu->open = true;
                contextMenu->anchorToPosition = true;
                contextMenu->highlightedIndex = -1;
                contextMenu->position = ImVec2(event.position.x, event.position.y);
            }
            if (callbacks.onContextMenu)
                callbacks.onContextMenu(state.hovered, state.hovered.graphPosition,
                                        local);
            return true;
        }

        if (event.type == EventType::KeyDown && event.key == Key::Escape) {
            const bool changed = state.panning || state.marqueeActive ||
                                 state.active.valid() || state.cablePreviewActive;
            state.panning = false;
            state.marqueeActive = false;
            state.active = {};
            clearGraphCablePreview(state);
            return changed;
        }

        if (event.type == EventType::KeyDown &&
            (event.key == Key::Down || event.key == Key::Right ||
             event.key == Key::Up || event.key == Key::Left)) {
            const int direction = (event.key == Key::Down || event.key == Key::Right)
                                      ? 1
                                      : -1;
            return moveGraphKeyboardFocus(state, nodes, cables, direction);
        }

        if (event.type == EventType::KeyDown &&
            (event.key == Key::Enter || event.key == Key::Space) &&
            state.focused.valid()) {
            if (callbacks.onActivate)
                callbacks.onActivate(state.focused);
            return true;
        }

        return false;
    });

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Canvas;
        style.panelBorder = false;     // owner: no grey frame around the graph
        style.canvasFocusRing = false; // focus reads per-node, not a big ring
        style.canvasDraw = [&state, &nodes, &cables, graphStyle](
                                ImDrawList& dl, const Node&,
                                Rect bounds, const paint::ControlState& cs) {
            paint::drawGraphGrid(&dl, topLeft(bounds), sizeOf(bounds),
                                 ImVec2(state.viewport.pan.x, state.viewport.pan.y),
                                 state.viewport.zoom, palette(), cs, graphStyle);

            for (const GraphCable& cable : cables) {
                Vec2 fromGraph;
                Vec2 toGraph;
                if (!portCenterGraph(nodes, cable.fromNode, cable.fromPort, fromGraph) ||
                    !portCenterGraph(nodes, cable.toNode, cable.toPort, toGraph))
                    continue;
                const Vec2 fromLocal = graphToScreen(state.viewport, fromGraph);
                const Vec2 toLocal = graphToScreen(state.viewport, toGraph);
                paint::ControlState cableState;
                cableState.selected = cable.selected ||
                                      (state.hovered.kind == GraphHitKind::Cable &&
                                       state.hovered.cableId == cable.id) ||
                                      (state.hovered.kind == GraphHitKind::CableEndpoint &&
                                       state.hovered.cableId == cable.id);
                GraphHit cableHit = makeGraphHit(GraphHitKind::Cable,
                                                 {(fromGraph.x + toGraph.x) * 0.5f,
                                                  (fromGraph.y + toGraph.y) * 0.5f});
                cableHit.cableId = cable.id;
                cableState.focused = sameGraphHit(state.focused, cableHit);
                cableState.hovered = cableState.selected;
                cableState.disabled = cable.muted || cable.invalid;
                paint::drawCable(&dl, ImVec2(bounds.x + fromLocal.x,
                                             bounds.y + fromLocal.y),
                                 ImVec2(bounds.x + toLocal.x,
                                        bounds.y + toLocal.y), palette(),
                                 cableState, cable.invalid ? palette().meterHot : cable.color,
                                 graphStyle.wireThickness, graphStyle,
                                 std::max(0.05f, state.viewport.zoom));
            }

            if (state.cablePreviewActive) {
                const Vec2 fromGraph = graphPortAnchor(nodes, state.cablePreviewStart);
                const Vec2 toGraph = state.cablePreviewValid
                                         ? graphPortAnchor(nodes, state.cablePreviewTarget)
                                         : state.cablePreviewPosition;
                const Vec2 fromLocal = graphToScreen(state.viewport, fromGraph);
                const Vec2 toLocal = graphToScreen(state.viewport, toGraph);
                paint::ControlState previewState;
                previewState.hovered = true;
                previewState.selected = state.cablePreviewValid;
                previewState.disabled = !state.cablePreviewValid;
                paint::drawCable(&dl, ImVec2(bounds.x + fromLocal.x,
                                             bounds.y + fromLocal.y),
                                 ImVec2(bounds.x + toLocal.x,
                                        bounds.y + toLocal.y), palette(),
                                 previewState,
                                 state.cablePreviewValid ? IM_COL32(255, 255, 255, 179)
                                                         : palette().meterHot,
                                 graphStyle.wireThickness, graphStyle,
                                 std::max(0.05f, state.viewport.zoom));
            }

            for (const GraphNode& graphNode : nodes) {
                const Rect localRect = graphToScreen(state.viewport, graphNode.bounds);
                const Rect nodeRect{std::round(bounds.x + localRect.x),
                                    std::round(bounds.y + localRect.y),
                                    localRect.w, localRect.h};
                GraphHit nodeHit = makeGraphHit(GraphHitKind::NodeBody,
                                                rectCenter(graphNode.bounds));
                nodeHit.nodeId = graphNode.id;
                paint::ControlState nodeState;
                // Hover does not light the selected border; selected/focused
                // nodes own that stronger rim treatment.
                nodeState.selected = graphNode.selected ||
                                     sameGraphHit(state.focused, nodeHit);
                nodeState.hovered = state.hovered.nodeId == graphNode.id;
                nodeState.focused = sameGraphHit(state.focused, nodeHit);
                nodeState.disabled = graphNode.disabled;
                const float zoomScale = std::max(0.05f, state.viewport.zoom);
                {
                    draw::ImGuiSurface boxSurface(&dl);
                    paint::drawModuleBox(boxSurface,
                                         draw::fontRef(ImGui::GetFont()),
                                         ImGui::GetFontSize() * 0.90f * zoomScale,
                                         topLeftDraw({nodeRect.x, nodeRect.y,
                                                      nodeRect.w, nodeRect.h}),
                                         sizeOfDraw({nodeRect.x, nodeRect.y,
                                                     nodeRect.w, nodeRect.h}),
                                         graphNode.title.c_str(), palette(),
                                         nodeState, graphNode.bypassed,
                                         graphNode.error, graphStyle,
                                         24.0f * zoomScale, ImGui::GetTime());
                }

                auto drawPortList = [&](const std::vector<GraphPort>& ports) {
                    for (const GraphPort& port : ports) {
                        Rect pr = graphToScreen(state.viewport,
                                                nodeLocalRect(graphNode, port.bounds));
                        pr.x = std::round(pr.x + bounds.x);
                        pr.y = std::round(pr.y + bounds.y);
                        paint::ControlState portState;
                        portState.hovered = state.hovered.kind == GraphHitKind::Port &&
                                            state.hovered.nodeId == graphNode.id &&
                                            state.hovered.portId == port.id;
                        GraphHit portHit = makeGraphHit(GraphHitKind::Port,
                                                        add({graphNode.bounds.x,
                                                             graphNode.bounds.y},
                                                            rectCenter(port.bounds)));
                        portHit.nodeId = graphNode.id;
                        portHit.portId = port.id;
                        portHit.output = port.direction == GraphPortDirection::Output;
                        portState.focused = sameGraphHit(state.focused, portHit);
                        portState.selected = portState.hovered || portState.focused ||
                                             (state.cablePreviewActive &&
                                              sameGraphHit(state.cablePreviewStart, portHit)) ||
                                             (state.cablePreviewValid &&
                                              sameGraphHit(state.cablePreviewTarget, portHit));
                        drawGraphPort(dl, pr, port, portState, graphStyle,
                                      portFlushEdge(graphNode, port));
                    }
                };
                drawPortList(graphNode.inputs);
                drawPortList(graphNode.outputs);

                for (const GraphNodePart& part : graphNode.parts) {
                    if (part.kind == GraphNodePartKind::Title)
                        continue;
                    Rect partRect = graphToScreen(state.viewport,
                                                  nodeLocalRect(graphNode, part.bounds));
                    partRect.x = std::round(partRect.x + bounds.x);
                    partRect.y = std::round(partRect.y + bounds.y);
                    paint::ControlState partState;
                    partState.hovered = state.hovered.kind == GraphHitKind::NodePart &&
                                        state.hovered.nodeId == graphNode.id &&
                                        state.hovered.partId == part.id;
                    partState.active = state.active.kind == GraphHitKind::NodePart &&
                                       state.active.nodeId == graphNode.id &&
                                       state.active.partId == part.id;
                    GraphHit partHit = makeGraphHit(GraphHitKind::NodePart,
                                                    add({graphNode.bounds.x,
                                                         graphNode.bounds.y},
                                                        rectCenter(part.bounds)));
                    partHit.nodeId = graphNode.id;
                    partHit.partId = part.id;
                    partState.focused = sameGraphHit(state.focused, partHit);
                    partState.selected = partState.focused;
                    {
                        draw::ImGuiSurface partSurface(&dl);
                        drawGraphPart(partSurface, draw::fontRef(ImGui::GetFont()),
                                      ImGui::GetFontSize() * zoomScale, partRect,
                                      part, partState);
                    }
                }
            }

            if (state.marqueeActive) {
                Rect r = graphToScreen(state.viewport, state.marquee);
                if (r.w < 0.0f) {
                    r.x += r.w;
                    r.w = -r.w;
                }
                if (r.h < 0.0f) {
                    r.y += r.h;
                    r.h = -r.h;
                }
                r.x += bounds.x;
                r.y += bounds.y;
                dl.AddRectFilled(topLeft(r), bottomRight(r),
                                 IM_COL32(0xff, 0xc2, 0x4a, 0x1F), 0.0f);
                dl.AddRect(topLeft(r), bottomRight(r),
                           IM_COL32(0xff, 0xc2, 0x4a, 0xB3), 0.0f);
            }

            // Connector tooltip, ImGui-backed path: build a FrameContext from
            // ImGui globals and reuse the shared helper.
            draw::ImGuiSurface tipSurface(&dl);
            draw::FrameContext tctx;
            tctx.font = draw::fontRef(ImGui::GetFont());
            tctx.fontSizePx = ImGui::GetFontSize();
            tctx.timeSeconds = ImGui::GetTime();
            tctx.pointer = {ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y};
            tctx.pointerValid = true;
            drawGraphPortTooltip(tipSurface, state, nodes, graphStyle, bounds,
                                 tctx);
        };
        style.canvasSurfaceDraw =
            [&state, &nodes, &cables, graphStyle](
                draw::Surface& surface, const Node&, Rect bounds,
                const paint::ControlState& cs, const draw::FrameContext& context) {
                paint::drawGraphGrid(surface, topLeftDraw(bounds), sizeOfDraw(bounds),
                                     {state.viewport.pan.x, state.viewport.pan.y},
                                     state.viewport.zoom, palette(), cs, graphStyle,
                                     context.timeSeconds);

                for (const GraphCable& cable : cables) {
                    Vec2 fromGraph;
                    Vec2 toGraph;
                    if (!portCenterGraph(nodes, cable.fromNode, cable.fromPort, fromGraph) ||
                        !portCenterGraph(nodes, cable.toNode, cable.toPort, toGraph))
                        continue;
                    const Vec2 fromLocal = graphToScreen(state.viewport, fromGraph);
                    const Vec2 toLocal = graphToScreen(state.viewport, toGraph);
                    paint::ControlState cableState;
                    cableState.selected = cable.selected ||
                                          (state.hovered.kind == GraphHitKind::Cable &&
                                           state.hovered.cableId == cable.id) ||
                                          (state.hovered.kind == GraphHitKind::CableEndpoint &&
                                           state.hovered.cableId == cable.id);
                    GraphHit cableHit = makeGraphHit(GraphHitKind::Cable,
                                                     {(fromGraph.x + toGraph.x) * 0.5f,
                                                      (fromGraph.y + toGraph.y) * 0.5f});
                    cableHit.cableId = cable.id;
                    cableState.focused = sameGraphHit(state.focused, cableHit);
                    cableState.hovered = cableState.selected;
                    cableState.disabled = cable.muted || cable.invalid;
                    paint::drawCable(surface,
                                     {bounds.x + fromLocal.x, bounds.y + fromLocal.y},
                                     {bounds.x + toLocal.x, bounds.y + toLocal.y},
                                     palette(), cableState,
                                     cable.invalid ? palette().meterHot : cable.color,
                                     graphStyle.wireThickness, graphStyle,
                                 std::max(0.05f, state.viewport.zoom));
                }

                if (state.cablePreviewActive) {
                    const Vec2 fromGraph = graphPortAnchor(nodes, state.cablePreviewStart);
                    const Vec2 toGraph = state.cablePreviewValid
                                             ? graphPortAnchor(nodes, state.cablePreviewTarget)
                                             : state.cablePreviewPosition;
                    const Vec2 fromLocal = graphToScreen(state.viewport, fromGraph);
                    const Vec2 toLocal = graphToScreen(state.viewport, toGraph);
                    paint::ControlState previewState;
                    previewState.hovered = true;
                    previewState.selected = state.cablePreviewValid;
                    previewState.disabled = !state.cablePreviewValid;
                    paint::drawCable(surface,
                                     {bounds.x + fromLocal.x, bounds.y + fromLocal.y},
                                     {bounds.x + toLocal.x, bounds.y + toLocal.y},
                                     palette(), previewState,
                                     state.cablePreviewValid ? IM_COL32(255, 255, 255, 179)
                                                             : palette().meterHot,
                                     graphStyle.wireThickness, graphStyle,
                                 std::max(0.05f, state.viewport.zoom));
                }

                for (const GraphNode& graphNode : nodes) {
                    const Rect localRect = graphToScreen(state.viewport, graphNode.bounds);
                    const Rect nodeRect{std::round(bounds.x + localRect.x),
                                    std::round(bounds.y + localRect.y),
                                        localRect.w, localRect.h};
                    GraphHit nodeHit = makeGraphHit(GraphHitKind::NodeBody,
                                                    rectCenter(graphNode.bounds));
                    nodeHit.nodeId = graphNode.id;
                    paint::ControlState nodeState;
                    // Hover does not light the selected border.
                    nodeState.selected = graphNode.selected ||
                                         sameGraphHit(state.focused, nodeHit);
                    nodeState.hovered = state.hovered.nodeId == graphNode.id;
                    nodeState.focused = sameGraphHit(state.focused, nodeHit);
                    nodeState.disabled = graphNode.disabled;
                    const float zoomScale = std::max(0.05f, state.viewport.zoom);
                    paint::drawModuleBox(surface, context.font,
                                         context.fontSizePx * 0.90f * zoomScale,
                                         topLeftDraw(nodeRect), sizeOfDraw(nodeRect),
                                         graphNode.title.c_str(), palette(), nodeState,
                                         graphNode.bypassed, graphNode.error, graphStyle,
                                         24.0f * zoomScale, context.timeSeconds);

                    auto drawPortList = [&](const std::vector<GraphPort>& ports) {
                        for (const GraphPort& port : ports) {
                            Rect pr = graphToScreen(state.viewport,
                                                    nodeLocalRect(graphNode, port.bounds));
                            pr.x = std::round(pr.x + bounds.x);
                            pr.y = std::round(pr.y + bounds.y);
                            paint::ControlState portState;
                            portState.hovered = state.hovered.kind == GraphHitKind::Port &&
                                                state.hovered.nodeId == graphNode.id &&
                                                state.hovered.portId == port.id;
                            GraphHit portHit = makeGraphHit(GraphHitKind::Port,
                                                            add({graphNode.bounds.x,
                                                                 graphNode.bounds.y},
                                                                rectCenter(port.bounds)));
                            portHit.nodeId = graphNode.id;
                            portHit.portId = port.id;
                            portHit.output = port.direction == GraphPortDirection::Output;
                            portState.focused = sameGraphHit(state.focused, portHit);
                            portState.selected = portState.hovered || portState.focused ||
                                                 (state.cablePreviewActive &&
                                                  sameGraphHit(state.cablePreviewStart, portHit)) ||
                                                 (state.cablePreviewValid &&
                                                  sameGraphHit(state.cablePreviewTarget, portHit));
                            drawGraphPort(surface, pr, port, portState, graphStyle,
                                          portFlushEdge(graphNode, port));
                            if (port.kind == GraphPortKind::Control &&
                                port.direction == GraphPortDirection::Output &&
                                !port.label.empty()) {
                                // Name control-out sockets, right-aligned.
                                const float ls = std::round(context.fontSizePx *
                                                            0.75f * zoomScale *
                                                            2.0f) *
                                                 0.5f;
                                if (ls < 1.0f)
                                    continue;
                                const draw::Vec2 ts = surface.measureText(
                                    context.font, ls, port.label.c_str());
                                surface.text(context.font, ls,
                                             {std::round(pr.x - 3.0f - ts.x),
                                              std::round(pr.y +
                                                         (pr.h - ts.y) * 0.5f)},
                                             paint::withAlpha(
                                                 (graphStyle.text & 0xFF000000u)
                                                     ? graphStyle.text
                                                     : IM_COL32(0xdf, 0xe6, 0xe2, 255),
                                                 0xC7),
                                             port.label.c_str());
                            }
                        }
                    };
                    drawPortList(graphNode.inputs);
                    drawPortList(graphNode.outputs);

                    for (const GraphNodePart& part : graphNode.parts) {
                        if (part.kind == GraphNodePartKind::Title)
                            continue;
                        Rect partRect = graphToScreen(state.viewport,
                                                      nodeLocalRect(graphNode, part.bounds));
                        partRect.x = std::round(partRect.x + bounds.x);
                        partRect.y = std::round(partRect.y + bounds.y);
                        paint::ControlState partState;
                        partState.hovered = state.hovered.kind == GraphHitKind::NodePart &&
                                            state.hovered.nodeId == graphNode.id &&
                                            state.hovered.partId == part.id;
                        partState.active = state.active.kind == GraphHitKind::NodePart &&
                                           state.active.nodeId == graphNode.id &&
                                           state.active.partId == part.id;
                        GraphHit partHit = makeGraphHit(GraphHitKind::NodePart,
                                                        add({graphNode.bounds.x,
                                                             graphNode.bounds.y},
                                                            rectCenter(part.bounds)));
                        partHit.nodeId = graphNode.id;
                        partHit.partId = part.id;
                        partState.focused = sameGraphHit(state.focused, partHit);
                        partState.selected = partState.focused;
                        drawGraphPart(surface, context.font,
                                      context.fontSizePx * zoomScale,
                                      partRect, part, partState);
                    }
                }

                if (state.marqueeActive) {
                    Rect r = graphToScreen(state.viewport, state.marquee);
                    if (r.w < 0.0f) {
                        r.x += r.w;
                        r.w = -r.w;
                    }
                    if (r.h < 0.0f) {
                        r.y += r.h;
                        r.h = -r.h;
                    }
                    r.x += bounds.x;
                    r.y += bounds.y;
                    surface.fillRect(topLeftDraw(r), bottomRightDraw(r),
                                     IM_COL32(0xff, 0xc2, 0x4a, 0x1F), 0.0f);
                    surface.strokeRect(topLeftDraw(r), bottomRightDraw(r),
                                       IM_COL32(0xff, 0xc2, 0x4a, 0xB3), 0.0f);
                }

                // Connector tooltip: hovering a pin for a moment shows its
                // label. Drawn LAST so it sits over the
                // graph; timing tracked in state (which pin, since when).
                drawGraphPortTooltip(surface, state, nodes, graphStyle, bounds,
                                     context);
            };
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

// Fire onActivate on mouse DOWN (and Enter/Space) instead of on release -- no
// click latency. The button's default release-activation stays silent because
// no onActivate is installed on it; onEvent runs before the tree's own key
// mapping so Enter/Space still work. Shared by the outline/transport buttons.
static void installActOnPress(Node& node, std::function<void(Node&)> onActivate)
{
    auto cb = std::make_shared<std::function<void(Node&)>>(std::move(onActivate));
    node.setOnEvent([cb](Node& n, const Event& e) {
        if (e.type == EventType::MouseDown && e.button == MouseButton::Left) {
            (*cb)(n);
            return true;
        }
        if (e.type == EventType::KeyDown &&
            (e.key == Key::Enter || e.key == Key::Space)) {
            (*cb)(n);
            return true;
        }
        return false;
    });
}

Node::Ptr outlineButton(NodeId id, std::string name,
                        std::function<void(Node&)> onActivate,
                        PaintRenderer* renderer, Vec2 size,
                        paint::OutlineButtonStyle outlineStyle, bool selected,
                        bool actOnPress)
{
    NodeId sid = id;
    auto node = button(std::move(id), std::move(name),
                       actOnPress ? std::function<void(Node&)>{}
                                  : std::move(onActivate),
                       nullptr);
    node->setIntrinsicSize(size);
    if (actOnPress && onActivate)
        installActOnPress(*node, std::move(onActivate));
    if (selected) {
        Semantics sem = node->semantics();
        sem.states |= SemanticState::Selected;
        node->setSemantics(sem);
    }
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::OutlineButton;
        style.outlineButtonStyle = outlineStyle;
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
        style.canvasSurfaceDraw =
            [label = node->semantics().name, top, bottom, animate](
                draw::Surface& surface, const Node&, Rect bounds,
                const paint::ControlState& state, const draw::FrameContext& context) {
                const float pulse = animate
                                        ? 0.5f + 0.5f *
                                                       std::sin((float)context.timeSeconds * 3.6f)
                                        : 0.0f;
                paint::drawAnimatedButton(surface, context.font,
                                          context.fontSizePx * 0.90f,
                                          topLeftDraw(bounds), sizeOfDraw(bounds),
                                          label.c_str(), top, bottom,
                                          palette(), state, pulse);
            };
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr gradientButton(NodeId id, std::string name,
                         std::function<void(Node&)> onActivate,
                         PaintRenderer* renderer, Vec2 size,
                         ImU32 top, ImU32 bottom)
{
    return animatedButton(std::move(id), std::move(name),
                          std::move(onActivate), renderer, size, top, bottom,
                          false);
}

Node::Ptr iconButton(NodeId id, std::string name, std::string glyph,
                     std::function<void(Node&)> onActivate,
                     PaintRenderer* renderer, IconFont font, Vec2 size,
                     bool lit)
{
    NodeId sid = id;
    auto node = button(std::move(id), std::move(name), std::move(onActivate), nullptr);
    node->setIntrinsicSize(size);
    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::IconButton;
        style.glyph = std::move(glyph);
        style.iconFont = font;
        style.lit = lit; // toggled transport-style buttons show the down face
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr outlineIconButton(NodeId id, std::string name, std::string glyph,
                            std::function<void(Node&)> onActivate,
                            PaintRenderer* renderer, Vec2 size,
                            paint::OutlineButtonStyle style, bool selected,
                            IconFont font, bool actOnPress)
{
    NodeId sid = id;
    auto node = button(std::move(id), std::move(name),
                       actOnPress ? std::function<void(Node&)>{}
                                  : std::move(onActivate),
                       nullptr);
    node->setIntrinsicSize(size);
    if (actOnPress && onActivate)
        installActOnPress(*node, std::move(onActivate));
    if (renderer) {
        VisualStyle vs;
        vs.kind = VisualKind::OutlineIconButton;
        vs.glyph = std::move(glyph);
        vs.iconFont = font;
        vs.outlineButtonStyle = style;
        vs.lit = selected;
        renderer->setStyle(sid, vs);
    }
    return node;
}

Node::Ptr transportButton(NodeId id, std::string name, Icon icon,
                          std::function<void(Node&)> onActivate,
                          PaintRenderer* renderer, Vec2 size,
                          paint::OutlineButtonStyle style, bool selected,
                          bool actOnPress)
{
    // Outline chrome + a VECTOR transport glyph, drawn through a Canvas node so
    // no new render kind is needed. `selected` shows the engaged fill (and, for
    // Record, fills the ring to a disc); actOnPress fires on mouse down.
    NodeId sid = id;
    auto node = button(std::move(id), std::move(name),
                       actOnPress ? std::function<void(Node&)>{}
                                  : std::move(onActivate),
                       nullptr);
    node->setIntrinsicSize(size);
    node->setFocusable(true);
    if (actOnPress && onActivate)
        installActOnPress(*node, std::move(onActivate));
    if (selected) {
        Semantics sem = node->semantics();
        sem.states |= SemanticState::Selected;
        node->setSemantics(sem);
    }
    if (renderer) {
        VisualStyle vs;
        vs.kind = VisualKind::Canvas;
        vs.panelBorder = false;     // drawTransportButton draws its own outline
        vs.canvasFocusRing = false; // and its own focus ring
        vs.canvasDraw = [icon, style](ImDrawList& dl, const Node&, Rect b,
                                      const paint::ControlState& st) {
            paint::drawTransportButton(&dl, icon, topLeft(b), sizeOf(b),
                                       palette(), st, style);
        };
        vs.canvasSurfaceDraw = [icon, style](draw::Surface& s, const Node&, Rect b,
                                             const paint::ControlState& st,
                                             const draw::FrameContext&) {
            paint::drawTransportButton(s, icon, topLeftDraw(b), sizeOfDraw(b),
                                       palette(), st, style);
        };
        renderer->setStyle(sid, vs);
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

namespace {

// Shared setup for the option-index widgets (segmented, cycleButton): the
// binding becomes a 0..count-1 integer index and, unless the caller provided
// one, formats as the selected label so semantics read naturally.
ValueBinding optionIndexBinding(ValueBinding binding,
                                const std::vector<std::string>& labels)
{
    binding.min = 0.0;
    binding.max = (double)std::max<size_t>(1, labels.size()) - 1.0;
    if (binding.step <= 0.0)
        binding.step = 1.0;
    if (!binding.format) {
        binding.format = [labels](double v) {
            const int i = (int)std::lround(v);
            return i >= 0 && i < (int)labels.size() ? labels[(size_t)i]
                                                    : std::string();
        };
    }
    return binding;
}

float optionRowWidth(const std::vector<std::string>& labels, float padPerSeg,
                     float minSeg)
{
    std::size_t longest = 0;
    for (const std::string& l : labels)
        longest = std::max(longest, l.size());
    // Build-time estimate (no font metrics needed outside a frame).
    return std::max(minSeg, padPerSeg + 7.5f * (float)longest);
}

} // namespace

Node::Ptr segmented(NodeId id, std::string name, std::vector<std::string> labels,
                    ValueBinding binding, PaintRenderer* renderer, Vec2 size)
{
    NodeId sid = id;
    const int count = (int)labels.size();
    auto node = Node::make(std::move(id), Role::Slider);
    if (size.x <= 0.0f)
        size.x = (float)std::max(1, count) * optionRowWidth(labels, 18.0f, 52.0f);
    if (size.y <= 0.0f)
        size.y = 24.0f;
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);

    ValueBinding indexBinding = optionIndexBinding(std::move(binding), labels);
    Semantics sem = named(Role::Slider, name);
    sem.description = "Segmented switch";
    setBindingSemantics(sem, indexBinding);
    node->setSemantics(sem);
    node->setValueBinding(std::move(indexBinding));

    node->setOnEvent([count](Node& n, const Event& event) {
        if (event.type != EventType::MouseDown ||
            event.button != MouseButton::Left)
            return false;
        const Rect bounds = n.bounds();
        if (bounds.w <= 0.0f || count <= 0)
            return false;
        const double frac = (event.position.x - bounds.x) / bounds.w;
        const int seg = std::clamp((int)(frac * count), 0, count - 1);
        return setBindingValue(n, (double)seg);
    });

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Segmented;
        style.segments = std::move(labels);
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr tabBar(NodeId id, std::string name, std::vector<std::string> labels,
                 ValueBinding binding, PaintRenderer* renderer, Vec2 size)
{
    NodeId sid = id;
    const int count = (int)labels.size();
    auto node = Node::make(std::move(id), Role::Group);
    if (size.x <= 0.0f)
        size.x = (float)std::max(1, count) * optionRowWidth(labels, 28.0f, 64.0f);
    if (size.y <= 0.0f)
        size.y = 26.0f;
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);

    ValueBinding indexBinding = optionIndexBinding(std::move(binding), labels);
    Semantics sem = named(Role::Group, name);
    sem.description = "Tab strip";
    setBindingSemantics(sem, indexBinding);
    node->setSemantics(sem);
    node->setValueBinding(std::move(indexBinding));

    node->setOnEvent([count](Node& n, const Event& event) {
        if (event.type == EventType::MouseDown) {
            if (event.button != MouseButton::Left)
                return false;
            const Rect bounds = n.bounds();
            if (bounds.w <= 0.0f || count <= 0)
                return false;
            const double frac = (event.position.x - bounds.x) / bounds.w;
            const int tab = std::clamp((int)(frac * count), 0, count - 1);
            return setBindingValue(n, (double)tab);
        }
        if (event.type == EventType::KeyDown) {
            if (event.key == Key::Home)
                return setBindingValue(n, 0.0);
            if (event.key == Key::End)
                return setBindingValue(n, (double)std::max(0, count - 1));
        }
        return false;
    });

    node->setSemanticChildren([sid, labels](const Node& n,
                                            std::vector<SemanticNode>& out) {
        const int count = (int)labels.size();
        if (count <= 0)
            return;
        const Rect b = n.bounds();
        const float tabW = b.w / (float)count;
        const int selected = std::clamp((int)std::lround(valueOf(n, nullptr)),
                                        0, count - 1);
        for (int i = 0; i < count; ++i) {
            SemanticNode child;
            child.id = sid + ".tab." + std::to_string(i);
            child.parent = n.id();
            child.bounds = {b.x + tabW * (float)i, b.y,
                            i == count - 1 ? b.w - tabW * (float)i : tabW,
                            b.h};
            child.role = Role::Button;
            child.name = labels[(size_t)i];
            child.value.text = child.name;
            child.states = stateMask(SemanticState::Focusable);
            if (i == selected)
                child.states |= SemanticState::Selected;
            child.actions = {Action::Focus, Action::Activate, Action::SetValue};
            out.push_back(std::move(child));
        }
    });

    node->setOnSemanticAction([sid, count](Node& n, const NodeId& semanticId,
                                           Action action, double) {
        if (action != Action::Focus && action != Action::Activate &&
            action != Action::SetValue)
            return false;
        for (int i = 0; i < count; ++i)
            if (semanticId == sid + ".tab." + std::to_string(i))
                return setBindingValue(n, (double)i);
        return false;
    });

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::TabBar;
        style.segments = std::move(labels);
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr cycleButton(NodeId id, std::string name, std::vector<std::string> labels,
                      ValueBinding binding, PaintRenderer* renderer, Vec2 size)
{
    NodeId sid = id;
    const int count = (int)labels.size();
    auto node = Node::make(std::move(id), Role::Button);
    if (size.x <= 0.0f)
        size.x = optionRowWidth(labels, 26.0f, 64.0f);
    if (size.y <= 0.0f)
        size.y = 26.0f;
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);

    ValueBinding indexBinding = optionIndexBinding(std::move(binding), labels);
    Semantics sem = named(Role::Button, name);
    sem.description = "Cycles through options";
    setBindingSemantics(sem, indexBinding);
    node->setSemantics(sem);
    node->setValueBinding(std::move(indexBinding));
    node->setOnActivate([count](Node& n) {
        const ValueBinding* b = n.valueBinding();
        if (!b || !b->get || !b->set || count <= 0)
            return;
        const int cur = std::clamp((int)std::lround(b->get()), 0, count - 1);
        setBindingValue(n, (double)((cur + 1) % count));
    });

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::CycleButton;
        style.segments = std::move(labels);
        renderer->setStyle(sid, style);
    }
    return node;
}

Node::Ptr ledButton(NodeId id, std::string name, std::string glyph,
                    ValueBinding binding, bool blink, PaintRenderer* renderer,
                    Vec2 size, ImU32 ledColor, IconFont font)
{
    NodeId sid = id;
    auto node = Node::make(std::move(id), Role::Toggle);
    node->setIntrinsicSize(size);
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
        style.kind = VisualKind::LedButton;
        style.glyph = std::move(glyph);
        style.iconFont = font;
        style.accent = ledColor;
        style.ledBlink = blink;
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

Node::Ptr checkbox(NodeId id, std::string name, ValueBinding binding,
                   PaintRenderer* renderer, Vec2 size)
{
    NodeId sid = id;
    if (size.y <= 0.0f)
        size.y = 22.0f;
    if (size.x <= 0.0f)
        size.x = std::max(22.0f, 26.0f + 7.5f * (float)name.size());
    if (binding.step <= 0.0)
        binding.step = 1.0;
    if (!binding.format) {
        binding.format = [](double value) {
            return value >= 0.5 ? std::string("Checked")
                                : std::string("Unchecked");
        };
    }

    auto node = Node::make(std::move(id), Role::Toggle);
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);

    Semantics sem = named(Role::Toggle, name);
    sem.description = "Checkbox";
    setBindingSemantics(sem, binding);
    const double mid = (binding.min + binding.max) * 0.5;
    if (sem.value.value >= mid)
        sem.states |= SemanticState::Checked;
    node->setSemantics(sem);
    node->setValueBinding(std::move(binding));
    node->setOnActivate([](Node& n) {
        const ValueBinding* b = n.valueBinding();
        if (!b || !b->get || !b->set)
            return;
        const bool next = b->get() < (b->min + b->max) * 0.5;
        if (!setBindingValue(n, next ? b->max : b->min))
            return;
        Semantics& sem = n.semantics();
        if (next)
            sem.states |= SemanticState::Checked;
        else
            sem.states &= ~stateMask(SemanticState::Checked);
    });

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Checkbox;
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

Node::Ptr knobDb(NodeId id, std::string name, ValueBinding binding,
                 double minDb, double maxDb, PaintRenderer* renderer,
                 float diameter, paint::KnobPainter painter)
{
    binding.min = minDb;
    binding.max = maxDb;
    if (!binding.format) {
        binding.format = [](double value) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.1f dB", value);
            return std::string(buf);
        };
    }
    return knob(std::move(id), std::move(name), std::move(binding), renderer,
                KnobStyle::Ring, false, diameter, std::move(painter));
}

Node::Ptr knob(NodeId id, std::string name, ValueBinding binding,
               std::function<KnobMod()> mod, PaintRenderer* renderer,
               KnobStyle knobStyle, bool bipolar, float diameter,
               paint::KnobPainter painter)
{
    NodeId sid = id;
    auto node = knob(std::move(id), std::move(name), std::move(binding),
                     renderer, knobStyle, bipolar, diameter, std::move(painter));
    if (renderer && mod) {
        if (const VisualStyle* base = renderer->styleFor(sid)) {
            VisualStyle style = *base;
            style.knobMod = std::move(mod);
            renderer->setStyle(sid, style);
        }
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
