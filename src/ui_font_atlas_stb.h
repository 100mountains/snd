// Private stb_truetype atlas for the pure retained OpenGL renderer.
#pragma once

#include "snd/ui_draw.h"

#include <cstdint>
#include <string>
#include <vector>

namespace snd::ui::draw {

class StbFontAtlas {
public:
    struct TextVertex {
        float x = 0.0f, y = 0.0f;
        float u = 0.0f, v = 0.0f;
        Color color = 0;
    };

    struct Glyph {
        float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        float s0 = 0.0f, t0 = 0.0f, s1 = 0.0f, t1 = 0.0f;
        float advance = 0.0f;
        bool valid = false;
    };

    struct Font {
        float sizePx = 13.0f;
        float ascent = 0.0f;
        int firstCodepoint = 32;
        int glyphCount = 0;
        std::vector<Glyph> glyphs;
    };

    bool build(std::string* error = nullptr);
    bool upload();
    void destroyGl();

    FontRef defaultFontRef() const;
    FontRef iconFontLucideRef() const;

    Vec2 measure(FontRef font, float sizePx, const char* begin,
                 const char* end = nullptr) const;
    void appendText(std::vector<TextVertex>& vertices, FontRef font,
                    float sizePx, Vec2 pos, Color color, const char* begin,
                    const char* end = nullptr) const;

    unsigned int textureId() const { return textureId_; }

private:
    Font* fontFromRef(FontRef font);
    const Font* fontFromRef(FontRef font) const;
    const Glyph* glyph(FontRef font, uint32_t codepoint) const;
    float glyphSize(FontRef font, uint32_t codepoint) const;
    float glyphAscent(FontRef font, uint32_t codepoint) const;

    Font base_;
    Font material_;
    Font lucide_;
    int width_ = 0;
    int height_ = 0;
    std::vector<unsigned char> alpha_;
    unsigned int textureId_ = 0;
};

} // namespace snd::ui::draw
