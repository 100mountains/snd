// snd::ui SVG assets: parse + rasterize an SVG document to an RGBA bitmap
// (nanosvg -- zlib-licensed, header-only) and upload it to a GL texture for
// ImGui. Vector logos/icons stay crisp at any size/DPI. rasterizeSvg is
// GL-free (unit-testable, headless); loadSvgTexture needs a live GL context.

#include "snd/ui_types.h"

#include <cmath>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif
#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

namespace snd::ui {

SvgBitmap rasterizeSvg(const char* svgText, int heightPx, ImU32 tint)
{
    SvgBitmap out;
    if (!svgText || heightPx <= 0)
        return out;

    std::string buf(svgText); // nsvgParse mutates its input buffer in place
    NSVGimage* image = nsvgParse(buf.data(), "px", 96.0f);
    if (!image || image->width <= 0.0f || image->height <= 0.0f) {
        if (image)
            nsvgDelete(image);
        return out;
    }

    const float scale = (float)heightPx / image->height;
    int w = (int)std::lround((double)image->width * scale);
    const int h = heightPx;
    if (w < 1)
        w = 1;

    out.rgba.assign((size_t)w * (size_t)h * 4u, 0);
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    nsvgRasterize(rast, image, 0.0f, 0.0f, scale, out.rgba.data(), w, h, w * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    out.w = w;
    out.h = h;

    if (tint != 0) {
        // Multiply every texel -- recolour a monochrome glyph, or fade one out.
        const float tr = ((tint >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
        const float tg = ((tint >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
        const float tb = ((tint >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
        const float ta = ((tint >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
        for (size_t i = 0; i + 3 < out.rgba.size(); i += 4) {
            out.rgba[i + 0] = (unsigned char)(out.rgba[i + 0] * tr);
            out.rgba[i + 1] = (unsigned char)(out.rgba[i + 1] * tg);
            out.rgba[i + 2] = (unsigned char)(out.rgba[i + 2] * tb);
            out.rgba[i + 3] = (unsigned char)(out.rgba[i + 3] * ta);
        }
    }
    return out;
}

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

SvgBitmap decodeImage(const unsigned char* bytes, int byteCount)
{
    SvgBitmap bmp;
    if (!bytes || byteCount <= 0)
        return bmp;
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(bytes, byteCount, &w, &h, &comp, 4);
    if (!px)
        return bmp;
    bmp.rgba.assign(px, px + (size_t)w * (size_t)h * 4);
    bmp.w = w;
    bmp.h = h;
    stbi_image_free(px);
    return bmp;
}

SvgTexture loadImageTexture(const unsigned char* bytes, int byteCount)
{
    SvgBitmap bmp = decodeImage(bytes, byteCount);
    if (bmp.rgba.empty())
        return {};
    return loadTextureRGBA(bmp.rgba.data(), bmp.w, bmp.h);
}

SvgTexture loadTextureRGBA(const unsigned char* rgba, int w, int h)
{
    SvgTexture tex;
    if (!rgba || w <= 0 || h <= 0)
        return tex;

    GLint previousTexture = 0;
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);

    GLuint id = 0;
    glGenTextures(1, &id);
    if (id == 0) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
        glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
        return tex;
    }
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);

    tex.id = static_cast<draw::TextureRef>(id);
    tex.w = w;
    tex.h = h;
    return tex;
}

SvgTexture loadSvgTexture(const char* svgText, int heightPx, ImU32 tint)
{
    SvgBitmap bmp = rasterizeSvg(svgText, heightPx, tint);
    if (bmp.rgba.empty())
        return {};
    return loadTextureRGBA(bmp.rgba.data(), bmp.w, bmp.h);
}

void releaseTexture(draw::TextureRef id)
{
    if (id == 0)
        return;
    GLuint gl = (GLuint)id;
    GLint previousTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glDeleteTextures(1, &gl);
    if ((GLuint)previousTexture == gl)
        glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace snd::ui
