// Pure retained UI window backed by OpenGL, without Dear ImGui.
#pragma once

#include "snd/ui_draw.h"
#include "snd/ui_retained.h"

#include <memory>
#include <string>
#include <vector>

namespace snd::ui::retained {

class PaintRenderer;

class GlWindow {
public:
    GlWindow();
    ~GlWindow();

    GlWindow(const GlWindow&) = delete;
    GlWindow& operator=(const GlWindow&) = delete;

    bool create(int width, int height, const std::string& title,
                bool decorated = true);
    void destroy();

    bool shouldClose() const;
    void setShouldClose(bool close);

    // Pumps platform/UI events, refreshes caller-owned bindings, lays out the
    // tree to the current window size, renders it, and leaves endFrame() to
    // present the swap chain. No ImGui context or font atlas is required.
    bool beginFrame(Tree& tree, PaintRenderer& renderer);
    void endFrame();

    int width() const;
    int height() const;
    Vec2 size() const;

    void setTitle(const std::string& title);
    void setClearColor(draw::Color color);
    draw::FrameContext frameContext() const;
    std::vector<std::string> takeDroppedFiles();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace snd::ui::retained
