// snd::ui -- window + Dear ImGui frame shell, plus SND's own widget helpers.
// ImGui itself stays vendored/unmodified; anything custom is drawn on top via
// ImDrawList. Consumers include this and use ImGui:: directly for widgets.
#pragma once

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

} // namespace snd::ui
