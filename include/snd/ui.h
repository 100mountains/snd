// snd::ui -- window + Dear ImGui frame shell, plus SND's own widget helpers.
// ImGui itself stays vendored/unmodified; anything custom is drawn on top via
// ImDrawList. Consumers include this and use ImGui:: directly for widgets.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"

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

class Window {
public:
    Window();
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // decorated=false creates a frameless window (the app draws its own
    // title bar and calls the position/size/minimize helpers below).
    bool create(int width, int height, const std::string& title, bool decorated = true);
    void destroy();

    bool shouldClose() const;
    // Veto or force a close request (e.g. to show an "unsaved changes" prompt
    // after the user hits the window's close button).
    void setShouldClose(bool close);

    // Poll events + start an ImGui frame. Returns false if the window is gone.
    bool beginFrame();
    // Render the ImGui frame and swap buffers.
    void endFrame();

    // Vsync for this window's swap (default 1). An app driving several
    // windows per frame sets 0 on the secondary ones so the waits don't
    // stack (the primary window's vsync already paces the loop).
    void setSwapInterval(int interval);

    // The GL clear behind everything the app draws (IM_COL32 packing).
    // Apps that paint their own chrome should set their canvas colour here so
    // padding/gaps don't show the library default.
    void setClearColor(unsigned int rgba);

    void setTitle(const std::string& title);
    int width() const;
    int height() const;

    // For app-drawn title bars on frameless windows.
    void getPosition(int& x, int& y) const;
    void setPosition(int x, int y);
    void setSize(int width, int height);
    void minimize();
    // Zoom, not fullscreen: fill the screen's working area keeping the
    // app-drawn title bar (the Mac title-bar double-click behaviour).
    // Toggling restores the previous frame.
    void toggleMaximize();
    bool isZoomed() const;
    // Native fullscreen (a Space on macOS, monitor fullscreen elsewhere) --
    // the green-glyph behaviour. No title bar while active.
    void toggleFullscreen();
    bool isFullscreen() const;

    // Hand an in-progress mouse-down to the OS as a window drag (call once
    // when the press lands on your custom title bar). Native and smooth on
    // macOS/Windows; no-op elsewhere.
    void beginNativeDrag();

    // Paths of files dropped onto the window since the last call (then cleared).
    std::vector<std::string> takeDroppedFiles();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// --- SND custom widgets (drawn via ImDrawList on top of stock ImGui) -------

// A filled rectangle at the current cursor with a 4-corner gradient.
// Advances the layout cursor like any other item.
void gradientPanel(const ImVec2& size, ImU32 topLeft, ImU32 topRight,
                   ImU32 bottomRight, ImU32 bottomLeft);

// Gradient-filled button. Returns true when clicked.
bool gradientButton(const char* label, const ImVec2& size, ImU32 top, ImU32 bottom);
// Generation/action button with the same gradient body plus a subtle live
// sweep. Use for actions that should read as energetic but still be a button.
bool animatedButton(const char* label, const ImVec2& size,
                    ImU32 top = 0, ImU32 bottom = 0);
// Generic SND button whose body is drawn by a custom painter. SND still owns
// hit-testing, focus indication, activation, and accessibility expectations.
bool button(const char* label, const ImVec2& size,
            const paint::ButtonPainter& painter);
// Border-first action button. Default style has no resting fill, shows an
// accent border on hover, and fills immediately while pressed.
bool outlineButton(const char* label, const ImVec2& size);
bool outlineButton(const char* label, const ImVec2& size,
                   const paint::OutlineButtonStyle& style,
                   bool selected = false);

// Segmented control: a pill group of mutually exclusive options (mono/stereo,
// filter slope, A/B). Click a segment, or use Left/Right when focused.
// size 0 = equal-width segments sized to the widest label. Returns true when
// the selection changes.
bool segmented(const char* id, const char* const* labels, int count,
               int* selected, const ImVec2& size = ImVec2(0, 0));

// Multi-state value button: shows the current option and cycles on click
// (right-click steps back). Pips under the label mark the position in the
// cycle. Sized to the widest option so it stays stable while cycling.
// Returns true when the value changes.
bool cycleButton(const char* id, const char* const* labels, int count,
                 int* index, const ImVec2& size = ImVec2(0, 0));

// Vector transport/tool icons, drawn as crisp geometry (no bitmaps, scale-
// independent like SVG). `active` renders with the accent colour + border.
enum class Icon {
    Play,
    Stop,
    Record,
    SkipToStart,
    SkipToEnd,
    Loop,
    Waveform,
    Spectrum,
    Follow, // playhead line + arrow: "view follows transport"
};
bool iconButton(const char* id, Icon icon, const ImVec2& size, ImU32 accent,
                bool active = false);

// --- Icon fonts (embedded: Material Icons + Lucide) -------------------------
// SND embeds two icon fonts. Material Icons (Apache-2.0) is merged into the
// default font, so ICON_MD_* from <snd/icons.h> works inline with text and in
// the widgets below. Lucide (ISC) is a second font (ICON_LC_*). Accessors
// return the font in the CURRENT window's context.
void loadFonts();           // internal: Window::create calls this per context
ImFont* iconFontMaterial(); // the default font (Material glyphs merged in)
ImFont* iconFontLucide();

// Tactile icon button: a raised light-grey face that presses into a dark inset
// (the bevel flips on press/toggle). `glyph` is an icon string from
// <snd/icons.h>. size 0 = a square sized to the frame height. `font` null =
// the Material/default font (pass iconFontLucide() for the Lucide style).
// `toggled` holds the pressed/inset look (on/off buttons); `face` 0 = the
// default light grey, else a themed face colour. Returns true on click.
bool iconButton(const char* id, const char* glyph, const ImVec2& size = ImVec2(0, 0),
                ImFont* font = nullptr, bool toggled = false, ImU32 face = 0);

// Tactile key with an integrated status LED ring (record-arm, monitor,
// transport latches). Click toggles *on; the face holds the inset look and
// the ring lights while latched. `blink` pulses the lit ring for armed/
// pending states. ledColor 0 = palette accent; font/face as iconButton.
// Returns true on click.
bool ledButton(const char* id, const char* glyph, bool* on, bool blink = false,
               const ImVec2& size = ImVec2(0, 0), ImFont* font = nullptr,
               ImU32 ledColor = 0, ImU32 face = 0);

// --- SVG + image assets -> bitmap / GPU texture -----------------------------
// Parse + rasterize an SVG document (nanosvg) so vector logos/icons stay crisp
// at any size or DPI. Rasterizes heightPx tall; width follows the source
// aspect. tint != 0 multiplies every texel (recolour a monochrome glyph, or
// fade one to a watermark). rasterizeSvg is GL-free and headless-safe;
// loadSvgTexture uploads to a GL texture, so call it once a context exists
// (after Window::create / inside the frame loop).
struct SvgBitmap {
    std::vector<unsigned char> rgba; // straight-alpha RGBA8, row-major
    int w = 0, h = 0;                // rgba is empty on a parse failure
};
SvgBitmap rasterizeSvg(const char* svgText, int heightPx, ImU32 tint = 0);

struct SvgTexture {
    ImTextureID id = ImTextureID_Invalid; // 0 on failure; feed to ImGui::Image
    int w = 0, h = 0;
};
SvgTexture loadSvgTexture(const char* svgText, int heightPx, ImU32 tint = 0);
// Decode a PNG held in memory to straight-alpha RGBA8 (vendored stb_image,
// PNG-only build). GL-free and headless-safe like rasterizeSvg; rgba is
// empty on a decode failure.
SvgBitmap decodeImage(const unsigned char* bytes, int byteCount);
// Decode + upload in one call. Same lifetime rules as loadSvgTexture.
SvgTexture loadImageTexture(const unsigned char* bytes, int byteCount);
// Upload caller-provided straight-alpha RGBA8 pixels (row-major, w*h*4 bytes)
// to a GL texture -- the raster-image path for decoded images or generated
// bitmaps. Same lifetime rules as loadSvgTexture.
SvgTexture loadTextureRGBA(const unsigned char* rgba, int w, int h);
void releaseTexture(ImTextureID id); // glDeleteTextures a loadSvgTexture/loadTextureRGBA id

// --- The audio widget set ---------------------------------------------------
// Themed controls for audio apps: knobs, switches, LEDs, meters, faders.
// They read the Palette below, so one setPalette() call when the app's theme
// changes re-skins every widget. Knobs are the vendored imgui-knobs
// underneath; the rest is SND's own drawing.

struct Palette {
    ImU32 accent = IM_COL32(240, 190, 90, 255);    // active/lit elements
    ImU32 accentDim = IM_COL32(120, 95, 45, 255);  // their darker relative
    ImU32 text = IM_COL32(230, 232, 240, 255);
    ImU32 textDim = IM_COL32(140, 145, 158, 255);
    ImU32 frame = IM_COL32(34, 37, 46, 255);       // control bodies
    ImU32 frameBright = IM_COL32(58, 63, 78, 255); // borders/tracks
    ImU32 ledOff = IM_COL32(70, 74, 88, 255);
    ImU32 meterLow = IM_COL32(80, 220, 120, 235);  // classic hardware zones
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
    // Optional nested submenu rows. State remains in PopupMenuState.
    std::vector<MenuItem> children;
    // Optional texture icon for the row (a loadSvgTexture/loadImageTexture
    // id); when valid it takes the icon column instead of `icon`. The
    // texture must outlive the menu. Deliberately the last field so every
    // existing positional aggregate initializer keeps its meaning; set it by
    // name (item.image = ...).
    ImTextureID image = ImTextureID_Invalid;
};

struct PopupMenuState {
    bool open = false;
    int highlightedIndex = -1;
    bool closeOnOutsideClick = true;
    bool anchorToPosition = false;
    ImVec2 position = ImVec2(0.0f, 0.0f);
    // Retained menus use this to keep nested submenu rows caller-owned.
    std::vector<std::string> openSubmenuPath;
    std::string typeahead;
};

struct MenuOptions {
    float width = 0.0f;
    float itemHeight = 0.0f;
    ImFont* iconFont = nullptr;
};

struct MenuResult {
    bool activated = false;
    int index = -1;
    // The activated item's id (its label when it has no id). Match on this.
    std::string id;
    // Full open path to the item ("parent/child/leaf") — disambiguates equal
    // ids under different submenus. NOT the item id; prefix checks against
    // item ids must use `id`.
    std::string targetId;
};

void openPopupMenu(const char* popupId);
MenuResult popupMenu(const char* popupId, const std::vector<MenuItem>& items,
                     const MenuOptions& options = {});
MenuResult dropdownMenu(const char* id, const char* currentLabel,
                        const std::vector<MenuItem>& items, int* selectedIndex,
                        const ImVec2& size = ImVec2(0, 0),
                        const MenuOptions& options = {});
// Same dropdown with a styled combo face (parity with the retained
// widgets::dropdownMenu style passthrough).
MenuResult dropdownMenu(const char* id, const char* currentLabel,
                        const std::vector<MenuItem>& items, int* selectedIndex,
                        const paint::OutlineButtonStyle& buttonStyle,
                        const ImVec2& size = ImVec2(0, 0),
                        const MenuOptions& options = {});
MenuResult contextMenu(const char* popupId, const std::vector<MenuItem>& items,
                       const MenuOptions& options = {});

// Rotary knob, normalized 0..1. Returns true while the value is changing.
// size 0 = a sensible default. The label is drawn under the knob.
bool knob(const char* label, float* value, float size = 0.0f,
          const char* format = "%.2f");

// Knob over a dB range (label shows dB). 0 dB gets a tick if in range.
bool knobDb(const char* label, float* db, float minDb, float maxDb,
            float size = 0.0f);

// Rotary knob library (SND-drawn, palette-themed). `style` picks the look;
// `value` maps linearly across [minV,maxV]. Draws `label` (text before "##")
// centred under the knob and, if `format` != nullptr, the value under that.
// `size` 0 = a sensible default diameter. `bipolar` grows the Ring value arc
// from 12 o'clock (for -x..+x params). `accent` 0 = palette accent. Returns
// true while the value is changing. (The plain knob()/knobDb() above stay the
// vendored imgui-knobs wiper/tick styles.)
enum class KnobStyle {
    Davies, // synth-panel knob: dark dished face, chrome skirt, pointer, ticks
    Ring,   // ring only: dim background arc + accent value arc + thumb dot
    Seq,    // sequencer ring: flat/butt-capped arcs, no thumb dot (SEQ matrix)
    Synth,  // flat disc + track ring + accent value arc + single pointer tick
    Nxd,    // scalloped rotating case + inner face + tick ring + pointer (AID/NxD)
};
bool knob(const char* label, float* value, float minV, float maxV,
          KnobStyle style, float size = 0.0f, const char* format = "%.2f",
          bool bipolar = false, ImU32 accent = 0);
bool knob(const char* label, float* value, float minV, float maxV,
          const paint::KnobPainter& painter, float size = 0.0f,
          const char* format = "%.2f", bool bipolar = false,
          ImU32 accent = 0);

// Modulation ring: a thin second arc at the knob rim sweeping `depth` of the
// normalized range away from the value, plus a live dot at the modulated
// position. SND draws it over any body -- built-in styles or custom painters
// -- so modulated knobs keep one shared look. color 0 = a light accent tint.
struct KnobMod {
    float depth = 0.0f;   // signed sweep from the value, in normalized units
    float value = -1.0f;  // live modulated position 0..1; < 0 hides the dot
    ImU32 color = 0;
};
bool knob(const char* label, float* value, float minV, float maxV,
          KnobStyle style, const KnobMod& mod, float size = 0.0f,
          const char* format = "%.2f", bool bipolar = false, ImU32 accent = 0);
bool knob(const char* label, float* value, float minV, float maxV,
          const paint::KnobPainter& painter, const KnobMod& mod,
          float size = 0.0f, const char* format = "%.2f", bool bipolar = false,
          ImU32 accent = 0);

// Animated on/off switch. Returns true when toggled.
bool toggle(const char* label, bool* on);

// Round indicator LED with a glow when lit. Clickable when `clickable`;
// returns true on click. onColor 0 = palette accent.
bool led(const char* id, bool on, float radius = 5.0f, bool clickable = false,
         ImU32 onColor = 0);

// Level meter on a dB scale (floorDb..0) with peak-hold. `level` is linear
// amplitude 0..1 for this frame; the state carries decay + hold between
// frames. Horizontal when size.x > size.y.
struct MeterState {
    float shown = 0.0f;   // decayed bar level (linear)
    float peak = 0.0f;    // held peak (linear)
    float peakAge = 0.0f; // frames since the peak was set
};
void meter(const char* id, MeterState& st, float level, const ImVec2& size,
           float floorDb = -48.0f);

// Vertical audio fader, normalized 0..1. Returns true while dragging.
bool fader(const char* id, float* value, const ImVec2& size);

// Small rounded tag ("VST3", "48k"...). fill 0 = translucent accent.
void badge(const char* text, ImU32 fill = 0);

// JUCE-style tooltip for the previous item: shows after the standard hover
// delay, wraps at `maxWidth` (auto height) like a TooltipWindow, and draws
// with the palette rather than raw ImGui style. Call right after the item it
// explains -- the SND replacement for ImGui::SetTooltip.
void tooltip(const char* text, float maxWidth = 400.0f);

// Dim uppercase caption with a rule to the right: section separators.
void sectionHeader(const char* text);

// Piano keyboard. Click plays (velocity from how far down the key you hit),
// dragging glissandos. noteOn/noteOff fire on transitions; also render any
// externally-held notes by passing them in `lit` (e.g. from incoming MIDI).
struct KeyboardState {
    int mouseNote = -1; // note the mouse currently holds, -1 = none
};
bool keyboard(const char* id, KeyboardState& st, const ImVec2& size,
              int firstNote, int octaves,
              const std::function<void(uint8_t note, uint8_t velocity)>& noteOn,
              const std::function<void(uint8_t note)>& noteOff,
              const bool* lit = nullptr /* 128 flags, optional */);

// Step-sequencer grid over a row-major bool array (rows x steps). Drag
// paints, alt-drag erases; playheadStep highlights a column. Returns true
// when any cell changed.
bool patternGrid(const char* id, bool* cells, int rows, int steps,
                 const ImVec2& size, int playheadStep = -1);
// Grid with a custom cell painter: SND draws the backdrop, playhead tint,
// border, and focus; the painter draws each cell body (velocity gradients,
// spans, chips...) from PatternCellPaintArgs. Interaction is unchanged.
bool patternGrid(const char* id, bool* cells, int rows, int steps,
                 const ImVec2& size, int playheadStep,
                 const paint::PatternCellPainter& cellPainter);

// Breakpoint envelope over 0..1 in both axes. Drag points, double-click
// empty space to add, right-click a point to delete. Points stay x-sorted;
// first/last stay pinned to x=0/x=1. Returns true while editing.
// Optional `tensions`: per-segment bend, -1..1 (entry i curves the segment
// points[i]->points[i+1]; kept the same length as points). Dragging the
// middle of a segment bends it; segments draw as eased curves.
struct EnvPoint {
    float x = 0.0f, y = 0.0f;
};
bool envelopeEditor(const char* id, std::vector<EnvPoint>& points,
                    const ImVec2& size, std::vector<float>* tensions = nullptr);

// 2D pad controlling two normalized values. Returns true while dragging.
bool xyPad(const char* id, float* x, float* y, const ImVec2& size);
// Custom-painted pad body (custom maps/pucks); SND keeps the drag,
// hit target, and focus ring, exactly as the knob/button painter hooks do.
bool xyPad(const char* id, float* x, float* y, const ImVec2& size,
           const paint::XYPadPainter& painter);

// Themed scrolling list; returns true when the selection changed.
bool selectableList(const char* id, const std::vector<std::string>& items,
                    int* selected, const ImVec2& size);

// Number field you drag horizontally, painted with SND's value-row style.
bool dragNumber(const char* label, float* value, float speed, float minV,
                float maxV, const char* format = "%.2f");

// File browser panel: directory listing + breadcrumb up-navigation.
// Returns true when the user picks a file (path written to outPath).
// extensions = comma list ("wav,flac"); null shows everything.
struct FileBrowserState {
    std::string dir;      // current directory ("" = start in HOME)
    std::string selected; // highlighted entry
};
bool fileBrowser(const char* id, FileBrowserState& st, const ImVec2& size,
                 std::string* outPath, const char* extensions = nullptr);

} // namespace snd::ui
