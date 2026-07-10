// Renderer-neutral drawing surface for SND UI paint helpers.
//
// This header is intentionally backend-free: no renderer, windowing, or
// retained-tree types. Backends adapt these operations to their renderer; SND
// paint code owns the visual vocabulary above this layer.
#pragma once

#include <cstdint>

namespace snd::ui::draw {

// Packed 0xAABBGGRR colour value matching SND's current UI colour constants.
using Color = uint32_t;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct FontRef {
    // Opaque pointer-sized token. Only backend adapters may interpret it.
    uintptr_t handle = 0;
};

// Opaque texture token (an snd::ui texture id, e.g. from loadSvgTexture /
// loadImageTexture). 0 = invalid. Only backend adapters may interpret it.
using TextureRef = uintptr_t;

struct FrameContext {
    FontRef font;
    FontRef iconFontLucide;
    float fontSizePx = 0.0f;
    double timeSeconds = 0.0;
    Vec2 pointer;
    bool pointerValid = false;
};

using CornerFlags = uint32_t;
inline constexpr CornerFlags kRoundCornerTopLeft = 1u << 0;
inline constexpr CornerFlags kRoundCornerTopRight = 1u << 1;
inline constexpr CornerFlags kRoundCornerBottomRight = 1u << 2;
inline constexpr CornerFlags kRoundCornerBottomLeft = 1u << 3;
inline constexpr CornerFlags kRoundCornersTop =
    kRoundCornerTopLeft | kRoundCornerTopRight;
inline constexpr CornerFlags kRoundCornersBottom =
    kRoundCornerBottomLeft | kRoundCornerBottomRight;
inline constexpr CornerFlags kRoundCornersLeft =
    kRoundCornerTopLeft | kRoundCornerBottomLeft;
inline constexpr CornerFlags kRoundCornersRight =
    kRoundCornerTopRight | kRoundCornerBottomRight;
inline constexpr CornerFlags kRoundCornersAll =
    kRoundCornersTop | kRoundCornersBottom;

class Surface {
public:
    virtual ~Surface() = default;

    virtual void fillRect(Vec2 min, Vec2 max, Color color,
                          float rounding = 0.0f,
                          CornerFlags corners = kRoundCornersAll) = 0;
    virtual void strokeRect(Vec2 min, Vec2 max, Color color,
                            float rounding = 0.0f,
                            float thickness = 1.0f,
                            CornerFlags corners = kRoundCornersAll) = 0;
    virtual void fillRectMultiColor(Vec2 min, Vec2 max, Color topLeft,
                                    Color topRight, Color bottomRight,
                                    Color bottomLeft) = 0;
    virtual void fillCircle(Vec2 center, float radius, Color color,
                            int segments = 0) = 0;
    virtual void strokeCircle(Vec2 center, float radius, Color color,
                              int segments = 0,
                              float thickness = 1.0f) = 0;
    virtual void line(Vec2 a, Vec2 b, Color color,
                      float thickness = 1.0f) = 0;
    virtual void polyline(const Vec2* points, int count, Color color,
                          bool closed, float thickness = 1.0f) = 0;
    virtual void fillTriangle(Vec2 a, Vec2 b, Vec2 c, Color color) = 0;
    virtual void bezierCubic(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4,
                             Color color, float thickness = 1.0f,
                             int segments = 0) = 0;
    virtual void fillConcavePoly(const Vec2* points, int count,
                                 Color color) = 0;
    virtual void pathArcTo(Vec2 center, float radius, float aMin, float aMax,
                           int segments = 0) = 0;
    virtual void pathLineTo(Vec2 point) = 0;
    virtual void pathClear() = 0;
    virtual void pathStroke(Color color, bool closed,
                            float thickness = 1.0f) = 0;
    virtual void text(FontRef font, float sizePx, Vec2 pos, Color color,
                      const char* begin, const char* end = nullptr) = 0;
    virtual Vec2 measureText(FontRef font, float sizePx, const char* begin,
                             const char* end = nullptr) = 0;
    virtual void pushClip(Vec2 min, Vec2 max, bool intersect) = 0;
    virtual void popClip() = 0;
    // Textured quad (menu row icons, decoded images). tint multiplies the
    // texels; pass 0xFFFFFFFF to draw unmodified.
    virtual void image(TextureRef texture, Vec2 min, Vec2 max, Color tint,
                       Vec2 uvMin = {0.0f, 0.0f},
                       Vec2 uvMax = {1.0f, 1.0f}) = 0;

    // ONE continuous stroked strip with a colour per point (gradient wires,
    // conic rims). Unlike per-segment line() calls there are no joint
    // overlaps, so translucent strokes don't bead at the seams and the
    // anti-alias fringe runs once along the whole strip. `colors` has one
    // entry per point; `closed` joins the last point back to the first.
    // The default falls back to per-segment lines for backends without a
    // native strip.
    virtual void polylineGradient(const Vec2* points, int count,
                                  const Color* colors,
                                  float thickness = 1.0f, bool closed = false)
    {
        if (!points || !colors || count < 2)
            return;
        for (int i = 0; i + 1 < count; ++i)
            line(points[i], points[i + 1], colors[i], thickness);
        if (closed)
            line(points[count - 1], points[0], colors[count - 1], thickness);
    }
};

} // namespace snd::ui::draw
