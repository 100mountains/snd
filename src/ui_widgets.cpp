// snd::ui audio widget set. Knobs wrap the vendored imgui-knobs (MIT,
// pinned in CMake); toggles, LEDs, meters, faders, badges and headers are
// SND's own ImDrawList drawing.

#include "snd/ui.h"

#include "imgui-knobs.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>

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

} // namespace snd::ui
