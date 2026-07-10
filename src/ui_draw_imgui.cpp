#include "ui_draw_imgui.h"

#include <cfloat>
#include <vector>

namespace snd::ui::draw {

namespace {

ImDrawFlags toImDrawCornerFlags(CornerFlags corners)
{
    if (corners == 0)
        return ImDrawFlags_RoundCornersNone;
    if ((corners & kRoundCornersAll) == kRoundCornersAll)
        return ImDrawFlags_RoundCornersAll;

    ImDrawFlags flags = 0;
    if (corners & kRoundCornerTopLeft)
        flags |= ImDrawFlags_RoundCornersTopLeft;
    if (corners & kRoundCornerTopRight)
        flags |= ImDrawFlags_RoundCornersTopRight;
    if (corners & kRoundCornerBottomRight)
        flags |= ImDrawFlags_RoundCornersBottomRight;
    if (corners & kRoundCornerBottomLeft)
        flags |= ImDrawFlags_RoundCornersBottomLeft;
    return flags;
}

} // namespace

void ImGuiSurface::fillRect(Vec2 min, Vec2 max, Color color, float rounding,
                            CornerFlags corners)
{
    if (drawList_)
        drawList_->AddRectFilled(toImVec2(min), toImVec2(max), color, rounding,
                                 toImDrawCornerFlags(corners));
}

void ImGuiSurface::strokeRect(Vec2 min, Vec2 max, Color color,
                              float rounding, float thickness,
                              CornerFlags corners)
{
    if (drawList_)
        drawList_->AddRect(toImVec2(min), toImVec2(max), color, rounding,
                           toImDrawCornerFlags(corners), thickness);
}

void ImGuiSurface::fillRectMultiColor(Vec2 min, Vec2 max, Color topLeft,
                                      Color topRight, Color bottomRight,
                                      Color bottomLeft)
{
    if (drawList_)
        drawList_->AddRectFilledMultiColor(toImVec2(min), toImVec2(max),
                                           topLeft, topRight, bottomRight,
                                           bottomLeft);
}

void ImGuiSurface::fillCircle(Vec2 center, float radius, Color color,
                              int segments)
{
    if (drawList_)
        drawList_->AddCircleFilled(toImVec2(center), radius, color, segments);
}

void ImGuiSurface::strokeCircle(Vec2 center, float radius, Color color,
                                int segments, float thickness)
{
    if (drawList_)
        drawList_->AddCircle(toImVec2(center), radius, color, segments,
                             thickness);
}

void ImGuiSurface::line(Vec2 a, Vec2 b, Color color, float thickness)
{
    if (drawList_)
        drawList_->AddLine(toImVec2(a), toImVec2(b), color, thickness);
}

void ImGuiSurface::polyline(const Vec2* points, int count, Color color,
                            bool closed, float thickness)
{
    if (!drawList_ || !points || count <= 0)
        return;
    std::vector<ImVec2> converted;
    converted.reserve((size_t)count);
    for (int i = 0; i < count; ++i)
        converted.push_back(toImVec2(points[i]));
    drawList_->AddPolyline(converted.data(), count, color,
                           closed ? ImDrawFlags_Closed : 0, thickness);
}

void ImGuiSurface::fillTriangle(Vec2 a, Vec2 b, Vec2 c, Color color)
{
    if (drawList_)
        drawList_->AddTriangleFilled(toImVec2(a), toImVec2(b), toImVec2(c),
                                     color);
}

void ImGuiSurface::bezierCubic(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4,
                               Color color, float thickness, int segments)
{
    if (drawList_)
        drawList_->AddBezierCubic(toImVec2(p1), toImVec2(p2), toImVec2(p3),
                                  toImVec2(p4), color, thickness, segments);
}

void ImGuiSurface::fillConcavePoly(const Vec2* points, int count, Color color)
{
    if (!drawList_ || !points || count <= 0)
        return;
    std::vector<ImVec2> converted;
    converted.reserve((size_t)count);
    for (int i = 0; i < count; ++i)
        converted.push_back(toImVec2(points[i]));
    drawList_->AddConcavePolyFilled(converted.data(), count, color);
}

void ImGuiSurface::pathArcTo(Vec2 center, float radius, float aMin, float aMax,
                             int segments)
{
    if (drawList_)
        drawList_->PathArcTo(toImVec2(center), radius, aMin, aMax, segments);
}

void ImGuiSurface::pathLineTo(Vec2 point)
{
    if (drawList_)
        drawList_->PathLineTo(toImVec2(point));
}

void ImGuiSurface::pathClear()
{
    if (drawList_)
        drawList_->PathClear();
}

void ImGuiSurface::pathStroke(Color color, bool closed, float thickness)
{
    if (drawList_)
        drawList_->PathStroke(color, closed ? ImDrawFlags_Closed : 0, thickness);
}

void ImGuiSurface::text(FontRef font, float sizePx, Vec2 pos, Color color,
                        const char* begin, const char* end)
{
    if (!drawList_ || !begin)
        return;
    ImFont* f = imFont(font);
    if (f && sizePx > 0.0f)
        drawList_->AddText(f, sizePx, toImVec2(pos), color, begin, end);
    else
        drawList_->AddText(toImVec2(pos), color, begin, end);
}

Vec2 ImGuiSurface::measureText(FontRef font, float sizePx, const char* begin,
                               const char* end)
{
    if (!begin)
        return {};
    ImFont* f = imFont(font);
    ImVec2 measured;
    if (f && sizePx > 0.0f)
        measured = f->CalcTextSizeA(sizePx, FLT_MAX, 0.0f, begin, end);
    else
        measured = ImGui::CalcTextSize(begin, end);
    return toDrawVec2(measured);
}

void ImGuiSurface::pushClip(Vec2 min, Vec2 max, bool intersect)
{
    if (drawList_)
        drawList_->PushClipRect(toImVec2(min), toImVec2(max), intersect);
}

void ImGuiSurface::popClip()
{
    if (drawList_)
        drawList_->PopClipRect();
}

} // namespace snd::ui::draw
