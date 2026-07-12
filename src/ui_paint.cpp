#include "snd/ui_paint.h"

#include "ui_draw_imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace snd::ui::paint {

ImU32 withAlpha(ImU32 c, uint32_t a) { return (c & 0x00FFFFFF) | (a << 24); }

namespace {
bool visible(ImU32 c) { return (c & 0xFF000000u) != 0; }

void drawMenuCheck(draw::Surface& surface, draw::Vec2 center, ImU32 color)
{
    surface.line({center.x - 5.0f, center.y},
                 {center.x - 1.5f, center.y + 4.0f}, color, 1.8f);
    surface.line({center.x - 1.5f, center.y + 4.0f},
                 {center.x + 5.5f, center.y - 5.0f}, color, 1.8f);
}

void drawMenuCheck(ImDrawList* dl, const ImVec2& center, ImU32 color)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawMenuCheck(surface, draw::toDrawVec2(center), color);
}

draw::FontRef effectiveButtonFont(const ButtonPaintArgs& args)
{
    if (args.fontRef.handle)
        return args.fontRef;
    return draw::fontRef(args.font);
}

float effectiveButtonFontSize(const ButtonPaintArgs& args)
{
    if (args.fontSizePx > 0.0f)
        return args.fontSizePx;
    if (args.drawList && args.font)
        return ImGui::GetFontSize() * args.fontScale;
    return 0.0f;
}
} // namespace

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


float knobAngle(float frac)
{
    return kKnobA0 + std::clamp(frac, 0.0f, 1.0f) * (kKnobA1 - kKnobA0);
}

ImVec2 dirAt(float angle) { return ImVec2(std::cos(angle), std::sin(angle)); }

void drawFocusRing(draw::Surface& surface, draw::Vec2 min, draw::Vec2 max,
                   const Palette& pal, float rounding, float expand)
{
    // One accent stroke, nothing else: no white ring, no second border. The
    // caller's rounding is honoured so circular controls stay circular;
    // rectangular controls draw square focus.
    const draw::Vec2 a{min.x - expand, min.y - expand};
    const draw::Vec2 b{max.x + expand, max.y + expand};
    surface.strokeRect(a, b, withAlpha(pal.accent, 0xE0),
                       rounding > 0.0f ? rounding + expand : 0.0f, 1.5f);
}

void drawFocusRing(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
                   const Palette& pal, float rounding, float expand)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawFocusRing(surface, draw::toDrawVec2(min), draw::toDrawVec2(max), pal,
                  rounding, expand);
}

void drawGradientPanel(draw::Surface& surface, draw::Vec2 topLeft,
                       draw::Vec2 size, ImU32 topLeftColor,
                       ImU32 topRightColor, ImU32 bottomRightColor,
                       ImU32 bottomLeftColor)
{
    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    surface.fillRectMultiColor(topLeft, mx, topLeftColor, topRightColor,
                               bottomRightColor, bottomLeftColor);
}

void drawGradientPanel(ImDrawList* dl, const ImVec2& topLeft,
                       const ImVec2& size, ImU32 topLeftColor,
                       ImU32 topRightColor, ImU32 bottomRightColor,
                       ImU32 bottomLeftColor)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawGradientPanel(surface, draw::toDrawVec2(topLeft), draw::toDrawVec2(size),
                      topLeftColor, topRightColor, bottomRightColor,
                      bottomLeftColor);
}

void drawGradientRect(draw::Surface& surface, draw::Vec2 mn, draw::Vec2 mx,
                      ImU32 top, ImU32 bottom, float rounding)
{
    if (mx.x <= mn.x || mx.y <= mn.y)
        return;
    const float h = mx.y - mn.y;
    const float r = std::clamp(rounding, 0.0f,
                               std::min((mx.x - mn.x) * 0.5f, h * 0.5f));
    auto at = [&](float y) { return mix(top, bottom, (y - mn.y) / h); };

    if (r <= 0.5f) {
        surface.fillRectMultiColor(mn, mx, top, top, bottom, bottom);
        return;
    }
    surface.fillRectMultiColor({mn.x, mn.y + r}, {mx.x, mx.y - r},
                               at(mn.y + r), at(mn.y + r),
                               at(mx.y - r), at(mx.y - r));
    const int strips = std::max(2, (int)std::ceil(r));
    for (int i = 0; i < strips; ++i) {
        const float y0 = r * (float)i / (float)strips;
        const float y1 = r * (float)(i + 1) / (float)strips;
        const float dyMid = r - (y0 + y1) * 0.5f;
        const float inset = r - std::sqrt(std::max(0.0f, r * r - dyMid * dyMid));
        surface.fillRectMultiColor({mn.x + inset, mn.y + y0},
                                   {mx.x - inset, mn.y + y1},
                                   at(mn.y + y0), at(mn.y + y0),
                                   at(mn.y + y1), at(mn.y + y1));
        surface.fillRectMultiColor({mn.x + inset, mx.y - y1},
                                   {mx.x - inset, mx.y - y0},
                                   at(mx.y - y1), at(mx.y - y1),
                                   at(mx.y - y0), at(mx.y - y0));
    }
}

void drawGradientRect(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx,
                      ImU32 top, ImU32 bottom, float rounding)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawGradientRect(surface, draw::toDrawVec2(mn), draw::toDrawVec2(mx),
                     top, bottom, rounding);
}

void drawGradientArc(draw::Surface& surface, draw::Vec2 center, float radius,
                     float a0, float a1, ImU32 colStart, ImU32 colEnd,
                     float thickness, int segments)
{
    if (radius <= 0.0f || a1 == a0)
        return;
    segments = std::max(2, segments);
    draw::Vec2 prev{center.x + std::cos(a0) * radius,
                    center.y + std::sin(a0) * radius};
    for (int i = 1; i <= segments; ++i) {
        const float t1 = (float)i / (float)segments;
        const float a = a0 + (a1 - a0) * t1;
        const draw::Vec2 p{center.x + std::cos(a) * radius,
                           center.y + std::sin(a) * radius};
        const float tMid = ((float)(i - 1) / (float)segments + t1) * 0.5f;
        surface.line(prev, p, mix(colStart, colEnd, tMid), thickness);
        prev = p;
    }
}

void drawGradientArc(ImDrawList* dl, const ImVec2& center, float radius,
                     float a0, float a1, ImU32 colStart, ImU32 colEnd,
                     float thickness, int segments)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawGradientArc(surface, draw::toDrawVec2(center), radius, a0, a1,
                    colStart, colEnd, thickness, segments);
}

void drawAnimatedButton(draw::Surface& surface, draw::FontRef font,
                        float fontSizePx, draw::Vec2 topLeft, draw::Vec2 size,
                        const char* text, ImU32 top, ImU32 bottom,
                        const Palette& pal, const ControlState& state,
                        float pulse)
{
    if (top == 0)
        top = pal.accent;
    if (bottom == 0)
        bottom = pal.accentDim;

    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    const float r = 4.0f;
    ImU32 topCol = top;
    ImU32 bottomCol = bottom;
    if (state.hovered && !state.disabled) {
        topCol = mix(topCol, IM_COL32(255, 255, 255, 255), 0.15f);
        bottomCol = mix(bottomCol, IM_COL32(255, 255, 255, 255), 0.11f);
    }
    if (state.active && !state.disabled) {
        topCol = mix(topCol, IM_COL32(0, 0, 0, 255), 0.10f);
        bottomCol = mix(bottomCol, IM_COL32(0, 0, 0, 255), 0.18f);
    }
    if (state.disabled) {
        topCol = mix(topCol, IM_COL32(0, 0, 0, 255), 0.55f);
        bottomCol = mix(bottomCol, IM_COL32(0, 0, 0, 255), 0.55f);
    }

    surface.fillRectMultiColor(topLeft, mx, topCol, topCol, bottomCol, bottomCol);
    surface.fillRect(topLeft,
                     {mx.x, topLeft.y + std::min(3.0f, size.y * 0.16f)},
                     withAlpha(IM_COL32(255, 255, 255, 255),
                               state.disabled ? 0x16 : 0x42), r);

    pulse = std::clamp(pulse, 0.0f, 1.0f);
    const bool glow = pulse > 0.001f && !state.disabled;
    if (glow) {
        const float hot = 0.30f + 0.70f * pulse;
        surface.fillRect(topLeft, mx,
                         withAlpha(pal.accent, (uint32_t)(0x24 * hot)), r);
        const float sweepW = std::max(8.0f, size.x * 0.22f);
        const float sx = topLeft.x - sweepW + pulse * (size.x + sweepW * 2.0f);
        surface.pushClip(topLeft, mx, true);
        surface.fillRect({sx, topLeft.y}, {sx + sweepW, mx.y},
                         withAlpha(IM_COL32(255, 255, 255, 255),
                                   (uint32_t)(0x38 * hot)), 3.0f);
        surface.popClip();
    }

    surface.strokeRect(topLeft, mx,
                       state.active || state.selected ? pal.accent
                                                      : IM_COL32(0, 0, 0, 130),
                       r, state.active || state.selected ? 1.8f : 1.0f);

    if (text && text[0] && fontSizePx > 0.0f) {
        const draw::Vec2 ts = surface.measureText(font, fontSizePx, text);
        const draw::Vec2 p{
            topLeft.x + std::max(0.0f, size.x - ts.x) * 0.5f,
            topLeft.y + std::max(0.0f, size.y - ts.y) * 0.5f +
                (state.active ? 1.0f : 0.0f)};
        surface.text(font, fontSizePx, p,
                     state.disabled ? pal.textDim
                                    : IM_COL32(255, 255, 255, 245),
                     text);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(surface, topLeft, mx, pal, r);
}

void drawAnimatedButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                        const ImVec2& size, const char* text, ImU32 top,
                        ImU32 bottom, const Palette& pal,
                        const ControlState& state, float pulse, float fontScale)
{
    if (!dl || !font)
        return;
    draw::ImGuiSurface surface(dl);
    drawAnimatedButton(surface, draw::fontRef(font),
                       ImGui::GetFontSize() * fontScale,
                       draw::toDrawVec2(topLeft), draw::toDrawVec2(size),
                       text, top, bottom, pal, state, pulse);
}

void drawKnob(draw::Surface& surface, draw::Vec2 topLeft, float size, float frac,
              KnobStyle style, const Palette& pal, const ControlState& state,
              bool bipolar, ImU32 accent)
{
    if (accent == 0)
        accent = pal.accent;
    if (state.disabled)
        accent = mix(accent, pal.frameBright, 0.65f);
    frac = std::clamp(frac, 0.0f, 1.0f);

    const float radius = size * 0.5f;
    const draw::Vec2 c{topLeft.x + radius, topLeft.y + radius};
    const float valAng = knobAngle(frac);
    const bool hot = !state.disabled && (state.hovered || state.active);

    if (style == KnobStyle::Ring || style == KnobStyle::Seq) {
        // Butt-capped background ring + accent value sweep.
        // Ring adds a thumb dot at the value; Seq is the pure flat-ended sweep.
        const float lineW = std::min(5.0f, radius * 0.42f);
        const float rr = radius - lineW * 0.5f - 1.0f;
        surface.pathArcTo(c, rr, kKnobA0, kKnobA1, 40);
        surface.pathStroke(pal.frameBright, false, lineW);
        const float aFrom = bipolar ? knobAngle(0.5f) : kKnobA0;
        if (valAng != aFrom) {
            surface.pathArcTo(c, rr, std::min(aFrom, valAng),
                              std::max(aFrom, valAng), 40);
            surface.pathStroke(accent, false, lineW);
        }
        if (style == KnobStyle::Ring) {
            const ImVec2 d = dirAt(valAng);
            surface.fillCircle({c.x + d.x * rr, c.y + d.y * rr},
                               lineW * 0.55f,
                               hot ? IM_COL32(255, 255, 255, 255) : accent);
        }
    } else if (style == KnobStyle::Synth) {
        // Synth knob: dished disc + faint track ring + accent value arc + a
        // single white pointer tick.
        surface.fillCircle({c.x, c.y + 1.5f}, radius,
                           IM_COL32(0, 0, 0, 90)); // shadow
        surface.fillCircle(c, radius,
                           IM_COL32(0x1c, 0x1f, 0x25, 255)); // body base
        surface.fillCircle({c.x, c.y - radius * 0.34f}, radius * 0.62f,
                           IM_COL32(0x28, 0x2c, 0x33, 150)); // top-lit
        surface.strokeCircle(c, radius - 0.5f,
                             IM_COL32(255, 255, 255, 16), 0, 1.0f); // rim
        const float tr = std::max(4.0f, radius - 6.0f);
        surface.pathArcTo(c, tr, kKnobA0, kKnobA1, 40);
        surface.pathStroke(IM_COL32(255, 255, 255, 26), false, 2.0f);
        const float aFrom = bipolar ? knobAngle(0.5f) : kKnobA0;
        if (std::abs(valAng - aFrom) > 0.001f) {
            surface.pathArcTo(c, tr, std::min(aFrom, valAng),
                              std::max(aFrom, valAng), 40);
            surface.pathStroke(accent, false, 2.4f);
        }
        const float ri = std::max(2.0f, tr - 8.0f);
        const ImVec2 d = dirAt(valAng);
        surface.line({c.x + d.x * ri, c.y + d.y * ri},
                     {c.x + d.x * (tr - 1.0f), c.y + d.y * (tr - 1.0f)},
                     state.disabled ? pal.textDim
                                    : IM_COL32(255, 255, 255, 235),
                     2.2f);
        if (hot)
            surface.fillCircle(c, radius - 2.0f, withAlpha(accent, 0x1A));
    } else if (style == KnobStyle::Nxd) {
        // Scalloped knob: a 12-scallop outer case that rotates with the value,
        // an inner face, a rotating tick ring, and a bold pointer.
        const float outerR = radius, innerR = outerR * 0.83f;
        const int tickCount = 12, samplesPerScallop = 10;
        const float tickStep = 6.2831853f / (float)tickCount;
        const float scallopDepth = std::clamp(outerR * 0.095f, 1.4f, 3.6f);
        surface.fillCircle({c.x, c.y + 1.8f}, outerR,
                           IM_COL32(0, 0, 0, 107)); // shadow

        std::vector<draw::Vec2> pts;
        pts.reserve((size_t)(tickCount * (samplesPerScallop + 1) + 1));
        for (int i = 0; i <= tickCount; ++i) {
            const int maxS = (i == tickCount) ? 0 : samplesPerScallop;
            for (int s = 0; s <= maxS; ++s) {
                const float u = (float)s / (float)samplesPerScallop;
                const float a = valAng + (float)i * tickStep + u * tickStep;
                const float rad = outerR - scallopDepth * std::sin(u * 3.14159265f);
                pts.push_back({c.x + std::cos(a) * rad,
                               c.y + std::sin(a) * rad});
            }
        }
        const ImU32 rim = mix(IM_COL32(0x30, 0x34, 0x3a, 255), IM_COL32(0x11, 0x13, 0x19, 255), 0.5f);
        surface.fillConcavePoly(pts.data(), (int)pts.size(), rim);
        surface.polyline(pts.data(), (int)pts.size(), IM_COL32(0, 0, 0, 184),
                         true, 1.2f);
        surface.fillCircle(c, innerR,
                           mix(IM_COL32(0x24, 0x29, 0x30, 255),
                               IM_COL32(0x12, 0x16, 0x1c, 255), 0.5f));
        surface.fillCircle({c.x - outerR * 0.11f, c.y - outerR * 0.14f},
                           innerR * 0.54f,
                           IM_COL32(255, 255, 255, 15)); // specular
        for (int i = 0; i < tickCount; ++i) {
            const float a = valAng + (float)i * tickStep;
            const ImVec2 d = dirAt(a);
            surface.line({c.x + d.x * (outerR - 3.7f),
                          c.y + d.y * (outerR - 3.7f)},
                         {c.x + d.x * (outerR - 1.2f),
                          c.y + d.y * (outerR - 1.2f)},
                         withAlpha(accent, i == 0 ? 0xF8 : 0x8A),
                         i == 0 ? 1.6f : 1.1f);
        }
        const ImVec2 d = dirAt(valAng);
        surface.line({c.x + d.x * outerR * 0.16f,
                      c.y + d.y * outerR * 0.16f},
                     {c.x + d.x * (outerR - 3.7f),
                      c.y + d.y * (outerR - 3.7f)},
                     state.disabled ? pal.textDim : accent,
                     std::clamp(outerR * 0.13f, 2.0f, 3.1f));
        if (hot)
            surface.polyline(pts.data(), (int)pts.size(),
                             withAlpha(accent, 0x2E), true, 1.6f);
    } else {
        surface.fillCircle({c.x, c.y + 1.4f}, radius, IM_COL32(0, 0, 0, 90));
        surface.fillCircle(c, radius, IM_COL32(0xc8, 0xcc, 0xd2, 255));
        surface.strokeCircle(c, radius - 0.8f,
                             IM_COL32(255, 255, 255, 60), 0, 1.2f);
        const float faceR = radius - std::max(2.5f, radius * 0.12f);
        const ImU32 face = mix(pal.frame, IM_COL32(0, 0, 0, 255), 0.30f);
        surface.fillCircle(c, faceR, face);
        surface.fillCircle({c.x, c.y - faceR * 0.30f}, faceR * 0.42f,
                           IM_COL32(255, 255, 255, 15));
        surface.strokeCircle(c, faceR, IM_COL32(10, 10, 10, 255), 0, 1.0f);
        surface.strokeCircle(c, radius, IM_COL32(0x19, 0x1b, 0x1e, 255),
                             0, 1.0f);
        for (int i = 0; i < 11; ++i) {
            const ImVec2 d = dirAt(knobAngle((float)i / 10.0f));
            surface.line({c.x + d.x * faceR * 0.74f,
                          c.y + d.y * faceR * 0.74f},
                         {c.x + d.x * faceR * 0.92f,
                          c.y + d.y * faceR * 0.92f},
                         withAlpha(pal.text, 0x40), 1.3f);
        }
        const ImVec2 d = dirAt(valAng);
        const ImU32 ptr = state.disabled ? pal.textDim
                                         : IM_COL32(0xee, 0xf1, 0xf5, 255);
        surface.line({c.x + d.x * faceR * 0.12f,
                      c.y + d.y * faceR * 0.12f},
                     {c.x + d.x * faceR * 0.80f,
                      c.y + d.y * faceR * 0.80f},
                     ptr, 2.4f);
        surface.fillCircle({c.x + d.x * faceR * 0.80f,
                            c.y + d.y * faceR * 0.80f},
                           1.2f, ptr);
        surface.fillCircle(c, 2.0f, ptr);
        if (hot)
            surface.strokeCircle(c, radius + 1.5f, withAlpha(accent, 0x66),
                                 0, 1.5f);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(surface, topLeft, {topLeft.x + size, topLeft.y + size},
                      pal, size * 0.5f, 2.0f);
}

void drawKnob(ImDrawList* dl, const ImVec2& topLeft, float size, float frac,
              KnobStyle style, const Palette& pal, const ControlState& state,
              bool bipolar, ImU32 accent)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawKnob(surface, draw::toDrawVec2(topLeft), size, frac, style, pal, state,
             bipolar, accent);
}

void drawKnobModRing(draw::Surface& surface, draw::Vec2 topLeft, float size,
                     float frac, const KnobMod& mod, const Palette& pal,
                     ImU32 accent)
{
    if (mod.depth == 0.0f && mod.value < 0.0f)
        return;
    if (accent == 0)
        accent = pal.accent;
    const ImU32 col = mod.color ? mod.color : mix(accent, pal.text, 0.55f);

    const float radius = size * 0.5f;
    const draw::Vec2 c{topLeft.x + radius, topLeft.y + radius};
    const float rArc = radius - 1.0f; // rim overlay; focus ring stays outside
    const float lineW = std::clamp(radius * 0.09f, 1.4f, 2.0f);

    frac = std::clamp(frac, 0.0f, 1.0f);
    if (mod.depth != 0.0f) {
        const float end = std::clamp(frac + mod.depth, 0.0f, 1.0f);
        const float a0 = knobAngle(std::min(frac, end));
        const float a1 = knobAngle(std::max(frac, end));
        if (a1 - a0 > 0.001f) {
            surface.pathArcTo(c, rArc, a0, a1, 32);
            surface.pathStroke(withAlpha(col, 0xB4), false, lineW);
        }
    }
    if (mod.value >= 0.0f) {
        const ImVec2 d = dirAt(knobAngle(std::clamp(mod.value, 0.0f, 1.0f)));
        surface.fillCircle({c.x + d.x * rArc, c.y + d.y * rArc},
                           lineW * 1.05f, col);
    }
}

void drawKnobModRing(ImDrawList* dl, const ImVec2& topLeft, float size,
                     float frac, const KnobMod& mod, const Palette& pal,
                     ImU32 accent)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawKnobModRing(surface, draw::toDrawVec2(topLeft), size, frac, mod, pal,
                    accent);
}

void drawPadlock(draw::Surface& surface, draw::Vec2 mn, draw::Vec2 mx,
                 ImU32 color)
{
    const float w = mx.x - mn.x, h = mx.y - mn.y;
    const float bodyTopY = mn.y + h * 0.45f;
    surface.fillRect({mn.x, bodyTopY}, mx, color, 1.5f);
    const draw::Vec2 centre{mn.x + w * 0.5f, bodyTopY};
    const float rx = w * 0.28f, ry = h * 0.405f; // shackle half-ellipse
    surface.pathClear();
    for (int i = 0; i <= 12; ++i) {
        const float t = -1.57079633f + 3.14159265f * ((float)i / 12.0f);
        surface.pathLineTo({centre.x + rx * std::sin(t),
                            centre.y - ry * std::cos(t)});
    }
    surface.pathStroke(color, false, 1.6f);
}

void drawPadlock(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 color)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawPadlock(surface, draw::toDrawVec2(mn), draw::toDrawVec2(mx), color);
}

void drawKnobWindow(draw::Surface& surface, draw::Vec2 topLeft, float size,
                    const KnobWindow& win, const Palette& pal, ImU32 accent,
                    ImU32 lockColor, float uiScale)
{
    if (size <= 0.0f)
        return;
    if (accent == 0)
        accent = pal.accent;
    if (lockColor == 0)
        lockColor = pal.meterHot;
    const ImU32 col = win.locked ? pal.textDim : accent;

    const draw::Vec2 c{topLeft.x + size * 0.5f, topLeft.y + size * 0.5f};
    const float radius = size * 0.5f + 2.5f * uiScale;
    const float aLo = knobAngle(std::min(win.lo, win.hi));
    const float aHi = knobAngle(std::max(win.lo, win.hi));
    if (aHi > aLo) {
        surface.pathArcTo(c, radius, aLo, aHi, 24);
        surface.pathStroke(withAlpha(col, win.locked ? 0x59 : 0xD9), false,
                           2.0f * uiScale);
    }
    for (const float a : {aLo, aHi}) {
        const ImVec2 d = dirAt(a);
        surface.fillCircle({c.x + d.x * radius, c.y + d.y * radius},
                           2.8f * uiScale, col, 10);
    }
    if (win.locked)
        drawPadlock(surface,
                    {topLeft.x + size - 9.0f * uiScale,
                     topLeft.y - 1.0f * uiScale},
                    {topLeft.x + size,
                     topLeft.y + (10.0f - 1.0f) * uiScale},
                    lockColor);
}

void drawKnobWindow(ImDrawList* dl, const ImVec2& topLeft, float size,
                    const KnobWindow& win, const Palette& pal, ImU32 accent,
                    ImU32 lockColor, float uiScale)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawKnobWindow(surface, draw::toDrawVec2(topLeft), size, win, pal, accent,
                   lockColor, uiScale);
}

int knobWindowHitEnd(const ImVec2& topLeft, float size, const KnobWindow& win,
                     const ImVec2& pressPos, float uiScale)
{
    const float grow = 6.0f * uiScale;
    if (pressPos.x < topLeft.x - grow || pressPos.x > topLeft.x + size + grow ||
        pressPos.y < topLeft.y - grow || pressPos.y > topLeft.y + size + grow)
        return -1;

    // Press angle -> 0..1 along the knob sweep; outside the sweep snaps to
    // the nearer endpoint.
    constexpr float twoPi = 6.28318531f;
    const ImVec2 c(topLeft.x + size * 0.5f, topLeft.y + size * 0.5f);
    float ang = std::atan2(pressPos.y - c.y, pressPos.x - c.x);
    while (ang < kKnobA0)
        ang += twoPi;
    while (ang > kKnobA0 + twoPi)
        ang -= twoPi;
    if (ang > kKnobA1)
        ang = (ang - kKnobA1) < (kKnobA0 + twoPi - ang) ? kKnobA1 : kKnobA0;
    const float t = (ang - kKnobA0) / (kKnobA1 - kKnobA0);
    return std::abs(t - win.hi) < std::abs(t - win.lo) ? 1 : 0;
}

void drawComboWindow(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                     const KnobWindow& win, const Palette& pal, ImU32 accent,
                     ImU32 lockColor, float uiScale)
{
    if (size.x <= 0.0f || size.y <= 0.0f)
        return;
    if (accent == 0)
        accent = pal.accent;
    if (lockColor == 0)
        lockColor = pal.meterHot;
    const ImU32 col = win.locked ? pal.textDim : accent;

    const float lo = std::clamp(std::min(win.lo, win.hi), 0.0f, 1.0f);
    const float hi = std::clamp(std::max(win.lo, win.hi), 0.0f, 1.0f);
    const float y = topLeft.y + size.y - 2.0f * uiScale;
    const float x0 = topLeft.x + lo * size.x;
    const float x1 = topLeft.x + hi * size.x;
    surface.fillRect({x0, y - 1.0f * uiScale},
                     {std::max(x0 + 2.0f * uiScale, x1),
                      y + 2.0f * uiScale},
                     withAlpha(col, win.locked ? 0x59 : 0xD9));
    surface.fillRect({x0 - 1.0f * uiScale, y - 6.0f * uiScale},
                     {x0 + 1.0f * uiScale, y + 2.0f * uiScale}, col);
    surface.fillRect({x1 - 1.0f * uiScale, y - 6.0f * uiScale},
                     {x1 + 1.0f * uiScale, y + 2.0f * uiScale}, col);
    if (win.locked)
        drawPadlock(surface,
                    {topLeft.x + size.x - 9.0f * uiScale,
                     topLeft.y - 1.0f * uiScale},
                    {topLeft.x + size.x,
                     topLeft.y + (10.0f - 1.0f) * uiScale},
                    lockColor);
}

void drawComboWindow(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                     const KnobWindow& win, const Palette& pal, ImU32 accent,
                     ImU32 lockColor, float uiScale)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawComboWindow(surface, draw::toDrawVec2(topLeft), draw::toDrawVec2(size),
                    win, pal, accent, lockColor, uiScale);
}

int comboWindowHitEnd(const ImVec2& topLeft, const ImVec2& size,
                      const KnobWindow& win, const ImVec2& pressPos,
                      float uiScale)
{
    const float grow = 3.0f * uiScale;
    if (pressPos.x < topLeft.x - grow || pressPos.x > topLeft.x + size.x + grow ||
        pressPos.y < topLeft.y - grow || pressPos.y > topLeft.y + size.y + grow)
        return -1;
    const float t = std::clamp((pressPos.x - topLeft.x) / std::max(1.0f, size.x),
                               0.0f, 1.0f);
    return std::abs(t - win.hi) < std::abs(t - win.lo) ? 1 : 0;
}

void drawDefaultKnob(const KnobPaintArgs& args)
{
    if (!args.palette || !args.state)
        return;
    if (args.surface)
        drawKnob(*args.surface, draw::toDrawVec2(args.topLeft), args.size,
                 args.normalizedValue, args.style, *args.palette, *args.state,
                 args.bipolar, args.accent);
    else
        drawKnob(args.drawList, args.topLeft, args.size, args.normalizedValue,
                 args.style, *args.palette, *args.state, args.bipolar, args.accent);
}

void drawKnobWithPainter(const KnobPaintArgs& args, const KnobPainter& painter)
{
    ImDrawList* dl = args.drawList;
    draw::ImGuiSurface localSurface(dl);
    draw::Surface* surface = args.surface ? args.surface : (dl ? &localSurface : nullptr);

    ControlState bodyState = args.state ? *args.state : ControlState{};
    bodyState.focused = false;
    KnobPaintArgs bodyArgs = args;
    bodyArgs.state = &bodyState;
    bodyArgs.surface = surface;

    if (painter)
        painter(bodyArgs);
    else
        drawDefaultKnob(bodyArgs);

    // SND-owned overlays after the body: modulation ring, then focus ring.
    if (surface && args.palette)
        drawKnobModRing(*surface, draw::toDrawVec2(args.topLeft), args.size,
                        args.normalizedValue, args.mod, *args.palette,
                        args.accent);

    if (surface && args.palette && args.state &&
        args.state->focused && !args.state->disabled) {
        drawFocusRing(*surface, draw::toDrawVec2(args.topLeft),
                      {args.topLeft.x + args.size, args.topLeft.y + args.size},
                      *args.palette, args.size * 0.5f, 2.0f);
    }
}

void drawToggle(draw::Surface& surface, draw::Vec2 topLeft, float width,
                float height, float anim, const Palette& pal,
                const ControlState& state)
{
    anim = std::clamp(anim, 0.0f, 1.0f);
    const float radius = height * 0.5f;
    ImU32 track = mix(pal.frame, pal.accent, anim);
    if (state.disabled)
        track = mix(track, pal.frame, 0.55f);
    const draw::Vec2 mx{topLeft.x + width, topLeft.y + height};
    surface.fillRect(topLeft, mx, track, radius);
    surface.strokeRect(topLeft, mx, pal.frameBright, radius);

    const float nx = topLeft.x + radius + anim * (width - height);
    ImU32 nub = state.hovered || state.focused ? IM_COL32(255, 255, 255, 255)
                                               : IM_COL32(228, 230, 238, 255);
    if (state.disabled)
        nub = mix(nub, pal.frame, 0.45f);
    surface.fillCircle({nx, topLeft.y + radius}, radius - 2.5f, nub);

    if (state.focused && !state.disabled)
        drawFocusRing(surface, topLeft, mx, pal, radius, 2.0f);
}

void drawToggle(ImDrawList* dl, const ImVec2& topLeft, float width, float height,
                float anim, const Palette& pal, const ControlState& state)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawToggle(surface, draw::toDrawVec2(topLeft), width, height, anim, pal,
               state);
}

void drawLed(draw::Surface& surface, draw::Vec2 center, float radius, bool on,
             const Palette& pal, const ControlState& state, ImU32 onColor)
{
    if (onColor == 0)
        onColor = pal.accent;
    if (state.disabled)
        onColor = mix(onColor, pal.ledOff, 0.65f);
    if (on) {
        if (!state.disabled) {
            surface.fillCircle(center, radius * 2.4f, withAlpha(onColor, 0x22));
            surface.fillCircle(center, radius * 1.7f, withAlpha(onColor, 0x55));
        }
        surface.fillCircle(center, radius, onColor);
        if (!state.disabled)
            surface.fillCircle({center.x - radius * 0.3f,
                                center.y - radius * 0.35f},
                               radius * 0.35f, IM_COL32(255, 255, 255, 180));
    } else {
        surface.fillCircle(center, radius, pal.ledOff);
        surface.strokeCircle(center, radius, pal.frameBright);
    }
    if (state.hovered && !state.disabled)
        surface.strokeCircle(center, radius * 1.9f, pal.textDim);
    if (state.focused && !state.disabled)
        surface.strokeCircle(center, radius * 2.25f, withAlpha(pal.text, 0xD8),
                             0, 1.5f);
}

void drawLed(ImDrawList* dl, const ImVec2& center, float radius, bool on,
             const Palette& pal, const ControlState& state, ImU32 onColor)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawLed(surface, draw::toDrawVec2(center), radius, on, pal, state, onColor);
}

void drawMeter(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
               float shownLevel, float peakLevel, float floorDb, const Palette& pal)
{
    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    surface.fillRect(topLeft, mx, pal.frame, 2.0f);

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
            surface.fillRect({topLeft.x + size.x * a, topLeft.y},
                             {topLeft.x + size.x * b, topLeft.y + size.y},
                             z.col, 1.5f);
        else
            surface.fillRect({topLeft.x, topLeft.y + size.y * (1.0f - b)},
                             {topLeft.x + size.x,
                              topLeft.y + size.y * (1.0f - a)},
                             z.col, 1.5f);
    }

    const float pk = dbNorm(peakLevel, floorDb);
    if (pk > 0.001f) {
        ImU32 pc = pk > z2 ? pal.meterHot : pk > z1 ? pal.meterMid : pal.meterLow;
        if (horizontal) {
            float x = topLeft.x + size.x * pk;
            surface.line({x, topLeft.y}, {x, topLeft.y + size.y}, pc, 1.0f);
        } else {
            float y = topLeft.y + size.y * (1.0f - pk);
            surface.line({topLeft.x, y}, {topLeft.x + size.x, y}, pc, 1.0f);
        }
    }

    surface.strokeRect(topLeft, mx, pal.frameBright, 2.0f);
}

void drawMeter(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
               float shownLevel, float peakLevel, float floorDb, const Palette& pal)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawMeter(surface, draw::toDrawVec2(topLeft), draw::toDrawVec2(size),
              shownLevel, peakLevel, floorDb, pal);
}

void drawFader(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
               float value, const Palette& pal, const ControlState& state)
{
    value = std::clamp(value, 0.0f, 1.0f);
    const float capH = 14.0f;
    const float cx = topLeft.x + size.x * 0.5f;
    const ImU32 accent = state.disabled ? mix(pal.accent, pal.frameBright, 0.65f)
                                        : pal.accent;

    surface.fillRect({cx - 2.0f, topLeft.y},
                     {cx + 2.0f, topLeft.y + size.y}, pal.frame, 2.0f);
    surface.strokeRect({cx - 2.0f, topLeft.y},
                       {cx + 2.0f, topLeft.y + size.y}, pal.frameBright, 2.0f);

    float capY = topLeft.y + (1.0f - value) * (size.y - capH);
    surface.fillRect({cx - 2.0f, capY + capH * 0.5f},
                     {cx + 2.0f, topLeft.y + size.y},
                     withAlpha(accent, 0x66), 2.0f);

    surface.fillRect({topLeft.x, capY}, {topLeft.x + size.x, capY + capH},
                     !state.disabled && (state.hovered || state.active)
                         ? pal.frameBright
                         : pal.frame,
                     3.0f);
    surface.strokeRect({topLeft.x, capY}, {topLeft.x + size.x, capY + capH},
                       pal.frameBright, 3.0f);
    surface.line({topLeft.x + 2.0f, capY + capH * 0.5f},
                 {topLeft.x + size.x - 2.0f, capY + capH * 0.5f},
                 accent, 1.5f);

    if (state.focused && !state.disabled)
        drawFocusRing(surface, topLeft, {topLeft.x + size.x, topLeft.y + size.y},
                      pal, 4.0f, 2.0f);
}

void drawFader(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
               float value, const Palette& pal, const ControlState& state)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawFader(surface, draw::toDrawVec2(topLeft), draw::toDrawVec2(size),
              value, pal, state);
}

void drawBadge(draw::Surface& surface, draw::FontRef font, draw::Vec2 topLeft,
               const char* text, float fontSize, ImU32 fill,
               const Palette& pal)
{
    if (!text || fontSize <= 0.0f)
        return;
    if (fill == 0)
        fill = withAlpha(pal.accent, 0x46);
    const draw::Vec2 ts = surface.measureText(font, fontSize, text);
    const draw::Vec2 sz{ts.x + 10.0f, fontSize + 5.0f};
    surface.fillRect(topLeft, {topLeft.x + sz.x, topLeft.y + sz.y}, fill, 3.0f);
    surface.text(font, fontSize, {topLeft.x + 5.0f, topLeft.y + 2.5f},
                 pal.text, text);
}

void drawBadge(ImDrawList* dl, ImFont* font, const ImVec2& topLeft, const char* text,
               float fontSize, ImU32 fill, const Palette& pal)
{
    if (!dl || !font)
        return;
    draw::ImGuiSurface surface(dl);
    drawBadge(surface, draw::fontRef(font), draw::toDrawVec2(topLeft), text,
              fontSize, fill, pal);
}

void drawTactileIconButton(draw::Surface& surface, draw::FontRef font,
                           draw::Vec2 topLeft, draw::Vec2 size,
                           const char* glyph,
                           const Palette& pal, const ControlState& state,
                           bool down, ImU32 face)
{
    if (!glyph)
        return;

    // A subtly raised icon key: light face, soft off-white inset on press.
    // The face never drops to a dark slab; the pressed state is a small colour
    // and shadow change with the glyph lit by the accent.
    down = down && !state.disabled;
    const ImU32 base = face ? face : IM_COL32(224, 227, 232, 255);
    const ImU32 rest = face ? mix(base, IM_COL32(255, 255, 255, 255), 0.10f) : base;
    ImU32 faceCol = down ? mix(rest, IM_COL32(255, 255, 255, 255), 0.30f) : rest;
    if (state.hovered && !down && !state.disabled)
        faceCol = mix(rest, IM_COL32(255, 255, 255, 255), 0.08f);
    if (state.disabled)
        faceCol = mix(rest, IM_COL32(0, 0, 0, 255), 0.28f);

    const float r = 4.0f;
    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    surface.fillRect(topLeft, mx, faceCol, r);
    surface.line({topLeft.x + r, topLeft.y + 1.0f},
                 {mx.x - r, topLeft.y + 1.0f},
                 down ? IM_COL32(255, 255, 255, 150)
                      : IM_COL32(255, 255, 255, 210),
                 1.0f);
    surface.line({topLeft.x + r, mx.y - 1.0f},
                 {mx.x - r, mx.y - 1.0f},
                 down ? IM_COL32(96, 101, 114, 72)
                      : IM_COL32(80, 84, 96, 118),
                 1.0f);
    surface.strokeRect(topLeft, mx,
                       down ? IM_COL32(80, 84, 96, 128)
                            : IM_COL32(80, 84, 96, 170),
                       r, 1.0f);

    const float iconPx = size.y * 0.60f;
    const ImU32 iconCol =
        state.disabled ? pal.textDim
                       : down ? pal.accent : IM_COL32(43, 46, 52, 255);
    if (iconPx > 0.0f) {
        draw::Vec2 ts = surface.measureText(font, iconPx, glyph);
        draw::Vec2 gp{topLeft.x + (size.x - ts.x) * 0.5f,
                      topLeft.y + (size.y - ts.y) * 0.5f};
        surface.text(font, iconPx, gp, iconCol, glyph);
    }

    if (state.focused && !state.disabled) {
        drawFocusRing(surface, topLeft, mx, pal, r, 2.0f);
    }
}

void drawTactileIconButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                           const ImVec2& size, const char* glyph,
                           const Palette& pal, const ControlState& state,
                           bool down, ImU32 face)
{
    if (!dl || !font)
        return;
    draw::ImGuiSurface surface(dl);
    drawTactileIconButton(surface, draw::fontRef(font), draw::toDrawVec2(topLeft),
                          draw::toDrawVec2(size), glyph, pal, state, down, face);
}

void drawLedButton(draw::Surface& surface, draw::FontRef font,
                   draw::Vec2 topLeft, draw::Vec2 size, const char* glyph,
                   float ledLevel,
                   const Palette& pal, const ControlState& state, bool down,
                   ImU32 ledColor, ImU32 face)
{
    drawTactileIconButton(surface, font, topLeft, size, glyph, pal, state, down,
                          face);

    if (ledColor == 0)
        ledColor = pal.accent;
    ledLevel = std::clamp(ledLevel, 0.0f, 1.0f);
    if (state.disabled)
        ledLevel *= 0.35f;

    const float r = 4.0f;
    const draw::Vec2 mn{topLeft.x + 2.5f, topLeft.y + 2.5f};
    const draw::Vec2 mx{topLeft.x + size.x - 2.5f, topLeft.y + size.y - 2.5f};
    if (ledLevel > 0.0f) // soft bloom outside the lit ring
        surface.strokeRect({mn.x - 1.5f, mn.y - 1.5f},
                           {mx.x + 1.5f, mx.y + 1.5f},
                           withAlpha(ledColor, (uint32_t)(0x52 * ledLevel)),
                           r + 1.0f, 3.0f);
    surface.strokeRect(mn, mx, mix(pal.ledOff, ledColor, ledLevel),
                       r - 1.0f, 1.8f);
}

void drawLedButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                   const ImVec2& size, const char* glyph, float ledLevel,
                   const Palette& pal, const ControlState& state, bool down,
                   ImU32 ledColor, ImU32 face)
{
    if (!dl || !font)
        return;
    draw::ImGuiSurface surface(dl);
    drawLedButton(surface, draw::fontRef(font), draw::toDrawVec2(topLeft),
                  draw::toDrawVec2(size), glyph, ledLevel, pal, state, down,
                  ledColor, face);
}

void drawVectorIconButton(draw::Surface& surface, draw::Vec2 topLeft,
                          draw::Vec2 size, Icon icon, ImU32 accent,
                          const Palette& pal, const ControlState& state,
                          bool active, ImU32 bgColor, ImU32 textColor)
{
    if (accent == 0)
        accent = pal.accent;

    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    ImU32 bg = bgColor ? bgColor
                       : state.active ? mix(pal.frame, pal.accent, 0.32f)
                         : state.hovered ? pal.frameBright
                                         : pal.frame;
    if (state.disabled)
        bg = mix(bg, pal.frame, 0.65f);
    surface.fillRect(topLeft, mx, bg, 4.0f);

    const bool lit = active || state.selected;
    if (lit)
        surface.strokeRect(topLeft, mx, accent, 4.0f, 2.0f);

    const ImU32 fg = state.disabled ? pal.textDim
                                    : lit ? accent
                                          : textColor ? textColor : pal.text;
    const draw::Vec2 c{topLeft.x + size.x * 0.5f,
                       topLeft.y + size.y * 0.5f};
    const float r = std::min(size.x, size.y) * 0.28f;
    drawTransportGlyph(surface, icon, c, r, fg);

    if (state.focused && !state.disabled)
        drawFocusRing(surface, topLeft, mx, pal, 4.0f);
}

namespace {
// The real Lucide transport SVGs (ISC-licensed). White stroke so image() tints
// the one master to any state colour. Rasterised to a texture once by
// loadTransportIcons and blitted below -- crisp at small sizes, where a
// hand-stroked thin polyline's anti-alias fringe would swallow a ~13px icon.
const char* transportIconSvg(Icon icon)
{
    switch (icon) {
    case Icon::Record: // lucide "circle"
        return R"SVG(<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='#fff' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><circle cx='12' cy='12' r='10'/></svg>)SVG";
    case Icon::Play: // lucide "play" (outline)
        return R"SVG(<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='#fff' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><polygon points='6 3 20 12 6 21'/></svg>)SVG";
    case Icon::Loop: // lucide "repeat"
        return R"SVG(<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='#fff' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><path d='m17 2 4 4-4 4'/><path d='M3 11v-1a4 4 0 0 1 4-4h14'/><path d='m7 22-4-4 4-4'/><path d='M21 13v1a4 4 0 0 1-4 4H3'/></svg>)SVG";
    case Icon::Stop: // lucide "square"
        return R"SVG(<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='#fff' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><rect x='3' y='3' width='18' height='18' rx='2'/></svg>)SVG";
    case Icon::SkipToStart: // lucide "skip-back"
        return R"SVG(<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='#fff' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><polygon points='19 20 9 12 19 4 19 20'/><line x1='5' x2='5' y1='19' y2='5'/></svg>)SVG";
    case Icon::SkipToEnd: // lucide "skip-forward"
        return R"SVG(<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='#fff' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><polygon points='5 4 15 12 5 20 5 4'/><line x1='19' x2='19' y1='5' y2='19'/></svg>)SVG";
    default:
        return nullptr;
    }
}

std::unordered_map<int, SvgTexture>& transportIconCache()
{
    static std::unordered_map<int, SvgTexture> cache;
    return cache;
}

const SvgTexture* transportIconTexture(Icon icon)
{
    auto& cache = transportIconCache();
    auto it = cache.find(static_cast<int>(icon));
    if (it != cache.end() && it->second.id != ImTextureID_Invalid)
        return &it->second;
    return nullptr;
}
} // namespace

void loadTransportIcons()
{
    auto& cache = transportIconCache();
    const Icon icons[] = {Icon::Record, Icon::Play,        Icon::Loop,
                          Icon::Stop,   Icon::SkipToStart, Icon::SkipToEnd};
    for (Icon ic : icons) {
        if (cache.count(static_cast<int>(ic)))
            continue; // idempotent; textures live for the program
        if (const char* svg = transportIconSvg(ic))
            cache[static_cast<int>(ic)] =
                loadSvgTexture(svg, 40); // 2x-crisp master; tinted+downsampled at draw
    }
}

void releaseTransportIcons()
{
    auto& cache = transportIconCache();
    for (auto& entry : cache)
        if (entry.second.id != ImTextureID_Invalid)
            releaseTexture(entry.second.id);
    cache.clear();
}

void drawTransportGlyph(draw::Surface& surface, Icon icon, draw::Vec2 c,
                        float r, ImU32 fg, float thickness)
{
    const bool outline = thickness > 0.0f;
    // Crisp path: blit the Lucide SVG rasterised at init (loadTransportIcons).
    // The full 24-unit viewBox spans 2.4r, matching the procedural P() mapping
    // below, and the tint recolours the white master to the state colour. Record
    // armed (!outline) still draws the filled disc in the switch. Falls back to
    // procedural stroking when no texture is loaded (headless/no GL).
    if (outline) {
        if (const SvgTexture* tex = transportIconTexture(icon)) {
            const float h = 1.2f * r;
            surface.image((draw::TextureRef)tex->id, {c.x - h, c.y - h},
                          {c.x + h, c.y + h}, fg);
            return;
        }
    }
    const float t = outline ? thickness : 2.0f;
    // Map Lucide's 24x24 viewBox (origin at its centre 12,12) onto this glyph,
    // so the shapes ARE the Lucide paths, scaled to radius r.
    const float s = r / 10.0f;
    auto P = [&](float vx, float vy) -> draw::Vec2 {
        return {c.x + (vx - 12.0f) * s, c.y + (vy - 12.0f) * s};
    };
    constexpr float kPi = 3.14159265f;
    switch (icon) {
    case Icon::Play: { // lucide "play": polygon 6,3 20,12 6,21
        const draw::Vec2 p[3] = {P(6, 3), P(20, 12), P(6, 21)};
        if (outline)
            surface.polyline(p, 3, fg, /*closed*/ true, t);
        else
            surface.fillTriangle(p[0], p[1], p[2], fg);
        break;
    }
    case Icon::Stop: // lucide "square": rect 3,3 18x18 rx2
        if (outline)
            surface.strokeRect(P(3, 3), P(21, 21), fg, 2.0f * s, t);
        else
            surface.fillRect(P(3, 3), P(21, 21), fg, 2.0f * s);
        break;
    case Icon::Record: // lucide "circle" r10; fills to a disc while armed
        if (outline)
            surface.strokeCircle(c, r, fg, 32, t);
        else
            surface.fillCircle(c, r * 0.82f, fg, 32);
        break;
    case Icon::SkipToStart: { // lucide "skip-back": bar x5 + triangle 19,20 9,12 19,4
        surface.line(P(5, 5), P(5, 19), fg, t);
        const draw::Vec2 p[3] = {P(19, 20), P(9, 12), P(19, 4)};
        if (outline)
            surface.polyline(p, 3, fg, true, t);
        else
            surface.fillTriangle(p[0], p[1], p[2], fg);
        break;
    }
    case Icon::SkipToEnd: { // lucide "skip-forward": bar x19 + triangle 5,4 15,12 5,20
        surface.line(P(19, 5), P(19, 19), fg, t);
        const draw::Vec2 p[3] = {P(5, 4), P(15, 12), P(5, 20)};
        if (outline)
            surface.polyline(p, 3, fg, true, t);
        else
            surface.fillTriangle(p[0], p[1], p[2], fg);
        break;
    }
    case Icon::Loop: { // lucide "repeat": two arrowed tracks, rounded corners
        surface.pathClear();               // top track: 3,11 -> 3,10 -arc-> 7,6 -> 21,6
        surface.pathLineTo(P(3, 11));
        surface.pathLineTo(P(3, 10));
        surface.pathArcTo(P(7, 10), 4.0f * s, kPi, 1.5f * kPi, 8);
        surface.pathLineTo(P(21, 6));
        surface.pathStroke(fg, false, t);
        const draw::Vec2 a1[3] = {P(17, 2), P(21, 6), P(17, 10)}; // top-right arrow
        surface.polyline(a1, 3, fg, false, t);
        surface.pathClear();               // bottom track: 21,13 -> 21,14 -arc-> 17,18 -> 3,18
        surface.pathLineTo(P(21, 13));
        surface.pathLineTo(P(21, 14));
        surface.pathArcTo(P(17, 14), 4.0f * s, 0.0f, 0.5f * kPi, 8);
        surface.pathLineTo(P(3, 18));
        surface.pathStroke(fg, false, t);
        const draw::Vec2 a2[3] = {P(7, 22), P(3, 18), P(7, 14)}; // bottom-left arrow
        surface.polyline(a2, 3, fg, false, t);
        break;
    }
    case Icon::Waveform: {
        const float heights[7] = {0.35f, 0.75f, 0.5f, 1.0f, 0.6f, 0.85f, 0.4f};
        const float step = (r * 2.2f) / 7.0f;
        for (int i = 0; i < 7; ++i) {
            const float x = c.x - r * 1.1f + step * (i + 0.5f);
            const float h = r * heights[i];
            surface.line({x, c.y - h}, {x, c.y + h}, fg, 2.0f);
        }
        break;
    }
    case Icon::Follow:
        surface.line({c.x - r * 0.9f, c.y - r},
                     {c.x - r * 0.9f, c.y + r}, fg, 2.0f);
        surface.line({c.x - r * 0.4f, c.y},
                     {c.x + r * 0.4f, c.y}, fg, 2.0f);
        surface.fillTriangle({c.x + r * 0.3f, c.y - r * 0.5f},
                             {c.x + r * 0.3f, c.y + r * 0.5f},
                             {c.x + r, c.y}, fg);
        break;
    case Icon::Spectrum:
        for (int i = 0; i < 12; ++i) {
            const float ft = i / 11.0f;
            const float x = c.x - r * 1.1f + ft * r * 2.2f;
            const float h = r * (0.2f + 0.8f * ft);
            surface.line({x, c.y + r * 0.9f},
                         {x, c.y + r * 0.9f - h * 1.8f}, fg, 1.5f);
        }
        break;
    }
}

void drawTransportButton(draw::Surface& surface, Icon icon, draw::Vec2 topLeft,
                         draw::Vec2 size, const Palette& pal,
                         const ControlState& state,
                         const OutlineButtonStyle& style, float glyphThickness)
{
    // house outline chrome, no text glyph
    drawOutlineButton(surface, draw::FontRef{}, 0.0f, topLeft, size, nullptr,
                      pal, state, style);
    const draw::Vec2 c{topLeft.x + size.x * 0.5f,
                       topLeft.y + size.y * 0.5f - 1.0f + style.labelOffsetY};
    const float r = std::min(size.x, size.y) * 0.30f;
    const ImU32 fg = state.disabled ? pal.textDim
                     : visible(style.text) ? style.text : pal.text;
    // Record fills to a solid disc while engaged (armed); every other transport
    // glyph is the outline shape. thickness 0 = filled.
    const bool recArmed =
        icon == Icon::Record && (state.selected || state.active);
    drawTransportGlyph(surface, icon, c, r, fg, recArmed ? 0.0f : glyphThickness);
}

void drawTransportButton(ImDrawList* dl, Icon icon, const ImVec2& topLeft,
                         const ImVec2& size, const Palette& pal,
                         const ControlState& state,
                         const OutlineButtonStyle& style, float glyphThickness)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawTransportButton(surface, icon, draw::toDrawVec2(topLeft),
                        draw::toDrawVec2(size), pal, state, style, glyphThickness);
}

void drawVectorIconButton(ImDrawList* dl, const ImVec2& topLeft,
                          const ImVec2& size, Icon icon, ImU32 accent,
                          const Palette& pal, const ControlState& state,
                          bool active)
{
    if (!dl)
        return;
    const ImGuiStyle& st = ImGui::GetStyle();
    const ImU32 bg = ImGui::GetColorU32(state.active      ? st.Colors[ImGuiCol_ButtonActive]
                                        : state.hovered  ? st.Colors[ImGuiCol_ButtonHovered]
                                                         : st.Colors[ImGuiCol_Button]);
    const ImU32 text = ImGui::GetColorU32(st.Colors[ImGuiCol_Text]);
    draw::ImGuiSurface surface(dl);
    drawVectorIconButton(surface, draw::toDrawVec2(topLeft), draw::toDrawVec2(size),
                         icon, accent, pal, state, active, bg, text);
}

void drawButton(draw::Surface& surface, draw::FontRef font, float fontSizePx,
                draw::Vec2 topLeft, draw::Vec2 size, const char* text,
                const Palette& pal, const ControlState& state)
{
    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    const float r = 4.0f;
    ImU32 fill = state.active || state.selected ? mix(pal.frame, pal.accent, 0.40f)
                                                : pal.frame;
    if (state.hovered && !state.active && !state.disabled)
        fill = pal.frameBright;
    if (state.disabled)
        fill = mix(fill, IM_COL32(0, 0, 0, 255), 0.30f);

    surface.fillRect(topLeft, mx, fill, r);
    surface.strokeRect(topLeft, mx, pal.frameBright, r);
    if (state.selected) {
        surface.fillRect({topLeft.x + 2.0f, mx.y - 4.0f},
                         {mx.x - 2.0f, mx.y - 2.0f}, pal.accent, 2.0f);
    }

    if (text && text[0] && fontSizePx > 0.0f) {
        const draw::Vec2 ts = surface.measureText(font, fontSizePx, text);
        const draw::Vec2 p{
            topLeft.x + std::max(0.0f, size.x - ts.x) * 0.5f,
            topLeft.y + std::max(0.0f, size.y - ts.y) * 0.5f};
        surface.text(font, fontSizePx, p, state.disabled ? pal.textDim : pal.text,
                     text);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(surface, topLeft, mx, pal, r);
}

void drawButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                const ImVec2& size, const char* text, const Palette& pal,
                const ControlState& state, float fontScale)
{
    if (!dl || !font)
        return;
    draw::ImGuiSurface surface(dl);
    drawButton(surface, draw::fontRef(font), ImGui::GetFontSize() * fontScale,
               draw::toDrawVec2(topLeft), draw::toDrawVec2(size), text, pal,
               state);
}

void drawSegmented(draw::Surface& surface, draw::FontRef font,
                   float fontSizePx, draw::Vec2 topLeft, draw::Vec2 size,
                   const char* const* labels, int count, int selected,
                   int hovered, const Palette& pal,
                   const ControlState& state)
{
    if (!labels || count <= 0)
        return;

    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    const float r = size.y * 0.5f; // pill
    ImU32 body = pal.frame;
    if (state.disabled)
        body = mix(body, IM_COL32(0, 0, 0, 255), 0.30f);
    surface.fillRect(topLeft, mx, body, r);

    const float segW = size.x / (float)count;
    for (int i = 0; i < count; ++i) {
        const float x0 = topLeft.x + segW * (float)i;
        const bool isSel = i == selected;
        const bool isHot = !state.disabled && i == hovered;

        if (isSel) {
            ImU32 fill = mix(pal.frame, pal.accent, state.disabled ? 0.18f : 0.40f);
            if (isHot && state.active)
                fill = mix(pal.frame, pal.accent, 0.50f);
            surface.fillRect({x0 + 2.0f, topLeft.y + 2.0f},
                             {x0 + segW - 2.0f, mx.y - 2.0f}, fill, r - 2.0f);
        } else if (isHot) {
            surface.fillRect({x0 + 2.0f, topLeft.y + 2.0f},
                             {x0 + segW - 2.0f, mx.y - 2.0f},
                             state.active ? mix(pal.frameBright, pal.accent, 0.20f)
                                          : pal.frameBright,
                             r - 2.0f);
        }
        if (i > 0 && !isSel && selected != i - 1) { // resting divider
            surface.line({x0, topLeft.y + size.y * 0.26f},
                         {x0, mx.y - size.y * 0.26f},
                         withAlpha(pal.frameBright, 0xB4), 1.0f);
        }

        const char* text = labels[i] ? labels[i] : "";
        const draw::Vec2 ts = surface.measureText(font, fontSizePx, text);
        const ImU32 txt = state.disabled ? pal.textDim
                          : isSel        ? pal.text
                          : isHot        ? mix(pal.textDim, pal.text, 0.55f)
                                         : pal.textDim;
        if (fontSizePx > 0.0f)
            surface.text(font, fontSizePx,
                         {x0 + (segW - ts.x) * 0.5f,
                          topLeft.y + (size.y - ts.y) * 0.5f},
                         txt, text);
    }

    surface.strokeRect(topLeft, mx, pal.frameBright, r);
    if (state.focused && !state.disabled)
        drawFocusRing(surface, topLeft, mx, pal, r);
}

void drawSegmented(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                   const ImVec2& size, const char* const* labels, int count,
                   int selected, int hovered, const Palette& pal,
                   const ControlState& state, float fontScale)
{
    if (!dl || !font)
        return;
    draw::ImGuiSurface surface(dl);
    drawSegmented(surface, draw::fontRef(font), ImGui::GetFontSize() * fontScale,
                  draw::toDrawVec2(topLeft), draw::toDrawVec2(size), labels,
                  count, selected, hovered, pal, state);
}

void drawTabBar(draw::Surface& surface, draw::FontRef font,
                float fontSizePx, draw::Vec2 topLeft, draw::Vec2 size,
                const char* const* labels, int count, int selected,
                int hovered, const Palette& pal, const ControlState& state)
{
    if (!labels || count <= 0)
        return;

    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    const float tabW = size.x / (float)count;
    const float railY = mx.y - 1.0f;
    const int sel = std::clamp(selected, 0, count - 1);

    surface.line({topLeft.x, railY}, {mx.x, railY},
                 withAlpha(pal.frameBright, 0xD0), 1.0f);

    for (int i = 0; i < count; ++i) {
        const float x0 = topLeft.x + tabW * (float)i;
        const float x1 = (i == count - 1) ? mx.x : x0 + tabW;
        const bool isSel = i == sel;
        const bool isHot = !state.disabled && i == hovered;
        const draw::Vec2 mn{x0, topLeft.y};
        const draw::Vec2 tabMx{x1, mx.y};

        ImU32 fill = 0;
        if (isSel)
            fill = mix(pal.frame, pal.accent, state.disabled ? 0.10f : 0.18f);
        else if (isHot)
            fill = state.active ? mix(pal.frameBright, pal.accent, 0.18f)
                                : withAlpha(pal.frameBright, 0x80);
        if (visible(fill))
            surface.fillRect({mn.x + 1.0f, mn.y + 1.0f},
                             {tabMx.x - 1.0f, tabMx.y}, fill, 4.0f);

        if (i > 0 && !isSel && sel != i - 1) {
            surface.line({x0, topLeft.y + 5.0f}, {x0, mx.y - 6.0f},
                         withAlpha(pal.frameBright, 0x80), 1.0f);
        }

        const char* text = labels[i] ? labels[i] : "";
        const draw::Vec2 ts = surface.measureText(font, fontSizePx, text);
        const ImU32 txt = state.disabled ? pal.textDim
                          : isSel        ? pal.text
                          : isHot        ? mix(pal.textDim, pal.text, 0.65f)
                                         : pal.textDim;
        if (fontSizePx > 0.0f)
            surface.text(font, fontSizePx,
                         {x0 + std::max(0.0f, tabW - ts.x) * 0.5f,
                          topLeft.y + std::max(0.0f, size.y - ts.y) * 0.5f -
                              1.0f},
                         txt, text);

        if (isSel) {
            surface.line({x0 + 3.0f, mx.y - 2.0f},
                         {x1 - 3.0f, mx.y - 2.0f},
                         state.disabled ? withAlpha(pal.frameBright, 0xAA)
                                        : pal.accent,
                         2.0f);
        }
    }

    if (state.focused && !state.disabled) {
        const float x0 = topLeft.x + tabW * (float)sel;
        const float x1 = sel == count - 1 ? mx.x : x0 + tabW;
        drawFocusRing(surface, {x0 + 1.0f, topLeft.y + 1.0f},
                      {x1 - 1.0f, mx.y - 2.0f}, pal, 4.0f, 1.5f);
    }
}

void drawTabBar(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                const ImVec2& size, const char* const* labels, int count,
                int selected, int hovered, const Palette& pal,
                const ControlState& state, float fontScale)
{
    if (!dl || !font)
        return;
    draw::ImGuiSurface surface(dl);
    drawTabBar(surface, draw::fontRef(font), ImGui::GetFontSize() * fontScale,
               draw::toDrawVec2(topLeft), draw::toDrawVec2(size), labels,
               count, selected, hovered, pal, state);
}

void drawCycleButton(draw::Surface& surface, draw::FontRef font,
                     float fontSizePx, draw::Vec2 topLeft, draw::Vec2 size,
                     const char* text, int index, int count,
                     const Palette& pal, const ControlState& state)
{
    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    const float r = 4.0f;
    ImU32 fill = state.active ? mix(pal.frame, pal.accent, 0.40f) : pal.frame;
    if (state.hovered && !state.active && !state.disabled)
        fill = pal.frameBright;
    if (state.disabled)
        fill = mix(fill, IM_COL32(0, 0, 0, 255), 0.30f);
    surface.fillRect(topLeft, mx, fill, r);
    surface.strokeRect(topLeft, mx, pal.frameBright, r);

    const float pipGap = 6.0f;
    const bool pips = count > 1 && count <= 8 && size.y >= 22.0f &&
                      pipGap * (float)(count - 1) <= size.x - 12.0f;
    if (text && text[0] && fontSizePx > 0.0f) {
        const draw::Vec2 ts = surface.measureText(font, fontSizePx, text);
        const draw::Vec2 p{
            topLeft.x + std::max(0.0f, size.x - ts.x) * 0.5f,
            topLeft.y + std::max(0.0f, size.y - ts.y) * 0.5f -
                (pips ? 1.5f : 0.0f)};
        surface.text(font, fontSizePx, p, state.disabled ? pal.textDim : pal.text,
                     text);
    }

    if (pips) { // position within the cycle
        const float y = mx.y - 4.5f;
        const float x0 = topLeft.x + size.x * 0.5f -
                         pipGap * (float)(count - 1) * 0.5f;
        for (int i = 0; i < count; ++i) {
            const bool onPip = i == index;
            surface.fillCircle({x0 + pipGap * (float)i, y},
                               onPip ? 1.8f : 1.3f,
                               onPip && !state.disabled
                                   ? pal.accent
                                   : withAlpha(pal.frameBright, 0xE6));
        }
    }

    if (state.focused && !state.disabled)
        drawFocusRing(surface, topLeft, mx, pal, r);
}

void drawCycleButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                     const ImVec2& size, const char* text, int index, int count,
                     const Palette& pal, const ControlState& state,
                     float fontScale)
{
    if (!dl || !font)
        return;
    draw::ImGuiSurface surface(dl);
    drawCycleButton(surface, draw::fontRef(font), ImGui::GetFontSize() * fontScale,
                    draw::toDrawVec2(topLeft), draw::toDrawVec2(size), text,
                    index, count, pal, state);
}

void drawOutlineButton(draw::Surface& surface, draw::FontRef font,
                       float fontSizePx, draw::Vec2 topLeft, draw::Vec2 size,
                       const char* text, const Palette& pal,
                       const ControlState& state,
                       const OutlineButtonStyle& style)
{
    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    const float r = style.rounding > 0.0f ? style.rounding : 0.0f;

    ImU32 fill = style.fill;
    if (state.hovered && visible(style.hoverFill))
        fill = style.hoverFill;
    if (state.active)
        fill = visible(style.activeFill) ? style.activeFill
                                         : withAlpha(pal.accent, 0x2A);
    if (state.selected && visible(style.selectedFill))
        fill = style.selectedFill;
    if (state.disabled && visible(fill))
        fill = mix(fill, IM_COL32(0, 0, 0, 255), 0.35f);
    if (visible(fill))
        surface.fillRect(topLeft, mx, fill, r);

    // engaged states fall back to the RESTING border, not accent: an accent
    // fallback equals the usual accent fill and the border vanishes into it
    // (owner: "borders disappear when pressed, leave borders in").
    const ImU32 engagedDefault = visible(style.border) ? style.border : pal.accent;
    ImU32 border = style.border;
    if (state.hovered)
        border = visible(style.hoverBorder) ? style.hoverBorder : engagedDefault;
    if (state.active)
        border = visible(style.activeBorder) ? style.activeBorder : engagedDefault;
    if (state.selected)
        border = visible(style.selectedBorder) ? style.selectedBorder : engagedDefault;
    if (state.disabled && visible(border))
        border = mix(border, pal.frameBright, 0.65f);
    if (visible(border)) {
        const float bt = state.active || state.selected ? style.engagedThickness
                                                        : style.borderThickness;
        surface.strokeRect(topLeft, mx, border, r, bt);
    }

    if (text && text[0] && fontSizePx > 0.0f) {
        const draw::Vec2 ts = surface.measureText(font, fontSizePx, text);
        const draw::Vec2 p{
            topLeft.x + std::max(0.0f, size.x - ts.x) * 0.5f,
            topLeft.y + std::max(0.0f, size.y - ts.y) * 0.5f + style.labelOffsetY};
        const ImU32 textCol = state.disabled ? pal.textDim
                                             : visible(style.text) ? style.text : pal.text;
        surface.text(font, fontSizePx, p, textCol, text);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(surface, topLeft, mx, pal, r);
}

void drawOutlineButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                       const ImVec2& size, const char* text,
                       const Palette& pal, const ControlState& state,
                       const OutlineButtonStyle& style)
{
    if (!dl || !font)
        return;
    draw::ImGuiSurface surface(dl);
    const float scale = style.fontScale > 0.0f ? style.fontScale : 0.90f;
    drawOutlineButton(surface, draw::fontRef(font), ImGui::GetFontSize() * scale,
                      draw::toDrawVec2(topLeft), draw::toDrawVec2(size), text,
                      pal, state, style);
}

void drawDefaultButton(const ButtonPaintArgs& args)
{
    if (!args.palette || !args.state)
        return;
    if (args.surface) {
        drawButton(*args.surface, effectiveButtonFont(args),
                   effectiveButtonFontSize(args),
                   draw::toDrawVec2(args.topLeft), draw::toDrawVec2(args.size),
                   args.text, *args.palette, *args.state);
    } else {
        drawButton(args.drawList, args.font, args.topLeft, args.size, args.text,
                   *args.palette, *args.state, args.fontScale);
    }
}

void drawButtonWithPainter(const ButtonPaintArgs& args,
                           const ButtonPainter& painter)
{
    ImDrawList* dl = args.drawList;
    draw::ImGuiSurface localSurface(dl);
    draw::Surface* surface = args.surface ? args.surface : (dl ? &localSurface : nullptr);

    ControlState bodyState = args.state ? *args.state : ControlState{};
    bodyState.focused = false;
    ButtonPaintArgs bodyArgs = args;
    bodyArgs.state = &bodyState;
    bodyArgs.surface = surface;
    bodyArgs.fontRef = effectiveButtonFont(args);
    bodyArgs.fontSizePx = effectiveButtonFontSize(args);

    if (painter)
        painter(bodyArgs);
    else
        drawDefaultButton(bodyArgs);

    if (surface && args.palette && args.state &&
        args.state->focused && !args.state->disabled) {
        drawFocusRing(*surface, draw::toDrawVec2(args.topLeft),
                      {args.topLeft.x + args.size.x,
                       args.topLeft.y + args.size.y},
                      *args.palette, 4.0f, 2.0f);
    }
}

void drawMenuPanel(draw::Surface& surface, draw::Vec2 topLeft,
                   draw::Vec2 size, const Palette& pal)
{
    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    surface.fillRect({topLeft.x + 2.0f, topLeft.y + 3.0f},
                     {mx.x + 2.0f, mx.y + 3.0f},
                     IM_COL32(0, 0, 0, 110), 0.0f);
    surface.fillRect(topLeft, mx,
                     mix(pal.frame, IM_COL32(0, 0, 0, 255), 0.18f), 0.0f);
    surface.strokeRect(topLeft, mx, pal.frameBright, 0.0f); // the one border
}

void drawMenuPanel(ImDrawList* dl, const ImVec2& topLeft,
                   const ImVec2& size, const Palette& pal)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawMenuPanel(surface, draw::toDrawVec2(topLeft), draw::toDrawVec2(size), pal);
}

void drawMenuItem(draw::Surface& surface, draw::FontRef font,
                  draw::FontRef iconFont, float fontSizePx,
                  draw::Vec2 topLeft, draw::Vec2 size,
                  const MenuItem& item, const Palette& pal,
                  const ControlState& state)
{
    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    if (item.separator) {
        const float y = topLeft.y + size.y * 0.5f;
        surface.line({topLeft.x + 8.0f, y}, {mx.x - 8.0f, y},
                     withAlpha(pal.frameBright, 0xA8), 1.0f);
        return;
    }

    ImU32 fill = 0;
    if (state.selected)
        fill = withAlpha(pal.accent, state.disabled ? 0x18 : 0x34);
    if (state.hovered && !state.disabled)
        fill = mix(visible(fill) ? fill : pal.frame, pal.frameBright, 0.72f);
    if (state.active && !state.disabled)
        fill = mix(visible(fill) ? fill : pal.frameBright, pal.accent, 0.32f);
    // Keyboard focus reads as a stronger fill, not another border: menu rows
    // carry exactly one visual (murk's filled highlight bar), square corners.
    if (state.focused && !state.disabled)
        fill = mix(visible(fill) ? fill : pal.frame, pal.accent, 0.30f);
    if (visible(fill))
        surface.fillRect(topLeft, mx, fill, 0.0f);

    const float centerY = topLeft.y + size.y * 0.5f;
    const ImU32 hotCol = item.danger ? pal.meterHot : pal.accent;
    const ImU32 textCol = state.disabled ? pal.textDim
                                         : item.danger ? mix(pal.text, pal.meterHot, 0.36f)
                                                       : pal.text;
    const ImU32 accent = state.disabled ? mix(hotCol, pal.frameBright, 0.60f)
                                        : hotCol;

    if (item.checked)
        drawMenuCheck(surface, {topLeft.x + 14.0f, centerY}, accent);
    else if (item.danger) {
        surface.fillTriangle({topLeft.x + 13.0f, centerY - 6.0f},
                             {topLeft.x + 18.0f, centerY + 5.0f},
                             {topLeft.x + 8.0f, centerY + 5.0f}, accent);
    }

    const bool hasImage = item.image != ImTextureID_Invalid;
    if (hasImage) {
        // Texture icon: per-row SVG/PNG image.
        const float side = std::min(size.y - 6.0f, 18.0f);
        const float ix = topLeft.x + 26.0f;
        const float iy = topLeft.y + (size.y - side) * 0.5f;
        surface.image((draw::TextureRef)item.image, {ix, iy},
                      {ix + side, iy + side},
                      state.disabled ? IM_COL32(255, 255, 255, 128)
                                     : IM_COL32(255, 255, 255, 255));
    } else if (!item.icon.empty()) {
        const draw::FontRef glyphFont = iconFont.handle ? iconFont : font;
        const float iconSize = fontSizePx;
        draw::Vec2 glyphSize = surface.measureText(glyphFont, iconSize,
                                                   item.icon.c_str());
        if (iconSize > 0.0f)
            surface.text(glyphFont, iconSize,
                         {topLeft.x + 26.0f,
                          topLeft.y + std::max(0.0f, size.y - glyphSize.y) * 0.5f},
                         state.disabled ? pal.textDim : accent, item.icon.c_str());
    }

    const float textX = topLeft.x +
                        (item.icon.empty() && !hasImage ? 30.0f : 50.0f);
    const char* label = item.label.c_str();
    draw::Vec2 labelSize = surface.measureText(font, fontSizePx, label);
    const bool hasSubmenu = !item.children.empty();
    const float arrowPad = hasSubmenu ? 18.0f : 0.0f;
    const float rightPad = item.rightText.empty()
                               ? 8.0f + arrowPad
                               : surface.measureText(font, fontSizePx,
                                                     item.rightText.c_str()).x + 24.0f +
                                     arrowPad;
    surface.pushClip({textX, topLeft.y}, {mx.x - rightPad, mx.y}, true);
    if (fontSizePx > 0.0f)
        surface.text(font, fontSizePx,
                     {textX, topLeft.y + std::max(0.0f, size.y - labelSize.y) * 0.5f},
                     textCol, label);
    surface.popClip();

    if (!item.rightText.empty()) {
        const ImU32 rightCol = state.disabled ? pal.textDim
                                             : item.danger ? accent : pal.textDim;
        draw::Vec2 rightSize = surface.measureText(font, fontSizePx,
                                                   item.rightText.c_str());
        if (fontSizePx > 0.0f)
            surface.text(font, fontSizePx,
                         {mx.x - 10.0f - arrowPad - rightSize.x,
                          topLeft.y + std::max(0.0f, size.y - rightSize.y) * 0.5f},
                         rightCol, item.rightText.c_str());
    }

    if (hasSubmenu) {
        const ImU32 arrowCol = state.disabled ? pal.textDim : pal.text;
        const float x = mx.x - 12.0f;
        surface.fillTriangle({x - 3.0f, centerY - 5.0f},
                             {x - 3.0f, centerY + 5.0f},
                             {x + 3.0f, centerY}, arrowCol);
    }
    // No focus ring on menu rows: keyboard focus reads as the stronger fill
    // (one visual per row, owner directive).
}

void drawMenuItem(ImDrawList* dl, ImFont* font, ImFont* iconFont,
                  const ImVec2& topLeft, const ImVec2& size,
                  const MenuItem& item, const Palette& pal,
                  const ControlState& state, float fontScale)
{
    if (!dl || !font)
        return;
    draw::ImGuiSurface surface(dl);
    drawMenuItem(surface, draw::fontRef(font), draw::fontRef(iconFont),
                 ImGui::GetFontSize() * fontScale, draw::toDrawVec2(topLeft),
                 draw::toDrawVec2(size), item, pal, state);
}

void drawListItem(draw::Surface& surface, draw::FontRef font, float fontSizePx,
                  draw::Vec2 topLeft, draw::Vec2 size, const char* text,
                  const Palette& pal, const ControlState& state)
{
    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    ImU32 fill = state.selected ? withAlpha(pal.accent, 0x38) : pal.frame;
    if (state.hovered && !state.disabled)
        fill = mix(fill, pal.frameBright, 0.65f);
    if (state.active && !state.disabled)
        fill = mix(fill, pal.accent, 0.35f);
    if (state.disabled)
        fill = mix(fill, IM_COL32(0, 0, 0, 255), 0.35f);

    surface.fillRect(topLeft, mx, fill, 3.0f);
    surface.strokeRect(topLeft, mx, state.selected ? pal.accent : pal.frameBright, 3.0f);
    if (state.selected) {
        surface.fillRect({topLeft.x + 2.0f, topLeft.y + 3.0f},
                         {topLeft.x + 5.0f, mx.y - 3.0f}, pal.accent, 2.0f);
    }

    if (text && text[0] && fontSizePx > 0.0f) {
        draw::Vec2 ts = surface.measureText(font, fontSizePx, text);
        surface.text(font, fontSizePx,
                     {topLeft.x + 10.0f,
                      topLeft.y + std::max(0.0f, size.y - ts.y) * 0.5f},
                     state.disabled ? pal.textDim : pal.text, text);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(surface, topLeft, mx, pal, 3.0f);
}

void drawListItem(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                  const ImVec2& size, const char* text, const Palette& pal,
                  const ControlState& state, float fontScale)
{
    if (!dl || !font)
        return;
    draw::ImGuiSurface surface(dl);
    drawListItem(surface, draw::fontRef(font), ImGui::GetFontSize() * fontScale,
                 draw::toDrawVec2(topLeft), draw::toDrawVec2(size), text, pal,
                 state);
}

void drawValueRow(draw::Surface& surface, draw::FontRef font, float fontSizePx,
                  draw::Vec2 topLeft, draw::Vec2 size, const char* label,
                  const char* valueText, const Palette& pal,
                  const ControlState& state, bool draggable)
{
    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    const float r = 0.0f; // house style: square
    ImU32 fill = state.active ? mix(pal.frame, pal.accent, 0.24f) : pal.frame;
    if (state.hovered && !state.active && !state.disabled)
        fill = mix(fill, pal.frameBright, 0.55f);
    if (state.disabled)
        fill = mix(fill, IM_COL32(0, 0, 0, 255), 0.35f);

    surface.fillRect(topLeft, mx, fill, r);
    surface.strokeRect(topLeft, mx, state.active ? pal.accent : pal.frameBright, r);

    const ImU32 accent = state.disabled ? mix(pal.accent, pal.frameBright, 0.65f)
                                        : pal.accent;
    const float railW = state.active ? 4.0f : 3.0f;
    surface.fillRect({topLeft.x + 2.0f, topLeft.y + 3.0f},
                     {topLeft.x + 2.0f + railW, mx.y - 3.0f},
                     state.active ? accent : withAlpha(accent, 0x86), 2.0f);

    const float padX = 10.0f;
    const float y = topLeft.y + std::max(0.0f, size.y - fontSizePx) * 0.5f - 1.0f;
    const char* labelText = label ? label : "";
    const char* value = valueText ? valueText : "";
    draw::Vec2 valueSize = surface.measureText(font, fontSizePx, value);
    const float valueX = std::max(topLeft.x + padX, mx.x - padX - valueSize.x);
    const float labelRight = std::max(topLeft.x + padX, valueX - 8.0f);

    surface.pushClip({topLeft.x + padX, topLeft.y}, {labelRight, mx.y}, true);
    if (labelText[0] && fontSizePx > 0.0f)
        surface.text(font, fontSizePx, {topLeft.x + padX, y}, pal.textDim,
                     labelText);
    surface.popClip();

    surface.pushClip({topLeft.x + padX, topLeft.y}, {mx.x - padX, mx.y}, true);
    if (value[0] && fontSizePx > 0.0f)
        surface.text(font, fontSizePx, {valueX, y},
                     state.disabled ? pal.textDim : pal.text, value);
    surface.popClip();

    if (draggable) {
        const float gripX = mx.x - 5.0f;
        const ImU32 grip = state.disabled ? withAlpha(pal.textDim, 0x60)
                                          : withAlpha(pal.textDim, state.hovered ? 0xB8 : 0x80);
        surface.line({gripX, topLeft.y + 7.0f}, {gripX, mx.y - 7.0f}, grip, 1.0f);
        surface.line({gripX - 3.0f, topLeft.y + 8.0f},
                     {gripX - 3.0f, mx.y - 8.0f}, withAlpha(grip, 0x72), 1.0f);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(surface, topLeft, mx, pal, r);
}

void drawValueRow(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                  const ImVec2& size, const char* label, const char* valueText,
                  const Palette& pal, const ControlState& state,
                  float fontScale, bool draggable)
{
    if (!dl || !font)
        return;
    draw::ImGuiSurface surface(dl);
    drawValueRow(surface, draw::fontRef(font), ImGui::GetFontSize() * fontScale,
                 draw::toDrawVec2(topLeft), draw::toDrawVec2(size), label,
                 valueText, pal, state, draggable);
}

namespace {
void drawPatternGridImpl(draw::Surface& surface, ImDrawList* drawListForPainter,
                         draw::Vec2 topLeft, draw::Vec2 size,
                         const bool* cells, int rows, int steps, int playheadStep,
                         const Palette& pal, const ControlState& state,
                         const PatternCellPainter& cellPainter)
{
    if (!cells || rows <= 0 || steps <= 0)
        return;

    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    const float cw = size.x / (float)steps;
    const float ch = size.y / (float)rows;

    surface.fillRect(topLeft, mx, pal.frame, 3.0f);
    if (playheadStep >= 0 && playheadStep < steps) {
        surface.fillRect({topLeft.x + playheadStep * cw, topLeft.y},
                         {topLeft.x + (playheadStep + 1) * cw,
                          topLeft.y + size.y},
                         withAlpha(pal.accent, state.disabled ? 0x14 : 0x24));
    }

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < steps; ++c) {
            draw::Vec2 a{topLeft.x + c * cw + 1.5f,
                         topLeft.y + r * ch + 1.5f};
            draw::Vec2 b{topLeft.x + (c + 1) * cw - 1.5f,
                         topLeft.y + (r + 1) * ch - 1.5f};
            const bool on = cells[r * steps + c];
            if (cellPainter) {
                PatternCellPaintArgs args;
                args.drawList = drawListForPainter;
                args.gridTopLeft = draw::toImVec2(topLeft);
                args.gridSize = draw::toImVec2(size);
                args.cellMin = draw::toImVec2(a);
                args.cellMax = draw::toImVec2(b);
                args.row = r;
                args.step = c;
                args.rows = rows;
                args.steps = steps;
                args.on = on;
                args.playheadStep = playheadStep;
                args.palette = &pal;
                args.state = &state;
                args.surface = &surface;
                cellPainter(args);
                continue;
            }
            ImU32 col = on ? (c == playheadStep ? pal.text : pal.accent)
                           : withAlpha(pal.frameBright, (c / 4) % 2 ? 0x30 : 0x55);
            if (state.disabled)
                col = mix(col, pal.frame, 0.55f);
            surface.fillRect(a, b, col, 2.0f);
        }
    }

    surface.strokeRect(topLeft, mx, pal.frameBright, 3.0f);
    if (state.focused && !state.disabled)
        drawFocusRing(surface, topLeft, mx, pal, 3.0f);
}
} // namespace

void drawPatternGrid(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                     const bool* cells, int rows, int steps, int playheadStep,
                     const Palette& pal, const ControlState& state,
                     const PatternCellPainter& cellPainter)
{
    drawPatternGridImpl(surface, nullptr, topLeft, size, cells, rows, steps,
                        playheadStep, pal, state, cellPainter);
}

void drawPatternGrid(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                     const bool* cells, int rows, int steps, int playheadStep,
                     const Palette& pal, const ControlState& state,
                     const PatternCellPainter& cellPainter)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawPatternGridImpl(surface, dl, draw::toDrawVec2(topLeft),
                        draw::toDrawVec2(size), cells, rows, steps,
                        playheadStep, pal, state, cellPainter);
}

void drawPatternGrid(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                     const bool* cells, int rows, int steps, int playheadStep,
                     const Palette& pal, const ControlState& state)
{
    drawPatternGrid(dl, topLeft, size, cells, rows, steps, playheadStep, pal,
                    state, {});
}

void drawPatternGrid(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                     const bool* cells, int rows, int steps, int playheadStep,
                     const Palette& pal, const ControlState& state)
{
    drawPatternGrid(surface, topLeft, size, cells, rows, steps, playheadStep,
                    pal, state, {});
}

void drawXYPad(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
               float x, float y, const Palette& pal, const ControlState& state)
{
    x = std::clamp(x, 0.0f, 1.0f);
    y = std::clamp(y, 0.0f, 1.0f);
    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    ImU32 accent = state.disabled ? mix(pal.accent, pal.frameBright, 0.65f)
                                  : pal.accent;

    surface.fillRect(topLeft, mx, pal.frame, 3.0f);
    const float cx = topLeft.x + x * size.x;
    const float cy = topLeft.y + (1.0f - y) * size.y;
    surface.line({cx, topLeft.y}, {cx, topLeft.y + size.y},
                 withAlpha(accent, state.disabled ? 0x30 : 0x50));
    surface.line({topLeft.x, cy}, {topLeft.x + size.x, cy},
                 withAlpha(accent, state.disabled ? 0x30 : 0x50));
    surface.fillCircle({cx, cy}, state.active ? 7.0f : 6.0f, accent);
    if (!state.disabled) {
        surface.fillCircle({cx - 1.5f, cy - 1.8f}, 1.6f,
                           IM_COL32(255, 255, 255, 170));
    }
    if (state.hovered && !state.disabled)
        surface.strokeCircle({cx, cy}, 10.0f, withAlpha(pal.text, 0x70), 0, 1.2f);
    surface.strokeRect(topLeft, mx, pal.frameBright, 3.0f);
    if (state.focused && !state.disabled)
        drawFocusRing(surface, topLeft, mx, pal, 3.0f);
}

void drawXYPad(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
               float x, float y, const Palette& pal, const ControlState& state)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawXYPad(surface, draw::toDrawVec2(topLeft), draw::toDrawVec2(size),
              x, y, pal, state);
}

void drawDefaultXYPad(const XYPadPaintArgs& args)
{
    if (!args.palette || !args.state)
        return;
    if (args.surface)
        drawXYPad(*args.surface, draw::toDrawVec2(args.topLeft),
                  draw::toDrawVec2(args.size), args.x, args.y,
                  *args.palette, *args.state);
    else
        drawXYPad(args.drawList, args.topLeft, args.size, args.x, args.y,
                  *args.palette, *args.state);
}

void drawXYPadWithPainter(const XYPadPaintArgs& args, const XYPadPainter& painter)
{
    ImDrawList* dl = args.drawList;
    draw::ImGuiSurface localSurface(dl);
    draw::Surface* surface = args.surface ? args.surface : (dl ? &localSurface : nullptr);

    ControlState bodyState = args.state ? *args.state : ControlState{};
    bodyState.focused = false;
    XYPadPaintArgs bodyArgs = args;
    bodyArgs.state = &bodyState;
    bodyArgs.surface = surface;

    if (painter)
        painter(bodyArgs);
    else
        drawDefaultXYPad(bodyArgs);

    if (surface && args.palette && args.state &&
        args.state->focused && !args.state->disabled) {
        drawFocusRing(*surface, draw::toDrawVec2(args.topLeft),
                      {args.topLeft.x + args.size.x,
                       args.topLeft.y + args.size.y},
                      *args.palette, 3.0f);
    }
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

    for (int w = 0; w < whites; ++w) {
        int note = firstNote + (w / 7) * 12 + whiteSemi[w % 7];
        ImVec2 a(topLeft.x + w * ww, topLeft.y);
        ImVec2 b(topLeft.x + (w + 1) * ww - 1.0f, topLeft.y + size.y);
        dl->AddRectFilled(a, b, isDown(note) ? litWhite : whiteUp,
                          2.0f, ImDrawFlags_RoundCornersBottom);
        dl->AddRect(a, b, IM_COL32(40, 42, 50, 255), 2.0f,
                    ImDrawFlags_RoundCornersBottom);
    }

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

void drawKeyboard(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                  int firstNote, int octaves, int mouseNote, const bool* lit,
                  const Palette& pal, const ControlState& state)
{
    if (octaves <= 0)
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

    for (int w = 0; w < whites; ++w) {
        int note = firstNote + (w / 7) * 12 + whiteSemi[w % 7];
        draw::Vec2 a{topLeft.x + w * ww, topLeft.y};
        draw::Vec2 b{topLeft.x + (w + 1) * ww - 1.0f, topLeft.y + size.y};
        surface.fillRect(a, b, isDown(note) ? litWhite : whiteUp, 2.0f,
                         draw::kRoundCornersBottom);
        surface.strokeRect(a, b, IM_COL32(40, 42, 50, 255), 2.0f, 1.0f,
                           draw::kRoundCornersBottom);
    }

    for (int w = 0; w < whites; ++w) {
        int inOct = w % 7;
        if (!blackAfterWhite[inOct] || w == whites - 1)
            continue;
        int note = firstNote + (w / 7) * 12 + blackSemi[inOct];
        float bx = topLeft.x + (w + 1) * ww - bw * 0.5f;
        draw::Vec2 a{bx, topLeft.y};
        draw::Vec2 b{bx + bw, topLeft.y + bh};
        surface.fillRect(a, b, isDown(note) ? litBlack : blackUp, 2.0f,
                         draw::kRoundCornersBottom);
        surface.strokeRect(a, b, IM_COL32(0, 0, 0, 255), 2.0f, 1.0f,
                           draw::kRoundCornersBottom);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(surface, topLeft, {topLeft.x + size.x, topLeft.y + size.y},
                      pal, 3.0f);
}

float envelopeEase(float t, float tension)
{
    t = std::clamp(t, 0.0f, 1.0f);
    tension = std::clamp(tension, -1.0f, 1.0f);
    if (tension > 0.0f)
        return std::pow(t, 1.0f + tension * 3.0f);
    if (tension < 0.0f)
        return 1.0f - std::pow(1.0f - t, 1.0f - tension * 3.0f);
    return t;
}

void drawEnvelope(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                  const std::vector<EnvPoint>& points,
                  const std::vector<float>* tensions,
                  int hotPoint, int activePoint,
                  int hotSegment, int activeSegment,
                  const Palette& pal, const ControlState& state)
{
    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    surface.fillRect(topLeft, mx, pal.frame, 3.0f);

    if (size.x > 0.0f && size.y > 0.0f && points.size() >= 2) {
        auto toScreen = [&](const EnvPoint& e) {
            return draw::Vec2{topLeft.x + e.x * size.x,
                              topLeft.y + (1.0f - e.y) * size.y};
        };
        auto segTen = [&](size_t i) {
            return tensions && i < tensions->size() ? (*tensions)[i] : 0.0f;
        };

        ImU32 accent = state.disabled ? mix(pal.accent, pal.frameBright, 0.65f)
                                      : pal.accent;
        ImU32 hot = state.disabled ? pal.textDim : pal.text;

        for (size_t i = 1; i < points.size(); ++i) {
            const int segment = (int)i - 1;
            const bool segmentHot = segment == (activeSegment >= 0 ? activeSegment
                                                                   : hotSegment);
            const ImU32 col = segmentHot ? hot : accent;
            const float ten = segTen(i - 1);
            if (ten == 0.0f) {
                surface.line(toScreen(points[i - 1]), toScreen(points[i]), col, 2.0f);
            } else {
                constexpr int kSteps = 16;
                draw::Vec2 prev = toScreen(points[i - 1]);
                for (int s = 1; s <= kSteps; ++s) {
                    const float t = (float)s / (float)kSteps;
                    EnvPoint q{points[i - 1].x + (points[i].x - points[i - 1].x) * t,
                               points[i - 1].y +
                                   (points[i].y - points[i - 1].y) *
                                       envelopeEase(t, ten)};
                    draw::Vec2 cur = toScreen(q);
                    surface.line(prev, cur, col, 2.0f);
                    prev = cur;
                }
            }
        }

        for (int i = 0; i < (int)points.size(); ++i) {
            const bool pointHot = i == (activePoint >= 0 ? activePoint : hotPoint);
            const draw::Vec2 s = toScreen(points[(size_t)i]);
            surface.fillCircle(s, pointHot ? 6.0f : 4.5f,
                               pointHot ? hot : accent);
        }
    }

    surface.strokeRect(topLeft, mx, pal.frameBright, 3.0f);
    if (state.focused && !state.disabled)
        drawFocusRing(surface, topLeft, mx, pal, 3.0f);
}

void drawEnvelope(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                  const std::vector<EnvPoint>& points,
                  const std::vector<float>* tensions,
                  int hotPoint, int activePoint,
                  int hotSegment, int activeSegment,
                  const Palette& pal, const ControlState& state)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawEnvelope(surface, draw::toDrawVec2(topLeft), draw::toDrawVec2(size),
                 points, tensions, hotPoint, activePoint, hotSegment,
                 activeSegment, pal, state);
}

namespace {

ImU32 graphColor(ImU32 preferred, ImU32 fallback)
{
    return visible(preferred) ? preferred : fallback;
}

ImU32 hsvToRgbColor(float h, float s, float v)
{
    h = h - std::floor(h);
    s = std::clamp(s, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    const float c = v * s;
    const float hp = h * 6.0f;
    const float x = c * (1.0f - std::abs(std::fmod(hp, 2.0f) - 1.0f));
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (hp < 1.0f) {
        r = c; g = x;
    } else if (hp < 2.0f) {
        r = x; g = c;
    } else if (hp < 3.0f) {
        g = c; b = x;
    } else if (hp < 4.0f) {
        g = x; b = c;
    } else if (hp < 5.0f) {
        r = x; b = c;
    } else {
        r = c; b = x;
    }
    const float m = v - c;
    return IM_COL32((int)((r + m) * 255.0f), (int)((g + m) * 255.0f),
                    (int)((b + m) * 255.0f), 255);
}

void graphCableControls(draw::Vec2 from, draw::Vec2 to,
                        const GraphSurfaceStyle& style,
                        draw::Vec2& c1, draw::Vec2& c2, float zoom)
{
    // The fixed constants are murk's, in GRAPH units -- scale them by the
    // viewport zoom when the endpoints are zoomed screen space, or wires
    // balloon into huge curls as everything else shrinks (the |dx| term
    // scales itself).
    const float z = zoom > 0.0f ? zoom : 1.0f;
    const float dx = std::max((style.wireDroop ? 40.0f : 38.0f) * z,
                              std::abs(to.x - from.x) *
                                  (style.wireDroop ? 0.50f : 0.52f));
    const float sag = style.wireDroop
                          ? std::min(80.0f * z,
                                     22.0f * z +
                                         std::abs(to.x - from.x) * 0.18f)
                          : 0.0f;
    c1 = {from.x + dx, from.y + sag};
    c2 = {to.x - dx, to.y + sag};
}

void graphCableControls(const ImVec2& from, const ImVec2& to,
                        const GraphSurfaceStyle& style,
                        ImVec2& c1, ImVec2& c2, float zoom)
{
    draw::Vec2 a;
    draw::Vec2 b;
    graphCableControls(draw::toDrawVec2(from), draw::toDrawVec2(to), style, a,
                       b, zoom);
    c1 = ImVec2(a.x, a.y);
    c2 = ImVec2(b.x, b.y);
}

} // namespace

namespace {

// Aurora field noise, ported from a compact shader-style gradient noise:
// hash2(p) = fract(sin([dot(p,(127.1,311.7)), dot(p,(269.5,183.3))])*43758.5453123)*2-1
void fieldHash2(float px, float py, float& hx, float& hy)
{
    const float d1 = px * 127.1f + py * 311.7f;
    const float d2 = px * 269.5f + py * 183.3f;
    const float s1 = std::sin(d1) * 43758.5453123f;
    const float s2 = std::sin(d2) * 43758.5453123f;
    hx = (s1 - std::floor(s1)) * 2.0f - 1.0f;
    hy = (s2 - std::floor(s2)) * 2.0f - 1.0f;
}

float fieldNoise(float px, float py)
{
    const float ix = std::floor(px), iy = std::floor(py);
    const float fx = px - ix, fy = py - iy;
    const float ux = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
    const float uy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);
    float hx, hy;
    fieldHash2(ix, iy, hx, hy);
    const float a = hx * fx + hy * fy;
    fieldHash2(ix + 1.0f, iy, hx, hy);
    const float b = hx * (fx - 1.0f) + hy * fy;
    fieldHash2(ix, iy + 1.0f, hx, hy);
    const float c = hx * fx + hy * (fy - 1.0f);
    fieldHash2(ix + 1.0f, iy + 1.0f, hx, hy);
    const float d = hx * (fx - 1.0f) + hy * (fy - 1.0f);
    const float m0 = a + (b - a) * ux;
    const float m1 = c + (d - c) * ux;
    return m0 + (m1 - m0) * uy;
}

// Aurora palette, cycled by fp in 0..6.
ImU32 fieldStopColor(float fp, float k)
{
    static const float kStops[7][3] = {
        {124, 96, 214}, {176, 104, 196}, {214, 120, 160}, {236, 172, 146},
        {120, 140, 214}, {86, 96, 184}, {124, 96, 214}};
    const int n = std::clamp((int)fp, 0, 5);
    const float f = std::clamp(fp - (float)n, 0.0f, 1.0f);
    float rgb[3];
    for (int i = 0; i < 3; ++i) {
        const float base = (kStops[n][i] + (kStops[n + 1][i] - kStops[n][i]) * f) / 255.0f;
        rgb[i] = std::min(1.0f, base * k);
    }
    return IM_COL32((int)(rgb[0] * 255.0f), (int)(rgb[1] * 255.0f),
                    (int)(rgb[2] * 255.0f), 255);
}

// Displaced field mesh drawn as flat facets on the Surface. AuroraMosaic adds
// the bright wireframe pass; Aurora subdivides for a smoother field.
void drawAuroraField(draw::Surface& surface, draw::Vec2 topLeft,
                     draw::Vec2 size, double timeSeconds, bool mosaicFacets)
{
    const float w = size.x, h = size.y;
    const float t = (float)timeSeconds;
    float cell = std::max(34.0f, std::min(w, h) / 8.0f);
    if (!mosaicFacets)
        cell *= 0.5f; // finer facets stand in for GL per-vertex smoothness
    const float amp = cell * (mosaicFacets ? 0.34f : 0.68f);
    const float pulse = cell * (mosaicFacets ? 0.4f : 0.8f);
    const float x0 = -cell, y0 = -cell;
    const int cols = (int)std::ceil((w + cell * 2.0f) / cell) + 1;
    const int rows = (int)std::ceil((h + cell * 2.0f) / cell) + 1;
    if (cols < 2 || rows < 2 || cols * rows > 8192)
        return;
    const float sx = (w + cell * 2.0f) / (float)(cols - 1);
    const float sy = (h + cell * 2.0f) / (float)(rows - 1);
    const float originX = w * 0.5f, originY = h * 0.5f;
    const float waveBright = mosaicFacets ? 0.22f : 0.07f;

    auto waveAt = [&](float bx, float by) {
        const float dx = bx - originX, dy = by - originY;
        const float dist = std::sqrt(dx * dx + dy * dy) + 1e-3f;
        return std::sin(dist * 0.012f - t) * std::exp(-dist * 0.0016f);
    };
    auto displace = [&](float bx, float by, float& ox, float& oy) {
        const float dx = bx - originX, dy = by - originY;
        const float dist = std::sqrt(dx * dx + dy * dy) + 1e-3f;
        const float wv = std::sin(dist * 0.012f - t) * std::exp(-dist * 0.0016f);
        const float nx = fieldNoise(bx * 0.002f, by * 0.002f + t * 0.05f);
        const float ny = fieldNoise(bx * 0.002f + 11.3f, by * 0.002f + t * 0.05f);
        ox = bx + nx * amp + (dx / dist) * wv * pulse;
        oy = by + ny * amp + (dy / dist) * wv * pulse;
    };
    auto facetColor = [&](float sx0, float sy0) {
        float px, py;
        displace(sx0, sy0, px, py);
        float fp = (px + py) / (w + h) * 0.5f + t * 0.012f;
        fp = (fp - std::floor(fp)) * 6.0f;
        const float wv = waveAt(sx0, sy0);
        const float k = std::clamp(1.0f + wv * waveBright, 0.55f, 1.3f);
        return fieldStopColor(fp, k);
    };

    std::vector<draw::Vec2> pts((size_t)cols * (size_t)rows);
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < cols; ++i) {
            float px, py;
            displace(x0 + (float)i * sx, y0 + (float)j * sy, px, py);
            pts[(size_t)j * cols + i] = {topLeft.x + px, topLeft.y + py};
        }

    for (int j = 0; j + 1 < rows; ++j) {
        for (int i = 0; i + 1 < cols; ++i) {
            const draw::Vec2 a = pts[(size_t)j * cols + i];
            const draw::Vec2 b = pts[(size_t)j * cols + i + 1];
            const draw::Vec2 c = pts[(size_t)(j + 1) * cols + i];
            const draw::Vec2 e = pts[(size_t)(j + 1) * cols + i + 1];
            const float bx = x0 + (float)i * sx, by = y0 + (float)j * sy;
            // seed at each triangle's centroid (mosaicBase semantics)
            surface.fillTriangle(a, b, c,
                                 facetColor(bx + sx / 3.0f, by + sy / 3.0f));
            surface.fillTriangle(b, e, c,
                                 facetColor(bx + sx * 2.0f / 3.0f,
                                            by + sy * 2.0f / 3.0f));
        }
    }

    if (mosaicFacets) { // AuroraMosaic wireframe pass (u_linecol * 3.2 factor)
        const ImU32 wire = IM_COL32(184, 204, 245, 0x83);
        for (int j = 0; j + 1 < rows; ++j) {
            for (int i = 0; i + 1 < cols; ++i) {
                const draw::Vec2 a = pts[(size_t)j * cols + i];
                const draw::Vec2 b = pts[(size_t)j * cols + i + 1];
                const draw::Vec2 c = pts[(size_t)(j + 1) * cols + i];
                const draw::Vec2 e = pts[(size_t)(j + 1) * cols + i + 1];
                surface.line(a, b, wire, 1.0f);
                surface.line(a, c, wire, 1.0f);
                surface.line(b, c, wire, 1.0f);
                surface.line(b, e, wire, 1.0f);
                surface.line(e, c, wire, 1.0f);
            }
        }
    }
}

} // namespace

const char* graphSkinName(GraphSkin skin)
{
    switch (skin) {
    case GraphSkin::TechSquare: return "Tech Square";
    case GraphSkin::ClassicRounded: return "Classic Rounded";
    case GraphSkin::Blueprint: return "Blueprint";
    case GraphSkin::Console: return "Console";
    case GraphSkin::Studio: return "Studio";
    case GraphSkin::Neo: return "Neo";
    case GraphSkin::Rainbow: return "Rainbow";
    case GraphSkin::Ember: return "Ember";
    case GraphSkin::Redline: return "Redline";
    case GraphSkin::Glacier: return "Glacier";
    case GraphSkin::Acid: return "Acid";
    case GraphSkin::Vapor: return "Vapor";
    case GraphSkin::Gold: return "Gold";
    case GraphSkin::Ghost: return "Ghost";
    case GraphSkin::Ultraviolet: return "Ultraviolet";
    }
    return "Tech Square";
}

GraphSurfaceStyle graphSkinStyle(GraphSkin skin)
{
    // murk Bob GraphEditorPanel::specFor, byte-exact. The parts murk keeps
    // constant across skins (selection amber, pin colours, wire weight) are
    // set once here.
    GraphSurfaceStyle s;
    s.selectedBorder = IM_COL32(0xff, 0xc2, 0x4a, 0xff);
    s.squarePins = true;
    s.pinAudio = IM_COL32(0x7f, 0xd1, 0xae, 0xff);
    s.pinMidi = IM_COL32(0xd9, 0x8a, 0xd9, 0xff);
    s.pinControl = IM_COL32(0xe0, 0xb0, 0x20, 0xff);
    s.wireThickness = 2.0f;
    s.wireDroop = true;
    switch (skin) {
    case GraphSkin::TechSquare:
        s.node = IM_COL32(0x1b, 0x1f, 0x25, 0xff);
        s.header = IM_COL32(0x25, 0x2b, 0x33, 0xff);
        s.border = IM_COL32(0x45, 0x4b, 0x55, 0xff);
        s.text = IM_COL32(0xdf, 0xe6, 0xe2, 0xff);
        s.accent = IM_COL32(0xff, 0x4d, 0x48, 0xff);
        s.corner = 0.0f;
        break;
    case GraphSkin::ClassicRounded:
        s.node = IM_COL32(0x24, 0x26, 0x2d, 0xff);
        s.header = IM_COL32(0x30, 0x34, 0x3d, 0xff);
        s.border = IM_COL32(0x55, 0x5c, 0x68, 0xff);
        s.text = IM_COL32(0xf1, 0xf2, 0xee, 0xff);
        s.accent = IM_COL32(0xff, 0x59, 0x4f, 0xff);
        s.corner = 6.0f;
        break;
    case GraphSkin::Blueprint:
        s.node = IM_COL32(0x10, 0x18, 0x20, 0xff);
        s.header = IM_COL32(0x16, 0x29, 0x38, 0xff);
        s.border = IM_COL32(0x2d, 0x6a, 0x82, 0xff);
        s.text = IM_COL32(0xd8, 0xf5, 0xff, 0xff);
        s.accent = IM_COL32(0x80, 0xd8, 0xff, 0xff);
        s.corner = 0.0f;
        break;
    case GraphSkin::Console:
        s.node = IM_COL32(0x10, 0x13, 0x0f, 0xff);
        s.header = IM_COL32(0x18, 0x20, 0x16, 0xff);
        s.border = IM_COL32(0x39, 0x51, 0x34, 0xff);
        s.text = IM_COL32(0xdc, 0xf5, 0xd2, 0xff);
        s.accent = IM_COL32(0x9b, 0xe5, 0x64, 0xff);
        s.corner = 0.0f;
        break;
    case GraphSkin::Studio:
        s.node = IM_COL32(0x20, 0x20, 0x20, 0xff);
        s.header = IM_COL32(0x2b, 0x29, 0x26, 0xff);
        s.border = IM_COL32(0x5a, 0x55, 0x4d, 0xff);
        s.text = IM_COL32(0xf4, 0xed, 0xe0, 0xff);
        s.accent = IM_COL32(0xd9, 0x5f, 0x43, 0xff);
        s.corner = 3.0f;
        break;
    case GraphSkin::Neo:
        // schema-ui-web "Turbo" (bob2/SKIN.md): near-black slab, conic
        // pink→purple→blue rim that spins while selected, purple→blue wires
        // at 75%, pins in the Turbo triple, no header stripe (owner picks).
        s.node = IM_COL32(0x11, 0x11, 0x11, 0xff);
        s.pinAudio = IM_COL32(0xe9, 0x2a, 0x67, 0xff);
        s.pinMidi = IM_COL32(0xa8, 0x53, 0xba, 0xff);
        s.pinControl = IM_COL32(0x2a, 0x8a, 0xf6, 0xff);
        s.header = IM_COL32(0x16, 0x16, 0x16, 0xff);
        s.border = IM_COL32(0xa8, 0x53, 0xba, 0xff); // fallback if rim cleared
        s.text = IM_COL32(0xf3, 0xf4, 0xf6, 0xff);
        s.accent = IM_COL32(0xa8, 0x53, 0xba, 0xff);
        s.selectedBorder = IM_COL32(0x2a, 0x8a, 0xf6, 0xff);
        s.corner = 10.0f;
        s.headerStripe = false;
        s.rimA = IM_COL32(0xe9, 0x2a, 0x67, 0xff);
        s.rimB = IM_COL32(0xa8, 0x53, 0xba, 0xff);
        s.rimC = IM_COL32(0x2a, 0x8a, 0xf6, 0xff);
        s.rimSpinSeconds = 4.0f; // Turbo: 4s linear infinite
        s.wireGradientStart = IM_COL32(0xae, 0x53, 0xba, 0xbf); // 75% opacity
        s.wireGradientEnd = IM_COL32(0x2a, 0x8a, 0xf6, 0xbf);
        // no glow: the filled-stack halo reads as fake blur (owner) -- the
        // capability stays for when a real blur pass exists
        break;
    case GraphSkin::Rainbow:
        // full six-stop hue wheel; the THEME carries through -- green/violet/
        // yellow pins, red→violet wires with extra weight, violet-dark slab
        s.node = IM_COL32(0x15, 0x13, 0x1c, 0xff);
        s.header = IM_COL32(0x1c, 0x1a, 0x24, 0xff);
        s.pinAudio = IM_COL32(0x4d, 0xff, 0x88, 0xff);
        s.pinMidi = IM_COL32(0xb8, 0x4d, 0xff, 0xff);
        s.pinControl = IM_COL32(0xff, 0xe9, 0x4d, 0xff);
        s.wireThickness = 2.4f;
        s.border = IM_COL32(0x8a, 0x5c, 0xf6, 0xff); // fallback if rim cleared
        s.text = IM_COL32(0xf2, 0xf3, 0xf7, 0xff);
        s.accent = IM_COL32(0x4d, 0xb8, 0xff, 0xff);
        s.selectedBorder = IM_COL32(0xff, 0xe9, 0x4d, 0xff);
        s.corner = 8.0f;
        s.headerStripe = false;
        s.rimA = IM_COL32(0xff, 0x4d, 0x4d, 0xff); // red
        s.rimB = IM_COL32(0xff, 0xb8, 0x4d, 0xff); // orange
        s.rimC = IM_COL32(0xff, 0xe9, 0x4d, 0xff); // yellow
        s.rimD = IM_COL32(0x4d, 0xff, 0x88, 0xff); // green
        s.rimE = IM_COL32(0x4d, 0xb8, 0xff, 0xff); // blue
        s.rimF = IM_COL32(0xb8, 0x4d, 0xff, 0xff); // violet
        s.rimSpinSeconds = 6.0f;
        s.wireGradientStart = IM_COL32(0xff, 0x4d, 0x4d, 0xd9); // red ->
        s.wireGradientEnd = IM_COL32(0xb8, 0x4d, 0xff, 0xd9);   // violet
        break;
    case GraphSkin::Ember:
        // fire: charcoal slab, deep-red -> orange -> gold sweep, warm text,
        // ember pin triple, thick glowing wires
        s.node = IM_COL32(0x16, 0x10, 0x0c, 0xff);
        s.pinAudio = IM_COL32(0xff, 0x6a, 0x00, 0xff);
        s.pinMidi = IM_COL32(0xff, 0xd1, 0x66, 0xff);
        s.pinControl = IM_COL32(0xb3, 0x40, 0x2a, 0xff);
        s.wireThickness = 2.6f;
        s.header = IM_COL32(0x1d, 0x15, 0x10, 0xff);
        s.border = IM_COL32(0xff, 0x6a, 0x00, 0xff); // fallback if rim cleared
        s.text = IM_COL32(0xf6, 0xe8, 0xdc, 0xff);
        s.accent = IM_COL32(0xff, 0x6a, 0x00, 0xff);
        s.selectedBorder = IM_COL32(0xff, 0xd1, 0x66, 0xff);
        s.corner = 4.0f;
        s.headerStripe = false;
        s.rimA = IM_COL32(0x8a, 0x1f, 0x0f, 0xff); // deep ember
        s.rimB = IM_COL32(0xff, 0x6a, 0x00, 0xff); // orange
        s.rimC = IM_COL32(0xff, 0xd1, 0x66, 0xff); // gold
        s.rimSpinSeconds = 3.5f;
        s.wireGradientStart = IM_COL32(0xff, 0x6a, 0x00, 0xd9);
        s.wireGradientEnd = IM_COL32(0xff, 0xd1, 0x66, 0xd9);
        break;
    case GraphSkin::Redline:
        // black and red: near-black slab, hard corners, a single red
        // highlight sweeping the outline, red pin family, razor-thin wires
        s.node = IM_COL32(0x0d, 0x0b, 0x0b, 0xff);
        s.pinAudio = IM_COL32(0xff, 0x3b, 0x30, 0xff);
        s.pinMidi = IM_COL32(0x8a, 0x1f, 0x1a, 0xff);
        s.pinControl = IM_COL32(0xf2, 0xe9, 0xe9, 0xff);
        s.wireThickness = 1.6f;
        s.header = IM_COL32(0x14, 0x10, 0x10, 0xff);
        s.border = IM_COL32(0xff, 0x3b, 0x30, 0xff); // fallback if rim cleared
        s.text = IM_COL32(0xf2, 0xe9, 0xe9, 0xff);
        s.accent = IM_COL32(0xff, 0x3b, 0x30, 0xff);
        s.selectedBorder = IM_COL32(0xff, 0x3b, 0x30, 0xff);
        s.corner = 0.0f;
        s.headerStripe = false;
        s.rimA = IM_COL32(0x3d, 0x0c, 0x0c, 0xff); // near-black red
        s.rimB = IM_COL32(0xff, 0x3b, 0x30, 0xff); // the highlight
        s.rimC = IM_COL32(0x8a, 0x1f, 0x1a, 0xff); // fade back down
        s.rimSpinSeconds = 4.0f;
        s.wireGradientStart = IM_COL32(0x8a, 0x1f, 0x1a, 0xd9);
        s.wireGradientEnd = IM_COL32(0xff, 0x3b, 0x30, 0xd9);
        break;
    case GraphSkin::Glacier:
        // ice: deep blue-black slab, white→blue rim like light through ice,
        // cold pin blues, hairline frozen wires
        s.node = IM_COL32(0x0c, 0x12, 0x18, 0xff);
        s.header = IM_COL32(0x10, 0x1a, 0x24, 0xff);
        s.border = IM_COL32(0x7f, 0xd4, 0xff, 0xff);
        s.text = IM_COL32(0xe8, 0xf4, 0xfb, 0xff);
        s.accent = IM_COL32(0x7f, 0xd4, 0xff, 0xff);
        s.selectedBorder = IM_COL32(0xd9, 0xf2, 0xff, 0xff);
        s.corner = 6.0f;
        s.headerStripe = false;
        s.pinAudio = IM_COL32(0x7f, 0xd4, 0xff, 0xff);
        s.pinMidi = IM_COL32(0xb0, 0xe6, 0xff, 0xff);
        s.pinControl = IM_COL32(0x4a, 0x9f, 0xd9, 0xff);
        s.wireThickness = 1.5f;
        s.rimA = IM_COL32(0xd9, 0xf2, 0xff, 0xff);
        s.rimB = IM_COL32(0x7f, 0xd4, 0xff, 0xff);
        s.rimC = IM_COL32(0x3a, 0x86, 0xc9, 0xff);
        s.rimSpinSeconds = 5.0f;
        s.wireGradientStart = IM_COL32(0xd9, 0xf2, 0xff, 0xd9);
        s.wireGradientEnd = IM_COL32(0x7f, 0xd4, 0xff, 0xd9);
        break;
    case GraphSkin::Acid:
        // toxic: black-green slab, acid rim, everything drips green, fat
        // corrosive wires
        s.node = IM_COL32(0x0a, 0x12, 0x0a, 0xff);
        s.header = IM_COL32(0x0f, 0x1a, 0x0e, 0xff);
        s.border = IM_COL32(0x8a, 0xff, 0x3d, 0xff);
        s.text = IM_COL32(0xe4, 0xf7, 0xd9, 0xff);
        s.accent = IM_COL32(0x8a, 0xff, 0x3d, 0xff);
        s.selectedBorder = IM_COL32(0xc8, 0xff, 0x5e, 0xff);
        s.corner = 0.0f;
        s.headerStripe = false;
        s.pinAudio = IM_COL32(0x8a, 0xff, 0x3d, 0xff);
        s.pinMidi = IM_COL32(0xc8, 0xff, 0x5e, 0xff);
        s.pinControl = IM_COL32(0x3d, 0xa8, 0x5c, 0xff);
        s.wireThickness = 2.6f;
        s.rimA = IM_COL32(0x2e, 0x7a, 0x1f, 0xff);
        s.rimB = IM_COL32(0x8a, 0xff, 0x3d, 0xff);
        s.rimC = IM_COL32(0xc8, 0xff, 0x5e, 0xff);
        s.rimSpinSeconds = 3.0f;
        s.wireGradientStart = IM_COL32(0x8a, 0xff, 0x3d, 0xd9);
        s.wireGradientEnd = IM_COL32(0xc8, 0xff, 0x5e, 0xd9);
        break;
    case GraphSkin::Vapor:
        // vaporwave: dusk-purple slab, pink→violet→cyan pastels, ROUND pins,
        // dreamy slow sweep
        s.node = IM_COL32(0x16, 0x12, 0x1f, 0xff);
        s.header = IM_COL32(0x1d, 0x17, 0x29, 0xff);
        s.border = IM_COL32(0xff, 0x71, 0xce, 0xff);
        s.text = IM_COL32(0xf4, 0xee, 0xfb, 0xff);
        s.accent = IM_COL32(0xff, 0x71, 0xce, 0xff);
        s.selectedBorder = IM_COL32(0x01, 0xcd, 0xfe, 0xff);
        s.corner = 10.0f;
        s.headerStripe = false;
        s.squarePins = false; // soft look: round pins
        s.pinAudio = IM_COL32(0xff, 0x71, 0xce, 0xff);
        s.pinMidi = IM_COL32(0xb9, 0x67, 0xff, 0xff);
        s.pinControl = IM_COL32(0x01, 0xcd, 0xfe, 0xff);
        s.rimA = IM_COL32(0xff, 0x71, 0xce, 0xff);
        s.rimB = IM_COL32(0xb9, 0x67, 0xff, 0xff);
        s.rimC = IM_COL32(0x01, 0xcd, 0xfe, 0xff);
        s.rimSpinSeconds = 7.0f;
        s.wireGradientStart = IM_COL32(0xff, 0x71, 0xce, 0xcc);
        s.wireGradientEnd = IM_COL32(0x01, 0xcd, 0xfe, 0xcc);
        break;
    case GraphSkin::Gold:
        // brass on black: warm near-black slab, champagne sweep, brass pins
        s.node = IM_COL32(0x13, 0x10, 0x09, 0xff);
        s.header = IM_COL32(0x1a, 0x15, 0x0c, 0xff);
        s.border = IM_COL32(0xe6, 0xb8, 0x4a, 0xff);
        s.text = IM_COL32(0xf7, 0xf0, 0xdc, 0xff);
        s.accent = IM_COL32(0xe6, 0xb8, 0x4a, 0xff);
        s.selectedBorder = IM_COL32(0xff, 0xe0, 0x8a, 0xff);
        s.corner = 2.0f;
        s.headerStripe = false;
        s.pinAudio = IM_COL32(0xe6, 0xb8, 0x4a, 0xff);
        s.pinMidi = IM_COL32(0xff, 0xe0, 0x8a, 0xff);
        s.pinControl = IM_COL32(0xa8, 0x87, 0x32, 0xff);
        s.rimA = IM_COL32(0x8a, 0x6a, 0x1f, 0xff);
        s.rimB = IM_COL32(0xe6, 0xb8, 0x4a, 0xff);
        s.rimC = IM_COL32(0xff, 0xe0, 0x8a, 0xff);
        s.rimSpinSeconds = 5.0f;
        s.wireGradientStart = IM_COL32(0xe6, 0xb8, 0x4a, 0xd9);
        s.wireGradientEnd = IM_COL32(0xff, 0xe0, 0x8a, 0xd9);
        break;
    case GraphSkin::Ghost:
        // greyscale minimal: a white highlight sweeping a grey outline,
        // grey pin ramp, hairline wires -- no colour anywhere
        s.node = IM_COL32(0x10, 0x10, 0x10, 0xff);
        s.header = IM_COL32(0x16, 0x16, 0x16, 0xff);
        s.border = IM_COL32(0xd9, 0xd9, 0xd9, 0xff);
        s.text = IM_COL32(0xf2, 0xf2, 0xf2, 0xff);
        s.accent = IM_COL32(0xd9, 0xd9, 0xd9, 0xff);
        s.selectedBorder = IM_COL32(0xff, 0xff, 0xff, 0xff);
        s.corner = 0.0f;
        s.headerStripe = false;
        s.pinAudio = IM_COL32(0xf2, 0xf2, 0xf2, 0xff);
        s.pinMidi = IM_COL32(0xa8, 0xa8, 0xa8, 0xff);
        s.pinControl = IM_COL32(0x6b, 0x6b, 0x6b, 0xff);
        s.wireThickness = 1.5f;
        s.rimA = IM_COL32(0x2e, 0x2e, 0x2e, 0xff);
        s.rimB = IM_COL32(0xf2, 0xf2, 0xf2, 0xff);
        s.rimC = IM_COL32(0x6b, 0x6b, 0x6b, 0xff);
        s.rimSpinSeconds = 4.0f;
        s.wireGradientStart = IM_COL32(0xf2, 0xf2, 0xf2, 0xb3);
        s.wireGradientEnd = IM_COL32(0x8a, 0x8a, 0x8a, 0xb3);
        break;
    case GraphSkin::Ultraviolet:
        // UV: deep violet slab, purple→magenta sweep, purple pin family
        s.node = IM_COL32(0x12, 0x0a, 0x1c, 0xff);
        s.header = IM_COL32(0x18, 0x0f, 0x26, 0xff);
        s.border = IM_COL32(0x9b, 0x4d, 0xff, 0xff);
        s.text = IM_COL32(0xef, 0xe6, 0xfa, 0xff);
        s.accent = IM_COL32(0x9b, 0x4d, 0xff, 0xff);
        s.selectedBorder = IM_COL32(0xd4, 0xa6, 0xff, 0xff);
        s.corner = 4.0f;
        s.headerStripe = false;
        s.pinAudio = IM_COL32(0x9b, 0x4d, 0xff, 0xff);
        s.pinMidi = IM_COL32(0xff, 0x4d, 0xd2, 0xff);
        s.pinControl = IM_COL32(0x6a, 0x3a, 0xb8, 0xff);
        s.wireThickness = 2.2f;
        s.rimA = IM_COL32(0x4a, 0x1a, 0x8a, 0xff);
        s.rimB = IM_COL32(0x9b, 0x4d, 0xff, 0xff);
        s.rimC = IM_COL32(0xff, 0x4d, 0xd2, 0xff);
        s.rimSpinSeconds = 4.5f;
        s.wireGradientStart = IM_COL32(0x9b, 0x4d, 0xff, 0xd9);
        s.wireGradientEnd = IM_COL32(0xff, 0x4d, 0xd2, 0xd9);
        break;
    }
    return s;
}

ImU32 graphBackdropFill(GraphSurfaceStyle::Backdrop mode)
{
    switch (mode) {
    case GraphSurfaceStyle::Backdrop::Mosaic:
    case GraphSurfaceStyle::Backdrop::Aurora:
    case GraphSurfaceStyle::Backdrop::AuroraMosaic:
        return IM_COL32(0x09, 0x0b, 0x11, 0xff); // murk mosaic/aurora base
    case GraphSurfaceStyle::Backdrop::GreenGrid:
        return IM_COL32(0x04, 0x13, 0x0b, 0xff);
    case GraphSurfaceStyle::Backdrop::Flat:
    case GraphSurfaceStyle::Backdrop::Grid:
        return IM_COL32(0x0d, 0x0f, 0x14, 0xff);
    }
    return IM_COL32(0x0d, 0x0f, 0x14, 0xff);
}

const char* graphBackdropName(GraphSurfaceStyle::Backdrop mode)
{
    // murk's Bg menu labels; Grid is SND's own mode, named plainly.
    switch (mode) {
    case GraphSurfaceStyle::Backdrop::Aurora: return "Aurora";
    case GraphSurfaceStyle::Backdrop::Mosaic: return "Mosaic";
    case GraphSurfaceStyle::Backdrop::AuroraMosaic: return "Aurora Mosaic";
    case GraphSurfaceStyle::Backdrop::GreenGrid: return "Green Grid (breathing)";
    case GraphSurfaceStyle::Backdrop::Grid: return "Grid";
    case GraphSurfaceStyle::Backdrop::Flat: return "Off";
    }
    return "Off";
}

void drawGraphGrid(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                   draw::Vec2 pan, float zoom, const Palette& pal,
                   const ControlState& state,
                   const GraphSurfaceStyle& style, double timeSeconds)
{
    const draw::Vec2 mx{topLeft.x + size.x, topLeft.y + size.y};
    const ImU32 defaultFill = mix(IM_COL32(0, 0, 0, 255), pal.frame, 0.72f);
    const ImU32 flatFill = graphColor(style.backdropFill, defaultFill);
    const bool aurora = style.backdrop == GraphSurfaceStyle::Backdrop::Aurora ||
                        style.backdrop == GraphSurfaceStyle::Backdrop::AuroraMosaic;
    surface.fillRect(topLeft, mx,
                     style.backdrop == GraphSurfaceStyle::Backdrop::GreenGrid
                         ? graphColor(style.backdropFill, IM_COL32(4, 19, 11, 255))
                     : style.backdrop == GraphSurfaceStyle::Backdrop::Mosaic
                         ? graphColor(style.backdropFill, IM_COL32(9, 11, 17, 255))
                     : aurora
                         ? graphColor(style.backdropFill, IM_COL32(10, 14, 34, 255))
                         : flatFill,
                     style.corner);
    if (size.x <= 0.0f || size.y <= 0.0f)
        return;

    if (aurora) {
        // Animated field backdrop, then a legibility veil over the graph.
        surface.pushClip(topLeft, mx, true);
        drawAuroraField(surface, topLeft, size, timeSeconds,
                        style.backdrop ==
                            GraphSurfaceStyle::Backdrop::AuroraMosaic);
        surface.popClip();
        surface.fillRect(topLeft, mx, IM_COL32(0, 0, 0, 102), style.corner);
        return;
    }

    if (style.backdrop == GraphSurfaceStyle::Backdrop::Mosaic) {
        // Faceted graph backdrop: 58px cells,
        // hue 0.58 + 0.18*n + 0.04*t with n = sin(cx*7 + t*0.5)*cos(cy*6 - t*0.4),
        // sat 0.55, bri 0.30 + 0.22*(0.5 + 0.5*sin(cx*5 + cy*4 + t*0.8)),
        // facets at alpha 0.5 with a bri*0.7 diagonal split, 0.40 black veil.
        const float cell = 58.0f;
        const float t = (float)timeSeconds;
        surface.pushClip(topLeft, mx, true);
        for (float y = topLeft.y; y < mx.y; y += cell) {
            for (float x = topLeft.x; x < mx.x; x += cell) {
                const float cx = (x + cell * 0.5f - topLeft.x) / size.x;
                const float cy = (y + cell * 0.5f - topLeft.y) / size.y;
                const float n = std::sin(cx * 7.0f + t * 0.5f) *
                                std::cos(cy * 6.0f - t * 0.4f);
                const float hue = 0.58f + 0.18f * n + 0.04f * t;
                const float bri = 0.30f + 0.22f * (0.5f + 0.5f *
                                  std::sin(cx * 5.0f + cy * 4.0f + t * 0.8f));
                const float fx0 = x + 0.5f, fy0 = y + 0.5f;
                const float fx1 = x + cell - 0.5f, fy1 = y + cell - 0.5f;
                surface.fillRect({fx0, fy0}, {fx1, fy1},
                                 withAlpha(hsvToRgbColor(hue, 0.55f, bri), 0x80));
                surface.fillTriangle({fx0, fy0}, {fx1, fy0}, {fx0, fy1},
                                     withAlpha(hsvToRgbColor(hue, 0.55f, bri * 0.7f),
                                               0x80));
            }
        }
        surface.popClip();
        surface.fillRect(topLeft, mx, IM_COL32(0, 0, 0, 102), style.corner);
    }

    if (style.backdrop == GraphSurfaceStyle::Backdrop::Flat ||
        style.backdrop == GraphSurfaceStyle::Backdrop::Mosaic) {
        return;
    }

    if (style.backdrop == GraphSurfaceStyle::Backdrop::GreenGrid) {
        // Fixed 38px screen-space grid, green @ 0.09,
        // no major lines, unaffected by pan/zoom.
        const ImU32 green = withAlpha(IM_COL32(57, 224, 138, 255), 0x17);
        surface.pushClip(topLeft, mx, true);
        for (float x = topLeft.x; x < mx.x; x += 38.0f)
            surface.line({x, topLeft.y}, {x, mx.y}, green, 1.0f);
        for (float y = topLeft.y; y < mx.y; y += 38.0f)
            surface.line({topLeft.x, y}, {mx.x, y}, green, 1.0f);
        surface.popClip();
        return;
    }

    float minor = 24.0f * std::max(0.05f, zoom);
    while (minor < 12.0f)
        minor *= 2.0f;
    while (minor > 48.0f)
        minor *= 0.5f;
    const float major = minor * 4.0f;
    const ImU32 gridBase = pal.frameBright;
    const ImU32 minorCol = withAlpha(gridBase, state.disabled ? 0x24 : 0x38);
    const ImU32 majorCol = withAlpha(gridBase, state.disabled ? 0x38 : 0x62);

    const float ox = std::fmod(pan.x * zoom, minor);
    const float oy = std::fmod(pan.y * zoom, minor);
    for (float x = topLeft.x + ox; x < mx.x; x += minor)
        if (x >= topLeft.x)
            surface.line({x, topLeft.y}, {x, mx.y}, minorCol, 1.0f);
    for (float y = topLeft.y + oy; y < mx.y; y += minor)
        if (y >= topLeft.y)
            surface.line({topLeft.x, y}, {mx.x, y}, minorCol, 1.0f);

    const float mox = std::fmod(pan.x * zoom, major);
    const float moy = std::fmod(pan.y * zoom, major);
    for (float x = topLeft.x + mox; x < mx.x; x += major)
        if (x >= topLeft.x)
            surface.line({x, topLeft.y}, {x, mx.y}, majorCol, 1.0f);
    for (float y = topLeft.y + moy; y < mx.y; y += major)
        if (y >= topLeft.y)
            surface.line({topLeft.x, y}, {mx.x, y}, majorCol, 1.0f);

}

void drawGraphGrid(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                   const ImVec2& pan, float zoom, const Palette& pal,
                   const ControlState& state,
                   const GraphSurfaceStyle& style)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawGraphGrid(surface, draw::toDrawVec2(topLeft), draw::toDrawVec2(size),
                  draw::toDrawVec2(pan), zoom, pal, state, style,
                  ImGui::GetTime());
}

namespace {

// N-stop looped angular palette: u 0..1 sweeps stop0→…→stopN-1→stop0.
ImU32 conicStopN(const ImU32* stops, int count, float u)
{
    if (count <= 0)
        return 0;
    if (count == 1)
        return stops[0];
    u = u - std::floor(u);
    const float seg = u * (float)count;
    const int i = std::min((int)seg, count - 1);
    return mix(stops[i], stops[(i + 1) % count], seg - (float)i);
}

// The style's rim stops: rimA..rimF, ended by the first zero past rimC.
int rimStops(const GraphSurfaceStyle& style, ImU32 out[6])
{
    const ImU32 all[6] = {style.rimA, style.rimB, style.rimC,
                          style.rimD, style.rimE, style.rimF};
    int n = 0;
    for (int i = 0; i < 6; ++i) {
        if (i >= 3 && (all[i] & 0xFF000000u) == 0)
            break;
        if ((all[i] & 0xFF000000u) != 0)
            out[n++] = all[i];
    }
    return n;
}

// Stroke a rounded-rect outline as a conic gradient through `stops` about
// the rect centre; phase rotates the sweep (1 = one revolution). ONE closed
// gradient strip: per-segment lines beaded at every joint and read as a
// soft tube (owner).
void strokeConicRim(draw::Surface& surface, draw::Vec2 a, draw::Vec2 b,
                    float corner, const ImU32* stops, int stopCount,
                    float phase, float thickness)
{
    const float w = b.x - a.x;
    const float h = b.y - a.y;
    if (w <= 0.0f || h <= 0.0f || stopCount <= 0)
        return;
    const float r = std::clamp(corner, 0.0f, std::min(w, h) * 0.5f);
    constexpr float kPi = 3.14159265f;
    constexpr int kEdge = 8; // segments per side (gradient resolution)
    constexpr int kArc = 4;  // segments per corner
    draw::Vec2 pts[4 * (kEdge + kArc)];
    int n = 0;
    const auto push = [&](draw::Vec2 p) {
        if (n > 0) { // corner arcs collapse to duplicates at r = 0
            const float dx = p.x - pts[n - 1].x;
            const float dy = p.y - pts[n - 1].y;
            if (dx * dx + dy * dy < 0.01f)
                return;
        }
        pts[n++] = p;
    };
    const auto edge = [&](draw::Vec2 p0, draw::Vec2 p1) {
        for (int i = 0; i < kEdge; ++i) {
            const float t = (float)i / kEdge;
            push({p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t});
        }
    };
    const auto arc = [&](draw::Vec2 centre, float a0, float a1) {
        for (int i = 0; i < kArc; ++i) {
            const float t = a0 + (a1 - a0) * (float)i / kArc;
            push({centre.x + std::cos(t) * r, centre.y + std::sin(t) * r});
        }
    };
    edge({a.x + r, a.y}, {b.x - r, a.y}); // clockwise from top-left
    arc({b.x - r, a.y + r}, -kPi * 0.5f, 0.0f);
    edge({b.x, a.y + r}, {b.x, b.y - r});
    arc({b.x - r, b.y - r}, 0.0f, kPi * 0.5f);
    edge({b.x - r, b.y}, {a.x + r, b.y});
    arc({a.x + r, b.y - r}, kPi * 0.5f, kPi);
    edge({a.x, b.y - r}, {a.x, a.y + r});
    arc({a.x + r, a.y + r}, kPi, kPi * 1.5f);
    if (n < 3)
        return;
    const draw::Vec2 c{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    ImU32 cols[4 * (kEdge + kArc)];
    for (int i = 0; i < n; ++i) {
        const float u =
            std::atan2(pts[i].y - c.y, pts[i].x - c.x) / (2.0f * kPi) - phase;
        cols[i] = conicStopN(stops, stopCount, u);
    }
    surface.polylineGradient(pts, n, cols, thickness, /*closed*/ true);
}

// Layered soft halo behind a node body, one tint per side (A left, B right);
// each colour's own alpha is the peak. FILLED rects largest-first so the
// overlap accumulates smoothly toward the body — strokes banded into
// concentric rings (owner: "weird two border thing").
void drawNodeGlow(draw::Surface& surface, draw::Vec2 a, draw::Vec2 b,
                  float corner, ImU32 glowA, ImU32 glowB)
{
    constexpr int kLayers = 5;
    const auto halo = [&](ImU32 colour, float dx) {
        if ((colour & 0xFF000000u) == 0)
            return;
        const auto alpha = (uint32_t)((float)(colour >> 24) /
                                      ((float)kLayers * 1.8f));
        if (alpha == 0)
            return;
        for (int i = kLayers; i >= 1; --i) {
            const float e = (float)i * 2.2f;
            surface.fillRect({a.x - e + dx, a.y - e}, {b.x + e + dx, b.y + e},
                             withAlpha(colour, alpha), corner + e);
        }
    };
    halo(glowA, -2.5f);
    halo(glowB, 2.5f);
}

} // namespace

void drawCable(draw::Surface& surface, draw::Vec2 from, draw::Vec2 to,
               const Palette& pal, const ControlState& state,
               ImU32 color, float thickness, const GraphSurfaceStyle& style,
               float zoom)
{
    // murk drawWires: one 2px drooping stroke, nothing else -- no endpoint
    // dots, halos, or hover states. Colour comes from the caller (audio /
    // midi / control at murk's alphas); default = audio 0x7fd1ae @ 0.85.
    if (color == 0)
        color = withAlpha(IM_COL32(0x7f, 0xd1, 0xae, 255), 0xD9);
    if (state.disabled)
        color = mix(color, pal.frameBright, 0.70f);

    draw::Vec2 c1;
    draw::Vec2 c2;
    graphCableControls(from, to, style, c1, c2, zoom);
    const float lineW =
        thickness > 0.0f ? thickness
                         : style.wireThickness > 0.0f ? style.wireThickness : 2.0f;
    if ((style.wireGradientEnd & 0xFF000000u) != 0) {
        // Gradient wire (Neo-class): sample the same cubic and lerp colour
        // along it, start→end.
        ImU32 g0 = color; // already disabled-dimmed above
        if ((style.wireGradientStart & 0xFF000000u) != 0)
            g0 = state.disabled
                     ? mix(style.wireGradientStart, pal.frameBright, 0.70f)
                     : style.wireGradientStart;
        ImU32 g1 = state.disabled
                       ? mix(style.wireGradientEnd, pal.frameBright, 0.70f)
                       : style.wireGradientEnd;
        // one continuous strip: per-segment lines double-blended at every
        // joint (24 beads along a translucent wire -- owner's "shadow")
        constexpr int kSegs = 24;
        draw::Vec2 pts[kSegs + 1];
        ImU32 cols[kSegs + 1];
        for (int i = 0; i <= kSegs; ++i) {
            const float t = (float)i / kSegs;
            const float it = 1.0f - t;
            pts[i] = {it * it * it * from.x + 3 * it * it * t * c1.x +
                          3 * it * t * t * c2.x + t * t * t * to.x,
                      it * it * it * from.y + 3 * it * it * t * c1.y +
                          3 * it * t * t * c2.y + t * t * t * to.y};
            cols[i] = mix(g0, g1, t);
        }
        surface.polylineGradient(pts, kSegs + 1, cols, lineW, /*closed*/ false);
        return;
    }
    surface.bezierCubic(from, c1, c2, to, color, lineW, 24);
}

void drawCable(ImDrawList* dl, const ImVec2& from, const ImVec2& to,
               const Palette& pal, const ControlState& state,
               ImU32 color, float thickness, const GraphSurfaceStyle& style,
               float zoom)
{
    if (!dl)
        return;
    draw::ImGuiSurface surface(dl);
    drawCable(surface, draw::toDrawVec2(from), draw::toDrawVec2(to), pal, state,
              color, thickness, style, zoom);
}

void drawModuleBox(draw::Surface& surface, draw::FontRef font, float fontSizePx,
                   draw::Vec2 topLeft, draw::Vec2 size, const char* title,
                   const Palette& pal,
                   const ControlState& state, bool bypassed, bool error,
                   const GraphSurfaceStyle& style, float headerH,
                   double timeSeconds)
{
    // murk NodeBox::paint, exact (GraphEditorPanel.cpp:411-467). Fallback
    // colours are murk's Tech Square skin so the box reads murk even with an
    // empty style.
    const auto col = [](ImU32 preferred, ImU32 fallback) {
        return (preferred & 0xFF000000u) != 0 ? preferred : fallback;
    };
    const ImU32 nodeCol = col(style.node, IM_COL32(0x1b, 0x1f, 0x25, 255));
    const ImU32 headerCol = col(style.header, IM_COL32(0x25, 0x2b, 0x33, 255));
    const ImU32 borderCol = col(style.border, IM_COL32(0x45, 0x4b, 0x55, 255));
    const ImU32 textCol = col(style.text, IM_COL32(0xdf, 0xe6, 0xe2, 255));
    const ImU32 accentCol = col(style.accent, IM_COL32(0xff, 0x4d, 0x48, 255));
    const ImU32 selectedCol =
        col(style.selectedBorder, IM_COL32(0xff, 0xc2, 0x4a, 255));

    // r = local bounds reduced 1.0
    const draw::Vec2 a{topLeft.x + 1.0f, topLeft.y + 1.0f};
    const draw::Vec2 b{topLeft.x + size.x - 1.0f, topLeft.y + size.y - 1.0f};
    const float corner = std::max(0.0f, style.corner);
    // whole-pixel header height: a fractional zoomed header puts the
    // separator (and the title centring) on subpixels, which shimmers
    const float hh =
        std::round(std::min(std::max(0.0f, headerH), b.y - a.y));

    if (((style.glowA | style.glowB) & 0xFF000000u) != 0 && !bypassed)
        drawNodeGlow(surface, a, b, corner, style.glowA, style.glowB);
    surface.fillRect(a, b, nodeCol, corner);

    // header bar + 3px accent stripe + bold title (murk rounds the header
    // rect with the same radius; stripe at 0.82 alpha). Two owner-directed
    // deviations from murk: the stripe starts below the corner arc so it
    // can't jut past rounded corners, and the title sits 12px in (murk's 8
    // crowds the stripe).
    const draw::Vec2 hMax{b.x, a.y + hh};
    surface.fillRect(a, hMax, headerCol, corner);
    if (style.headerStripe) {
        const float stripeTop = std::min(a.y + corner, hMax.y);
        surface.fillRect({a.x, stripeTop}, {a.x + 3.0f, hMax.y},
                         withAlpha(accentCol, 0xD1), 0.0f);
    }
    if (title && title[0] && fontSizePx >= 1.0f) { // sub-1px: skip, not assert
        // half-pixel-quantized size + whole-pixel position: continuous zoom
        // otherwise re-centres the title every frame and the text wobbles
        const float titlePx = std::max(1.0f, std::round(fontSizePx * 2.0f) * 0.5f);
        const draw::Vec2 ts = surface.measureText(font, titlePx, title);
        surface.pushClip({a.x + 12.0f, a.y}, {b.x - 8.0f, hMax.y}, true);
        surface.text(font, titlePx,
                     {std::round(a.x + 12.0f),
                      std::round(a.y + std::max(0.0f, hh - ts.y) * 0.5f)},
                     textCol, title);
        surface.popClip();
    }
    // 1px black separator under the header (black @ 0.42)
    surface.fillRect({a.x, hMax.y}, {b.x, hMax.y + 1.0f},
                     IM_COL32(0, 0, 0, 107), 0.0f);

    // border last so it sits over the header edges; selected = murk amber 2px.
    // A conic rim (Neo-class skins) replaces both border and selectedBorder;
    // selection spins it instead of recolouring.
    ImU32 rim[6];
    const int rimCount = rimStops(style, rim);
    if (rimCount > 0) {
        float phase = 0.0f;
        if (state.selected && style.rimSpinSeconds > 0.0f)
            phase = (float)std::fmod(timeSeconds / (double)style.rimSpinSeconds,
                                     1.0);
        // a LIP, not a tube (owner): the colour band sits just inside a
        // hard 1px dark arris on the true outline, like a machined edge
        const float inset = 1.5f;
        strokeConicRim(surface, {a.x + inset, a.y + inset},
                       {b.x - inset, b.y - inset},
                       std::max(0.0f, corner - inset), rim, rimCount, phase,
                       1.5f); // owner: tight; spin marks selection
        surface.strokeRect(a, b, IM_COL32(0, 0, 0, 150), corner, 1.0f);
    } else {
        surface.strokeRect(a, b, state.selected ? selectedCol : borderCol,
                           corner, state.selected ? 2.0f : 1.0f);
    }

    if (bypassed) {
        // murk: dim the whole box 0.45 black + bold BYPASS tag in the header
        surface.fillRect(a, b, IM_COL32(0, 0, 0, 115), corner);
        const char* tag = "BYPASS";
        const float tagSize = std::round(fontSizePx * 0.75f * 2.0f) * 0.5f;
        if (tagSize >= 1.0f) {
            const draw::Vec2 ts = surface.measureText(font, tagSize, tag);
            surface.text(font, tagSize,
                         {std::round(b.x - 8.0f - ts.x),
                          std::round(a.y + std::max(0.0f, hh - ts.y) * 0.5f)},
                         IM_COL32(0xff, 0x9e, 0x3d, 255), tag);
        }
    }
    if (error) { // SND semantic murk has no equivalent for; small hot triangle
        const draw::Vec2 c{b.x - 12.0f, a.y + hh + 10.0f};
        surface.fillTriangle({c.x, c.y - 5.0f}, {c.x - 5.0f, c.y + 4.0f},
                             {c.x + 5.0f, c.y + 4.0f}, pal.meterHot);
    }
    // No focus ring on module boxes (owner: it doubled the border and put a
    // blue outer ring over Neo's rim). Keyboard focus reads through the
    // selected border / rim spin, murk-style.
}

void drawModuleBox(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                   const ImVec2& size, const char* title, const Palette& pal,
                   const ControlState& state, bool bypassed, bool error,
                   const GraphSurfaceStyle& style)
{
    if (!dl || !font)
        return;
    draw::ImGuiSurface surface(dl);
    drawModuleBox(surface, draw::fontRef(font), ImGui::GetFontSize() * 0.90f,
                  draw::toDrawVec2(topLeft), draw::toDrawVec2(size), title,
                  pal, state, bypassed, error, style, 24.0f, ImGui::GetTime());
}

void drawSectionHeader(draw::Surface& surface, draw::FontRef font,
                       draw::Vec2 topLeft, const char* text, float fontSize,
                       float width, const Palette& pal)
{
    if (!text || fontSize <= 0.0f)
        return;
    draw::Vec2 ts = surface.measureText(font, fontSize, text);
    surface.text(font, fontSize, {topLeft.x, topLeft.y + 4.0f},
                 pal.textDim, text);
    if (width > ts.x + 12.0f)
        surface.line({topLeft.x + ts.x + 8.0f,
                      topLeft.y + 4.0f + fontSize * 0.55f},
                     {topLeft.x + width,
                      topLeft.y + 4.0f + fontSize * 0.55f},
                     withAlpha(pal.frameBright, 0xAA), 1.0f);
}

void drawTooltip(draw::Surface& surface, draw::FontRef font, float fontSizePx,
                 draw::Vec2 anchor, const char* text, const Palette& pal,
                 draw::Vec2 clipMax)
{
    if (!text || !text[0] || fontSizePx < 1.0f)
        return;
    const draw::Vec2 ts = surface.measureText(font, fontSizePx, text);
    const float padX = 7.0f, padY = 4.0f;
    draw::Vec2 tl{anchor.x + 14.0f, anchor.y + 14.0f}; // down-right of the cursor
    const draw::Vec2 sz{ts.x + padX * 2.0f, ts.y + padY * 2.0f};
    if (clipMax.x > 0.0f && tl.x + sz.x > clipMax.x)
        tl.x = std::max(0.0f, clipMax.x - sz.x);
    if (clipMax.y > 0.0f && tl.y + sz.y > clipMax.y)
        tl.y = anchor.y - 14.0f - sz.y; // flip above the cursor
    tl.x = std::round(tl.x);
    tl.y = std::round(tl.y);
    const draw::Vec2 br{tl.x + sz.x, tl.y + sz.y};
    surface.fillRect(tl, br, pal.frame, 3.0f);
    surface.strokeRect(tl, br, pal.frameBright, 3.0f);
    surface.text(font, fontSizePx, {tl.x + padX, tl.y + padY}, pal.text, text);
}

void drawSectionHeader(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                       const char* text, float fontSize, float width,
                       const Palette& pal)
{
    if (!dl || !font)
        return;
    draw::ImGuiSurface surface(dl);
    drawSectionHeader(surface, draw::fontRef(font), draw::toDrawVec2(topLeft),
                      text, fontSize, width, pal);
}

} // namespace snd::ui::paint
