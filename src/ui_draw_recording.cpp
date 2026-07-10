#include "snd/ui_draw_recording.h"

#include <cstring>
#include <utility>

namespace snd::ui::draw {

void RecordingSurface::push(RecordedOp op) { ops_.push_back(std::move(op)); }

void RecordingSurface::fillRect(Vec2 min, Vec2 max, Color color,
                                float rounding, CornerFlags corners)
{
    push({"fillRect", {min, max}, {color}, rounding, (float)corners});
}

void RecordingSurface::strokeRect(Vec2 min, Vec2 max, Color color,
                                  float rounding, float thickness,
                                  CornerFlags corners)
{
    RecordedOp op;
    op.name = "strokeRect";
    op.points = {min, max};
    op.colors = {color};
    op.a = rounding;
    op.b = thickness;
    op.c = (float)corners;
    push(std::move(op));
}

void RecordingSurface::fillRectMultiColor(Vec2 min, Vec2 max, Color topLeft,
                                          Color topRight, Color bottomRight,
                                          Color bottomLeft)
{
    push({"fillRectMultiColor", {min, max},
          {topLeft, topRight, bottomRight, bottomLeft}});
}

void RecordingSurface::fillCircle(Vec2 center, float radius, Color color,
                                  int segments)
{
    push({"fillCircle", {center}, {color}, radius, (float)segments});
}

void RecordingSurface::strokeCircle(Vec2 center, float radius, Color color,
                                    int segments, float thickness)
{
    push({"strokeCircle", {center}, {color}, radius, thickness,
          (float)segments});
}

void RecordingSurface::line(Vec2 a, Vec2 b, Color color, float thickness)
{
    push({"line", {a, b}, {color}, thickness});
}

void RecordingSurface::polyline(const Vec2* points, int count, Color color,
                                bool closed, float thickness)
{
    RecordedOp op;
    op.name = "polyline";
    if (points && count > 0)
        op.points.assign(points, points + count);
    op.colors = {color};
    op.a = thickness;
    op.flag = closed;
    push(std::move(op));
}

void RecordingSurface::fillTriangle(Vec2 a, Vec2 b, Vec2 c, Color color)
{
    push({"fillTriangle", {a, b, c}, {color}});
}

void RecordingSurface::bezierCubic(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4,
                                   Color color, float thickness, int segments)
{
    push({"bezierCubic", {p1, p2, p3, p4}, {color}, thickness,
          (float)segments});
}

void RecordingSurface::fillConcavePoly(const Vec2* points, int count,
                                       Color color)
{
    RecordedOp op;
    op.name = "fillConcavePoly";
    if (points && count > 0)
        op.points.assign(points, points + count);
    op.colors = {color};
    push(std::move(op));
}

void RecordingSurface::pathArcTo(Vec2 center, float radius, float aMin,
                                 float aMax, int segments)
{
    push({"pathArcTo", {center}, {}, radius, aMin, aMax,
          segments != 0});
}

void RecordingSurface::pathLineTo(Vec2 point)
{
    push({"pathLineTo", {point}});
}

void RecordingSurface::pathClear()
{
    push({"pathClear"});
}

void RecordingSurface::pathStroke(Color color, bool closed, float thickness)
{
    RecordedOp op;
    op.name = "pathStroke";
    op.colors = {color};
    op.a = thickness;
    op.flag = closed;
    push(std::move(op));
}

void RecordingSurface::text(FontRef, float sizePx, Vec2 pos, Color color,
                            const char* begin, const char* end)
{
    RecordedOp op;
    op.name = "text";
    op.points = {pos};
    op.colors = {color};
    op.a = sizePx;
    if (begin)
        op.text.assign(begin, end ? end : begin + std::strlen(begin));
    push(std::move(op));
}

Vec2 RecordingSurface::measureText(FontRef, float sizePx, const char* begin,
                                   const char* end)
{
    if (!begin || sizePx <= 0.0f)
        return {};
    const size_t len = (size_t)((end ? end : begin + std::strlen(begin)) - begin);
    return {(float)len * sizePx * 0.5f, sizePx};
}

void RecordingSurface::pushClip(Vec2 min, Vec2 max, bool intersect)
{
    RecordedOp op;
    op.name = "pushClip";
    op.points = {min, max};
    op.flag = intersect;
    push(std::move(op));
}

void RecordingSurface::popClip()
{
    push({"popClip"});
}

} // namespace snd::ui::draw
