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

    void setTitle(const std::string& title);
    int width() const;
    int height() const;

    // For app-drawn title bars on frameless windows.
    void getPosition(int& x, int& y) const;
    void setPosition(int x, int y);
    void setSize(int width, int height);
    void minimize();
    void toggleMaximize();

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

// Rotary knob, normalized 0..1. Returns true while the value is changing.
// size 0 = a sensible default. The label is drawn under the knob.
bool knob(const char* label, float* value, float size = 0.0f,
          const char* format = "%.2f");

// Knob over a dB range (label shows dB). 0 dB gets a tick if in range.
bool knobDb(const char* label, float* db, float minDb, float maxDb,
            float size = 0.0f);

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

} // namespace snd::ui
