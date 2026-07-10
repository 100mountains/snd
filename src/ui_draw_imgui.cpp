#include "ui_draw_imgui.h"

#include "imgui_internal.h" // ImDrawListSharedData: TexUvWhitePixel for PrimWriteVtx

#include <cfloat>
#include <cmath>
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

void ImGuiSurface::polylineGradient(const Vec2* points, int count,
                                    const Color* colors, float thickness,
                                    bool closed)
{
    if (!drawList_ || !points || !colors || count < 2)
        return;
    const float half = std::max(thickness, 1.0f) * 0.5f;
    const float aa = 1.0f; // fringe width
    const int spans = closed ? count : count - 1;

    // averaged joint normals (open ends keep their single segment normal)
    const auto segNormal = [&](int i) {
        const Vec2& p0 = points[i];
        const Vec2& p1 = points[(i + 1) % count];
        const float dx = p1.x - p0.x;
        const float dy = p1.y - p0.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f)
            return ImVec2(0.0f, 0.0f);
        return ImVec2(-dy / len, dx / len);
    };
    std::vector<ImVec2> normals((size_t)count);
    for (int i = 0; i < count; ++i) {
        ImVec2 n(0.0f, 0.0f);
        if (closed || i > 0) {
            const ImVec2 a = segNormal((i - 1 + count) % count);
            n.x += a.x;
            n.y += a.y;
        }
        if (closed || i < count - 1) {
            const ImVec2 b = segNormal(i);
            n.x += b.x;
            n.y += b.y;
        }
        const float len = std::sqrt(n.x * n.x + n.y * n.y);
        if (len > 1e-4f) {
            n.x /= len;
            n.y /= len;
        }
        normals[(size_t)i] = n;
    }

    ImDrawList* dl = drawList_;
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    dl->PrimReserve(spans * 18, count * 4);
    const unsigned int base = dl->_VtxCurrentIdx;
    for (int i = 0; i < count; ++i) {
        const ImVec2 p(points[i].x, points[i].y);
        const ImVec2 n = normals[(size_t)i];
        const ImU32 col = colors[i];
        const ImU32 colT = col & 0x00FFFFFFu; // fringe fades to alpha 0
        dl->PrimWriteVtx(ImVec2(p.x + n.x * half, p.y + n.y * half), uv, col);
        dl->PrimWriteVtx(ImVec2(p.x - n.x * half, p.y - n.y * half), uv, col);
        dl->PrimWriteVtx(ImVec2(p.x + n.x * (half + aa), p.y + n.y * (half + aa)),
                         uv, colT);
        dl->PrimWriteVtx(ImVec2(p.x - n.x * (half + aa), p.y - n.y * (half + aa)),
                         uv, colT);
    }
    const auto V = [&](int point, int which) {
        return (ImDrawIdx)(base + (unsigned int)(point * 4 + which));
    };
    for (int sIdx = 0; sIdx < spans; ++sIdx) {
        const int i = sIdx;
        const int j = (sIdx + 1) % count;
        dl->PrimWriteIdx(V(i, 0)); dl->PrimWriteIdx(V(j, 0)); dl->PrimWriteIdx(V(j, 1));
        dl->PrimWriteIdx(V(i, 0)); dl->PrimWriteIdx(V(j, 1)); dl->PrimWriteIdx(V(i, 1));
        dl->PrimWriteIdx(V(i, 2)); dl->PrimWriteIdx(V(j, 2)); dl->PrimWriteIdx(V(j, 0));
        dl->PrimWriteIdx(V(i, 2)); dl->PrimWriteIdx(V(j, 0)); dl->PrimWriteIdx(V(i, 0));
        dl->PrimWriteIdx(V(i, 1)); dl->PrimWriteIdx(V(j, 1)); dl->PrimWriteIdx(V(j, 3));
        dl->PrimWriteIdx(V(i, 1)); dl->PrimWriteIdx(V(j, 3)); dl->PrimWriteIdx(V(i, 3));
    }
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
    if (sizePx < 0.5f)
        return; // invisible; ImGui asserts on font_size <= 0
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
    if (sizePx < 0.5f)
        return {};
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

void ImGuiSurface::image(TextureRef texture, Vec2 min, Vec2 max, Color tint,
                         Vec2 uvMin, Vec2 uvMax)
{
    if (!drawList_ || !texture)
        return;
    drawList_->AddImage((ImTextureID)texture, toImVec2(min), toImVec2(max),
                        toImVec2(uvMin), toImVec2(uvMax), tint);
}

void ImGuiSurface::popClip()
{
    if (drawList_)
        drawList_->PopClipRect();
}

} // namespace snd::ui::draw
