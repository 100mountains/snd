// snd::ui audio widget set. Knobs wrap the vendored imgui-knobs (MIT,
// pinned in CMake); toggles, LEDs, meters, faders, badges and headers are
// SND's own ImDrawList drawing.

#include "snd/ui.h"

#include "imgui-knobs.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace snd::ui {

namespace {

Palette gPalette;

ImU32 withAlpha(ImU32 c, uint32_t a) { return (c & 0x00FFFFFF) | (a << 24); }

ImVec4 toVec4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

ImU32 mixCol(ImU32 a, ImU32 b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    ImU32 out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        uint32_t ca = (a >> shift) & 0xFF, cb = (b >> shift) & 0xFF;
        out |= (uint32_t)(ca + (cb - (float)ca) * t + 0.5f) << shift;
    }
    return out;
}

// linear amplitude -> 0..1 position on a floorDb..0 scale
float dbNorm(float lin, float floorDb)
{
    if (lin <= 0.0f)
        return 0.0f;
    float db = 20.0f * std::log10(lin);
    return std::clamp(1.0f - db / floorDb, 0.0f, 1.0f);
}

} // namespace

void setPalette(const Palette& p) { gPalette = p; }
const Palette& palette() { return gPalette; }

bool knob(const char* label, float* value, float size, const char* format)
{
    if (size <= 0.0f)
        size = ImGui::GetFontSize() * 3.4f;
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, toVec4(gPalette.accent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toVec4(gPalette.accentDim));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, toVec4(gPalette.frame));
    bool changed = ImGuiKnobs::Knob(label, value, 0.0f, 1.0f, 0.0f, format,
                                    ImGuiKnobVariant_Wiper, size);
    ImGui::PopStyleColor(3);
    return changed;
}

bool knobDb(const char* label, float* db, float minDb, float maxDb, float size)
{
    if (size <= 0.0f)
        size = ImGui::GetFontSize() * 3.4f;
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, toVec4(gPalette.accent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toVec4(gPalette.accentDim));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, toVec4(gPalette.frame));
    bool changed = ImGuiKnobs::Knob(label, db, minDb, maxDb, 0.0f, "%.1f dB",
                                    ImGuiKnobVariant_Tick, size);
    ImGui::PopStyleColor(3);
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
    const float r = h * 0.5f;
    ImU32 track = mixCol(gPalette.frame, gPalette.accent, anim);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), track, r);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), gPalette.frameBright, r);

    float nx = p.x + r + anim * (w - h);
    ImU32 nub = ImGui::IsItemHovered() ? IM_COL32(255, 255, 255, 255)
                                       : IM_COL32(228, 230, 238, 255);
    dl->AddCircleFilled(ImVec2(nx, p.y + r), r - 2.5f, nub);

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
    if (on) {
        dl->AddCircleFilled(c, radius * 2.4f, withAlpha(onColor, 0x22));
        dl->AddCircleFilled(c, radius * 1.7f, withAlpha(onColor, 0x55));
        dl->AddCircleFilled(c, radius, onColor);
        dl->AddCircleFilled(ImVec2(c.x - radius * 0.3f, c.y - radius * 0.35f),
                            radius * 0.35f, IM_COL32(255, 255, 255, 180));
    } else {
        dl->AddCircleFilled(c, radius, gPalette.ledOff);
        dl->AddCircle(c, radius, gPalette.frameBright);
    }
    if (clickable && ImGui::IsItemHovered())
        dl->AddCircle(c, radius * 1.9f, gPalette.textDim);
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

    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), gPalette.frame, 2.0f);

    const bool horizontal = size.x > size.y;
    const float fill = dbNorm(st.shown, floorDb);
    const float z1 = dbNorm(std::pow(10.0f, -12.0f / 20.0f), floorDb);
    const float z2 = dbNorm(std::pow(10.0f, -3.0f / 20.0f), floorDb);

    // three hardware zones, each clipped by the fill level
    struct Zone {
        float a, b;
        ImU32 col;
    } zones[3] = {{0.0f, z1, gPalette.meterLow},
                  {z1, z2, gPalette.meterMid},
                  {z2, 1.0f, gPalette.meterHot}};
    for (auto& z : zones) {
        float a = z.a, b = std::min(z.b, fill);
        if (b <= a)
            continue;
        if (horizontal)
            dl->AddRectFilled(ImVec2(p.x + size.x * a, p.y),
                              ImVec2(p.x + size.x * b, p.y + size.y), z.col, 1.5f);
        else
            dl->AddRectFilled(ImVec2(p.x, p.y + size.y * (1.0f - b)),
                              ImVec2(p.x + size.x, p.y + size.y * (1.0f - a)), z.col,
                              1.5f);
    }

    // peak-hold tick
    float pk = dbNorm(st.peak, floorDb);
    if (pk > 0.001f) {
        ImU32 pc = pk > z2 ? gPalette.meterHot : pk > z1 ? gPalette.meterMid
                                                         : gPalette.meterLow;
        if (horizontal) {
            float x = p.x + size.x * pk;
            dl->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + size.y), pc, 1.0f);
        } else {
            float y = p.y + size.y * (1.0f - pk);
            dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + size.x, y), pc, 1.0f);
        }
    }

    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), gPalette.frameBright, 2.0f);
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
    const float capH = 14.0f;
    const float cx = p.x + size.x * 0.5f;

    // slot
    dl->AddRectFilled(ImVec2(cx - 2.0f, p.y), ImVec2(cx + 2.0f, p.y + size.y),
                      gPalette.frame, 2.0f);
    dl->AddRect(ImVec2(cx - 2.0f, p.y), ImVec2(cx + 2.0f, p.y + size.y),
                gPalette.frameBright, 2.0f);

    // travel below the cap picks up the accent
    float capY = p.y + (1.0f - *value) * (size.y - capH);
    dl->AddRectFilled(ImVec2(cx - 2.0f, capY + capH * 0.5f),
                      ImVec2(cx + 2.0f, p.y + size.y),
                      withAlpha(gPalette.accent, 0x66), 2.0f);

    // cap
    bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    dl->AddRectFilled(ImVec2(p.x, capY), ImVec2(p.x + size.x, capY + capH),
                      hot ? gPalette.frameBright : gPalette.frame, 3.0f);
    dl->AddRect(ImVec2(p.x, capY), ImVec2(p.x + size.x, capY + capH),
                gPalette.frameBright, 3.0f);
    dl->AddLine(ImVec2(p.x + 2.0f, capY + capH * 0.5f),
                ImVec2(p.x + size.x - 2.0f, capY + capH * 0.5f), gPalette.accent,
                1.5f);
    return changed;
}

void badge(const char* text, ImU32 fill)
{
    if (fill == 0)
        fill = withAlpha(gPalette.accent, 0x46);
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize() * 0.78f;
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz(ts.x + 10.0f, fs + 5.0f);
    ImGui::Dummy(sz);
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), fill, 3.0f);
    dl->AddText(font, fs, ImVec2(p.x + 5.0f, p.y + 2.5f), gPalette.text, text);
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

    auto isDown = [&](int note) {
        return note == st.mouseNote || (lit && note >= 0 && note < 128 && lit[note]);
    };

    // draw: white keys
    for (int w = 0; w < whites; ++w) {
        int note = firstNote + (w / 7) * 12 + whiteSemi[w % 7];
        ImVec2 a(p.x + w * ww, p.y), b(p.x + (w + 1) * ww - 1.0f, p.y + size.y);
        dl->AddRectFilled(a, b,
                          isDown(note) ? gPalette.accent : IM_COL32(232, 233, 238, 255),
                          2.0f, ImDrawFlags_RoundCornersBottom);
        dl->AddRect(a, b, IM_COL32(40, 42, 50, 255), 2.0f,
                    ImDrawFlags_RoundCornersBottom);
    }
    // black keys on top
    for (int w = 0; w < whites; ++w) {
        int inOct = w % 7;
        if (!blackAfterWhite[inOct] || w == whites - 1)
            continue;
        int note = firstNote + (w / 7) * 12 + blackSemi[inOct];
        float bx = p.x + (w + 1) * ww - bw * 0.5f;
        ImVec2 a(bx, p.y), b(bx + bw, p.y + bh);
        dl->AddRectFilled(a, b,
                          isDown(note) ? gPalette.accentDim : IM_COL32(18, 19, 26, 255),
                          2.0f, ImDrawFlags_RoundCornersBottom);
        dl->AddRect(a, b, IM_COL32(0, 0, 0, 255), 2.0f, ImDrawFlags_RoundCornersBottom);
    }
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
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddText(font, fs, ImVec2(p.x, p.y + 4.0f), gPalette.textDim, buf);
    if (w > ts.x + 12.0f)
        dl->AddLine(ImVec2(p.x + ts.x + 8.0f, p.y + 4.0f + fs * 0.55f),
                    ImVec2(p.x + w, p.y + 4.0f + fs * 0.55f),
                    withAlpha(gPalette.frameBright, 0xAA), 1.0f);
}

bool patternGrid(const char* id, bool* cells, int rows, int steps,
                 const ImVec2& size, int playheadStep)
{
    if (rows <= 0 || steps <= 0 || !cells)
        return false;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    auto* dl = ImGui::GetWindowDrawList();

    const float cw = size.x / steps, ch = size.y / rows;
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

    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), gPalette.frame, 3.0f);
    if (playheadStep >= 0 && playheadStep < steps)
        dl->AddRectFilled(ImVec2(p.x + playheadStep * cw, p.y),
                          ImVec2(p.x + (playheadStep + 1) * cw, p.y + size.y),
                          withAlpha(gPalette.accent, 0x24));

    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < steps; ++c) {
            ImVec2 a(p.x + c * cw + 1.5f, p.y + r * ch + 1.5f);
            ImVec2 b(p.x + (c + 1) * cw - 1.5f, p.y + (r + 1) * ch - 1.5f);
            bool on = cells[r * steps + c];
            ImU32 col = on ? (c == playheadStep ? gPalette.text : gPalette.accent)
                           : withAlpha(gPalette.frameBright,
                                       (c / 4) % 2 ? 0x30 : 0x55);
            dl->AddRectFilled(a, b, col, 2.0f);
        }
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), gPalette.frameBright, 3.0f);
    return changed;
}

bool envelopeEditor(const char* id, std::vector<EnvPoint>& points, const ImVec2& size)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    auto* dl = ImGui::GetWindowDrawList();
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID dragKey = ImGui::GetItemID();

    if (points.empty())
        points = {{0.0f, 0.5f}, {1.0f, 0.5f}};

    auto toScreen = [&](const EnvPoint& e) {
        return ImVec2(p.x + e.x * size.x, p.y + (1.0f - e.y) * size.y);
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

    int dragging = store->GetInt(dragKey, -1);
    if (ImGui::IsItemActivated())
        store->SetInt(dragKey, dragging = hot);
    if (!ImGui::IsItemActive())
        store->SetInt(dragKey, dragging = -1);

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
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && hot < 0) {
        EnvPoint e{std::clamp((m.x - p.x) / size.x, 0.0f, 1.0f),
                   std::clamp(1.0f - (m.y - p.y) / size.y, 0.0f, 1.0f)};
        auto it = std::find_if(points.begin(), points.end(),
                               [&](const EnvPoint& q) { return q.x > e.x; });
        points.insert(it, e);
        changed = true;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1) && hot > 0 &&
        hot < (int)points.size() - 1) {
        points.erase(points.begin() + hot);
        changed = true;
    }

    // draw
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), gPalette.frame, 3.0f);
    for (size_t i = 1; i < points.size(); ++i)
        dl->AddLine(toScreen(points[i - 1]), toScreen(points[i]), gPalette.accent,
                    2.0f);
    for (int i = 0; i < (int)points.size(); ++i) {
        ImVec2 s = toScreen(points[(size_t)i]);
        dl->AddCircleFilled(s, i == hot || i == dragging ? 6.0f : 4.5f,
                            i == hot || i == dragging ? gPalette.text
                                                      : gPalette.accent);
    }
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), gPalette.frameBright, 3.0f);
    return changed;
}

bool xyPad(const char* id, float* x, float* y, const ImVec2& size)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    auto* dl = ImGui::GetWindowDrawList();
    bool changed = false;

    if (ImGui::IsItemActive()) {
        ImVec2 m = ImGui::GetIO().MousePos;
        float nx = std::clamp((m.x - p.x) / size.x, 0.0f, 1.0f);
        float ny = std::clamp(1.0f - (m.y - p.y) / size.y, 0.0f, 1.0f);
        changed = nx != *x || ny != *y;
        *x = nx;
        *y = ny;
    }

    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), gPalette.frame, 3.0f);
    // crosshair
    float cx = p.x + *x * size.x, cy = p.y + (1.0f - *y) * size.y;
    dl->AddLine(ImVec2(cx, p.y), ImVec2(cx, p.y + size.y),
                withAlpha(gPalette.accent, 0x50));
    dl->AddLine(ImVec2(p.x, cy), ImVec2(p.x + size.x, cy),
                withAlpha(gPalette.accent, 0x50));
    dl->AddCircleFilled(ImVec2(cx, cy), 6.0f, gPalette.accent);
    dl->AddCircleFilled(ImVec2(cx - 1.5f, cy - 1.8f), 1.6f,
                        IM_COL32(255, 255, 255, 170));
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), gPalette.frameBright, 3.0f);
    return changed;
}

bool selectableList(const char* id, const std::vector<std::string>& items,
                    int* selected, const ImVec2& size)
{
    bool changed = false;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toVec4(gPalette.frame));
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
    ImGui::PushStyleColor(ImGuiCol_FrameBg, toVec4(gPalette.frame));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, toVec4(gPalette.frameBright));
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(gPalette.text));
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
    ImGui::TextColored(toVec4(gPalette.textDim), "%s", st.dir.c_str());

    ImGui::PushStyleColor(ImGuiCol_ChildBg, toVec4(gPalette.frame));
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
