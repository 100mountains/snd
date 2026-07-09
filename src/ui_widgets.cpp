// snd::ui audio widget set. Knobs wrap the vendored imgui-knobs (MIT,
// pinned in CMake); toggles, LEDs, meters, faders, badges and headers are
// SND's own ImDrawList drawing.

#include "snd/ui.h"
#include "snd/ui_paint.h"

#include "imgui-knobs.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace snd::ui {

namespace {

Palette gPalette;

} // namespace

void setPalette(const Palette& p) { gPalette = p; }
const Palette& palette() { return gPalette; }

bool knob(const char* label, float* value, float size, const char* format)
{
    if (size <= 0.0f)
        size = ImGui::GetFontSize() * 3.4f;
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, paint::toVec4(gPalette.accent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, paint::toVec4(gPalette.accentDim));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, paint::toVec4(gPalette.frame));
    bool changed = ImGuiKnobs::Knob(label, value, 0.0f, 1.0f, 0.0f, format,
                                    ImGuiKnobVariant_Wiper, size);
    ImGui::PopStyleColor(3);
    return changed;
}

bool knobDb(const char* label, float* db, float minDb, float maxDb, float size)
{
    if (size <= 0.0f)
        size = ImGui::GetFontSize() * 3.4f;
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, paint::toVec4(gPalette.accent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, paint::toVec4(gPalette.accentDim));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, paint::toVec4(gPalette.frame));
    bool changed = ImGuiKnobs::Knob(label, db, minDb, maxDb, 0.0f, "%.1f dB",
                                    ImGuiKnobVariant_Tick, size);
    ImGui::PopStyleColor(3);
    return changed;
}

bool knob(const char* label, float* value, float minV, float maxV,
          KnobStyle style, float size, const char* format, bool bipolar,
          ImU32 accent)
{
    const float fs = ImGui::GetFontSize();
    if (size <= 0.0f)
        size = fs * 3.2f;
    if (accent == 0)
        accent = gPalette.accent;

    // display label = text before "##"
    const char* labelEnd = label ? label : "";
    while (*labelEnd && !(labelEnd[0] == '#' && labelEnd[1] == '#'))
        ++labelEnd;
    const bool hasLabel = labelEnd != label;
    const float labelH = hasLabel ? fs * 0.90f + 3.0f : 0.0f;
    const float valueH = format ? fs * 0.80f + 1.0f : 0.0f;

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label && label[0] ? label : "##knob",
                           ImVec2(size, size + labelH + valueH));
    bool changed = false;
    if (ImGui::IsItemActive()) {
        float dy = ImGui::GetIO().MouseDelta.y;
        if (dy != 0.0f) {
            float speed = (maxV - minV) / 220.0f;
            if (ImGui::GetIO().KeyShift)
                speed *= 0.15f; // fine adjust
            float lo = std::min(minV, maxV), hi = std::max(minV, maxV);
            float nv = std::clamp(*value - dy * speed, lo, hi);
            if (nv != *value) {
                *value = nv;
                changed = true;
            }
        }
    }
    const float R = size * 0.5f;
    const ImVec2 c(p.x + R, p.y + R);
    float frac = (maxV != minV) ? (*value - minV) / (maxV - minV) : 0.0f;
    frac = std::clamp(frac, 0.0f, 1.0f);
    auto* dl = ImGui::GetWindowDrawList();
    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = ImGui::IsItemFocused();
    paint::drawKnob(dl, p, size, frac, style, gPalette, state, bipolar, accent);

    ImFont* font = ImGui::GetFont();
    if (hasLabel) {
        const float w = font->CalcTextSizeA(fs * 0.90f, FLT_MAX, 0.0f, label, labelEnd).x;
        dl->AddText(font, fs * 0.90f, ImVec2(c.x - w * 0.5f, p.y + size + 2.0f),
                    gPalette.text, label, labelEnd);
    }
    if (format) {
        char vbuf[32];
        std::snprintf(vbuf, sizeof vbuf, format, *value);
        const float w = font->CalcTextSizeA(fs * 0.80f, FLT_MAX, 0.0f, vbuf).x;
        dl->AddText(font, fs * 0.80f, ImVec2(c.x - w * 0.5f, p.y + size + labelH),
                    gPalette.textDim, vbuf);
    }
    return changed;
}

bool toggle(const char* label, bool* on)
{
    const float h = ImGui::GetFrameHeight() * 0.80f;
    const float w = h * 1.85f;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    float fullW = w + (labelSize.x > 0 ? style.ItemInnerSpacing.x + labelSize.x : 0);

    ImVec2 p = ImGui::GetCursorScreenPos();
    bool changed = ImGui::InvisibleButton(label, ImVec2(fullW, std::max(h, labelSize.y)));
    if (changed)
        *on = !*on;

    // slide the nub with a little per-widget animation state
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID id = ImGui::GetItemID();
    float target = *on ? 1.0f : 0.0f;
    float anim = store->GetFloat(id, target);
    float step = ImGui::GetIO().DeltaTime * 9.0f;
    anim = std::clamp(anim + std::clamp(target - anim, -step, step), 0.0f, 1.0f);
    store->SetFloat(id, anim);

    auto* dl = ImGui::GetWindowDrawList();
    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = ImGui::IsItemFocused();
    paint::drawToggle(dl, p, w, h, anim, gPalette, state);

    if (labelSize.x > 0)
        dl->AddText(ImVec2(p.x + w + style.ItemInnerSpacing.x,
                           p.y + (h - ImGui::GetFontSize()) * 0.5f),
                    gPalette.text, label);
    return changed;
}

bool led(const char* id, bool on, float radius, bool clickable, ImU32 onColor)
{
    if (onColor == 0)
        onColor = gPalette.accent;
    const float pad = radius * 0.9f;
    const ImVec2 sz(radius * 2.0f + pad * 2.0f, radius * 2.0f + pad * 2.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();

    bool clicked = false;
    if (clickable)
        clicked = ImGui::InvisibleButton(id, sz);
    else
        ImGui::Dummy(sz);

    ImVec2 c(p.x + sz.x * 0.5f, p.y + sz.y * 0.5f);
    auto* dl = ImGui::GetWindowDrawList();
    paint::ControlState state;
    state.hovered = clickable && ImGui::IsItemHovered();
    state.active = clickable && ImGui::IsItemActive();
    state.focused = clickable && ImGui::IsItemFocused();
    paint::drawLed(dl, c, radius, on, gPalette, state, onColor);
    return clicked;
}

void meter(const char* id, MeterState& st, float level, const ImVec2& size,
           float floorDb)
{
    // ballistics: fast attack, ~10%/frame release, peak held ~1.5s
    st.shown = std::max(level, st.shown * 0.90f);
    if (st.shown < 1e-5f)
        st.shown = 0.0f;
    if (level >= st.peak || st.peakAge > 90.0f) {
        st.peak = level;
        st.peakAge = 0.0f;
    }
    st.peakAge += 1.0f;

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    auto* dl = ImGui::GetWindowDrawList();
    paint::drawMeter(dl, p, size, st.shown, st.peak, floorDb, gPalette);
    (void)id;
}

bool fader(const char* id, float* value, const ImVec2& size)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    bool changed = false;
    if (ImGui::IsItemActive()) {
        const float capH = 14.0f;
        float rel = 1.0f - (ImGui::GetIO().MousePos.y - p.y - capH * 0.5f) /
                              std::max(1.0f, size.y - capH);
        float nv = std::clamp(rel, 0.0f, 1.0f);
        changed = nv != *value;
        *value = nv;
    }

    auto* dl = ImGui::GetWindowDrawList();
    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = ImGui::IsItemFocused();
    paint::drawFader(dl, p, size, *value, gPalette, state);
    return changed;
}

void badge(const char* text, ImU32 fill)
{
    if (fill == 0)
        fill = paint::withAlpha(gPalette.accent, 0x46);
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize() * 0.78f;
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz(ts.x + 10.0f, fs + 5.0f);
    ImGui::Dummy(sz);
    paint::drawBadge(ImGui::GetWindowDrawList(), font, p, text, fs, fill, gPalette);
}

bool iconButton(const char* id, const char* glyph, const ImVec2& size, ImFont* font,
                bool toggled, ImU32 face)
{
    ImVec2 sz = size;
    if (sz.x <= 0.0f)
        sz.x = sz.y = ImGui::GetFrameHeight() * 1.35f;
    else if (sz.y <= 0.0f)
        sz.y = sz.x;

    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, sz);
    const bool down = ImGui::IsItemActive() || toggled; // pressed / inset look
    ImFont* f = font ? font : ImGui::GetFont();
    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = ImGui::IsItemFocused();
    state.selected = toggled;
    paint::drawTactileIconButton(ImGui::GetWindowDrawList(), f, p, sz, glyph,
                                 gPalette, state, down, face);
    return clicked;
}

bool keyboard(const char* id, KeyboardState& st, const ImVec2& size, int firstNote,
              int octaves, const std::function<void(uint8_t, uint8_t)>& noteOn,
              const std::function<void(uint8_t)>& noteOff, const bool* lit)
{
    const int whites = octaves * 7;
    if (whites <= 0)
        return false;

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool active = ImGui::IsItemActive();
    auto* dl = ImGui::GetWindowDrawList();

    const float ww = size.x / (float)whites; // white key width
    const float bw = ww * 0.62f;             // black key width
    const float bh = size.y * 0.60f;         // black key height

    // semitone offsets within an octave
    static const int whiteSemi[7] = {0, 2, 4, 5, 7, 9, 11};
    static const int blackAfterWhite[7] = {1, 1, 0, 1, 1, 1, 0}; // C D (no E) F G A (no B)
    static const int blackSemi[7] = {1, 3, 0, 6, 8, 10, 0};

    // hit test: black keys first (they sit on top)
    int hitNote = -1;
    float hitDepth = 0.0f; // 0 at key top .. 1 at its bottom, for velocity
    if (ImGui::IsItemHovered() || active) {
        ImVec2 m = ImGui::GetIO().MousePos;
        float lx = m.x - p.x, ly = m.y - p.y;
        if (lx >= 0 && lx < size.x && ly >= 0 && ly < size.y) {
            if (ly < bh) {
                for (int w = 0; w < whites && hitNote < 0; ++w) {
                    int inOct = w % 7;
                    if (!blackAfterWhite[inOct])
                        continue;
                    float bx = (w + 1) * ww - bw * 0.5f;
                    if (lx >= bx && lx < bx + bw)
                        hitNote = firstNote + (w / 7) * 12 + blackSemi[inOct];
                }
                if (hitNote >= 0)
                    hitDepth = ly / bh;
            }
            if (hitNote < 0) {
                int w = std::min(whites - 1, (int)(lx / ww));
                hitNote = firstNote + (w / 7) * 12 + whiteSemi[w % 7];
                hitDepth = ly / size.y;
            }
        }
    }

    // transitions
    bool played = false;
    int want = active ? hitNote : -1;
    if (want != st.mouseNote) {
        if (st.mouseNote >= 0 && noteOff)
            noteOff((uint8_t)st.mouseNote);
        if (want >= 0 && want < 128 && noteOn) {
            uint8_t vel = (uint8_t)std::clamp(40.0f + hitDepth * 87.0f, 1.0f, 127.0f);
            noteOn((uint8_t)want, vel);
            played = true;
        }
        st.mouseNote = want >= 0 && want < 128 ? want : -1;
    }

    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = ImGui::IsItemFocused();
    paint::drawKeyboard(dl, p, size, firstNote, octaves, st.mouseNote, lit,
                        gPalette, state);
    return played;
}

void sectionHeader(const char* text)
{
    char buf[64];
    size_t n = std::min(sizeof(buf) - 1, std::strlen(text));
    for (size_t i = 0; i < n; ++i)
        buf[i] = (char)std::toupper((unsigned char)text[i]);
    buf[n] = 0;

    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize() * 0.80f;
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, buf);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = std::max(ts.x, ImGui::GetContentRegionAvail().x);
    ImGui::Dummy(ImVec2(w, fs + 8.0f));
    paint::drawSectionHeader(ImGui::GetWindowDrawList(), font, p, buf, fs, w, gPalette);
}

bool patternGrid(const char* id, bool* cells, int rows, int steps,
                 const ImVec2& size, int playheadStep)
{
    if (rows <= 0 || steps <= 0 || !cells)
        return false;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);

    const float cw = size.x / steps;
    const float ch = size.y / rows;
    bool changed = false;

    if (ImGui::IsItemActive()) {
        ImVec2 m = ImGui::GetIO().MousePos;
        int c = (int)((m.x - p.x) / cw), r = (int)((m.y - p.y) / ch);
        if (c >= 0 && c < steps && r >= 0 && r < rows) {
            bool want = !ImGui::GetIO().KeyAlt; // drag paints, alt-drag erases
            if (cells[r * steps + c] != want) {
                cells[r * steps + c] = want;
                changed = true;
            }
        }
    }

    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = ImGui::IsItemFocused();
    paint::drawPatternGrid(ImGui::GetWindowDrawList(), p, size, cells, rows, steps,
                           playheadStep, gPalette, state);
    return changed;
}

// Segment ease used when tensions are present: +t = slow start, -t = fast
// start (matches Bob's envelope contours so edited shapes sound identical).
static float easeSegment(float t, float ten)
{
    ten = std::clamp(ten, -1.0f, 1.0f);
    if (ten > 0.0f)
        return std::pow(t, 1.0f + ten * 3.0f);
    if (ten < 0.0f)
        return 1.0f - std::pow(1.0f - t, 1.0f - ten * 3.0f);
    return t;
}

bool envelopeEditor(const char* id, std::vector<EnvPoint>& points, const ImVec2& size,
                    std::vector<float>* tensions)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    auto* dl = ImGui::GetWindowDrawList();
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID dragKey = ImGui::GetItemID();
    ImGuiID segKey = dragKey + 1;

    if (points.empty())
        points = {{0.0f, 0.5f}, {1.0f, 0.5f}};
    if (tensions)
        tensions->resize(points.size(), 0.0f);

    auto toScreen = [&](const EnvPoint& e) {
        return ImVec2(p.x + e.x * size.x, p.y + (1.0f - e.y) * size.y);
    };
    auto segTen = [&](size_t i) {
        return tensions && i < tensions->size() ? (*tensions)[i] : 0.0f;
    };
    // envelope value at normalized x, honoring tensions
    auto evalAt = [&](float x) {
        size_t i = 0;
        while (i + 2 < points.size() && x > points[i + 1].x)
            ++i;
        const auto& a = points[i];
        const auto& b = points[i + 1];
        float t = std::clamp((x - a.x) / std::max(1e-6f, b.x - a.x), 0.0f, 1.0f);
        return a.y + (b.y - a.y) * easeSegment(t, segTen(i));
    };

    ImVec2 m = ImGui::GetIO().MousePos;
    const float grab = 8.0f;
    bool changed = false;

    int hot = -1;
    for (int i = 0; i < (int)points.size(); ++i) {
        ImVec2 s = toScreen(points[i]);
        if (std::fabs(m.x - s.x) < grab && std::fabs(m.y - s.y) < grab) {
            hot = i;
            break;
        }
    }
    // hot segment: near the curve but not a point (only when bending exists)
    int hotSeg = -1;
    if (tensions && hot < 0 && points.size() >= 2 && m.x >= p.x &&
        m.x <= p.x + size.x) {
        float nx = std::clamp((m.x - p.x) / size.x, 0.0f, 1.0f);
        float sy = p.y + (1.0f - evalAt(nx)) * size.y;
        if (std::fabs(m.y - sy) < grab) {
            size_t i = 0;
            while (i + 2 < points.size() && nx > points[i + 1].x)
                ++i;
            hotSeg = (int)i;
        }
    }

    int dragging = store->GetInt(dragKey, -1);
    int dragSeg = store->GetInt(segKey, -1);
    if (ImGui::IsItemActivated()) {
        store->SetInt(dragKey, dragging = hot);
        store->SetInt(segKey, dragSeg = hot < 0 ? hotSeg : -1);
    }
    if (!ImGui::IsItemActive()) {
        store->SetInt(dragKey, dragging = -1);
        store->SetInt(segKey, dragSeg = -1);
    }

    if (dragging >= 0 && dragging < (int)points.size()) {
        auto& e = points[(size_t)dragging];
        float lo = dragging == 0 ? 0.0f : points[(size_t)dragging - 1].x;
        float hi = dragging == (int)points.size() - 1 ? 1.0f
                                                      : points[(size_t)dragging + 1].x;
        if (dragging == 0)
            hi = 0.0f; // endpoints pinned in x
        if (dragging == (int)points.size() - 1)
            lo = 1.0f;
        e.x = std::clamp((m.x - p.x) / size.x, std::min(lo, hi), std::max(lo, hi));
        e.y = std::clamp(1.0f - (m.y - p.y) / size.y, 0.0f, 1.0f);
        changed = true;
    } else if (tensions && dragSeg >= 0 && dragSeg + 1 < (int)points.size()) {
        // vertical drag bends the segment toward the mouse
        float dy = ImGui::GetIO().MouseDelta.y;
        if (dy != 0.0f) {
            float dir = points[(size_t)dragSeg + 1].y >= points[(size_t)dragSeg].y
                            ? 1.0f
                            : -1.0f;
            (*tensions)[(size_t)dragSeg] = std::clamp(
                (*tensions)[(size_t)dragSeg] + dir * dy * (3.0f / size.y), -1.0f,
                1.0f);
            changed = true;
        }
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && hot < 0) {
        EnvPoint e{std::clamp((m.x - p.x) / size.x, 0.0f, 1.0f),
                   std::clamp(1.0f - (m.y - p.y) / size.y, 0.0f, 1.0f)};
        auto it = std::find_if(points.begin(), points.end(),
                               [&](const EnvPoint& q) { return q.x > e.x; });
        if (tensions)
            tensions->insert(tensions->begin() + (it - points.begin()), 0.0f);
        points.insert(it, e);
        changed = true;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1) && hot > 0 &&
        hot < (int)points.size() - 1) {
        points.erase(points.begin() + hot);
        if (tensions && hot < (int)tensions->size())
            tensions->erase(tensions->begin() + hot);
        changed = true;
    }

    // draw
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), gPalette.frame, 3.0f);
    for (size_t i = 1; i < points.size(); ++i) {
        float ten = segTen(i - 1);
        ImU32 col = tensions && (int)(i - 1) == (hotSeg >= 0 ? hotSeg : dragSeg)
                        ? gPalette.text
                        : gPalette.accent;
        if (ten == 0.0f) {
            dl->AddLine(toScreen(points[i - 1]), toScreen(points[i]), col, 2.0f);
        } else {
            const int steps = 16;
            ImVec2 prev = toScreen(points[i - 1]);
            for (int s = 1; s <= steps; ++s) {
                float t = (float)s / steps;
                EnvPoint q{points[i - 1].x + (points[i].x - points[i - 1].x) * t,
                           points[i - 1].y +
                               (points[i].y - points[i - 1].y) * easeSegment(t, ten)};
                ImVec2 cur = toScreen(q);
                dl->AddLine(prev, cur, col, 2.0f);
                prev = cur;
            }
        }
    }
    for (int i = 0; i < (int)points.size(); ++i) {
        ImVec2 s = toScreen(points[(size_t)i]);
        dl->AddCircleFilled(s, i == hot || i == dragging ? 6.0f : 4.5f,
                            i == hot || i == dragging ? gPalette.text
                                                      : gPalette.accent);
    }
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), gPalette.frameBright, 3.0f);
    if (ImGui::IsItemFocused())
        paint::drawFocusRing(dl, p, ImVec2(p.x + size.x, p.y + size.y), gPalette, 3.0f);
    return changed;
}

bool xyPad(const char* id, float* x, float* y, const ImVec2& size)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    bool changed = false;

    if (ImGui::IsItemActive()) {
        ImVec2 m = ImGui::GetIO().MousePos;
        float nx = std::clamp((m.x - p.x) / size.x, 0.0f, 1.0f);
        float ny = std::clamp(1.0f - (m.y - p.y) / size.y, 0.0f, 1.0f);
        changed = nx != *x || ny != *y;
        *x = nx;
        *y = ny;
    }

    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = ImGui::IsItemFocused();
    paint::drawXYPad(ImGui::GetWindowDrawList(), p, size, *x, *y, gPalette, state);
    return changed;
}

bool selectableList(const char* id, const std::vector<std::string>& items,
                    int* selected, const ImVec2& size)
{
    bool changed = false;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, paint::toVec4(gPalette.frame));
    if (ImGui::BeginChild(id, size, ImGuiChildFlags_Borders)) {
        for (int i = 0; i < (int)items.size(); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(items[(size_t)i].c_str(), selected && *selected == i)) {
                if (selected && *selected != i) {
                    *selected = i;
                    changed = true;
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    return changed;
}

bool dragNumber(const char* label, float* value, float speed, float minV, float maxV,
                const char* format)
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, paint::toVec4(gPalette.frame));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, paint::toVec4(gPalette.frameBright));
    ImGui::PushStyleColor(ImGuiCol_Text, paint::toVec4(gPalette.text));
    bool changed = ImGui::DragFloat(label, value, speed, minV, maxV, format);
    ImGui::PopStyleColor(3);
    return changed;
}

bool fileBrowser(const char* id, FileBrowserState& st, const ImVec2& size,
                 std::string* outPath, const char* extensions)
{
    namespace fs = std::filesystem;
    if (st.dir.empty()) {
        const char* home = getenv("HOME");
#if defined(_WIN32)
        if (!home)
            home = getenv("USERPROFILE");
#endif
        st.dir = home ? home : ".";
    }

    auto matches = [&](const fs::path& path) {
        if (!extensions)
            return true;
        auto ext = path.extension().string();
        if (!ext.empty() && ext[0] == '.')
            ext.erase(0, 1);
        for (auto& c : ext)
            c = (char)tolower((unsigned char)c);
        std::string list = extensions;
        size_t pos = 0;
        while (pos != std::string::npos) {
            size_t comma = list.find(',', pos);
            std::string one = list.substr(
                pos, comma == std::string::npos ? std::string::npos : comma - pos);
            if (one == ext)
                return true;
            pos = comma == std::string::npos ? comma : comma + 1;
        }
        return false;
    };

    bool picked = false;
    ImGui::PushID(id);
    ImGui::BeginGroup();

    // breadcrumb: up button + current dir
    if (ImGui::SmallButton("^ up")) {
        fs::path parent = fs::path(st.dir).parent_path();
        if (!parent.empty())
            st.dir = parent.string();
        st.selected.clear();
    }
    ImGui::SameLine();
    ImGui::TextColored(paint::toVec4(gPalette.textDim), "%s", st.dir.c_str());

    ImGui::PushStyleColor(ImGuiCol_ChildBg, paint::toVec4(gPalette.frame));
    if (ImGui::BeginChild("##files", ImVec2(size.x, size.y - ImGui::GetFrameHeight()),
                          ImGuiChildFlags_Borders)) {
        std::error_code ec;
        std::vector<fs::directory_entry> dirs, files;
        for (auto it = fs::directory_iterator(st.dir, ec);
             !ec && it != fs::directory_iterator(); it.increment(ec)) {
            auto name = it->path().filename().string();
            if (!name.empty() && name[0] == '.')
                continue; // hidden
            if (it->is_directory(ec))
                dirs.push_back(*it);
            else if (matches(it->path()))
                files.push_back(*it);
        }
        auto byName = [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.path().filename() < b.path().filename();
        };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);

        for (auto& d : dirs) {
            std::string label = "[dir] " + d.path().filename().string();
            if (ImGui::Selectable(label.c_str(), false,
                                  ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(0)) {
                st.dir = d.path().string();
                st.selected.clear();
            }
        }
        for (auto& f : files) {
            std::string name = f.path().filename().string();
            bool sel = st.selected == f.path().string();
            if (ImGui::Selectable(name.c_str(), sel,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                st.selected = f.path().string();
                if (ImGui::IsMouseDoubleClicked(0) && outPath) {
                    *outPath = st.selected;
                    picked = true;
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    ImGui::PopID();
    return picked;
}

} // namespace snd::ui
