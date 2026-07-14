#include "snd/ui_draw_recording.h"
#include "snd/ui_retained_gl.h"
#include "snd/ui_retained_widgets.h"

#include <utility>

int main()
{
    namespace r = snd::ui::retained;
    namespace w = snd::ui::retained::widgets;

    r::PaintRenderer renderer;
    auto root = w::column("root", 4.0f);
    root->addChild(w::button("root.ok", "OK", {}, &renderer));

    r::Tree tree(std::move(root));
    tree.layout({120.0f, 48.0f});

    snd::ui::draw::RecordingSurface surface;
    renderer.render(tree, surface, {});

    return tree.validate().empty() ? 0 : 1;
}
