#include "snd/ui_types.h"

namespace snd::ui {

namespace {
Palette gPalette;
}

void setPalette(const Palette& p) { gPalette = p; }
const Palette& palette() { return gPalette; }

} // namespace snd::ui
