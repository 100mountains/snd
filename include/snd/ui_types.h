// Shared UI data types used by immediate and retained SND UI.
//
// This header is deliberately renderer-neutral. `snd/ui.h` remains the Dear
// ImGui compatibility shell; retained/paint code includes this header instead.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "snd/ui_draw.h"

#ifndef IM_COL32_R_SHIFT
using ImU32 = uint32_t; // legacy-compatible colour alias, not an ImGui dep.
struct ImVec2 {
    float x = 0.0f;
    float y = 0.0f;
    constexpr ImVec2() = default;
    constexpr ImVec2(float x_, float y_) : x(x_), y(y_) {}
};
struct ImVec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
    constexpr ImVec4() = default;
    constexpr ImVec4(float x_, float y_, float z_, float w_)
        : x(x_), y(y_), z(z_), w(w_) {}
};
struct ImDrawList;
struct ImFont;
#define IM_COL32_R_SHIFT 0
#define IM_COL32_G_SHIFT 8
#define IM_COL32_B_SHIFT 16
#define IM_COL32_A_SHIFT 24
#define IM_COL32(R, G, B, A)                                                   \
    (((ImU32)(A) << IM_COL32_A_SHIFT) | ((ImU32)(B) << IM_COL32_B_SHIFT) |     \
     ((ImU32)(G) << IM_COL32_G_SHIFT) | ((ImU32)(R) << IM_COL32_R_SHIFT))
#endif

namespace snd::ui {

namespace paint {
struct KnobPaintArgs;
struct ButtonPaintArgs;
struct XYPadPaintArgs;
struct PatternCellPaintArgs;
struct OutlineButtonStyle;
using KnobPainter = std::function<void(const KnobPaintArgs&)>;
using ButtonPainter = std::function<void(const ButtonPaintArgs&)>;
using XYPadPainter = std::function<void(const XYPadPaintArgs&)>;
using PatternCellPainter = std::function<void(const PatternCellPaintArgs&)>;
} // namespace paint

enum class Icon {
    Play,
    Stop,
    Record,
    SkipToStart,
    SkipToEnd,
    Loop,
    Waveform,
    Spectrum,
    Follow,
};

struct SvgBitmap {
    std::vector<unsigned char> rgba; // straight-alpha RGBA8, row-major
    int w = 0;
    int h = 0; // rgba is empty on a parse/decode failure
};

struct SvgTexture {
    draw::TextureRef id = 0; // 0 on failure; renderer adapters interpret it.
    int w = 0;
    int h = 0;
};

SvgBitmap rasterizeSvg(const char* svgText, int heightPx, ImU32 tint = 0);
SvgBitmap decodeImage(const unsigned char* bytes, int byteCount);
SvgTexture loadSvgTexture(const char* svgText, int heightPx, ImU32 tint = 0);
SvgTexture loadImageTexture(const unsigned char* bytes, int byteCount);
SvgTexture loadTextureRGBA(const unsigned char* rgba, int w, int h);
void releaseTexture(draw::TextureRef id);

struct Palette {
    ImU32 accent = IM_COL32(240, 190, 90, 255);
    ImU32 accentDim = IM_COL32(120, 95, 45, 255);
    ImU32 text = IM_COL32(230, 232, 240, 255);
    ImU32 textDim = IM_COL32(140, 145, 158, 255);
    ImU32 frame = IM_COL32(34, 37, 46, 255);
    ImU32 frameBright = IM_COL32(58, 63, 78, 255);
    ImU32 ledOff = IM_COL32(70, 74, 88, 255);
    ImU32 meterLow = IM_COL32(80, 220, 120, 235);
    ImU32 meterMid = IM_COL32(255, 185, 70, 235);
    ImU32 meterHot = IM_COL32(255, 80, 70, 235);
};
void setPalette(const Palette& p);
const Palette& palette();

struct MenuItem {
    std::string id;
    std::string label;
    std::string icon;
    bool separator = false;
    bool enabled = true;
    bool checked = false;
    std::string rightText;
    bool danger = false;
    std::vector<MenuItem> children;
    // Optional texture icon for the row. The texture must outlive the menu.
    draw::TextureRef image = 0;
};

struct PopupMenuState {
    bool open = false;
    int highlightedIndex = -1;
    bool closeOnOutsideClick = true;
    bool anchorToPosition = false;
    draw::Vec2 position;
    std::vector<std::string> openSubmenuPath;
    std::string typeahead;
};

struct MenuResult {
    bool activated = false;
    int index = -1;
    std::string id;
    std::string targetId;
};

enum class KnobStyle {
    Davies,
    Ring,
    Seq,
    Synth,
    Nxd,
};

struct KnobMod {
    float depth = 0.0f;
    float value = -1.0f;
    ImU32 color = 0;
};

struct MeterState {
    float shown = 0.0f;
    float peak = 0.0f;
    float peakAge = 0.0f;
};

struct AutoPoint {
    float time = 0.0f;
    float value = 0.0f;
};

struct ToastStack {
    struct Item {
        std::string text;
        double bornAt = 0.0;
        double expiry = 0.0;
    };
    std::vector<Item> items;
};

struct TableModel {
    std::vector<std::string> headers;
    std::vector<float> colWidths;
    std::function<std::string(int row, int col)> cell;
    int rows = 0;
};

struct DragPayload {
    std::string kind;
    std::string label;
    std::string id;
    bool active = false;
};

struct CommandItem {
    std::string label;
    std::string id;
};

struct KeyboardState {
    int mouseNote = -1;
};

struct EnvPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct FileBrowserState {
    std::string dir;
    std::string selected;
};

} // namespace snd::ui

namespace snd::ui::draw {

inline Vec2 toDrawVec2(const ::ImVec2& v) { return {v.x, v.y}; }
inline ::ImVec2 toImVec2(Vec2 v) { return ::ImVec2(v.x, v.y); }
inline FontRef fontRef(::ImFont* font)
{
    return {reinterpret_cast<uintptr_t>(font)};
}
inline ::ImFont* imFont(FontRef font)
{
    return reinterpret_cast<::ImFont*>(font.handle);
}

} // namespace snd::ui::draw

#define SND_UI_DRAW_COMPAT_FUNCS 1
