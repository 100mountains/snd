// Private OpenGL adapter for snd::ui::draw::Surface.
#pragma once

#include "snd/ui_draw.h"
#include "ui_font_atlas_stb.h"

#include <vector>

namespace snd::ui::draw {

class OpenGLSurface final : public Surface {
public:
    explicit OpenGLSurface(StbFontAtlas& fonts);
    ~OpenGLSurface() override;

    bool init();
    void destroyGl();
    void beginFrame(Vec2 logicalSize, int framebufferWidth,
                    int framebufferHeight);
    void endFrame();

    void fillRect(Vec2 min, Vec2 max, Color color,
                  float rounding = 0.0f,
                  CornerFlags corners = kRoundCornersAll) override;
    void strokeRect(Vec2 min, Vec2 max, Color color,
                    float rounding = 0.0f,
                    float thickness = 1.0f,
                    CornerFlags corners = kRoundCornersAll) override;
    void fillRectMultiColor(Vec2 min, Vec2 max, Color topLeft, Color topRight,
                            Color bottomRight, Color bottomLeft) override;
    void fillCircle(Vec2 center, float radius, Color color,
                    int segments = 0) override;
    void strokeCircle(Vec2 center, float radius, Color color,
                      int segments = 0, float thickness = 1.0f) override;
    void line(Vec2 a, Vec2 b, Color color, float thickness = 1.0f) override;
    void polyline(const Vec2* points, int count, Color color, bool closed,
                  float thickness = 1.0f) override;
    void fillTriangle(Vec2 a, Vec2 b, Vec2 c, Color color) override;
    void bezierCubic(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, Color color,
                     float thickness = 1.0f, int segments = 0) override;
    void fillConcavePoly(const Vec2* points, int count, Color color) override;
    void pathArcTo(Vec2 center, float radius, float aMin, float aMax,
                   int segments = 0) override;
    void pathLineTo(Vec2 point) override;
    void pathClear() override;
    void pathStroke(Color color, bool closed,
                    float thickness = 1.0f) override;
    void text(FontRef font, float sizePx, Vec2 pos, Color color,
              const char* begin, const char* end = nullptr) override;
    Vec2 measureText(FontRef font, float sizePx, const char* begin,
                     const char* end = nullptr) override;
    void pushClip(Vec2 min, Vec2 max, bool intersect) override;
    void popClip() override;

private:
    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        Color color = 0;
    };

    struct ClipRect {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
    };

    void drawSolid(const std::vector<Vertex>& vertices);
    void drawTextured(const std::vector<StbFontAtlas::TextVertex>& vertices);
    void drawRaw(const void* vertices, int count, int stride, bool textured);
    void fillConvexPoly(const std::vector<Vec2>& points, Color color);
    void applyClip() const;
    static Vertex makeVertex(Vec2 p, Color color,
                             float u = 0.0f, float v = 0.0f);
    std::vector<Vec2> roundedRectPath(Vec2 min, Vec2 max, float rounding,
                                      CornerFlags corners) const;

    StbFontAtlas& fonts_;
    unsigned int program_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int attribPosition_ = -1;
    int attribTexCoord_ = -1;
    int attribColor_ = -1;
    int uniformTexture_ = -1;
    int uniformProjMtx_ = -1;
    int uniformUseTexture_ = -1;
    Vec2 logicalSize_;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;
    std::vector<ClipRect> clipStack_;
    std::vector<Vec2> path_;
};

} // namespace snd::ui::draw
