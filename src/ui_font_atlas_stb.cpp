#include "ui_font_atlas_stb.h"

#include "snd/icons/IconsLucide.h"
#include "snd/icons/IconsMaterialDesign.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace snd::ui::draw {

namespace {

constexpr int kAtlasWidth = 4096;
constexpr int kAtlasHeight = 4096;
constexpr float kBaseSize = 13.0f;
constexpr float kIconSize = 16.0f;

struct LoadedFont {
    std::vector<unsigned char> bytes;
    std::filesystem::path path;
};

std::filesystem::path sourceRoot()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path();
}

std::vector<std::filesystem::path> candidateFontPaths(const char* name)
{
    std::vector<std::filesystem::path> paths;
    // An absolute path (SND_UI_FONT pointing at a system/user font) wins.
    if (std::filesystem::path direct(name); direct.is_absolute())
        paths.push_back(direct);
    paths.push_back(sourceRoot() / "third_party" / "fonts" / name);
    paths.push_back(std::filesystem::current_path() / "third_party" / "fonts" / name);
    paths.push_back(std::filesystem::current_path().parent_path() /
                    "third_party" / "fonts" / name);
    return paths;
}

bool loadFontFile(const char* name, LoadedFont& out, std::string* error)
{
    for (const auto& path : candidateFontPaths(name)) {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            continue;
        out.bytes.assign(std::istreambuf_iterator<char>(in),
                         std::istreambuf_iterator<char>());
        out.path = path;
        if (!out.bytes.empty())
            return true;
    }
    if (error)
        *error = std::string("SND UI font not found: ") + name;
    return false;
}

uint32_t nextUtf8(const char*& p, const char* end)
{
    if (!p || (end && p >= end) || *p == '\0')
        return 0;
    const unsigned char c0 = (unsigned char)*p++;
    if (c0 < 0x80)
        return c0;
    if ((c0 >> 5) == 0x6 && (!end || p < end)) {
        const unsigned char c1 = (unsigned char)*p++;
        return ((uint32_t)(c0 & 0x1Fu) << 6) | (uint32_t)(c1 & 0x3Fu);
    }
    if ((c0 >> 4) == 0xE && (!end || p + 1 < end)) {
        const unsigned char c1 = (unsigned char)*p++;
        const unsigned char c2 = (unsigned char)*p++;
        return ((uint32_t)(c0 & 0x0Fu) << 12) |
               ((uint32_t)(c1 & 0x3Fu) << 6) |
               (uint32_t)(c2 & 0x3Fu);
    }
    if ((c0 >> 3) == 0x1E && (!end || p + 2 < end)) {
        const unsigned char c1 = (unsigned char)*p++;
        const unsigned char c2 = (unsigned char)*p++;
        const unsigned char c3 = (unsigned char)*p++;
        return ((uint32_t)(c0 & 0x07u) << 18) |
               ((uint32_t)(c1 & 0x3Fu) << 12) |
               ((uint32_t)(c2 & 0x3Fu) << 6) |
               (uint32_t)(c3 & 0x3Fu);
    }
    return 0xFFFDu;
}

bool packFont(stbtt_pack_context& ctx, const LoadedFont& loaded, float sizePx,
              int firstCodepoint, int glyphCount, StbFontAtlas::Font& out,
              std::string* error)
{
    std::vector<stbtt_packedchar> packed((std::size_t)glyphCount);
    if (!stbtt_PackFontRange(&ctx, loaded.bytes.data(), 0, sizePx,
                             firstCodepoint, glyphCount, packed.data())) {
        if (error)
            *error = "SND UI font atlas overflow while packing " +
                     loaded.path.filename().string();
        return false;
    }

    out.sizePx = sizePx;
    stbtt_fontinfo info;
    if (stbtt_InitFont(&info, loaded.bytes.data(), 0)) {
        int ascent = 0;
        int descent = 0;
        int lineGap = 0;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
        out.ascent = (float)ascent * stbtt_ScaleForPixelHeight(&info, sizePx);
        (void)descent;
        (void)lineGap;
    } else {
        out.ascent = sizePx;
    }
    out.firstCodepoint = firstCodepoint;
    out.glyphCount = glyphCount;
    out.glyphs.assign((std::size_t)glyphCount, {});
    for (int i = 0; i < glyphCount; ++i) {
        const stbtt_packedchar& pc = packed[(std::size_t)i];
        StbFontAtlas::Glyph glyph;
        glyph.x0 = pc.xoff;
        glyph.y0 = pc.yoff;
        glyph.x1 = pc.xoff2;
        glyph.y1 = pc.yoff2;
        glyph.s0 = (float)pc.x0 / (float)kAtlasWidth;
        glyph.t0 = (float)pc.y0 / (float)kAtlasHeight;
        glyph.s1 = (float)pc.x1 / (float)kAtlasWidth;
        glyph.t1 = (float)pc.y1 / (float)kAtlasHeight;
        glyph.advance = pc.xadvance;
        glyph.valid = pc.x1 > pc.x0 && pc.y1 > pc.y0;
        out.glyphs[(std::size_t)i] = glyph;
    }
    return true;
}

const StbFontAtlas::Glyph* glyphInFont(const StbFontAtlas::Font& font,
                                       uint32_t codepoint)
{
    const int idx = (int)codepoint - font.firstCodepoint;
    if (idx < 0 || idx >= font.glyphCount)
        return nullptr;
    const StbFontAtlas::Glyph& glyph = font.glyphs[(std::size_t)idx];
    return glyph.valid ? &glyph : nullptr;
}

} // namespace

bool StbFontAtlas::build(std::string* error)
{
    LoadedFont base;
    LoadedFont material;
    LoadedFont lucide;
    // SND_UI_FONT overrides the UI text face: a filename in third_party/fonts,
    // or an absolute path to any .ttf/.otf. SND_UI_FONT_SIZE overrides the bake
    // size (a thinner face usually wants a point or two more than the 13px the
    // default bitmap face is drawn for). Both exist so a face can be tried
    // without a rebuild; unset = the built-in default.
    const char* faceEnv = std::getenv("SND_UI_FONT");
    const char* face = faceEnv && faceEnv[0] ? faceEnv : "ProggyClean.ttf";
    float baseSize = kBaseSize;
    if (const char* sizeEnv = std::getenv("SND_UI_FONT_SIZE"))
        if (const float parsed = (float)std::atof(sizeEnv); parsed > 0.0f)
            baseSize = parsed;
    if (!loadFontFile(face, base, error) ||
        !loadFontFile(FONT_ICON_FILE_NAME_MD, material, error) ||
        !loadFontFile(FONT_ICON_FILE_NAME_LC, lucide, error)) {
        return false;
    }

    width_ = kAtlasWidth;
    height_ = kAtlasHeight;
    alpha_.assign((std::size_t)width_ * (std::size_t)height_, 0);

    stbtt_pack_context ctx;
    if (!stbtt_PackBegin(&ctx, alpha_.data(), width_, height_, 0, 1, nullptr)) {
        if (error)
            *error = "SND UI font atlas could not initialise stb_truetype packer";
        return false;
    }
    // Oversample the TEXT face 2x2: stb rasterises it at twice the resolution
    // per axis and the packed quad carries sub-pixel positioning, which is what
    // keeps thin stems from smearing to grey at UI sizes. The icon faces stay
    // at 1x -- they are ~2000-glyph ranges, where 4x the atlas area buys little
    // (icons are blocky shapes, not hairlines) and risks an atlas overflow.
    stbtt_PackSetOversampling(&ctx, 2, 2);
    const bool baseOk = packFont(ctx, base, baseSize, 32, 95, base_, error);
    stbtt_PackSetOversampling(&ctx, 1, 1);
    const bool ok =
        baseOk &&
        packFont(ctx, material, kIconSize, ICON_MIN_MD,
                 ICON_MAX_16_MD - ICON_MIN_MD + 1, material_, error) &&
        packFont(ctx, lucide, kIconSize, ICON_MIN_LC,
                 ICON_MAX_16_LC - ICON_MIN_LC + 1, lucide_, error);
    stbtt_PackEnd(&ctx);
    return ok;
}

bool StbFontAtlas::upload()
{
    if (alpha_.empty())
        return false;
    if (textureId_ != 0)
        return true;

    GLint previousTexture = 0;
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);

    glGenTextures(1, &textureId_);
    if (textureId_ == 0) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
        glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, textureId_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width_, height_, 0, GL_RED,
                 GL_UNSIGNED_BYTE, alpha_.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
    return textureId_ != 0;
}

void StbFontAtlas::destroyGl()
{
    if (textureId_ != 0) {
        GLint previousTexture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
        const GLuint deleted = textureId_;
        glDeleteTextures(1, &textureId_);
        if ((GLuint)previousTexture == deleted)
            glBindTexture(GL_TEXTURE_2D, 0);
        textureId_ = 0;
    }
}

FontRef StbFontAtlas::defaultFontRef() const
{
    return {reinterpret_cast<uintptr_t>(&base_)};
}

FontRef StbFontAtlas::iconFontLucideRef() const
{
    return {reinterpret_cast<uintptr_t>(&lucide_)};
}

StbFontAtlas::Font* StbFontAtlas::fontFromRef(FontRef font)
{
    if (font.handle == reinterpret_cast<uintptr_t>(&lucide_))
        return &lucide_;
    if (font.handle == reinterpret_cast<uintptr_t>(&material_))
        return &material_;
    return &base_;
}

const StbFontAtlas::Font* StbFontAtlas::fontFromRef(FontRef font) const
{
    if (font.handle == reinterpret_cast<uintptr_t>(&lucide_))
        return &lucide_;
    if (font.handle == reinterpret_cast<uintptr_t>(&material_))
        return &material_;
    return &base_;
}

const StbFontAtlas::Glyph* StbFontAtlas::glyph(FontRef font,
                                               uint32_t codepoint) const
{
    const Font* requested = fontFromRef(font);
    if (requested == &base_) {
        if (const Glyph* materialGlyph = glyphInFont(material_, codepoint))
            return materialGlyph;
    }
    return glyphInFont(*requested, codepoint);
}

float StbFontAtlas::glyphSize(FontRef font, uint32_t codepoint) const
{
    const Font* requested = fontFromRef(font);
    if (requested == &base_ && glyphInFont(material_, codepoint))
        return material_.sizePx;
    return requested->sizePx;
}

float StbFontAtlas::glyphAscent(FontRef font, uint32_t codepoint) const
{
    const Font* requested = fontFromRef(font);
    if (requested == &base_ && glyphInFont(material_, codepoint))
        return material_.ascent;
    return requested->ascent > 0.0f ? requested->ascent : requested->sizePx;
}

Vec2 StbFontAtlas::measure(FontRef font, float sizePx, const char* begin,
                           const char* end) const
{
    if (!begin)
        return {};
    if (sizePx <= 0.0f)
        sizePx = kBaseSize;
    if (!end)
        end = begin + std::char_traits<char>::length(begin);

    float x = 0.0f;
    float maxX = 0.0f;
    float y = sizePx;
    const char* p = begin;
    while (p < end && *p) {
        const uint32_t cp = nextUtf8(p, end);
        if (cp == '\n') {
            maxX = std::max(maxX, x);
            x = 0.0f;
            y += sizePx;
            continue;
        }
        if (const Glyph* g = glyph(font, cp)) {
            const float scale = sizePx / glyphSize(font, cp);
            x += g->advance * scale;
        }
    }
    return {std::max(maxX, x), y};
}

// ── surface-free text measurement (ui_draw.h): a process-wide stb atlas built
// once for CPU glyph metrics (no GL upload), so layout can size text before any
// renderer/surface exists. Same ProggyClean the GL renderer draws with. ───────
namespace {
const StbFontAtlas& measurementAtlas()
{
    static StbFontAtlas atlas;
    static const bool built = atlas.build(nullptr);
    (void)built;
    return atlas;
}
} // namespace

Vec2 measureText(float sizePx, const char* begin, const char* end)
{
    if (sizePx < 0.5f || !begin)
        return {};
    const StbFontAtlas& atlas = measurementAtlas();
    return atlas.measure(atlas.defaultFontRef(), sizePx, begin, end);
}

void StbFontAtlas::appendText(std::vector<TextVertex>& vertices, FontRef font,
                              float sizePx, Vec2 pos, Color color,
                              const char* begin, const char* end) const
{
    if (!begin || alpha_.empty())
        return;
    if (sizePx <= 0.0f)
        sizePx = kBaseSize;
    if (!end)
        end = begin + std::char_traits<char>::length(begin);

    float x = pos.x;
    float y = pos.y;
    const char* p = begin;
    while (p < end && *p) {
        const uint32_t cp = nextUtf8(p, end);
        if (cp == '\n') {
            x = pos.x;
            y += sizePx;
            continue;
        }
        const Glyph* g = glyph(font, cp);
        if (!g)
            continue;
        const float scale = sizePx / glyphSize(font, cp);
        const float baseline = y + glyphAscent(font, cp) * scale;
        const float x0 = x + g->x0 * scale;
        const float y0 = baseline + g->y0 * scale;
        const float x1 = x + g->x1 * scale;
        const float y1 = baseline + g->y1 * scale;
        vertices.push_back({x0, y0, g->s0, g->t0, color});
        vertices.push_back({x1, y0, g->s1, g->t0, color});
        vertices.push_back({x1, y1, g->s1, g->t1, color});
        vertices.push_back({x0, y0, g->s0, g->t0, color});
        vertices.push_back({x1, y1, g->s1, g->t1, color});
        vertices.push_back({x0, y1, g->s0, g->t1, color});
        x += g->advance * scale;
    }
}

} // namespace snd::ui::draw
