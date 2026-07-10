// Private Dear ImGui adapter for the renderer-neutral SND draw surface.
#pragma once

#include "snd/ui_draw.h"

#include "imgui.h"

namespace snd::ui::draw {

inline Vec2 toDrawVec2(const ImVec2& v) { return {v.x, v.y}; }
inline ImVec2 toImVec2(Vec2 v) { return ImVec2(v.x, v.y); }
inline FontRef fontRef(ImFont* font)
{
    return {reinterpret_cast<uintptr_t>(font)};
}
inline ImFont* imFont(FontRef font)
{
    return reinterpret_cast<ImFont*>(font.handle);
}

class ImGuiSurface final : public Surface {
public:
    explicit ImGuiSurface(ImDrawList* drawList) : drawList_(drawList) {}

    ImDrawList* drawList() const { return drawList_; }

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
    ImDrawList* drawList_ = nullptr;
};

} // namespace snd::ui::draw
