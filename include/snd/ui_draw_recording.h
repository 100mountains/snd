// Headless diagnostic recording surface for SND UI paint tests.
//
// This is not a production renderer. It records draw operations so selftests
// can assert geometry, colours, and operation ordering without a live graphics
// backend.
#pragma once

#include "snd/ui_draw.h"

#include <string>
#include <vector>

namespace snd::ui::draw {

struct RecordedOp {
    std::string name;
    std::vector<Vec2> points;
    std::vector<Color> colors;
    float a = 0.0f;
    float b = 0.0f;
    float c = 0.0f;
    bool flag = false;
    std::string text;
};

class RecordingSurface final : public Surface {
public:
    const std::vector<RecordedOp>& ops() const { return ops_; }
    void clear() { ops_.clear(); }

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
    void image(TextureRef texture, Vec2 min, Vec2 max, Color tint,
               Vec2 uvMin = {0.0f, 0.0f}, Vec2 uvMax = {1.0f, 1.0f}) override;

private:
    void push(RecordedOp op);

    std::vector<RecordedOp> ops_;
};

} // namespace snd::ui::draw
