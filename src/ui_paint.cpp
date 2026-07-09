#include "snd/ui_paint.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace snd::ui::paint {

ImU32 withAlpha(ImU32 c, uint32_t a) { return (c & 0x00FFFFFF) | (a << 24); }

ImU32 mix(ImU32 a, ImU32 b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    ImU32 out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        uint32_t ca = (a >> shift) & 0xFF;
        uint32_t cb = (b >> shift) & 0xFF;
        out |= (uint32_t)(ca + (cb - (float)ca) * t + 0.5f) << shift;
    }
    return out;
}

ImVec4 toVec4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

float dbNorm(float linear, float floorDb)
{
    if (linear <= 0.0f)
        return 0.0f;
    float db = 20.0f * std::log10(linear);
    return std::clamp(1.0f - db / floorDb, 0.0f, 1.0f);
}

constexpr float kKnobA0 = -3.92699082f; // -225 deg
constexpr float kKnobA1 = 0.78539816f;  // +45 deg

float knobAngle(float frac)
{
    return kKnobA0 + std::clamp(frac, 0.0f, 1.0f) * (kKnobA1 - kKnobA0);
}

ImVec2 dirAt(float angle) { return ImVec2(std::cos(angle), std::sin(angle)); }

void drawFocusRing(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
                   const Palette& pal, float rounding, float expand)
{
    if (!dl)
        return;
    const ImVec2 a(min.x - expand, min.y - expand);
    const ImVec2 b(max.x + expand, max.y + expand);
    dl->AddRect(a, b, withAlpha(pal.text, 0xD8), rounding + expand, 0, 1.5f);
    dl->AddRect(ImVec2(a.x - 1.5f, a.y - 1.5f), ImVec2(b.x + 1.5f, b.y + 1.5f),
                withAlpha(pal.accent, 0xB0), rounding + expand + 1.5f, 0, 1.0f);
}

void drawKnob(ImDrawList* dl, const ImVec2& topLeft, float size, float frac,
              KnobStyle style, const Palette& pal, const ControlState& state,
              bool bipolar, ImU32 accent)
{
    if (!dl)
        return;
    if (accent == 0)
        accent = pal.accent;
    if (state.disabled)
        accent = mix(accent, pal.frameBright, 0.65f);
    frac = std::clamp(frac, 0.0f, 1.0f);

    const float radius = size * 0.5f;
    const ImVec2 c(topLeft.x + radius, topLeft.y + radius);
    const float valAng = knobAngle(frac);
    const bool hot = !state.disabled && (state.hovered || state.active);

    if (style == KnobStyle::Ring || style == KnobStyle::Seq) {
        // Butt-capped background ring + accent value sweep (murk's SEQ RingKnobLnf).
        // Ring adds a thumb dot at the value; Seq is the pure flat-ended sweep.
        const float lineW = std::min(5.0f, radius * 0.42f);
        const float rr = radius - lineW * 0.5f - 1.0f;
        dl->PathArcTo(c, rr, kKnobA0, kKnobA1, 40);
        dl->PathStroke(pal.frameBright, 0, lineW);
        const float aFrom = bipolar ? knobAngle(0.5f) : kKnobA0;
        if (valAng != aFrom) {
            dl->PathArcTo(c, rr, std::min(aFrom, valAng), std::max(aFrom, valAng), 40);
            dl->PathStroke(accent, 0, lineW);
        }
        if (style == KnobStyle::Ring) {
            const ImVec2 d = dirAt(valAng);
            dl->AddCircleFilled(ImVec2(c.x + d.x * rr, c.y + d.y * rr), lineW * 0.55f,
                                hot ? IM_COL32(255, 255, 255, 255) : accent);
        }
    } else if (style == KnobStyle::Synth) {
        // murk's FlatLnf synth knob: dished disc + faint track ring + accent value
        // arc + a single white pointer tick.
        dl->AddCircleFilled(ImVec2(c.x, c.y + 1.5f), radius, IM_COL32(0, 0, 0, 90)); // shadow
        dl->AddCircleFilled(c, radius, IM_COL32(0x1c, 0x1f, 0x25, 255));             // body base
        dl->AddCircleFilled(ImVec2(c.x, c.y - radius * 0.34f), radius * 0.62f,
                            IM_COL32(0x28, 0x2c, 0x33, 150));                        // top-lit
        dl->AddCircle(c, radius - 0.5f, IM_COL32(255, 255, 255, 16), 0, 1.0f);       // rim
        const float tr = std::max(4.0f, radius - 6.0f);
        dl->PathArcTo(c, tr, kKnobA0, kKnobA1, 40);
        dl->PathStroke(IM_COL32(255, 255, 255, 26), 0, 2.0f);
        const float aFrom = bipolar ? knobAngle(0.5f) : kKnobA0;
        if (std::abs(valAng - aFrom) > 0.001f) {
            dl->PathArcTo(c, tr, std::min(aFrom, valAng), std::max(aFrom, valAng), 40);
            dl->PathStroke(accent, 0, 2.4f);
        }
        const float ri = std::max(2.0f, tr - 8.0f);
        const ImVec2 d = dirAt(valAng);
        dl->AddLine(ImVec2(c.x + d.x * ri, c.y + d.y * ri),
                    ImVec2(c.x + d.x * (tr - 1.0f), c.y + d.y * (tr - 1.0f)),
                    state.disabled ? pal.textDim : IM_COL32(255, 255, 255, 235), 2.2f);
        if (hot)
            dl->AddCircleFilled(c, radius - 2.0f, withAlpha(accent, 0x1A));
    } else if (style == KnobStyle::Nxd) {
        // murk's AID/NxD scalloped knob: a 12-scallop outer case that rotates with
        // the value, an inner face, a rotating tick ring, and a bold pointer.
        const float outerR = radius, innerR = outerR * 0.83f;
        const int tickCount = 12, samplesPerScallop = 10;
        const float tickStep = 6.2831853f / (float)tickCount;
        const float scallopDepth = std::clamp(outerR * 0.095f, 1.4f, 3.6f);
        dl->AddCircleFilled(ImVec2(c.x, c.y + 1.8f), outerR, IM_COL32(0, 0, 0, 107)); // shadow

        std::vector<ImVec2> pts;
        pts.reserve((size_t)(tickCount * (samplesPerScallop + 1) + 1));
        for (int i = 0; i <= tickCount; ++i) {
            const int maxS = (i == tickCount) ? 0 : samplesPerScallop;
            for (int s = 0; s <= maxS; ++s) {
                const float u = (float)s / (float)samplesPerScallop;
                const float a = valAng + (float)i * tickStep + u * tickStep;
                const float rad = outerR - scallopDepth * std::sin(u * 3.14159265f);
                pts.push_back(ImVec2(c.x + std::cos(a) * rad, c.y + std::sin(a) * rad));
            }
        }
        const ImU32 rim = mix(IM_COL32(0x30, 0x34, 0x3a, 255), IM_COL32(0x11, 0x13, 0x19, 255), 0.5f);
        dl->AddConcavePolyFilled(pts.data(), (int)pts.size(), rim);
        dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(0, 0, 0, 184), ImDrawFlags_Closed, 1.2f);
        dl->AddCircleFilled(c, innerR, mix(IM_COL32(0x24, 0x29, 0x30, 255), IM_COL32(0x12, 0x16, 0x1c, 255), 0.5f));
        dl->AddCircleFilled(ImVec2(c.x - outerR * 0.11f, c.y - outerR * 0.14f), innerR * 0.54f,
                            IM_COL32(255, 255, 255, 15)); // specular
        for (int i = 0; i < tickCount; ++i) {
            const float a = valAng + (float)i * tickStep;
            const ImVec2 d = dirAt(a);
            dl->AddLine(ImVec2(c.x + d.x * (outerR - 3.7f), c.y + d.y * (outerR - 3.7f)),
                        ImVec2(c.x + d.x * (outerR - 1.2f), c.y + d.y * (outerR - 1.2f)),
                        withAlpha(accent, i == 0 ? 0xF8 : 0x8A), i == 0 ? 1.6f : 1.1f);
        }
        const ImVec2 d = dirAt(valAng);
        dl->AddLine(ImVec2(c.x + d.x * outerR * 0.16f, c.y + d.y * outerR * 0.16f),
                    ImVec2(c.x + d.x * (outerR - 3.7f), c.y + d.y * (outerR - 3.7f)),
                    state.disabled ? pal.textDim : accent, std::clamp(outerR * 0.13f, 2.0f, 3.1f));
        if (hot)
            dl->AddPolyline(pts.data(), (int)pts.size(), withAlpha(accent, 0x2E), ImDrawFlags_Closed, 1.6f);
    } else {
        dl->AddCircleFilled(ImVec2(c.x, c.y + 1.4f), radius, IM_COL32(0, 0, 0, 90));
        dl->AddCircleFilled(c, radius, IM_COL32(0xc8, 0xcc, 0xd2, 255));
        dl->AddCircle(c, radius - 0.8f, IM_COL32(255, 255, 255, 60), 0, 1.2f);
        const float faceR = radius - std::max(2.5f, radius * 0.12f);
        const ImU32 face = mix(pal.frame, IM_COL32(0, 0, 0, 255), 0.30f);
        dl->AddCircleFilled(c, faceR, face);
        dl->AddCircleFilled(ImVec2(c.x, c.y - faceR * 0.30f), faceR * 0.42f,
                            IM_COL32(255, 255, 255, 15));
        dl->AddCircle(c, faceR, IM_COL32(10, 10, 10, 255), 0, 1.0f);
        dl->AddCircle(c, radius, IM_COL32(0x19, 0x1b, 0x1e, 255), 0, 1.0f);
        for (int i = 0; i < 11; ++i) {
            const ImVec2 d = dirAt(knobAngle((float)i / 10.0f));
            dl->AddLine(ImVec2(c.x + d.x * faceR * 0.74f, c.y + d.y * faceR * 0.74f),
                        ImVec2(c.x + d.x * faceR * 0.92f, c.y + d.y * faceR * 0.92f),
                        withAlpha(pal.text, 0x40), 1.3f);
        }
        const ImVec2 d = dirAt(valAng);
        const ImU32 ptr = state.disabled ? pal.textDim
                                         : IM_COL32(0xee, 0xf1, 0xf5, 255);
        dl->AddLine(ImVec2(c.x + d.x * faceR * 0.12f, c.y + d.y * faceR * 0.12f),
                    ImVec2(c.x + d.x * faceR * 0.80f, c.y + d.y * faceR * 0.80f),
                    ptr, 2.4f);
        dl->AddCircleFilled(ImVec2(c.x + d.x * faceR * 0.80f, c.y + d.y * faceR * 0.80f),
                            1.2f, ptr);
        dl->AddCircleFilled(c, 2.0f, ptr);
        if (hot)
            dl->AddCircle(c, radius + 1.5f, withAlpha(accent, 0x66), 0, 1.5f);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, ImVec2(topLeft.x + size, topLeft.y + size),
                      pal, size * 0.5f, 2.0f);
}

void drawToggle(ImDrawList* dl, const ImVec2& topLeft, float width, float height,
                float anim, const Palette& pal, const ControlState& state)
{
    if (!dl)
        return;
    anim = std::clamp(anim, 0.0f, 1.0f);
    const float radius = height * 0.5f;
    ImU32 track = mix(pal.frame, pal.accent, anim);
    if (state.disabled)
        track = mix(track, pal.frame, 0.55f);
    dl->AddRectFilled(topLeft, ImVec2(topLeft.x + width, topLeft.y + height),
                      track, radius);
    dl->AddRect(topLeft, ImVec2(topLeft.x + width, topLeft.y + height),
                pal.frameBright, radius);

    const float nx = topLeft.x + radius + anim * (width - height);
    ImU32 nub = state.hovered || state.focused ? IM_COL32(255, 255, 255, 255)
                                               : IM_COL32(228, 230, 238, 255);
    if (state.disabled)
        nub = mix(nub, pal.frame, 0.45f);
    dl->AddCircleFilled(ImVec2(nx, topLeft.y + radius), radius - 2.5f, nub);

    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, ImVec2(topLeft.x + width, topLeft.y + height),
                      pal, radius, 2.0f);
}

void drawLed(ImDrawList* dl, const ImVec2& center, float radius, bool on,
             const Palette& pal, const ControlState& state, ImU32 onColor)
{
    if (!dl)
        return;
    if (onColor == 0)
        onColor = pal.accent;
    if (state.disabled)
        onColor = mix(onColor, pal.ledOff, 0.65f);
    if (on) {
        if (!state.disabled) {
            dl->AddCircleFilled(center, radius * 2.4f, withAlpha(onColor, 0x22));
            dl->AddCircleFilled(center, radius * 1.7f, withAlpha(onColor, 0x55));
        }
        dl->AddCircleFilled(center, radius, onColor);
        if (!state.disabled)
            dl->AddCircleFilled(ImVec2(center.x - radius * 0.3f,
                                       center.y - radius * 0.35f),
                                radius * 0.35f, IM_COL32(255, 255, 255, 180));
    } else {
        dl->AddCircleFilled(center, radius, pal.ledOff);
        dl->AddCircle(center, radius, pal.frameBright);
    }
    if (state.hovered && !state.disabled)
        dl->AddCircle(center, radius * 1.9f, pal.textDim);
    if (state.focused && !state.disabled)
        dl->AddCircle(center, radius * 2.25f, withAlpha(pal.text, 0xD8), 0, 1.5f);
}

void drawMeter(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
               float shownLevel, float peakLevel, float floorDb, const Palette& pal)
{
    if (!dl)
        return;
    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    dl->AddRectFilled(topLeft, mx, pal.frame, 2.0f);

    const bool horizontal = size.x > size.y;
    const float fill = dbNorm(shownLevel, floorDb);
    const float z1 = dbNorm(std::pow(10.0f, -12.0f / 20.0f), floorDb);
    const float z2 = dbNorm(std::pow(10.0f, -3.0f / 20.0f), floorDb);

    struct Zone {
        float a, b;
        ImU32 col;
    } zones[3] = {{0.0f, z1, pal.meterLow},
                  {z1, z2, pal.meterMid},
                  {z2, 1.0f, pal.meterHot}};
    for (auto& z : zones) {
        float a = z.a;
        float b = std::min(z.b, fill);
        if (b <= a)
            continue;
        if (horizontal)
            dl->AddRectFilled(ImVec2(topLeft.x + size.x * a, topLeft.y),
                              ImVec2(topLeft.x + size.x * b, topLeft.y + size.y),
                              z.col, 1.5f);
        else
            dl->AddRectFilled(ImVec2(topLeft.x, topLeft.y + size.y * (1.0f - b)),
                              ImVec2(topLeft.x + size.x,
                                     topLeft.y + size.y * (1.0f - a)),
                              z.col, 1.5f);
    }

    const float pk = dbNorm(peakLevel, floorDb);
    if (pk > 0.001f) {
        ImU32 pc = pk > z2 ? pal.meterHot : pk > z1 ? pal.meterMid : pal.meterLow;
        if (horizontal) {
            float x = topLeft.x + size.x * pk;
            dl->AddLine(ImVec2(x, topLeft.y), ImVec2(x, topLeft.y + size.y), pc, 1.0f);
        } else {
            float y = topLeft.y + size.y * (1.0f - pk);
            dl->AddLine(ImVec2(topLeft.x, y), ImVec2(topLeft.x + size.x, y), pc, 1.0f);
        }
    }

    dl->AddRect(topLeft, mx, pal.frameBright, 2.0f);
}

void drawFader(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
               float value, const Palette& pal, const ControlState& state)
{
    if (!dl)
        return;
    value = std::clamp(value, 0.0f, 1.0f);
    const float capH = 14.0f;
    const float cx = topLeft.x + size.x * 0.5f;
    const ImU32 accent = state.disabled ? mix(pal.accent, pal.frameBright, 0.65f)
                                        : pal.accent;

    dl->AddRectFilled(ImVec2(cx - 2.0f, topLeft.y),
                      ImVec2(cx + 2.0f, topLeft.y + size.y), pal.frame, 2.0f);
    dl->AddRect(ImVec2(cx - 2.0f, topLeft.y),
                ImVec2(cx + 2.0f, topLeft.y + size.y), pal.frameBright, 2.0f);

    float capY = topLeft.y + (1.0f - value) * (size.y - capH);
    dl->AddRectFilled(ImVec2(cx - 2.0f, capY + capH * 0.5f),
                      ImVec2(cx + 2.0f, topLeft.y + size.y),
                      withAlpha(accent, 0x66), 2.0f);

    dl->AddRectFilled(ImVec2(topLeft.x, capY),
                      ImVec2(topLeft.x + size.x, capY + capH),
                      !state.disabled && (state.hovered || state.active)
                          ? pal.frameBright
                          : pal.frame,
                      3.0f);
    dl->AddRect(ImVec2(topLeft.x, capY), ImVec2(topLeft.x + size.x, capY + capH),
                pal.frameBright, 3.0f);
    dl->AddLine(ImVec2(topLeft.x + 2.0f, capY + capH * 0.5f),
                ImVec2(topLeft.x + size.x - 2.0f, capY + capH * 0.5f),
                accent, 1.5f);

    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y),
                      pal, 4.0f, 2.0f);
}

void drawBadge(ImDrawList* dl, ImFont* font, const ImVec2& topLeft, const char* text,
               float fontSize, ImU32 fill, const Palette& pal)
{
    if (!dl || !font || !text)
        return;
    if (fill == 0)
        fill = withAlpha(pal.accent, 0x46);
    ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    ImVec2 sz(ts.x + 10.0f, fontSize + 5.0f);
    dl->AddRectFilled(topLeft, ImVec2(topLeft.x + sz.x, topLeft.y + sz.y),
                      fill, 3.0f);
    dl->AddText(font, fontSize, ImVec2(topLeft.x + 5.0f, topLeft.y + 2.5f),
                pal.text, text);
}

void drawTactileIconButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                           const ImVec2& size, const char* glyph,
                           const Palette& pal, const ControlState& state,
                           bool down, ImU32 face)
{
    if (!dl || !font || !glyph)
        return;

    ImU32 light = face ? face : IM_COL32(0xc6, 0xc9, 0xcf, 255);
    down = down && !state.disabled;
    if (state.hovered && !down && !state.disabled)
        light = mix(light, IM_COL32(255, 255, 255, 255), 0.12f);
    ImU32 dark = face ? mix(face, IM_COL32(0, 0, 0, 255), 0.55f)
                      : IM_COL32(0x35, 0x38, 0x3e, 255);
    if (state.disabled) {
        light = mix(light, IM_COL32(0, 0, 0, 255), 0.28f);
        dark = mix(dark, IM_COL32(0, 0, 0, 255), 0.28f);
    }
    ImU32 faceCol = down ? dark : light;

    const float r = 4.0f;
    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    dl->AddRectFilled(topLeft, mx, faceCol, r);
    ImU32 topEdge = down ? IM_COL32(0, 0, 0, 110) : IM_COL32(255, 255, 255, 150);
    ImU32 botEdge = down ? IM_COL32(255, 255, 255, 40) : IM_COL32(0, 0, 0, 90);
    dl->AddLine(ImVec2(topLeft.x + r, topLeft.y + 1.0f),
                ImVec2(mx.x - r, topLeft.y + 1.0f), topEdge, 1.6f);
    dl->AddLine(ImVec2(topLeft.x + r, mx.y - 1.5f),
                ImVec2(mx.x - r, mx.y - 1.5f), botEdge, 1.6f);
    dl->AddRect(topLeft, mx, IM_COL32(0, 0, 0, 110), r, 0, 1.0f);

    const float iconPx = size.y * 0.60f;
    ImU32 iconCol = state.disabled ? pal.textDim
                                   : down ? IM_COL32(0xec, 0xef, 0xf3, 255)
                                          : IM_COL32(0x2b, 0x2e, 0x34, 255);
    ImVec2 ts = font->CalcTextSizeA(iconPx, FLT_MAX, 0.0f, glyph);
    ImVec2 gp(topLeft.x + (size.x - ts.x) * 0.5f,
              topLeft.y + (size.y - ts.y) * 0.5f + (down ? 1.0f : 0.0f));
    dl->AddText(font, iconPx, gp, iconCol, glyph);

    if (state.focused && !state.disabled) {
        drawFocusRing(dl, topLeft, mx, pal, r, 2.0f);
    }
}

void drawButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                const ImVec2& size, const char* text, const Palette& pal,
                const ControlState& state, float fontScale)
{
    if (!dl || !font)
        return;

    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    const float r = 4.0f;
    ImU32 fill = state.active || state.selected ? mix(pal.frame, pal.accent, 0.40f)
                                                : pal.frame;
    if (state.hovered && !state.active && !state.disabled)
        fill = pal.frameBright;
    if (state.disabled)
        fill = mix(fill, IM_COL32(0, 0, 0, 255), 0.30f);

    dl->AddRectFilled(topLeft, mx, fill, r);
    dl->AddRect(topLeft, mx, pal.frameBright, r);
    if (state.selected) {
        dl->AddRectFilled(ImVec2(topLeft.x + 2.0f, mx.y - 4.0f),
                          ImVec2(mx.x - 2.0f, mx.y - 2.0f), pal.accent, 2.0f);
    }

    if (text && text[0]) {
        const float fs = ImGui::GetFontSize() * fontScale;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
        ImVec2 p(topLeft.x + std::max(0.0f, size.x - ts.x) * 0.5f,
                 topLeft.y + std::max(0.0f, size.y - ts.y) * 0.5f);
        dl->AddText(font, fs, p, state.disabled ? pal.textDim : pal.text, text);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, mx, pal, r);
}

void drawListItem(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                  const ImVec2& size, const char* text, const Palette& pal,
                  const ControlState& state, float fontScale)
{
    if (!dl || !font)
        return;

    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    ImU32 fill = state.selected ? withAlpha(pal.accent, 0x38) : pal.frame;
    if (state.hovered && !state.disabled)
        fill = mix(fill, pal.frameBright, 0.65f);
    if (state.active && !state.disabled)
        fill = mix(fill, pal.accent, 0.35f);
    if (state.disabled)
        fill = mix(fill, IM_COL32(0, 0, 0, 255), 0.35f);

    dl->AddRectFilled(topLeft, mx, fill, 3.0f);
    dl->AddRect(topLeft, mx, state.selected ? pal.accent : pal.frameBright, 3.0f);
    if (state.selected) {
        dl->AddRectFilled(ImVec2(topLeft.x + 2.0f, topLeft.y + 3.0f),
                          ImVec2(topLeft.x + 5.0f, mx.y - 3.0f),
                          pal.accent, 2.0f);
    }

    if (text && text[0]) {
        const float fs = ImGui::GetFontSize() * fontScale;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
        ImVec2 p(topLeft.x + 10.0f + (state.selected ? 3.0f : 0.0f),
                 topLeft.y + std::max(0.0f, size.y - ts.y) * 0.5f);
        dl->AddText(font, fs, p, state.disabled ? pal.textDim : pal.text, text);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, mx, pal, 3.0f);
}

void drawPatternGrid(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                     const bool* cells, int rows, int steps, int playheadStep,
                     const Palette& pal, const ControlState& state)
{
    if (!dl || !cells || rows <= 0 || steps <= 0)
        return;

    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    const float cw = size.x / (float)steps;
    const float ch = size.y / (float)rows;

    dl->AddRectFilled(topLeft, mx, pal.frame, 3.0f);
    if (playheadStep >= 0 && playheadStep < steps) {
        dl->AddRectFilled(ImVec2(topLeft.x + playheadStep * cw, topLeft.y),
                          ImVec2(topLeft.x + (playheadStep + 1) * cw,
                                 topLeft.y + size.y),
                          withAlpha(pal.accent, state.disabled ? 0x14 : 0x24));
    }

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < steps; ++c) {
            ImVec2 a(topLeft.x + c * cw + 1.5f,
                     topLeft.y + r * ch + 1.5f);
            ImVec2 b(topLeft.x + (c + 1) * cw - 1.5f,
                     topLeft.y + (r + 1) * ch - 1.5f);
            const bool on = cells[r * steps + c];
            ImU32 col = on ? (c == playheadStep ? pal.text : pal.accent)
                           : withAlpha(pal.frameBright, (c / 4) % 2 ? 0x30 : 0x55);
            if (state.disabled)
                col = mix(col, pal.frame, 0.55f);
            dl->AddRectFilled(a, b, col, 2.0f);
        }
    }

    dl->AddRect(topLeft, mx, pal.frameBright, 3.0f);
    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, mx, pal, 3.0f);
}

void drawXYPad(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
               float x, float y, const Palette& pal, const ControlState& state)
{
    if (!dl)
        return;

    x = std::clamp(x, 0.0f, 1.0f);
    y = std::clamp(y, 0.0f, 1.0f);
    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    ImU32 accent = state.disabled ? mix(pal.accent, pal.frameBright, 0.65f)
                                  : pal.accent;

    dl->AddRectFilled(topLeft, mx, pal.frame, 3.0f);
    const float cx = topLeft.x + x * size.x;
    const float cy = topLeft.y + (1.0f - y) * size.y;
    dl->AddLine(ImVec2(cx, topLeft.y), ImVec2(cx, topLeft.y + size.y),
                withAlpha(accent, state.disabled ? 0x30 : 0x50));
    dl->AddLine(ImVec2(topLeft.x, cy), ImVec2(topLeft.x + size.x, cy),
                withAlpha(accent, state.disabled ? 0x30 : 0x50));
    dl->AddCircleFilled(ImVec2(cx, cy), state.active ? 7.0f : 6.0f, accent);
    if (!state.disabled) {
        dl->AddCircleFilled(ImVec2(cx - 1.5f, cy - 1.8f), 1.6f,
                            IM_COL32(255, 255, 255, 170));
    }
    if (state.hovered && !state.disabled)
        dl->AddCircle(ImVec2(cx, cy), 10.0f, withAlpha(pal.text, 0x70), 0, 1.2f);
    dl->AddRect(topLeft, mx, pal.frameBright, 3.0f);
    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, mx, pal, 3.0f);
}

void drawKeyboard(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                  int firstNote, int octaves, int mouseNote, const bool* lit,
                  const Palette& pal, const ControlState& state)
{
    if (!dl || octaves <= 0)
        return;

    const int whites = octaves * 7;
    if (whites <= 0)
        return;

    const float ww = size.x / (float)whites;
    const float bw = ww * 0.62f;
    const float bh = size.y * 0.60f;
    static const int whiteSemi[7] = {0, 2, 4, 5, 7, 9, 11};
    static const int blackAfterWhite[7] = {1, 1, 0, 1, 1, 1, 0};
    static const int blackSemi[7] = {1, 3, 0, 6, 8, 10, 0};

    auto isDown = [&](int note) {
        return note == mouseNote || (lit && note >= 0 && note < 128 && lit[note]);
    };

    const ImU32 whiteUp = state.disabled ? mix(IM_COL32(232, 233, 238, 255),
                                                pal.frame, 0.45f)
                                         : IM_COL32(232, 233, 238, 255);
    const ImU32 blackUp = state.disabled ? mix(IM_COL32(18, 19, 26, 255),
                                                pal.frame, 0.35f)
                                         : IM_COL32(18, 19, 26, 255);
    const ImU32 litWhite = state.disabled ? mix(pal.accent, pal.frameBright, 0.65f)
                                          : pal.accent;
    const ImU32 litBlack = state.disabled ? mix(pal.accentDim, pal.frameBright, 0.65f)
                                          : pal.accentDim;

    const ImVec2 kbBottomRight(topLeft.x + size.x, topLeft.y + size.y);
    const ImU32 keySep = IM_COL32(28, 30, 38, 255);

    // White keys: one light bed rounded at the bottom, lit columns painted on
    // top, then thin hairlines at the interior boundaries. No per-key gap or
    // border (those used to stack into thick grey lines between keys).
    dl->AddRectFilled(topLeft, kbBottomRight, whiteUp, 2.0f,
                      ImDrawFlags_RoundCornersBottom);
    for (int w = 0; w < whites; ++w) {
        int note = firstNote + (w / 7) * 12 + whiteSemi[w % 7];
        if (!isDown(note))
            continue;
        ImVec2 a(topLeft.x + w * ww, topLeft.y);
        ImVec2 b(topLeft.x + (w + 1) * ww, topLeft.y + size.y);
        ImDrawFlags corner = ImDrawFlags_RoundCornersNone;
        if (w == 0)
            corner = ImDrawFlags_RoundCornersBottomLeft;
        else if (w == whites - 1)
            corner = ImDrawFlags_RoundCornersBottomRight;
        dl->AddRectFilled(a, b, litWhite, 2.0f, corner);
    }
    for (int w = 1; w < whites; ++w) {
        float x = topLeft.x + w * ww;
        dl->AddLine(ImVec2(x, topLeft.y), ImVec2(x, topLeft.y + size.y), keySep);
    }
    dl->AddRect(topLeft, kbBottomRight, keySep, 2.0f,
                ImDrawFlags_RoundCornersBottom);

    for (int w = 0; w < whites; ++w) {
        int inOct = w % 7;
        if (!blackAfterWhite[inOct] || w == whites - 1)
            continue;
        int note = firstNote + (w / 7) * 12 + blackSemi[inOct];
        float bx = topLeft.x + (w + 1) * ww - bw * 0.5f;
        ImVec2 a(bx, topLeft.y);
        ImVec2 b(bx + bw, topLeft.y + bh);
        dl->AddRectFilled(a, b, isDown(note) ? litBlack : blackUp,
                          2.0f, ImDrawFlags_RoundCornersBottom);
        dl->AddRect(a, b, IM_COL32(0, 0, 0, 255), 2.0f,
                    ImDrawFlags_RoundCornersBottom);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y),
                      pal, 3.0f);
}

void drawSectionHeader(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                       const char* text, float fontSize, float width,
                       const Palette& pal)
{
    if (!dl || !font || !text)
        return;
    ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    dl->AddText(font, fontSize, ImVec2(topLeft.x, topLeft.y + 4.0f),
                pal.textDim, text);
    if (width > ts.x + 12.0f)
        dl->AddLine(ImVec2(topLeft.x + ts.x + 8.0f, topLeft.y + 4.0f + fontSize * 0.55f),
                    ImVec2(topLeft.x + width, topLeft.y + 4.0f + fontSize * 0.55f),
                    withAlpha(pal.frameBright, 0xAA), 1.0f);
}

} // namespace snd::ui::paint
