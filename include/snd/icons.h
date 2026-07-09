#pragma once
// SND icon library: named glyph constants for the two embedded icon fonts.
//   Material Icons (Apache-2.0) -> ICON_MD_*  (merged into the default font)
//   Lucide (ISC)               -> ICON_LC_*  (draw with snd::ui::iconFontLucide())
//
// e.g.  snd::ui::iconButton("cfg", ICON_MD_SETTINGS);
//       ImGui::TextUnformatted(ICON_MD_FOLDER " Open");
//
// The codepoint headers are vendored from IconFontCppHeaders (zlib); the fonts
// live in third_party/fonts with their licences.
#include "snd/icons/IconsMaterialDesign.h"
#include "snd/icons/IconsLucide.h"
