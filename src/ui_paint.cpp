#include "snd/ui_paint.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace snd::ui::paint {

ImU32 withAlpha(ImU32 c, uint32_t a) { return (c & 0x00FFFFFF) | (a << 24); }

namespace {
bool visible(ImU32 c) { return (c & 0xFF000000u) != 0; }

void drawMenuCheck(ImDrawList* dl, const ImVec2& center, ImU32 color)
{
    if (!dl)
        return;
    dl->AddLine(ImVec2(center.x - 5.0f, center.y),
                ImVec2(center.x - 1.5f, center.y + 4.0f), color, 1.8f);
    dl->AddLine(ImVec2(center.x - 1.5f, center.y + 4.0f),
                ImVec2(center.x + 5.5f, center.y - 5.0f), color, 1.8f);
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

void drawGradientPanel(ImDrawList* dl, const ImVec2& topLeft,
                       const ImVec2& size, ImU32 topLeftColor,
                       ImU32 topRightColor, ImU32 bottomRightColor,
                       ImU32 bottomLeftColor)
{
    if (!dl)
        return;
    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    dl->AddRectFilledMultiColor(topLeft, mx, topLeftColor, topRightColor,
                                bottomRightColor, bottomLeftColor);
}

void drawAnimatedButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                        const ImVec2& size, const char* text, ImU32 top,
                        ImU32 bottom, const Palette& pal,
                        const ControlState& state, float pulse, float fontScale)
{
    if (!dl || !font)
        return;
    if (top == 0)
        top = pal.accent;
    if (bottom == 0)
        bottom = pal.accentDim;

    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
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

    dl->AddRectFilledMultiColor(topLeft, mx, topCol, topCol, bottomCol, bottomCol);
    dl->AddRectFilled(topLeft, ImVec2(mx.x, topLeft.y + std::min(3.0f, size.y * 0.16f)),
                      withAlpha(IM_COL32(255, 255, 255, 255),
                                state.disabled ? 0x16 : 0x42), r);

    pulse = std::clamp(pulse, 0.0f, 1.0f);
    const bool glow = pulse > 0.001f && !state.disabled;
    if (glow) {
        const float hot = 0.30f + 0.70f * pulse;
        dl->AddRectFilled(topLeft, mx, withAlpha(pal.accent, (uint32_t)(0x24 * hot)), r);
        const float sweepW = std::max(8.0f, size.x * 0.22f);
        const float sx = topLeft.x - sweepW + pulse * (size.x + sweepW * 2.0f);
        dl->PushClipRect(topLeft, mx, true);
        dl->AddRectFilled(ImVec2(sx, topLeft.y),
                          ImVec2(sx + sweepW, mx.y),
                          withAlpha(IM_COL32(255, 255, 255, 255),
                                    (uint32_t)(0x38 * hot)), 3.0f);
        dl->PopClipRect();
    }

    dl->AddRect(topLeft, mx,
                state.active || state.selected ? pal.accent
                                               : IM_COL32(0, 0, 0, 130),
                r, 0, state.active || state.selected ? 1.8f : 1.0f);

    if (text && text[0]) {
        const float fs = ImGui::GetFontSize() * fontScale;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
        ImVec2 p(topLeft.x + std::max(0.0f, size.x - ts.x) * 0.5f,
                 topLeft.y + std::max(0.0f, size.y - ts.y) * 0.5f +
                     (state.active ? 1.0f : 0.0f));
        dl->AddText(font, fs, p, state.disabled ? pal.textDim
                                                : IM_COL32(255, 255, 255, 245),
                    text);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, mx, pal, r);
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

void drawKnobModRing(ImDrawList* dl, const ImVec2& topLeft, float size,
                     float frac, const KnobMod& mod, const Palette& pal,
                     ImU32 accent)
{
    if (!dl || (mod.depth == 0.0f && mod.value < 0.0f))
        return;
    if (accent == 0)
        accent = pal.accent;
    const ImU32 col = mod.color ? mod.color : mix(accent, pal.text, 0.55f);

    const float radius = size * 0.5f;
    const ImVec2 c(topLeft.x + radius, topLeft.y + radius);
    const float rArc = radius - 1.0f; // rim overlay; focus ring stays outside
    const float lineW = std::clamp(radius * 0.09f, 1.4f, 2.0f);

    frac = std::clamp(frac, 0.0f, 1.0f);
    if (mod.depth != 0.0f) {
        const float end = std::clamp(frac + mod.depth, 0.0f, 1.0f);
        const float a0 = knobAngle(std::min(frac, end));
        const float a1 = knobAngle(std::max(frac, end));
        if (a1 - a0 > 0.001f) {
            dl->PathArcTo(c, rArc, a0, a1, 32);
            dl->PathStroke(withAlpha(col, 0xB4), 0, lineW);
        }
    }
    if (mod.value >= 0.0f) {
        const ImVec2 d = dirAt(knobAngle(std::clamp(mod.value, 0.0f, 1.0f)));
        dl->AddCircleFilled(ImVec2(c.x + d.x * rArc, c.y + d.y * rArc),
                            lineW * 1.05f, col);
    }
}

void drawDefaultKnob(const KnobPaintArgs& args)
{
    if (!args.palette || !args.state)
        return;
    drawKnob(args.drawList, args.topLeft, args.size, args.normalizedValue,
             args.style, *args.palette, *args.state, args.bipolar, args.accent);
}

void drawKnobWithPainter(const KnobPaintArgs& args, const KnobPainter& painter)
{
    ControlState bodyState = args.state ? *args.state : ControlState{};
    bodyState.focused = false;
    KnobPaintArgs bodyArgs = args;
    bodyArgs.state = &bodyState;

    if (painter)
        painter(bodyArgs);
    else
        drawDefaultKnob(bodyArgs);

    // SND-owned overlays after the body: modulation ring, then focus ring.
    if (args.drawList && args.palette)
        drawKnobModRing(args.drawList, args.topLeft, args.size,
                        args.normalizedValue, args.mod, *args.palette,
                        args.accent);

    if (args.drawList && args.palette && args.state &&
        args.state->focused && !args.state->disabled) {
        drawFocusRing(args.drawList, args.topLeft,
                      ImVec2(args.topLeft.x + args.size, args.topLeft.y + args.size),
                      *args.palette, args.size * 0.5f, 2.0f);
    }
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

void drawLedButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                   const ImVec2& size, const char* glyph, float ledLevel,
                   const Palette& pal, const ControlState& state, bool down,
                   ImU32 ledColor, ImU32 face)
{
    if (!dl || !font)
        return;
    drawTactileIconButton(dl, font, topLeft, size, glyph, pal, state, down, face);

    if (ledColor == 0)
        ledColor = pal.accent;
    ledLevel = std::clamp(ledLevel, 0.0f, 1.0f);
    if (state.disabled)
        ledLevel *= 0.35f;

    const float r = 4.0f;
    const ImVec2 mn(topLeft.x + 2.5f, topLeft.y + 2.5f);
    const ImVec2 mx(topLeft.x + size.x - 2.5f, topLeft.y + size.y - 2.5f);
    if (ledLevel > 0.0f) // soft bloom outside the lit ring
        dl->AddRect(ImVec2(mn.x - 1.5f, mn.y - 1.5f),
                    ImVec2(mx.x + 1.5f, mx.y + 1.5f),
                    withAlpha(ledColor, (uint32_t)(0x52 * ledLevel)),
                    r + 1.0f, 0, 3.0f);
    dl->AddRect(mn, mx, mix(pal.ledOff, ledColor, ledLevel), r - 1.0f, 0, 1.8f);
}

void drawVectorIconButton(ImDrawList* dl, const ImVec2& topLeft,
                          const ImVec2& size, Icon icon, ImU32 accent,
                          const Palette& pal, const ControlState& state,
                          bool active)
{
    if (!dl)
        return;
    if (accent == 0)
        accent = pal.accent;

    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    const ImGuiStyle& st = ImGui::GetStyle();
    ImU32 bg = ImGui::GetColorU32(state.active      ? st.Colors[ImGuiCol_ButtonActive]
                                  : state.hovered  ? st.Colors[ImGuiCol_ButtonHovered]
                                                   : st.Colors[ImGuiCol_Button]);
    if (state.disabled)
        bg = mix(bg, pal.frame, 0.65f);
    dl->AddRectFilled(topLeft, mx, bg, 4.0f);

    const bool lit = active || state.selected;
    if (lit)
        dl->AddRect(topLeft, mx, accent, 4.0f, 0, 2.0f);

    const ImU32 fg = state.disabled ? pal.textDim
                                    : lit ? accent
                                          : ImGui::GetColorU32(st.Colors[ImGuiCol_Text]);
    const ImVec2 c(topLeft.x + size.x * 0.5f, topLeft.y + size.y * 0.5f);
    const float r = std::min(size.x, size.y) * 0.28f;

    switch (icon) {
    case Icon::Play:
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.7f, c.y - r),
                              ImVec2(c.x - r * 0.7f, c.y + r),
                              ImVec2(c.x + r, c.y), fg);
        break;
    case Icon::Stop:
        dl->AddRectFilled(ImVec2(c.x - r * 0.8f, c.y - r * 0.8f),
                          ImVec2(c.x + r * 0.8f, c.y + r * 0.8f), fg, 2.0f);
        break;
    case Icon::Record:
        dl->AddCircleFilled(c, r * 0.9f, fg, 24);
        break;
    case Icon::SkipToStart:
        dl->AddRectFilled(ImVec2(c.x - r, c.y - r),
                          ImVec2(c.x - r + 2.5f, c.y + r), fg);
        dl->AddTriangleFilled(ImVec2(c.x + r, c.y - r),
                              ImVec2(c.x + r, c.y + r),
                              ImVec2(c.x - r * 0.5f, c.y), fg);
        break;
    case Icon::SkipToEnd:
        dl->AddRectFilled(ImVec2(c.x + r - 2.5f, c.y - r),
                          ImVec2(c.x + r, c.y + r), fg);
        dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r),
                              ImVec2(c.x - r, c.y + r),
                              ImVec2(c.x + r * 0.5f, c.y), fg);
        break;
    case Icon::Loop: {
        dl->PathArcTo(c, r, 0.3f, 2.0f * 3.14159265f - 0.6f, 24);
        dl->PathStroke(fg, 0, 2.0f);
        const float ax = c.x + r * std::cos(0.3f);
        const float ay = c.y + r * std::sin(0.3f);
        dl->AddTriangleFilled(ImVec2(ax - 4.0f, ay - 1.0f),
                              ImVec2(ax + 3.0f, ay + 3.0f),
                              ImVec2(ax + 2.0f, ay - 5.0f), fg);
        break;
    }
    case Icon::Waveform: {
        const float heights[7] = {0.35f, 0.75f, 0.5f, 1.0f, 0.6f, 0.85f, 0.4f};
        const float step = (r * 2.2f) / 7.0f;
        for (int i = 0; i < 7; ++i) {
            const float x = c.x - r * 1.1f + step * (i + 0.5f);
            const float h = r * heights[i];
            dl->AddLine(ImVec2(x, c.y - h), ImVec2(x, c.y + h), fg, 2.0f);
        }
        break;
    }
    case Icon::Follow:
        dl->AddLine(ImVec2(c.x - r * 0.9f, c.y - r),
                    ImVec2(c.x - r * 0.9f, c.y + r), fg, 2.0f);
        dl->AddLine(ImVec2(c.x - r * 0.4f, c.y),
                    ImVec2(c.x + r * 0.4f, c.y), fg, 2.0f);
        dl->AddTriangleFilled(ImVec2(c.x + r * 0.3f, c.y - r * 0.5f),
                              ImVec2(c.x + r * 0.3f, c.y + r * 0.5f),
                              ImVec2(c.x + r, c.y), fg);
        break;
    case Icon::Spectrum:
        for (int i = 0; i < 12; ++i) {
            const float t = i / 11.0f;
            const float x = c.x - r * 1.1f + t * r * 2.2f;
            const float h = r * (0.2f + 0.8f * t);
            dl->AddLine(ImVec2(x, c.y + r * 0.9f),
                        ImVec2(x, c.y + r * 0.9f - h * 1.8f), fg, 1.5f);
        }
        break;
    }

    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, mx, pal, 4.0f);
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

void drawSegmented(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                   const ImVec2& size, const char* const* labels, int count,
                   int selected, int hovered, const Palette& pal,
                   const ControlState& state, float fontScale)
{
    if (!dl || !font || !labels || count <= 0)
        return;

    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    const float r = size.y * 0.5f; // pill
    ImU32 body = pal.frame;
    if (state.disabled)
        body = mix(body, IM_COL32(0, 0, 0, 255), 0.30f);
    dl->AddRectFilled(topLeft, mx, body, r);

    const float segW = size.x / (float)count;
    for (int i = 0; i < count; ++i) {
        const float x0 = topLeft.x + segW * (float)i;
        const bool isSel = i == selected;
        const bool isHot = !state.disabled && i == hovered;

        if (isSel) {
            ImU32 fill = mix(pal.frame, pal.accent, state.disabled ? 0.18f : 0.40f);
            if (isHot && state.active)
                fill = mix(pal.frame, pal.accent, 0.50f);
            dl->AddRectFilled(ImVec2(x0 + 2.0f, topLeft.y + 2.0f),
                              ImVec2(x0 + segW - 2.0f, mx.y - 2.0f), fill,
                              r - 2.0f);
        } else if (isHot) {
            dl->AddRectFilled(ImVec2(x0 + 2.0f, topLeft.y + 2.0f),
                              ImVec2(x0 + segW - 2.0f, mx.y - 2.0f),
                              state.active ? mix(pal.frameBright, pal.accent, 0.20f)
                                           : pal.frameBright,
                              r - 2.0f);
        }
        if (i > 0 && !isSel && selected != i - 1) { // resting divider
            dl->AddLine(ImVec2(x0, topLeft.y + size.y * 0.26f),
                        ImVec2(x0, mx.y - size.y * 0.26f),
                        withAlpha(pal.frameBright, 0xB4), 1.0f);
        }

        const char* text = labels[i] ? labels[i] : "";
        const float fs = ImGui::GetFontSize() * fontScale;
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
        const ImU32 txt = state.disabled ? pal.textDim
                          : isSel        ? pal.text
                          : isHot        ? mix(pal.textDim, pal.text, 0.55f)
                                         : pal.textDim;
        dl->AddText(font, fs,
                    ImVec2(x0 + (segW - ts.x) * 0.5f,
                           topLeft.y + (size.y - ts.y) * 0.5f),
                    txt, text);
    }

    dl->AddRect(topLeft, mx, pal.frameBright, r);
    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, mx, pal, r);
}

void drawCycleButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                     const ImVec2& size, const char* text, int index, int count,
                     const Palette& pal, const ControlState& state,
                     float fontScale)
{
    if (!dl || !font)
        return;

    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    const float r = 4.0f;
    ImU32 fill = state.active ? mix(pal.frame, pal.accent, 0.40f) : pal.frame;
    if (state.hovered && !state.active && !state.disabled)
        fill = pal.frameBright;
    if (state.disabled)
        fill = mix(fill, IM_COL32(0, 0, 0, 255), 0.30f);
    dl->AddRectFilled(topLeft, mx, fill, r);
    dl->AddRect(topLeft, mx, pal.frameBright, r);

    const float pipGap = 6.0f;
    const bool pips = count > 1 && count <= 8 && size.y >= 22.0f &&
                      pipGap * (float)(count - 1) <= size.x - 12.0f;
    if (text && text[0]) {
        const float fs = ImGui::GetFontSize() * fontScale;
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
        const ImVec2 p(topLeft.x + std::max(0.0f, size.x - ts.x) * 0.5f,
                       topLeft.y + std::max(0.0f, size.y - ts.y) * 0.5f -
                           (pips ? 1.5f : 0.0f));
        dl->AddText(font, fs, p, state.disabled ? pal.textDim : pal.text, text);
    }

    if (pips) { // position within the cycle
        const float y = mx.y - 4.5f;
        const float x0 = topLeft.x + size.x * 0.5f -
                         pipGap * (float)(count - 1) * 0.5f;
        for (int i = 0; i < count; ++i) {
            const bool onPip = i == index;
            dl->AddCircleFilled(ImVec2(x0 + pipGap * (float)i, y),
                                onPip ? 1.8f : 1.3f,
                                onPip && !state.disabled
                                    ? pal.accent
                                    : withAlpha(pal.frameBright, 0xE6));
        }
    }

    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, mx, pal, r);
}

void drawOutlineButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                       const ImVec2& size, const char* text,
                       const Palette& pal, const ControlState& state,
                       const OutlineButtonStyle& style)
{
    if (!dl || !font)
        return;

    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
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
        dl->AddRectFilled(topLeft, mx, fill, r);

    ImU32 border = style.border;
    if (state.hovered)
        border = visible(style.hoverBorder) ? style.hoverBorder : pal.accent;
    if (state.active)
        border = visible(style.activeBorder) ? style.activeBorder : pal.accent;
    if (state.selected)
        border = visible(style.selectedBorder) ? style.selectedBorder : pal.accent;
    if (state.disabled && visible(border))
        border = mix(border, pal.frameBright, 0.65f);
    if (visible(border))
        dl->AddRect(topLeft, mx, border, r, 0, state.active || state.selected ? 1.6f : 1.0f);

    if (text && text[0]) {
        const float fs = ImGui::GetFontSize() *
                         (style.fontScale > 0.0f ? style.fontScale : 0.90f);
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
        ImVec2 p(topLeft.x + std::max(0.0f, size.x - ts.x) * 0.5f,
                 topLeft.y + std::max(0.0f, size.y - ts.y) * 0.5f);
        const ImU32 textCol = state.disabled ? pal.textDim
                                             : visible(style.text) ? style.text : pal.text;
        dl->AddText(font, fs, p, textCol, text);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, mx, pal, r);
}

void drawDefaultButton(const ButtonPaintArgs& args)
{
    if (!args.palette || !args.state)
        return;
    drawButton(args.drawList, args.font, args.topLeft, args.size, args.text,
               *args.palette, *args.state, args.fontScale);
}

void drawButtonWithPainter(const ButtonPaintArgs& args,
                           const ButtonPainter& painter)
{
    ControlState bodyState = args.state ? *args.state : ControlState{};
    bodyState.focused = false;
    ButtonPaintArgs bodyArgs = args;
    bodyArgs.state = &bodyState;

    if (painter)
        painter(bodyArgs);
    else
        drawDefaultButton(bodyArgs);

    if (args.drawList && args.palette && args.state &&
        args.state->focused && !args.state->disabled) {
        drawFocusRing(args.drawList, args.topLeft,
                      ImVec2(args.topLeft.x + args.size.x,
                             args.topLeft.y + args.size.y),
                      *args.palette, 4.0f, 2.0f);
    }
}

void drawMenuPanel(ImDrawList* dl, const ImVec2& topLeft,
                   const ImVec2& size, const Palette& pal)
{
    if (!dl)
        return;
    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    dl->AddRectFilled(ImVec2(topLeft.x + 2.0f, topLeft.y + 3.0f),
                      ImVec2(mx.x + 2.0f, mx.y + 3.0f),
                      IM_COL32(0, 0, 0, 110), 4.0f);
    dl->AddRectFilled(topLeft, mx, mix(pal.frame, IM_COL32(0, 0, 0, 255), 0.18f), 4.0f);
    dl->AddRect(topLeft, mx, pal.frameBright, 4.0f);
}

void drawMenuItem(ImDrawList* dl, ImFont* font, ImFont* iconFont,
                  const ImVec2& topLeft, const ImVec2& size,
                  const MenuItem& item, const Palette& pal,
                  const ControlState& state, float fontScale)
{
    if (!dl || !font)
        return;

    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    if (item.separator) {
        const float y = topLeft.y + size.y * 0.5f;
        dl->AddLine(ImVec2(topLeft.x + 8.0f, y), ImVec2(mx.x - 8.0f, y),
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
    if (visible(fill))
        dl->AddRectFilled(topLeft, mx, fill, 3.0f);

    if ((state.hovered || state.focused) && !state.disabled)
        dl->AddRect(topLeft, mx, state.focused ? pal.accent : pal.frameBright, 3.0f);

    const float fs = ImGui::GetFontSize() * fontScale;
    const float centerY = topLeft.y + size.y * 0.5f;
    const ImU32 hotCol = item.danger ? pal.meterHot : pal.accent;
    const ImU32 textCol = state.disabled ? pal.textDim
                                         : item.danger ? mix(pal.text, pal.meterHot, 0.36f)
                                                       : pal.text;
    const ImU32 accent = state.disabled ? mix(hotCol, pal.frameBright, 0.60f)
                                        : hotCol;

    if (item.checked)
        drawMenuCheck(dl, ImVec2(topLeft.x + 14.0f, centerY), accent);
    else if (item.danger) {
        const ImVec2 a(topLeft.x + 13.0f, centerY - 6.0f);
        const ImVec2 b(topLeft.x + 18.0f, centerY + 5.0f);
        const ImVec2 c(topLeft.x + 8.0f, centerY + 5.0f);
        dl->AddTriangleFilled(a, b, c, accent);
    }

    if (!item.icon.empty()) {
        ImFont* glyphFont = iconFont ? iconFont : font;
        const float iconSize = ImGui::GetFontSize();
        ImVec2 glyphSize = glyphFont->CalcTextSizeA(iconSize, FLT_MAX, 0.0f,
                                                    item.icon.c_str());
        dl->AddText(glyphFont, iconSize,
                    ImVec2(topLeft.x + 26.0f,
                           topLeft.y + std::max(0.0f, size.y - glyphSize.y) * 0.5f),
                    state.disabled ? pal.textDim : accent, item.icon.c_str());
    }

    const float textX = topLeft.x + (item.icon.empty() ? 30.0f : 50.0f);
    const char* label = item.label.c_str();
    ImVec2 labelSize = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
    const bool hasSubmenu = !item.children.empty();
    const float arrowPad = hasSubmenu ? 18.0f : 0.0f;
    const float rightPad = item.rightText.empty()
                               ? 8.0f + arrowPad
                               : font->CalcTextSizeA(fs, FLT_MAX, 0.0f,
                                                     item.rightText.c_str()).x + 24.0f +
                                     arrowPad;
    dl->PushClipRect(ImVec2(textX, topLeft.y), ImVec2(mx.x - rightPad, mx.y), true);
    dl->AddText(font, fs, ImVec2(textX, topLeft.y + std::max(0.0f, size.y - labelSize.y) * 0.5f),
                textCol, label);
    dl->PopClipRect();

    if (!item.rightText.empty()) {
        const ImU32 rightCol = state.disabled ? pal.textDim
                                             : item.danger ? accent : pal.textDim;
        ImVec2 rightSize = font->CalcTextSizeA(fs, FLT_MAX, 0.0f,
                                               item.rightText.c_str());
        dl->AddText(font, fs,
                    ImVec2(mx.x - 10.0f - arrowPad - rightSize.x,
                           topLeft.y + std::max(0.0f, size.y - rightSize.y) * 0.5f),
                    rightCol, item.rightText.c_str());
    }

    if (hasSubmenu) {
        const ImU32 arrowCol = state.disabled ? pal.textDim : pal.text;
        const float x = mx.x - 12.0f;
        dl->AddTriangleFilled(ImVec2(x - 3.0f, centerY - 5.0f),
                              ImVec2(x - 3.0f, centerY + 5.0f),
                              ImVec2(x + 3.0f, centerY), arrowCol);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, mx, pal, 3.0f);
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
        ImVec2 p(topLeft.x + 10.0f,
                 topLeft.y + std::max(0.0f, size.y - ts.y) * 0.5f);
        dl->AddText(font, fs, p, state.disabled ? pal.textDim : pal.text, text);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, mx, pal, 3.0f);
}

void drawValueRow(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                  const ImVec2& size, const char* label, const char* valueText,
                  const Palette& pal, const ControlState& state,
                  float fontScale, bool draggable)
{
    if (!dl || !font)
        return;

    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    const float r = 3.0f;
    ImU32 fill = state.active ? mix(pal.frame, pal.accent, 0.24f) : pal.frame;
    if (state.hovered && !state.active && !state.disabled)
        fill = mix(fill, pal.frameBright, 0.55f);
    if (state.disabled)
        fill = mix(fill, IM_COL32(0, 0, 0, 255), 0.35f);

    dl->AddRectFilled(topLeft, mx, fill, r);
    dl->AddRect(topLeft, mx, state.active ? pal.accent : pal.frameBright, r);

    const ImU32 accent = state.disabled ? mix(pal.accent, pal.frameBright, 0.65f)
                                        : pal.accent;
    const float railW = state.active ? 4.0f : 3.0f;
    dl->AddRectFilled(ImVec2(topLeft.x + 2.0f, topLeft.y + 3.0f),
                      ImVec2(topLeft.x + 2.0f + railW, mx.y - 3.0f),
                      state.active ? accent : withAlpha(accent, 0x86), 2.0f);

    const float fs = ImGui::GetFontSize() * fontScale;
    const float padX = 10.0f;
    const float y = topLeft.y + std::max(0.0f, size.y - fs) * 0.5f - 1.0f;
    const char* labelText = label ? label : "";
    const char* value = valueText ? valueText : "";
    ImVec2 valueSize = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, value);
    const float valueX = std::max(topLeft.x + padX, mx.x - padX - valueSize.x);
    const float labelRight = std::max(topLeft.x + padX, valueX - 8.0f);

    dl->PushClipRect(ImVec2(topLeft.x + padX, topLeft.y),
                     ImVec2(labelRight, mx.y), true);
    if (labelText[0]) {
        dl->AddText(font, fs, ImVec2(topLeft.x + padX, y), pal.textDim, labelText);
    }
    dl->PopClipRect();

    dl->PushClipRect(ImVec2(topLeft.x + padX, topLeft.y),
                     ImVec2(mx.x - padX, mx.y), true);
    if (value[0]) {
        dl->AddText(font, fs, ImVec2(valueX, y),
                    state.disabled ? pal.textDim : pal.text,
                    value);
    }
    dl->PopClipRect();

    if (draggable) {
        const float gripX = mx.x - 5.0f;
        const ImU32 grip = state.disabled ? withAlpha(pal.textDim, 0x60)
                                          : withAlpha(pal.textDim, state.hovered ? 0xB8 : 0x80);
        dl->AddLine(ImVec2(gripX, topLeft.y + 7.0f), ImVec2(gripX, mx.y - 7.0f), grip, 1.0f);
        dl->AddLine(ImVec2(gripX - 3.0f, topLeft.y + 8.0f),
                    ImVec2(gripX - 3.0f, mx.y - 8.0f), withAlpha(grip, 0x72), 1.0f);
    }

    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, mx, pal, r);
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

void drawEnvelope(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                  const std::vector<EnvPoint>& points,
                  const std::vector<float>* tensions,
                  int hotPoint, int activePoint,
                  int hotSegment, int activeSegment,
                  const Palette& pal, const ControlState& state)
{
    if (!dl)
        return;

    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    dl->AddRectFilled(topLeft, mx, pal.frame, 3.0f);

    if (size.x > 0.0f && size.y > 0.0f && points.size() >= 2) {
        auto toScreen = [&](const EnvPoint& e) {
            return ImVec2(topLeft.x + e.x * size.x,
                          topLeft.y + (1.0f - e.y) * size.y);
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
                dl->AddLine(toScreen(points[i - 1]), toScreen(points[i]), col, 2.0f);
            } else {
                constexpr int kSteps = 16;
                ImVec2 prev = toScreen(points[i - 1]);
                for (int s = 1; s <= kSteps; ++s) {
                    const float t = (float)s / (float)kSteps;
                    EnvPoint q{points[i - 1].x + (points[i].x - points[i - 1].x) * t,
                               points[i - 1].y +
                                   (points[i].y - points[i - 1].y) *
                                       envelopeEase(t, ten)};
                    ImVec2 cur = toScreen(q);
                    dl->AddLine(prev, cur, col, 2.0f);
                    prev = cur;
                }
            }
        }

        for (int i = 0; i < (int)points.size(); ++i) {
            const bool pointHot = i == (activePoint >= 0 ? activePoint : hotPoint);
            const ImVec2 s = toScreen(points[(size_t)i]);
            dl->AddCircleFilled(s, pointHot ? 6.0f : 4.5f,
                                pointHot ? hot : accent);
        }
    }

    dl->AddRect(topLeft, mx, pal.frameBright, 3.0f);
    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, mx, pal, 3.0f);
}

void drawGraphGrid(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                   const ImVec2& pan, float zoom, const Palette& pal,
                   const ControlState& state)
{
    if (!dl)
        return;
    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    dl->AddRectFilled(topLeft, mx,
                      mix(IM_COL32(0, 0, 0, 255), pal.frame, 0.72f), 3.0f);
    if (size.x <= 0.0f || size.y <= 0.0f)
        return;

    float minor = 24.0f * std::max(0.05f, zoom);
    while (minor < 12.0f)
        minor *= 2.0f;
    while (minor > 48.0f)
        minor *= 0.5f;
    const float major = minor * 4.0f;
    const ImU32 minorCol = withAlpha(pal.frameBright, state.disabled ? 0x24 : 0x38);
    const ImU32 majorCol = withAlpha(pal.frameBright, state.disabled ? 0x38 : 0x62);

    const float ox = std::fmod(pan.x * zoom, minor);
    const float oy = std::fmod(pan.y * zoom, minor);
    for (float x = topLeft.x + ox; x < mx.x; x += minor)
        if (x >= topLeft.x)
            dl->AddLine(ImVec2(x, topLeft.y), ImVec2(x, mx.y), minorCol, 1.0f);
    for (float y = topLeft.y + oy; y < mx.y; y += minor)
        if (y >= topLeft.y)
            dl->AddLine(ImVec2(topLeft.x, y), ImVec2(mx.x, y), minorCol, 1.0f);

    const float mox = std::fmod(pan.x * zoom, major);
    const float moy = std::fmod(pan.y * zoom, major);
    for (float x = topLeft.x + mox; x < mx.x; x += major)
        if (x >= topLeft.x)
            dl->AddLine(ImVec2(x, topLeft.y), ImVec2(x, mx.y), majorCol, 1.0f);
    for (float y = topLeft.y + moy; y < mx.y; y += major)
        if (y >= topLeft.y)
            dl->AddLine(ImVec2(topLeft.x, y), ImVec2(mx.x, y), majorCol, 1.0f);

    dl->AddRect(topLeft, mx, state.focused ? pal.accent : pal.frameBright, 3.0f);
    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, mx, pal, 3.0f);
}

void drawCable(ImDrawList* dl, const ImVec2& from, const ImVec2& to,
               const Palette& pal, const ControlState& state,
               ImU32 color, float thickness)
{
    if (!dl)
        return;
    if (color == 0)
        color = pal.accent;
    color = state.disabled ? mix(color, pal.frameBright, 0.70f) : color;
    if (state.hovered)
        color = mix(color, IM_COL32(255, 255, 255, 255), 0.18f);

    const float dx = std::max(38.0f, std::abs(to.x - from.x) * 0.52f);
    const ImVec2 c1(from.x + dx, from.y);
    const ImVec2 c2(to.x - dx, to.y);
    const float lineW = std::max(1.0f, thickness + (state.selected ? 1.4f : 0.0f));

    if (state.selected || state.focused)
        dl->AddBezierCubic(from, c1, c2, to, withAlpha(pal.text, 0x80),
                           lineW + 3.0f, 24);
    dl->AddBezierCubic(from, c1, c2, to, color, lineW, 24);

    const float r = state.selected || state.hovered ? 4.0f : 3.0f;
    dl->AddCircleFilled(from, r, color, 16);
    dl->AddCircleFilled(to, r, color, 16);
}

void drawModuleBox(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                   const ImVec2& size, const char* title, const Palette& pal,
                   const ControlState& state, bool bypassed, bool error)
{
    if (!dl || !font)
        return;
    const ImVec2 mx(topLeft.x + size.x, topLeft.y + size.y);
    const float r = 5.0f;
    ImU32 body = state.selected ? mix(pal.frame, pal.accent, 0.18f) : pal.frame;
    if (state.hovered && !state.disabled)
        body = mix(body, pal.frameBright, 0.35f);
    if (state.disabled || bypassed)
        body = mix(body, IM_COL32(0, 0, 0, 255), 0.35f);

    dl->AddRectFilled(topLeft, mx, body, r);
    const ImVec2 titleMax(mx.x, std::min(mx.y, topLeft.y + 26.0f));
    ImU32 titleFill = mix(pal.frame, pal.frameBright, 0.35f);
    if (error)
        titleFill = mix(titleFill, pal.meterHot, 0.35f);
    if (bypassed)
        titleFill = mix(titleFill, pal.textDim, 0.20f);
    dl->AddRectFilled(topLeft, titleMax, titleFill, r, ImDrawFlags_RoundCornersTop);

    const ImU32 border = state.selected ? pal.accent
                        : error          ? pal.meterHot
                                         : pal.frameBright;
    dl->AddRect(topLeft, mx, border, r, 0, state.selected ? 2.0f : 1.0f);
    if (state.selected) {
        dl->AddRectFilled(ImVec2(topLeft.x + 3.0f, topLeft.y + 30.0f),
                          ImVec2(topLeft.x + 6.0f, mx.y - 6.0f),
                          pal.accent, 2.0f);
    }

    const char* label = title ? title : "";
    if (label[0]) {
        const float fs = ImGui::GetFontSize() * 0.90f;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
        const ImVec2 p(topLeft.x + 10.0f,
                       topLeft.y + std::max(0.0f, titleMax.y - topLeft.y - ts.y) * 0.5f);
        dl->PushClipRect(ImVec2(topLeft.x + 8.0f, topLeft.y),
                         ImVec2(mx.x - 8.0f, titleMax.y), true);
        dl->AddText(font, fs, p, state.disabled ? pal.textDim : pal.text, label);
        dl->PopClipRect();
    }

    if (bypassed) {
        const ImU32 slash = withAlpha(pal.textDim, 0x92);
        dl->AddLine(ImVec2(topLeft.x + 9.0f, mx.y - 9.0f),
                    ImVec2(mx.x - 9.0f, topLeft.y + 9.0f), slash, 1.5f);
    }
    if (error) {
        const ImVec2 c(mx.x - 13.0f, topLeft.y + 13.0f);
        dl->AddTriangleFilled(ImVec2(c.x, c.y - 6.0f),
                              ImVec2(c.x - 6.0f, c.y + 5.0f),
                              ImVec2(c.x + 6.0f, c.y + 5.0f),
                              pal.meterHot);
    }
    if (state.focused && !state.disabled)
        drawFocusRing(dl, topLeft, mx, pal, r);
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
