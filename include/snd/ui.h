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

    bool create(int width, int height, const std::string& title);
    void destroy();

    bool shouldClose() const;

    // Poll events + start an ImGui frame. Returns false if the window is gone.
    bool beginFrame();
    // Render the ImGui frame and swap buffers.
    void endFrame();

    void setTitle(const std::string& title);
    int width() const;
    int height() const;

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

} // namespace snd::ui
