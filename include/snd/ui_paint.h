// Shared SND UI paint primitives.
//
// These helpers are draw-only: they do not own interaction, layout, app state,
// plugin parameters, or audio-thread data. Immediate and retained widgets can
// call the same functions after their own hit-testing/state handling so the
// pixels, focus treatment, and hardware-control vocabulary stay aligned.
#pragma once

#include <cstdint>

#include "snd/ui.h"

namespace snd::ui::paint {

struct ControlState {
    bool hovered = false;
    bool active = false;
    bool focused = false;
    bool disabled = false;
    bool selected = false;
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

void drawKnob(ImDrawList* dl, const ImVec2& topLeft, float size, float frac,
              KnobStyle style, const Palette& pal, const ControlState& state,
              bool bipolar = false, ImU32 accent = 0);

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

void drawButton(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                const ImVec2& size, const char* text, const Palette& pal,
                const ControlState& state, float fontScale = 0.90f);

void drawListItem(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                  const ImVec2& size, const char* text, const Palette& pal,
                  const ControlState& state, float fontScale = 0.90f);

void drawPatternGrid(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                     const bool* cells, int rows, int steps, int playheadStep,
                     const Palette& pal, const ControlState& state);

void drawXYPad(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
               float x, float y, const Palette& pal, const ControlState& state);

void drawKeyboard(ImDrawList* dl, const ImVec2& topLeft, const ImVec2& size,
                  int firstNote, int octaves, int mouseNote, const bool* lit,
                  const Palette& pal, const ControlState& state);

void drawSectionHeader(ImDrawList* dl, ImFont* font, const ImVec2& topLeft,
                       const char* text, float fontSize, float width,
                       const Palette& pal);

} // namespace snd::ui::paint
