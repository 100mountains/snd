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
};

struct XYPadPaintArgs {
    ImDrawList* drawList = nullptr;
    ImVec2 topLeft;
    ImVec2 size;
    float x = 0.0f; // normalized 0..1
    float y = 0.0f; // normalized 0..1, 0 = bottom
    const Palette* palette = nullptr;
    const ControlState* state = nullptr;
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
};

struct GraphSurfaceStyle {
    enum class Backdrop {
        Flat,
        Grid,
        GreenGrid,
        Mosaic,
    };

    ImU32 node = 0;
    ImU32 header = 0;
    ImU32 border = 0;
    ImU32 text = 0;
    ImU32 accent = 0;
    ImU32 selectedBorder = 0;
    float corner = 5.0f;
    bool squarePins = false;
    ImU32 pinAudio = 0;
    ImU32 pinMidi = 0;
    ImU32 pinControl = 0;
    float wireThickness = 2.6f;
    bool wireDroop = false;
    Backdrop backdrop = Backdrop::Grid;
    ImU32 backdropFill = 0;
};

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
void drawFocusRing(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
                   const Palette& pal, float rounding, float expand = 2.0f);

void drawGradientPanel(ImDrawList* dl, const ImVec2& topLeft,
                       const ImVec2& size, ImU32 topLeftColor,
                       ImU32 topRightColor, ImU32 bottomRightColor,
                       ImU32 bottomLeftColor);

// Rounded rect filled with a vertical top->bottom gradient (ImDrawList has
// no rounded multi-colour rect; the corner caps are 1px strips following the
// corner circle, the straight body is one exact gradient quad).
void drawGradientRect(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
                      ImU32 top, ImU32 bottom, float rounding = 0.0f);

// Arc stroked with a colour gradient from a0 to a1, sampled per segment
// (murk-style two-colour value arcs).
void drawGradientArc(ImDrawList* dl, const ImVec2& center, float radius,
                     float a0, float a1, ImU32 colStart, ImU32 colEnd,
                     float thickness, int segments = 32);

void drawAnimatedButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                        const ImVec2& size, const char* text, ImU32 top,
                        ImU32 bottom, const Palette& pal,
                        const ControlState& state, float pulse = 0.0f,
                        float fontScale = 0.90f);

void drawKnob(ImDrawList* dl, const ImVec2& topLeft, float size, float frac,
              KnobStyle style, const Palette& pal, const ControlState& state,
              bool bipolar = false, ImU32 accent = 0);
// Modulation overlay: depth arc from the value angle + live position dot at
// the knob rim. SND owns this overlay (drawKnobWithPainter applies it after
// the body, before the focus ring) so custom faces keep the shared mod look.
void drawKnobModRing(ImDrawList* dl, const ImVec2& topLeft, float size,
                     float frac, const KnobMod& mod, const Palette& pal,
                     ImU32 accent = 0);
void drawDefaultKnob(const KnobPaintArgs& args);
void drawKnobWithPainter(const KnobPaintArgs& args,
                         const KnobPainter& painter = {});

// Randomise-window overlay ("ghost fences", murk's GhostOverlay): an
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
void drawKnobWindow(ImDrawList* dl, const ImVec2& topLeft, float size,
                    const KnobWindow& win, const Palette& pal,
                    ImU32 accent = 0, ImU32 lockColor = 0, float uiScale = 1.0f);

// Which window end a press should grab: 0 = lo, 1 = hi, or -1 when the press
// is outside the control rect grown by 6*uiScale px. Inside, the nearest end
// by knob angle wins (murk knobProp semantics); callers keep click-vs-drag
// lock toggling.
int knobWindowHitEnd(const ImVec2& topLeft, float size, const KnobWindow& win,
                     const ImVec2& pressPos, float uiScale = 1.0f);

// The same window as an index bracket along a combo/dropdown's bottom edge.
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
void drawPadlock(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 color);

void drawToggle(ImDrawList* dl, const ImVec2& topLeft, float width, float height,
                float anim, const Palette& pal, const ControlState& state);

void drawLed(ImDrawList* dl, const ImVec2& center, float radius, bool on,
             const Palette& pal, const ControlState& state, ImU32 onColor = 0);

void drawMeter(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
               float shownLevel, float peakLevel, float floorDb, const Palette& pal);

void drawFader(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
               float value, const Palette& pal, const ControlState& state);

void drawBadge(ImDrawList* dl, ImFont* font, const ImVec2& topLeft, const char* text,
               float fontSize, ImU32 fill, const Palette& pal);

void drawTactileIconButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                           const ImVec2& size, const char* glyph,
                           const Palette& pal, const ControlState& state,
                           bool down, ImU32 face = 0);

// Tactile key plus an integrated status LED ring inset at the face edge.
// ledLevel 0 = unlit, 1 = fully lit (widgets animate it for arm-blink).
void drawLedButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                   const ImVec2& size, const char* glyph, float ledLevel,
                   const Palette& pal, const ControlState& state, bool down,
                   ImU32 ledColor = 0, ImU32 face = 0);

void drawVectorIconButton(ImDrawList* dl, const ImVec2& topLeft,
                          const ImVec2& size, Icon icon, ImU32 accent,
                          const Palette& pal, const ControlState& state,
                          bool active = false);

void drawButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                const ImVec2& size, const char* text, const Palette& pal,
                const ControlState& state, float fontScale = 0.90f);

// Pill group of mutually exclusive segments. `selected` is the current
// option, `hovered` the segment under the pointer (-1 = none); the pressed
// tint follows state.active on the hovered segment.
void drawSegmented(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                   const ImVec2& size, const char* const* labels, int count,
                   int selected, int hovered, const Palette& pal,
                   const ControlState& state, float fontScale = 0.90f);

// Multi-state value button body: flat button face with the current option
// label and a pip row marking `index` within `count` states.
void drawCycleButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                     const ImVec2& size, const char* text, int index, int count,
                     const Palette& pal, const ControlState& state,
                     float fontScale = 0.90f);
void drawOutlineButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                       const ImVec2& size, const char* text,
                       const Palette& pal, const ControlState& state,
                       const OutlineButtonStyle& style = {});
void drawDefaultButton(const ButtonPaintArgs& args);
void drawButtonWithPainter(const ButtonPaintArgs& args,
                           const ButtonPainter& painter = {});

void drawMenuPanel(ImDrawList* dl, const ImVec2& topLeft,
                   const ImVec2& size, const Palette& pal);
void drawMenuItem(ImDrawList* dl, ImFont* font, ImFont* iconFont,
                  const ImVec2& topLeft, const ImVec2& size,
                  const MenuItem& item, const Palette& pal,
                  const ControlState& state, float fontScale = 0.90f);

void drawListItem(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                  const ImVec2& size, const char* text, const Palette& pal,
                  const ControlState& state, float fontScale = 0.90f);

void drawValueRow(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                  const ImVec2& size, const char* label, const char* valueText,
                  const Palette& pal, const ControlState& state,
                  float fontScale = 0.90f, bool draggable = false);

void drawPatternGrid(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                     const bool* cells, int rows, int steps, int playheadStep,
                     const Palette& pal, const ControlState& state);
// Grid with custom cell bodies: SND draws backdrop, playhead tint, border,
// and focus; `cellPainter` draws each cell from PatternCellPaintArgs.
void drawPatternGrid(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                     const bool* cells, int rows, int steps, int playheadStep,
                     const Palette& pal, const ControlState& state,
                     const PatternCellPainter& cellPainter);

void drawXYPad(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
               float x, float y, const Palette& pal, const ControlState& state);
void drawDefaultXYPad(const XYPadPaintArgs& args);
// Body painter hook for xyPad faces (murk-style maps/pucks). The painter
// draws the body with focus suppressed; SND draws the focus ring after, as
// with the knob/button hooks.
void drawXYPadWithPainter(const XYPadPaintArgs& args,
                          const XYPadPainter& painter = {});

void drawKeyboard(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
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

void drawGraphGrid(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                   const ImVec2& pan, float zoom, const Palette& pal,
                   const ControlState& state,
                   const GraphSurfaceStyle& style = {});
void drawCable(ImDrawList* dl, const ImVec2& from, const ImVec2& to,
               const Palette& pal, const ControlState& state,
               ImU32 color = 0, float thickness = 2.6f,
               const GraphSurfaceStyle& style = {});
// Draws module chrome only. GraphNode/ModuleBox internals such as meters,
// readouts, toggles, actions, and ports remain structured UI parts.
void drawModuleBox(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                   const ImVec2& size, const char* title, const Palette& pal,
                   const ControlState& state, bool bypassed = false,
                   bool error = false,
                   const GraphSurfaceStyle& style = {});

void drawSectionHeader(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                       const char* text, float fontSize, float width,
                       const Palette& pal);

} // namespace snd::ui::paint
