// Shared SND UI paint primitives.
//
// These helpers are draw-only: they do not own interaction, layout, app state,
// plugin parameters, or audio-thread data. Immediate and retained widgets can
// call the same functions after their own hit-testing/state handling so the
// pixels, focus treatment, and hardware-control vocabulary stay aligned.
#pragma once

#include <cstdint>
#include <vector>

#include "snd/ui.h"
#include "snd/ui_draw.h"

namespace snd::ui::paint {

struct ControlState {
    bool hovered = false;
    bool active = false;
    bool focused = false;
    bool disabled = false;
    bool selected = false;
};

struct KnobPaintArgs {
    ImDrawList* drawList = nullptr;
    ImVec2 topLeft;
    float size = 0.0f;
    float rawValue = 0.0f;
    float normalizedValue = 0.0f;
    KnobStyle style = KnobStyle::Ring;
    bool bipolar = false;
    ImU32 accent = 0;
    const Palette* palette = nullptr;
    const ControlState* state = nullptr;
    KnobMod mod; // depth 0 + value < 0 = no modulation overlay
    draw::Surface* surface = nullptr;
};

struct ButtonPaintArgs {
    ImDrawList* drawList = nullptr;
    ImFont* font = nullptr;
    ImVec2 topLeft;
    ImVec2 size;
    const char* text = "";
    ImU32 face = 0;
    const Palette* palette = nullptr;
    const ControlState* state = nullptr;
    float fontScale = 0.90f;
    draw::Surface* surface = nullptr;
    draw::FontRef fontRef;
    float fontSizePx = 0.0f;
};

struct XYPadPaintArgs {
    ImDrawList* drawList = nullptr;
    ImVec2 topLeft;
    ImVec2 size;
    float x = 0.0f; // normalized 0..1
    float y = 0.0f; // normalized 0..1, 0 = bottom
    const Palette* palette = nullptr;
    const ControlState* state = nullptr;
    draw::Surface* surface = nullptr;
};

// One call per cell, after SND draws the grid backdrop/playhead tint and
// before the border/focus overlays. cellMin/cellMax carry the standard cell
// inset; the grid rect plus row/step indices let a painter draw spans or
// chips beyond its own cell (the draw list is not clipped per cell).
struct PatternCellPaintArgs {
    ImDrawList* drawList = nullptr;
    ImVec2 gridTopLeft;
    ImVec2 gridSize;
    ImVec2 cellMin;
    ImVec2 cellMax;
    int row = 0;
    int step = 0;
    int rows = 0;
    int steps = 0;
    bool on = false;
    int playheadStep = -1;
    const Palette* palette = nullptr;
    const ControlState* state = nullptr;
    draw::Surface* surface = nullptr;
};

struct OutlineButtonStyle {
    ImU32 fill = 0;
    ImU32 hoverFill = 0;
    ImU32 activeFill = 0;
    ImU32 selectedFill = 0;
    ImU32 border = 0;
    ImU32 hoverBorder = 0;
    ImU32 activeBorder = 0;
    ImU32 selectedBorder = 0;
    ImU32 text = 0;
    float rounding = 0.0f;
    float fontScale = 0.90f;
    // Stroke weights: resting border and the active/selected ("engaged")
    // border. Equal by default (1px) so an engaged button is the same size as a
    // resting one; raise engagedThickness only for a deliberate chunky latch.
    float borderThickness = 1.0f;
    float engagedThickness = 1.0f;
    // Vertical nudge (px) for the centred label/glyph; 0 = optically centred.
    float labelOffsetY = 0.0f;
};

struct GraphSurfaceStyle {
    enum class Backdrop {
        Flat,
        Grid,
        GreenGrid,
        Mosaic,
        Aurora,       // smooth drifting aurora mesh
        AuroraMosaic, // faceted aurora mesh + bright wireframe
    };

    ImU32 node = 0;
    ImU32 header = 0;
    ImU32 border = 0;
    ImU32 text = 0;
    ImU32 accent = 0;
    ImU32 selectedBorder = 0;
    float corner = 0.0f; // TechSquare default
    bool squarePins = false;
    ImU32 pinAudio = 0;
    ImU32 pinMidi = 0;
    ImU32 pinControl = 0;
    float wireThickness = 2.0f;
    bool wireDroop = false;
    Backdrop backdrop = Backdrop::Grid;
    ImU32 backdropFill = 0;

    // ── gradient extras (inert at 0, so flat skins pay nothing) ──
    // Conic rim: when rimA is set, the node outline is stroked as an angular
    // sweep through the consecutive non-zero stops rimA..rimF (looping back
    // to rimA) in place of border/selectedBorder. Selected nodes spin the
    // sweep (rimSpinSeconds per revolution, 0 = static); the spin reads the
    // timeSeconds passed to drawModuleBox.
    ImU32 rimA = 0;
    ImU32 rimB = 0;
    ImU32 rimC = 0;
    float rimSpinSeconds = 0.0f;
    // Gradient cables: when wireGradientEnd is set, every cable is stroked as
    // a per-segment lerp from wireGradientStart (or the cable's own colour
    // when 0) to wireGradientEnd.
    ImU32 wireGradientStart = 0;
    ImU32 wireGradientEnd = 0;
    // Soft dual halo behind the node body, one colour per side (A left,
    // B right); each colour carries its own peak alpha. A filled-stack
    // approximation -- stays until a REAL blur (GL post pass) replaces it
    // (owner); the current presets leave it off.
    ImU32 glowA = 0;
    ImU32 glowB = 0;
    // The 3px accent stripe down the header's left edge.
    // Skins that want a clean slab (Neo) turn it off.
    bool headerStripe = true;
    // Extra rim stops (appended for aggregate stability): a 6-stop sweep is
    // enough for a full hue wheel. First zero stop past rimC ends the list.
    ImU32 rimD = 0;
    ImU32 rimE = 0;
    ImU32 rimF = 0;
    // Hover a pin -> a tooltip showing its GraphPort::label.
    bool portTooltips = true;
};

// House node skins: five hardware-style bases plus the gradient family, each
// a FULL theme (slab, header, text, pins, wire
// gradient + weight, rim, spin): Neo (Turbo neon), Rainbow (six-stop hue
// wheel), Ember (fire), Redline (black + red sweep), Glacier (ice), Acid
// (toxic green), Vapor (vaporwave pastels, round pins), Gold (brass on
// black), Ghost (greyscale highlight sweep), Ultraviolet (UV to magenta).
// Embedded as presets so consumers pick by name instead of re-transcribing
// colour tables.
enum class GraphSkin {
    TechSquare,
    ClassicRounded,
    Blueprint,
    Console,
    Studio,
    Neo, // house default
    Rainbow,
    Ember,
    Redline,
    Glacier,
    Acid,
    Vapor,
    Gold,
    Ghost,
    Ultraviolet,
};
inline constexpr int kGraphSkinCount = 15;
const char* graphSkinName(GraphSkin skin);
// Node/pin/wire style for a skin. Backdrop fields keep their defaults so the
// canvas can stay an independent choice: pick a Backdrop mode and pair it with
// graphBackdropFill().
GraphSurfaceStyle graphSkinStyle(GraphSkin skin);
// Per backdrop mode: base canvas colour and menu label.
ImU32 graphBackdropFill(GraphSurfaceStyle::Backdrop mode);
const char* graphBackdropName(GraphSurfaceStyle::Backdrop mode);

ImU32 withAlpha(ImU32 c, uint32_t a);
ImU32 mix(ImU32 a, ImU32 b, float t);
ImVec4 toVec4(ImU32 c);

// Linear amplitude -> 0..1 position on a floorDb..0 dB scale.
float dbNorm(float linear, float floorDb);

// Rotary sweep shared by SND knobs: frac 0 = 7 o'clock, frac 1 = 5 o'clock.
// The endpoints are public so overlays drawn by consumers land exactly on the
// arc the knob bodies draw (knobAngle(0)/knobAngle(1)).
inline constexpr float kKnobA0 = -3.92699082f; // -225 deg
inline constexpr float kKnobA1 = 0.78539816f;  // +45 deg
float knobAngle(float frac);
ImVec2 dirAt(float angle);

// Draw the SND focus treatment outside a control rect. Retained widgets should
// use the same ring for keyboard focus; immediate widgets call it when ImGui
// reports item focus.
void drawFocusRing(draw::Surface& surface, draw::Vec2 min, draw::Vec2 max,
                   const Palette& pal, float rounding, float expand = 2.0f);
void drawFocusRing(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
                   const Palette& pal, float rounding, float expand = 2.0f);

// A hover tooltip box: palette frame fill, frameBright border, text, drawn
// at `anchor` (its top-left sits down-right of the anchor and is nudged back
// inside `clipMax` when given). Same look as the immediate snd::ui::tooltip.
void drawTooltip(draw::Surface& surface, draw::FontRef font, float fontSizePx,
                 draw::Vec2 anchor, const char* text, const Palette& pal,
                 draw::Vec2 clipMax = {0.0f, 0.0f});
void drawTooltip(ImDrawList* dl, ImFont* font, float fontSizePx,
                 const ImVec2& anchor, const char* text, const Palette& pal,
                 const ImVec2& clipMax = ImVec2(0.0f, 0.0f));

void drawGradientPanel(draw::Surface& surface, draw::Vec2 topLeft,
                       draw::Vec2 size, ImU32 topLeftColor,
                       ImU32 topRightColor, ImU32 bottomRightColor,
                       ImU32 bottomLeftColor);
void drawGradientPanel(ImDrawList* dl, const ImVec2& topLeft,
                       const ImVec2& size, ImU32 topLeftColor,
                       ImU32 topRightColor, ImU32 bottomRightColor,
                       ImU32 bottomLeftColor);

// Rounded rect filled with a vertical top->bottom gradient (ImDrawList has
// no rounded multi-colour rect; the corner caps are 1px strips following the
// corner circle, the straight body is one exact gradient quad).
void drawGradientRect(draw::Surface& surface, draw::Vec2 min, draw::Vec2 max,
                      ImU32 top, ImU32 bottom, float rounding = 0.0f);
void drawGradientRect(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
                      ImU32 top, ImU32 bottom, float rounding = 0.0f);

// Arc stroked with a colour gradient from a0 to a1, sampled per segment
// (two-colour value arcs).
void drawGradientArc(draw::Surface& surface, draw::Vec2 center, float radius,
                     float a0, float a1, ImU32 colStart, ImU32 colEnd,
                     float thickness, int segments = 32);
void drawGradientArc(ImDrawList* dl, const ImVec2& center, float radius,
                     float a0, float a1, ImU32 colStart, ImU32 colEnd,
                     float thickness, int segments = 32);

void drawAnimatedButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                        const ImVec2& size, const char* text, ImU32 top,
                        ImU32 bottom, const Palette& pal,
                        const ControlState& state, float pulse = 0.0f,
                        float fontScale = 0.90f);
void drawAnimatedButton(draw::Surface& surface, draw::FontRef font,
                        float fontSizePx, draw::Vec2 topLeft, draw::Vec2 size,
                        const char* text, ImU32 top, ImU32 bottom,
                        const Palette& pal, const ControlState& state,
                        float pulse = 0.0f);

void drawKnob(draw::Surface& surface, draw::Vec2 topLeft, float size, float frac,
              KnobStyle style, const Palette& pal, const ControlState& state,
              bool bipolar = false, ImU32 accent = 0);
void drawKnob(ImDrawList* dl, const ImVec2& topLeft, float size, float frac,
              KnobStyle style, const Palette& pal, const ControlState& state,
              bool bipolar = false, ImU32 accent = 0);
// Modulation overlay: depth arc from the value angle + live position dot at
// the knob rim. SND owns this overlay (drawKnobWithPainter applies it after
// the body, before the focus ring) so custom faces keep the shared mod look.
void drawKnobModRing(draw::Surface& surface, draw::Vec2 topLeft, float size,
                     float frac, const KnobMod& mod, const Palette& pal,
                     ImU32 accent = 0);
void drawKnobModRing(ImDrawList* dl, const ImVec2& topLeft, float size,
                     float frac, const KnobMod& mod, const Palette& pal,
                     ImU32 accent = 0);
void drawDefaultKnob(const KnobPaintArgs& args);
void drawKnobWithPainter(const KnobPaintArgs& args,
                         const KnobPainter& painter = {});

// Randomise-window overlay ("ghost fences"): an
// independent {lo, hi} window over a control with two separately grabbable
// end dots and a padlock when locked out of randomisation. Unlike KnobMod
// (one signed depth anchored to the current value) the two ends are
// value-independent. Draw-only plus pure hit-tests: the caller owns the
// overlay input mode, drag state, and click-vs-drag lock toggling.
struct KnobWindow {
    float lo = 0.0f;
    float hi = 1.0f;
    bool locked = false;
};

// Window arc just outside the knob circle (all styles, like the focus ring),
// end dots at knobAngle(lo)/knobAngle(hi), padlock at the control's top-right
// while locked. Call after the knob body with the same topLeft/size.
// accent 0 = palette accent (the fence dims itself when locked); lockColor 0
// = pal.meterHot; uiScale scales the fixed paddings for zoomed UIs.
void drawKnobWindow(draw::Surface& surface, draw::Vec2 topLeft, float size,
                    const KnobWindow& win, const Palette& pal,
                    ImU32 accent = 0, ImU32 lockColor = 0,
                    float uiScale = 1.0f);
void drawKnobWindow(ImDrawList* dl, const ImVec2& topLeft, float size,
                    const KnobWindow& win, const Palette& pal,
                    ImU32 accent = 0, ImU32 lockColor = 0, float uiScale = 1.0f);

// Which window end a press should grab: 0 = lo, 1 = hi, or -1 when the press
// is outside the control rect grown by 6*uiScale px. Inside, the nearest end
// by knob angle wins; callers keep click-vs-drag
// lock toggling.
int knobWindowHitEnd(const ImVec2& topLeft, float size, const KnobWindow& win,
                     const ImVec2& pressPos, float uiScale = 1.0f);

// The same window as an index bracket along a combo/dropdown's bottom edge.
void drawComboWindow(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                     const KnobWindow& win, const Palette& pal,
                     ImU32 accent = 0, ImU32 lockColor = 0,
                     float uiScale = 1.0f);
void drawComboWindow(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                     const KnobWindow& win, const Palette& pal,
                     ImU32 accent = 0, ImU32 lockColor = 0, float uiScale = 1.0f);

// 0 / 1 / -1 as knobWindowHitEnd, with a 3*uiScale px grow and the press
// mapped horizontally across the control width.
int comboWindowHitEnd(const ImVec2& topLeft, const ImVec2& size,
                      const KnobWindow& win, const ImVec2& pressPos,
                      float uiScale = 1.0f);

// Small padlock glyph filling mn..mx (the lock treatment the window overlays
// use; also usable standalone on locked toggles or rows).
void drawPadlock(draw::Surface& surface, draw::Vec2 mn, draw::Vec2 mx,
                 ImU32 color);
void drawPadlock(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 color);

void drawToggle(draw::Surface& surface, draw::Vec2 topLeft, float width,
                float height, float anim, const Palette& pal,
                const ControlState& state);
void drawToggle(ImDrawList* dl, const ImVec2& topLeft, float width, float height,
                float anim, const Palette& pal, const ControlState& state);

void drawLed(draw::Surface& surface, draw::Vec2 center, float radius, bool on,
             const Palette& pal, const ControlState& state, ImU32 onColor = 0);
void drawLed(ImDrawList* dl, const ImVec2& center, float radius, bool on,
             const Palette& pal, const ControlState& state, ImU32 onColor = 0);

void drawMeter(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
               float shownLevel, float peakLevel, float floorDb,
               const Palette& pal);
void drawMeter(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
               float shownLevel, float peakLevel, float floorDb, const Palette& pal);

void drawFader(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
               float value, const Palette& pal, const ControlState& state);
void drawFader(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
               float value, const Palette& pal, const ControlState& state);

// Horizontal dual-handle range slider. lo/hi are normalized 0..1 (drawn
// swapped if hi < lo); the span between them fills with the accent. activeHandle
// highlights a grabbed end: 0 = low, 1 = high, -1 = none.
void drawRangeSlider(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                     float lo, float hi, const Palette& pal,
                     const ControlState& state, int activeHandle = -1);
void drawRangeSlider(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                     float lo, float hi, const Palette& pal,
                     const ControlState& state, int activeHandle = -1);

// Progress bar: an accent fill (0..1 of the width) over a track. indeterminate
// ignores progress and sweeps a chip driven by timeSeconds.
void drawProgressBar(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                     float progress, const Palette& pal,
                     bool indeterminate = false, double timeSeconds = 0.0);
void drawProgressBar(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                     float progress, const Palette& pal,
                     bool indeterminate = false, double timeSeconds = 0.0);

// Waveform view: a min/max peak envelope of `samples` (count entries in -1..1)
// across the width, over a centre line. playhead / selStart / selEnd are 0..1
// fractions of the width; pass < 0 to omit each.
void drawWaveform(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                  const float* samples, int count, const Palette& pal,
                  float playhead = -1.0f, float selStart = -1.0f,
                  float selEnd = -1.0f);
void drawWaveform(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                  const float* samples, int count, const Palette& pal,
                  float playhead = -1.0f, float selStart = -1.0f,
                  float selEnd = -1.0f);

// Spectrum view: `bins` magnitude values (0..1) as bottom-anchored accent bars.
void drawSpectrum(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                  const float* mags, int bins, const Palette& pal);
void drawSpectrum(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                  const float* mags, int bins, const Palette& pal);

// Timeline ruler: bar ticks + bar numbers and minor beat ticks across
// [startBeat, endBeat] (beatsPerBar per bar). playhead is a 0..1 fraction of
// the width (< 0 = none).
void drawTimelineRuler(draw::Surface& surface, draw::FontRef font,
                       float fontSizePx, draw::Vec2 topLeft, draw::Vec2 size,
                       double startBeat, double endBeat, double beatsPerBar,
                       const Palette& pal, float playhead = -1.0f);
void drawTimelineRuler(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                       const ImVec2& size, double startBeat, double endBeat,
                       double beatsPerBar, const Palette& pal,
                       float playhead = -1.0f);

// Automation lane: the curve through `points` (time-sorted, 0..1) with a dot at
// each and a hold to each edge. activePoint highlights a grabbed point;
// playhead is a 0..1 fraction (< 0 = none).
void drawAutomationLane(draw::Surface& surface, draw::Vec2 topLeft,
                        draw::Vec2 size, const AutoPoint* points, int count,
                        const Palette& pal, int activePoint = -1,
                        float playhead = -1.0f);
void drawAutomationLane(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                        const AutoPoint* points, int count, const Palette& pal,
                        int activePoint = -1, float playhead = -1.0f);

// HSV colour picker: a saturation/value square (in the current hue) above a hue
// bar, with cursors on each. h/s/v are 0..1; laid out within `size`.
void drawColorPicker(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                     float h, float s, float v, const Palette& pal);
void drawColorPicker(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                     float h, float s, float v, const Palette& pal);

// One toast panel: a filled rounded rect + accent border + left-aligned text,
// all multiplied by `alpha` (0..1) for fade in/out.
void drawToast(draw::Surface& surface, draw::FontRef font, float fontSizePx,
               draw::Vec2 topLeft, draw::Vec2 size, const char* text,
               const Palette& pal, float alpha);
void drawToast(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
               const ImVec2& size, const char* text, const Palette& pal,
               float alpha);

// Popover frame: a rounded panel + border with an optional beak (triangle) at
// x-offset beakCenterX from topLeft.x, on the top edge (beakOnTop) or bottom.
// beakCenterX < 0 = no beak. The caller draws content inside.
void drawPopover(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                 const Palette& pal, float beakCenterX = -1.0f,
                 bool beakOnTop = false);
void drawPopover(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                 const Palette& pal, float beakCenterX = -1.0f,
                 bool beakOnTop = false);

void drawBadge(ImDrawList* dl, ImFont* font, const ImVec2& topLeft, const char* text,
               float fontSize, ImU32 fill, const Palette& pal);
void drawBadge(draw::Surface& surface, draw::FontRef font, draw::Vec2 topLeft,
               const char* text, float fontSize, ImU32 fill,
               const Palette& pal);

void drawTactileIconButton(draw::Surface& surface, draw::FontRef font,
                           draw::Vec2 topLeft, draw::Vec2 size,
                           const char* glyph, const Palette& pal,
                           const ControlState& state, bool down,
                           ImU32 face = 0);
void drawTactileIconButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                           const ImVec2& size, const char* glyph,
                           const Palette& pal, const ControlState& state,
                           bool down, ImU32 face = 0);

// Tactile key plus an integrated status LED ring inset at the face edge.
// ledLevel 0 = unlit, 1 = fully lit (widgets animate it for arm-blink).
void drawLedButton(draw::Surface& surface, draw::FontRef font,
                   draw::Vec2 topLeft, draw::Vec2 size, const char* glyph,
                   float ledLevel, const Palette& pal,
                   const ControlState& state, bool down,
                   ImU32 ledColor = 0, ImU32 face = 0);
void drawLedButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                   const ImVec2& size, const char* glyph, float ledLevel,
                   const Palette& pal, const ControlState& state, bool down,
                   ImU32 ledColor = 0, ImU32 face = 0);

void drawVectorIconButton(draw::Surface& surface, draw::Vec2 topLeft,
                          draw::Vec2 size, Icon icon, ImU32 accent,
                          const Palette& pal, const ControlState& state,
                          bool active = false, ImU32 bgColor = 0,
                          ImU32 textColor = 0);
void drawVectorIconButton(ImDrawList* dl, const ImVec2& topLeft,
                          const ImVec2& size, Icon icon, ImU32 accent,
                          const Palette& pal, const ControlState& state,
                          bool active = false);

// A single vector transport/scope glyph (Icon), centred at `center`, scaled to
// radius `r`, in `color`. `thickness` 0 draws the FILLED shape (record disc,
// solid play triangle); >0 draws the OUTLINE version at that stroke width (a
// ring, a hollow triangle, a two-arrow repeat) -- the shapes mirror the Lucide
// icon set. Crisp at any size.
void drawTransportGlyph(draw::Surface& surface, Icon icon, draw::Vec2 center,
                        float r, ImU32 color, float thickness = 0.0f);
void drawTransportGlyph(ImDrawList* dl, Icon icon, const ImVec2& center,
                        float r, ImU32 color, float thickness = 0.0f);

// drawTransportGlyph blits the real Lucide transport SVGs rasterised to textures
// so the glyphs stay crisp at button sizes -- a hand-stroked thin polyline's
// anti-alias fringe swallows a ~13px icon (grey mush, rounded-off arrows). Call
// loadTransportIcons once after a GL context exists (same rule as loadSvgTexture;
// the app owns the timing). Idempotent. Without it (headless/no GL) the glyphs
// fall back to procedural stroking. releaseTransportIcons frees the textures.
void loadTransportIcons();
void releaseTransportIcons();

// A transport button: the house OUTLINE chrome (drawOutlineButton) with a
// centred vector transport glyph. `glyphThickness` >0 draws the outline glyph;
// Record shows a ring when idle and a filled disc when engaged (state.selected).
void drawTransportButton(draw::Surface& surface, Icon icon, draw::Vec2 topLeft,
                         draw::Vec2 size, const Palette& pal,
                         const ControlState& state,
                         const OutlineButtonStyle& style = {},
                         float glyphThickness = 1.6f);
void drawTransportButton(ImDrawList* dl, Icon icon, const ImVec2& topLeft,
                         const ImVec2& size, const Palette& pal,
                         const ControlState& state,
                         const OutlineButtonStyle& style = {},
                         float glyphThickness = 1.6f);

void drawButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                const ImVec2& size, const char* text, const Palette& pal,
                const ControlState& state, float fontScale = 0.90f);
void drawButton(draw::Surface& surface, draw::FontRef font, float fontSizePx,
                draw::Vec2 topLeft, draw::Vec2 size, const char* text,
                const Palette& pal, const ControlState& state);

// Square checkbox with optional label. `checked` is the caller-owned on/off
// state; state.hovered/active/focused/disabled drive transient chrome.
void drawCheckbox(draw::Surface& surface, draw::FontRef font, float fontSizePx,
                  draw::Vec2 topLeft, draw::Vec2 size, const char* text,
                  bool checked, const Palette& pal,
                  const ControlState& state);
void drawCheckbox(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                  const ImVec2& size, const char* text, bool checked,
                  const Palette& pal, const ControlState& state,
                  float fontScale = 0.90f);

// Pill group of mutually exclusive segments. `selected` is the current
// option, `hovered` the segment under the pointer (-1 = none); the pressed
// tint follows state.active on the hovered segment.
void drawSegmented(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                   const ImVec2& size, const char* const* labels, int count,
                   int selected, int hovered, const Palette& pal,
                   const ControlState& state, float fontScale = 0.90f);
void drawSegmented(draw::Surface& surface, draw::FontRef font,
                   float fontSizePx, draw::Vec2 topLeft, draw::Vec2 size,
                   const char* const* labels, int count, int selected,
                   int hovered, const Palette& pal,
                   const ControlState& state);

// Tab strip for switching views/pages. Equal-width tabs by default; `selected`
// is the active page, `hovered` is the tab under the pointer (-1 = none).
void drawTabBar(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                const ImVec2& size, const char* const* labels, int count,
                int selected, int hovered, const Palette& pal,
                const ControlState& state, float fontScale = 0.90f);
void drawTabBar(draw::Surface& surface, draw::FontRef font,
                float fontSizePx, draw::Vec2 topLeft, draw::Vec2 size,
                const char* const* labels, int count, int selected,
                int hovered, const Palette& pal, const ControlState& state);

// Multi-state value button body: flat button face with the current option
// label and a pip row marking `index` within `count` states.
void drawCycleButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                     const ImVec2& size, const char* text, int index, int count,
                     const Palette& pal, const ControlState& state,
                     float fontScale = 0.90f);
void drawCycleButton(draw::Surface& surface, draw::FontRef font,
                     float fontSizePx, draw::Vec2 topLeft, draw::Vec2 size,
                     const char* text, int index, int count,
                     const Palette& pal, const ControlState& state);
void drawOutlineButton(draw::Surface& surface, draw::FontRef font,
                       float fontSizePx, draw::Vec2 topLeft, draw::Vec2 size,
                       const char* text, const Palette& pal,
                       const ControlState& state,
                       const OutlineButtonStyle& style = {});
void drawOutlineButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                       const ImVec2& size, const char* text,
                       const Palette& pal, const ControlState& state,
                       const OutlineButtonStyle& style = {});
void drawDefaultButton(const ButtonPaintArgs& args);
void drawButtonWithPainter(const ButtonPaintArgs& args,
                           const ButtonPainter& painter = {});

void drawMenuPanel(ImDrawList* dl, const ImVec2& topLeft,
                   const ImVec2& size, const Palette& pal);
void drawMenuPanel(draw::Surface& surface, draw::Vec2 topLeft,
                   draw::Vec2 size, const Palette& pal);
void drawMenuItem(draw::Surface& surface, draw::FontRef font,
                  draw::FontRef iconFont, float fontSizePx,
                  draw::Vec2 topLeft, draw::Vec2 size,
                  const MenuItem& item, const Palette& pal,
                  const ControlState& state);
void drawMenuItem(ImDrawList* dl, ImFont* font, ImFont* iconFont,
                  const ImVec2& topLeft, const ImVec2& size,
                  const MenuItem& item, const Palette& pal,
                  const ControlState& state, float fontScale = 0.90f);

void drawListItem(draw::Surface& surface, draw::FontRef font, float fontSizePx,
                  draw::Vec2 topLeft, draw::Vec2 size, const char* text,
                  const Palette& pal, const ControlState& state);
void drawListItem(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                  const ImVec2& size, const char* text, const Palette& pal,
                  const ControlState& state, float fontScale = 0.90f);

void drawValueRow(draw::Surface& surface, draw::FontRef font, float fontSizePx,
                  draw::Vec2 topLeft, draw::Vec2 size, const char* label,
                  const char* valueText, const Palette& pal,
                  const ControlState& state, bool draggable = false);
void drawValueRow(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                  const ImVec2& size, const char* label, const char* valueText,
                  const Palette& pal, const ControlState& state,
                  float fontScale = 0.90f, bool draggable = false);

void drawPatternGrid(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                     const bool* cells, int rows, int steps, int playheadStep,
                     const Palette& pal, const ControlState& state);
void drawPatternGrid(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                     const bool* cells, int rows, int steps, int playheadStep,
                     const Palette& pal, const ControlState& state);
// Grid with custom cell bodies: SND draws backdrop, playhead tint, border,
// and focus; `cellPainter` draws each cell from PatternCellPaintArgs.
void drawPatternGrid(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                     const bool* cells, int rows, int steps, int playheadStep,
                     const Palette& pal, const ControlState& state,
                     const PatternCellPainter& cellPainter);
void drawPatternGrid(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                     const bool* cells, int rows, int steps, int playheadStep,
                     const Palette& pal, const ControlState& state,
                     const PatternCellPainter& cellPainter);

void drawXYPad(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
               float x, float y, const Palette& pal, const ControlState& state);
void drawXYPad(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
               float x, float y, const Palette& pal, const ControlState& state);
void drawDefaultXYPad(const XYPadPaintArgs& args);
// Body painter hook for xyPad faces (custom maps/pucks). The painter
// draws the body with focus suppressed; SND draws the focus ring after, as
// with the knob/button hooks.
void drawXYPadWithPainter(const XYPadPaintArgs& args,
                          const XYPadPainter& painter = {});

void drawKeyboard(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                  int firstNote, int octaves, int mouseNote, const bool* lit,
                  const Palette& pal, const ControlState& state);
void drawKeyboard(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                  int firstNote, int octaves, int mouseNote, const bool* lit,
                  const Palette& pal, const ControlState& state);

// Segment ease used by envelope/curve editors. Positive tension slows the
// start of a segment; negative tension makes it start faster.
float envelopeEase(float t, float tension);

void drawEnvelope(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                  const std::vector<EnvPoint>& points,
                  const std::vector<float>* tensions,
                  int hotPoint, int activePoint,
                  int hotSegment, int activeSegment,
                  const Palette& pal, const ControlState& state);
void drawEnvelope(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                  const std::vector<EnvPoint>& points,
                  const std::vector<float>* tensions,
                  int hotPoint, int activePoint,
                  int hotSegment, int activeSegment,
                  const Palette& pal, const ControlState& state);

void drawGraphGrid(draw::Surface& surface, draw::Vec2 topLeft, draw::Vec2 size,
                   draw::Vec2 pan, float zoom, const Palette& pal,
                   const ControlState& state,
                   const GraphSurfaceStyle& style = {}, double timeSeconds = 0.0);
void drawGraphGrid(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                   const ImVec2& pan, float zoom, const Palette& pal,
                   const ControlState& state,
                   const GraphSurfaceStyle& style = {});
// `zoom` scales the gravity constants (bulge/sag minimums) so a wire keeps
// its graph-space shape at any viewport zoom -- pass the viewport zoom when
// the endpoints are in zoomed screen space, and 1 when they are graph-space.
// This keeps the drawn curve identical to the hit-tested one (hit testing
// samples in graph space).
void drawCable(draw::Surface& surface, draw::Vec2 from, draw::Vec2 to,
               const Palette& pal, const ControlState& state,
               ImU32 color = 0, float thickness = 2.6f,
               const GraphSurfaceStyle& style = {}, float zoom = 1.0f);
void drawCable(ImDrawList* dl, const ImVec2& from, const ImVec2& to,
               const Palette& pal, const ControlState& state,
               ImU32 color = 0, float thickness = 2.6f,
               const GraphSurfaceStyle& style = {}, float zoom = 1.0f);
// Draws module chrome only. GraphNode/ModuleBox internals such as meters,
// readouts, toggles, actions, and ports remain structured UI parts.
// Body + per-skin corner, selected 0xffffc24a border, 24px header (scale via
// headerH) with a 3px accent stripe and bold title, 1px black underline,
// BYPASS veil + tag. Pass headerH pre-scaled by the viewport zoom.
// timeSeconds drives the style's rim spin (only read when the skin animates;
// pass the render clock).
void drawModuleBox(draw::Surface& surface, draw::FontRef font, float fontSizePx,
                   draw::Vec2 topLeft, draw::Vec2 size, const char* title,
                   const Palette& pal, const ControlState& state,
                   bool bypassed = false, bool error = false,
                   const GraphSurfaceStyle& style = {},
                   float headerH = 24.0f, double timeSeconds = 0.0);
void drawModuleBox(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                   const ImVec2& size, const char* title, const Palette& pal,
                   const ControlState& state, bool bypassed = false,
                   bool error = false,
                   const GraphSurfaceStyle& style = {});

void drawSectionHeader(draw::Surface& surface, draw::FontRef font,
                       draw::Vec2 topLeft, const char* text, float fontSize,
                       float width, const Palette& pal);
void drawSectionHeader(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                       const char* text, float fontSize, float width,
                       const Palette& pal);

} // namespace snd::ui::paint
