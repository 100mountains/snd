#include "ui_draw_gl.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace snd::ui::draw {

namespace {

constexpr float kPi = 3.14159265358979323846f;

const char* glslVersion()
{
#if defined(__APPLE__)
    return "#version 150\n";
#else
    return "#version 130\n";
#endif
}

unsigned int compileShader(unsigned int type, const char* source)
{
    const unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

unsigned int createProgram()
{
    const char* vertexBody =
        "in vec2 Position;\n"
        "in vec2 TexCoord;\n"
        "in vec4 Color;\n"
        "uniform mat4 ProjMtx;\n"
        "out vec2 Frag_UV;\n"
        "out vec4 Frag_Color;\n"
        "void main() {\n"
        "  Frag_UV = TexCoord;\n"
        "  Frag_Color = Color;\n"
        "  gl_Position = ProjMtx * vec4(Position.xy, 0, 1);\n"
        "}\n";
    const char* fragmentBody =
        "uniform sampler2D Texture;\n"
        "uniform int TextureMode;\n"
        "in vec2 Frag_UV;\n"
        "in vec4 Frag_Color;\n"
        "out vec4 Out_Color;\n"
        "void main() {\n"
        "  vec4 texel = TextureMode != 0 ? texture(Texture, Frag_UV) : vec4(1.0);\n"
        "  if (TextureMode == 1)\n"
        "    Out_Color = vec4(Frag_Color.rgb, Frag_Color.a * texel.r);\n"
        "  else\n"
        "    Out_Color = Frag_Color * texel;\n"
        "}\n";
    std::string vertexSource = std::string(glslVersion()) + vertexBody;
    std::string fragmentSource = std::string(glslVersion()) + fragmentBody;
    const unsigned int vert = compileShader(GL_VERTEX_SHADER, vertexSource.c_str());
    const unsigned int frag = compileShader(GL_FRAGMENT_SHADER, fragmentSource.c_str());
    if (!vert || !frag) {
        if (vert)
            glDeleteShader(vert);
        if (frag)
            glDeleteShader(frag);
        return 0;
    }

    const unsigned int program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glBindAttribLocation(program, 0, "Position");
    glBindAttribLocation(program, 1, "TexCoord");
    glBindAttribLocation(program, 2, "Color");
    glLinkProgram(program);
    glDetachShader(program, vert);
    glDetachShader(program, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);
    int ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

float length(Vec2 v)
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

Vec2 lerp(Vec2 a, Vec2 b, float t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

float cross(Vec2 a, Vec2 b, Vec2 c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

float polygonArea(const Vec2* points, int count)
{
    float area = 0.0f;
    for (int i = 0; i < count; ++i) {
        const Vec2 a = points[i];
        const Vec2 b = points[(i + 1) % count];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5f;
}

bool pointInTriangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c)
{
    const float c1 = cross(a, b, p);
    const float c2 = cross(b, c, p);
    const float c3 = cross(c, a, p);
    const bool neg = c1 < 0.0f || c2 < 0.0f || c3 < 0.0f;
    const bool pos = c1 > 0.0f || c2 > 0.0f || c3 > 0.0f;
    return !(neg && pos);
}

int circleSegments(float radius, int requested)
{
    if (requested > 0)
        return requested;
    return std::clamp((int)std::ceil(radius * 0.75f), 16, 96);
}

} // namespace

OpenGLSurface::OpenGLSurface(StbFontAtlas& fonts) : fonts_(fonts) {}

OpenGLSurface::~OpenGLSurface()
{
    destroyGl();
}

OpenGLSurface::Vertex OpenGLSurface::makeVertex(Vec2 p, Color color,
                                                float u, float v)
{
    return {p.x, p.y, u, v, color};
}

bool OpenGLSurface::init()
{
    if (program_ != 0)
        return true;

    program_ = createProgram();
    if (program_ == 0)
        return false;
    attribPosition_ = glGetAttribLocation(program_, "Position");
    attribTexCoord_ = glGetAttribLocation(program_, "TexCoord");
    attribColor_ = glGetAttribLocation(program_, "Color");
    uniformTexture_ = glGetUniformLocation(program_, "Texture");
    uniformProjMtx_ = glGetUniformLocation(program_, "ProjMtx");
    uniformTextureMode_ = glGetUniformLocation(program_, "TextureMode");

    glGenBuffers(1, &vbo_);
    glGenVertexArrays(1, &vao_);
    return vao_ != 0 && vbo_ != 0;
}

void OpenGLSurface::destroyGl()
{
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

void OpenGLSurface::beginFrame(Vec2 logicalSize, int framebufferWidth,
                               int framebufferHeight)
{
    logicalSize_ = {std::max(1.0f, logicalSize.x),
                    std::max(1.0f, logicalSize.y)};
    framebufferWidth_ = std::max(1, framebufferWidth);
    framebufferHeight_ = std::max(1, framebufferHeight);
    scaleX_ = (float)framebufferWidth_ / logicalSize_.x;
    scaleY_ = (float)framebufferHeight_ / logicalSize_.y;
    path_.clear();
    clipStack_.clear();

    init();
    fonts_.upload();

    glViewport(0, 0, framebufferWidth_, framebufferHeight_);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    applyClip();
}

void OpenGLSurface::endFrame()
{
    path_.clear();
    clipStack_.clear();
    glDisable(GL_SCISSOR_TEST);
}

void OpenGLSurface::drawRaw(const void* vertices, int count, int stride,
                            TextureMode textureMode, unsigned int texture)
{
    if (program_ == 0 || vao_ == 0 || vbo_ == 0 || count <= 0 || !vertices)
        return;

    const float L = 0.0f;
    const float R = logicalSize_.x;
    const float T = 0.0f;
    const float B = logicalSize_.y;
    const float ortho[4][4] = {
        {2.0f / (R - L), 0.0f, 0.0f, 0.0f},
        {0.0f, 2.0f / (T - B), 0.0f, 0.0f},
        {0.0f, 0.0f, -1.0f, 0.0f},
        {(R + L) / (L - R), (T + B) / (B - T), 0.0f, 1.0f},
    };

    glUseProgram(program_);
    glUniform1i(uniformTexture_, 0);
    glUniform1i(uniformTextureMode_, (int)textureMode);
    glUniformMatrix4fv(uniformProjMtx_, 1, GL_FALSE, &ortho[0][0]);
    if (textureMode != TextureMode::None) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture ? texture : fonts_.textureId());
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, (std::size_t)count * (std::size_t)stride,
                 vertices, GL_STREAM_DRAW);
    glEnableVertexAttribArray((unsigned int)attribPosition_);
    glEnableVertexAttribArray((unsigned int)attribTexCoord_);
    glEnableVertexAttribArray((unsigned int)attribColor_);
    glVertexAttribPointer((unsigned int)attribPosition_, 2, GL_FLOAT, GL_FALSE,
                          stride, (void*)0);
    glVertexAttribPointer((unsigned int)attribTexCoord_, 2, GL_FLOAT, GL_FALSE,
                          stride, (void*)(sizeof(float) * 2));
    glVertexAttribPointer((unsigned int)attribColor_, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          stride, (void*)(sizeof(float) * 4));
    glDrawArrays(GL_TRIANGLES, 0, count);
    glDisableVertexAttribArray((unsigned int)attribPosition_);
    glDisableVertexAttribArray((unsigned int)attribTexCoord_);
    glDisableVertexAttribArray((unsigned int)attribColor_);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    if (textureMode != TextureMode::None)
        glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void OpenGLSurface::image(TextureRef texture, Vec2 mn, Vec2 mx, Color tint,
                          Vec2 uvMin, Vec2 uvMax)
{
    if (!texture)
        return;
    const StbFontAtlas::TextVertex v[6] = {
        {mn.x, mn.y, uvMin.x, uvMin.y, tint},
        {mx.x, mn.y, uvMax.x, uvMin.y, tint},
        {mx.x, mx.y, uvMax.x, uvMax.y, tint},
        {mn.x, mn.y, uvMin.x, uvMin.y, tint},
        {mx.x, mx.y, uvMax.x, uvMax.y, tint},
        {mn.x, mx.y, uvMin.x, uvMax.y, tint},
    };
    drawRaw(v, 6, (int)sizeof(StbFontAtlas::TextVertex), TextureMode::Rgba,
            (unsigned int)texture);
}

void OpenGLSurface::drawSolid(const std::vector<Vertex>& vertices)
{
    drawRaw(vertices.data(), (int)vertices.size(), (int)sizeof(Vertex),
            TextureMode::None);
}

void OpenGLSurface::drawTextured(
    const std::vector<StbFontAtlas::TextVertex>& vertices)
{
    drawRaw(vertices.data(), (int)vertices.size(),
            (int)sizeof(StbFontAtlas::TextVertex), TextureMode::AlphaMask);
}

void OpenGLSurface::applyClip() const
{
    ClipRect clip{0.0f, 0.0f, logicalSize_.x, logicalSize_.y};
    if (!clipStack_.empty())
        clip = clipStack_.back();
    const int x = (int)std::floor(clip.x0 * scaleX_);
    const int y = (int)std::floor((logicalSize_.y - clip.y1) * scaleY_);
    const int w = (int)std::ceil(std::max(0.0f, clip.x1 - clip.x0) * scaleX_);
    const int h = (int)std::ceil(std::max(0.0f, clip.y1 - clip.y0) * scaleY_);
    glScissor(x, y, w, h);
}

std::vector<Vec2> OpenGLSurface::roundedRectPath(Vec2 mn, Vec2 mx,
                                                 float rounding,
                                                 CornerFlags corners) const
{
    if (mx.x < mn.x)
        std::swap(mx.x, mn.x);
    if (mx.y < mn.y)
        std::swap(mx.y, mn.y);
    const float r = std::clamp(rounding, 0.0f,
                               std::min((mx.x - mn.x), (mx.y - mn.y)) * 0.5f);
    std::vector<Vec2> pts;
    if (r <= 0.0f || corners == 0) {
        pts.push_back({mn.x, mn.y});
        pts.push_back({mx.x, mn.y});
        pts.push_back({mx.x, mx.y});
        pts.push_back({mn.x, mx.y});
        return pts;
    }

    auto addArc = [&](Vec2 center, float a0, float a1, bool enabled) {
        if (!enabled) {
            pts.push_back({center.x + std::cos(a1) * r,
                           center.y + std::sin(a1) * r});
            return;
        }
        const int seg = std::max(3, (int)std::ceil(r * 0.45f));
        for (int i = 0; i <= seg; ++i) {
            const float t = (float)i / (float)seg;
            const float a = a0 + (a1 - a0) * t;
            pts.push_back({center.x + std::cos(a) * r,
                           center.y + std::sin(a) * r});
        }
    };

    addArc({mx.x - r, mn.y + r}, -kPi * 0.5f, 0.0f,
           (corners & kRoundCornerTopRight) != 0);
    addArc({mx.x - r, mx.y - r}, 0.0f, kPi * 0.5f,
           (corners & kRoundCornerBottomRight) != 0);
    addArc({mn.x + r, mx.y - r}, kPi * 0.5f, kPi,
           (corners & kRoundCornerBottomLeft) != 0);
    addArc({mn.x + r, mn.y + r}, kPi, kPi * 1.5f,
           (corners & kRoundCornerTopLeft) != 0);
    return pts;
}

void OpenGLSurface::fillConvexPoly(const std::vector<Vec2>& points, Color color)
{
    if (points.size() < 3)
        return;
    std::vector<Vertex> verts;
    verts.reserve((points.size() - 2) * 3);
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        verts.push_back(makeVertex(points[0], color));
        verts.push_back(makeVertex(points[i], color));
        verts.push_back(makeVertex(points[i + 1], color));
    }
    drawSolid(verts);
}

void OpenGLSurface::fillRect(Vec2 min, Vec2 max, Color color, float rounding,
                             CornerFlags corners)
{
    fillConvexPoly(roundedRectPath(min, max, rounding, corners), color);
}

void OpenGLSurface::strokeRect(Vec2 min, Vec2 max, Color color,
                               float rounding, float thickness,
                               CornerFlags corners)
{
    const std::vector<Vec2> pts = roundedRectPath(min, max, rounding, corners);
    polyline(pts.data(), (int)pts.size(), color, true, thickness);
}

void OpenGLSurface::fillRectMultiColor(Vec2 mn, Vec2 mx, Color tl, Color tr,
                                       Color br, Color bl)
{
    std::vector<Vertex> verts;
    verts.reserve(6);
    verts.push_back(makeVertex(mn, tl));
    verts.push_back(makeVertex({mx.x, mn.y}, tr));
    verts.push_back(makeVertex(mx, br));
    verts.push_back(makeVertex(mn, tl));
    verts.push_back(makeVertex(mx, br));
    verts.push_back(makeVertex({mn.x, mx.y}, bl));
    drawSolid(verts);
}

void OpenGLSurface::fillCircle(Vec2 center, float radius, Color color,
                               int segments)
{
    if (radius <= 0.0f)
        return;
    segments = circleSegments(radius, segments);
    std::vector<Vertex> verts;
    verts.reserve((std::size_t)segments * 3);
    for (int i = 0; i < segments; ++i) {
        const float a0 = (float)i / (float)segments * kPi * 2.0f;
        const float a1 = (float)(i + 1) / (float)segments * kPi * 2.0f;
        verts.push_back(makeVertex(center, color));
        verts.push_back(makeVertex({center.x + std::cos(a0) * radius,
                                center.y + std::sin(a0) * radius}, color));
        verts.push_back(makeVertex({center.x + std::cos(a1) * radius,
                                center.y + std::sin(a1) * radius}, color));
    }
    drawSolid(verts);
}

void OpenGLSurface::strokeCircle(Vec2 center, float radius, Color color,
                                 int segments, float thickness)
{
    if (radius <= 0.0f)
        return;
    segments = circleSegments(radius, segments);
    std::vector<Vec2> pts;
    pts.reserve((std::size_t)segments);
    for (int i = 0; i < segments; ++i) {
        const float a = (float)i / (float)segments * kPi * 2.0f;
        pts.push_back({center.x + std::cos(a) * radius,
                       center.y + std::sin(a) * radius});
    }
    polyline(pts.data(), (int)pts.size(), color, true, thickness);
}

void OpenGLSurface::line(Vec2 a, Vec2 b, Color color, float thickness)
{
    const Vec2 d{b.x - a.x, b.y - a.y};
    const float len = length(d);
    if (len <= 0.0001f)
        return;
    const float half = std::max(1.0f, thickness) * 0.5f;
    const Vec2 n{-d.y / len * half, d.x / len * half};
    std::vector<Vertex> verts;
    verts.reserve(6);
    verts.push_back(makeVertex({a.x + n.x, a.y + n.y}, color));
    verts.push_back(makeVertex({b.x + n.x, b.y + n.y}, color));
    verts.push_back(makeVertex({b.x - n.x, b.y - n.y}, color));
    verts.push_back(makeVertex({a.x + n.x, a.y + n.y}, color));
    verts.push_back(makeVertex({b.x - n.x, b.y - n.y}, color));
    verts.push_back(makeVertex({a.x - n.x, a.y - n.y}, color));
    drawSolid(verts);
}

void OpenGLSurface::polyline(const Vec2* points, int count, Color color,
                             bool closed, float thickness)
{
    if (!points || count < 2)
        return;
    for (int i = 0; i + 1 < count; ++i)
        line(points[i], points[i + 1], color, thickness);
    if (closed)
        line(points[count - 1], points[0], color, thickness);
}

void OpenGLSurface::fillTriangle(Vec2 a, Vec2 b, Vec2 c, Color color)
{
    std::vector<Vertex> verts{makeVertex(a, color), makeVertex(b, color), makeVertex(c, color)};
    drawSolid(verts);
}

void OpenGLSurface::bezierCubic(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4,
                                Color color, float thickness, int segments)
{
    segments = segments > 0 ? segments : 32;
    std::vector<Vec2> pts;
    pts.reserve((std::size_t)segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const float t = (float)i / (float)segments;
        const Vec2 a = lerp(p1, p2, t);
        const Vec2 b = lerp(p2, p3, t);
        const Vec2 c = lerp(p3, p4, t);
        pts.push_back(lerp(lerp(a, b, t), lerp(b, c, t), t));
    }
    polyline(pts.data(), (int)pts.size(), color, false, thickness);
}

void OpenGLSurface::fillConcavePoly(const Vec2* points, int count, Color color)
{
    if (!points || count < 3)
        return;
    std::vector<int> indices;
    indices.reserve((std::size_t)count);
    for (int i = 0; i < count; ++i)
        indices.push_back(i);

    const float orientation = polygonArea(points, count) >= 0.0f ? 1.0f : -1.0f;
    std::vector<Vertex> verts;
    verts.reserve((std::size_t)(count - 2) * 3);
    int guard = 0;
    while (indices.size() > 3 && guard++ < count * count) {
        bool clipped = false;
        for (std::size_t i = 0; i < indices.size(); ++i) {
            const int i0 = indices[(i + indices.size() - 1) % indices.size()];
            const int i1 = indices[i];
            const int i2 = indices[(i + 1) % indices.size()];
            const Vec2 a = points[i0];
            const Vec2 b = points[i1];
            const Vec2 c = points[i2];
            if (orientation * cross(a, b, c) <= 0.0f)
                continue;
            bool contains = false;
            for (int idx : indices) {
                if (idx == i0 || idx == i1 || idx == i2)
                    continue;
                if (pointInTriangle(points[idx], a, b, c)) {
                    contains = true;
                    break;
                }
            }
            if (contains)
                continue;
            verts.push_back(makeVertex(a, color));
            verts.push_back(makeVertex(b, color));
            verts.push_back(makeVertex(c, color));
            indices.erase(indices.begin() + (std::ptrdiff_t)i);
            clipped = true;
            break;
        }
        if (!clipped)
            break;
    }

    if (indices.size() == 3) {
        verts.push_back(makeVertex(points[indices[0]], color));
        verts.push_back(makeVertex(points[indices[1]], color));
        verts.push_back(makeVertex(points[indices[2]], color));
    }
    if (verts.empty()) {
        std::vector<Vec2> fallback(points, points + count);
        fillConvexPoly(fallback, color);
        return;
    }
    drawSolid(verts);
}

void OpenGLSurface::pathArcTo(Vec2 center, float radius, float aMin,
                              float aMax, int segments)
{
    if (radius <= 0.0f)
        return;
    segments = segments > 0 ? segments
                            : std::max(6, (int)std::ceil(std::abs(aMax - aMin) *
                                                         radius * 0.18f));
    for (int i = 0; i <= segments; ++i) {
        const float t = (float)i / (float)segments;
        const float a = aMin + (aMax - aMin) * t;
        path_.push_back({center.x + std::cos(a) * radius,
                         center.y + std::sin(a) * radius});
    }
}

void OpenGLSurface::pathLineTo(Vec2 point)
{
    path_.push_back(point);
}

void OpenGLSurface::pathClear()
{
    path_.clear();
}

void OpenGLSurface::pathStroke(Color color, bool closed, float thickness)
{
    polyline(path_.data(), (int)path_.size(), color, closed, thickness);
    path_.clear();
}

void OpenGLSurface::text(FontRef font, float sizePx, Vec2 pos, Color color,
                         const char* begin, const char* end)
{
    std::vector<StbFontAtlas::TextVertex> verts;
    fonts_.appendText(verts, font, sizePx, pos, color, begin, end);
    drawTextured(verts);
}

Vec2 OpenGLSurface::measureText(FontRef font, float sizePx, const char* begin,
                                const char* end)
{
    return fonts_.measure(font, sizePx, begin, end);
}

void OpenGLSurface::pushClip(Vec2 min, Vec2 max, bool intersect)
{
    ClipRect next{std::min(min.x, max.x), std::min(min.y, max.y),
                  std::max(min.x, max.x), std::max(min.y, max.y)};
    if (intersect && !clipStack_.empty()) {
        const ClipRect& prev = clipStack_.back();
        next.x0 = std::max(next.x0, prev.x0);
        next.y0 = std::max(next.y0, prev.y0);
        next.x1 = std::min(next.x1, prev.x1);
        next.y1 = std::min(next.y1, prev.y1);
    }
    clipStack_.push_back(next);
    applyClip();
}

void OpenGLSurface::popClip()
{
    if (!clipStack_.empty())
        clipStack_.pop_back();
    applyClip();
}

} // namespace snd::ui::draw
