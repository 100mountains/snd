#include "snd/ui_draw_recording.h"
#include "snd/ui_retained_gl.h"
#include "snd/ui_retained_widgets.h"
#include "snd/ui_types.h"

#include <utility>

namespace {

using SvgTextureLoadFn = snd::ui::SvgTexture (*)(const char*, int, ImU32);
using ImageTextureLoadFn = snd::ui::SvgTexture (*)(const unsigned char*, int);
using RgbaTextureLoadFn = snd::ui::SvgTexture (*)(const unsigned char*, int, int);
using TextureReleaseFn = void (*)(snd::ui::draw::TextureRef);

SvgTextureLoadFn gSvgTextureLoad = &snd::ui::loadSvgTexture;
ImageTextureLoadFn gImageTextureLoad = &snd::ui::loadImageTexture;
RgbaTextureLoadFn gRgbaTextureLoad = &snd::ui::loadTextureRGBA;
TextureReleaseFn gTextureRelease = &snd::ui::releaseTexture;

} // namespace

int main()
{
    namespace r = snd::ui::retained;
    namespace w = snd::ui::retained::widgets;

    if (!gSvgTextureLoad || !gImageTextureLoad || !gRgbaTextureLoad ||
        !gTextureRelease)
        return 2;

    r::PaintRenderer renderer;
    auto root = w::column("root", 4.0f);
    root->addChild(w::button("root.ok", "OK", {}, &renderer));

    r::Tree tree(std::move(root));
    tree.layout({120.0f, 48.0f});

    snd::ui::draw::RecordingSurface surface;
    renderer.render(tree, surface, {});

    return tree.validate().empty() ? 0 : 1;
}
