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

ImU32 withAlpha(ImU32 c, uint32_t a);
ImU32 mix(ImU32 a, ImU32 b, float t);
ImVec4 toVec4(ImU32 c);

// Linear amplitude -> 0..1 position on a floorDb..0 dB scale.
float dbNorm(float linear, float floorDb);

// Rotary sweep shared by SND knobs: frac 0 = 7 o'clock, frac 1 = 5 o'clock.
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

void drawXYPad(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
               float x, float y, const Palette& pal, const ControlState& state);

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
                   const ControlState& state);
void drawCable(ImDrawList* dl, const ImVec2& from, const ImVec2& to,
               const Palette& pal, const ControlState& state,
               ImU32 color = 0, float thickness = 2.6f);
// Draws module chrome only. GraphNode/ModuleBox internals such as meters,
// readouts, toggles, actions, and ports remain structured UI parts.
void drawModuleBox(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                   const ImVec2& size, const char* title, const Palette& pal,
                   const ControlState& state, bool bypassed = false,
                   bool error = false);

void drawSectionHeader(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                       const char* text, float fontSize, float width,
                       const Palette& pal);

} // namespace snd::ui::paint
