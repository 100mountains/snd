// snd::ui implementation: GLFW + OpenGL3 + Dear ImGui shell.

#include "snd/ui.h"
#include "snd/platform.h"
#include "snd/ui_paint.h"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#endif

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <cmath>
#include <vector>

namespace snd::ui {

namespace {
int gWindowCount = 0;             // glfwTerminate when the last one dies
GLFWwindow* gShareWindow = nullptr; // GL object sharing (textures cross windows)
} // namespace

struct Window::Impl {
    GLFWwindow* window = nullptr;
    ImGuiContext* ctx = nullptr; // one ImGui context per window
    std::vector<std::string> droppedFiles;

    static void dropCallback(GLFWwindow* w, int count, const char** paths)
    {
        auto* self = (Impl*)glfwGetWindowUserPointer(w);
        for (int i = 0; i < count; ++i)
            self->droppedFiles.push_back(paths[i]);
    }
};

Window::Window() : impl(new Impl) {}
Window::~Window() { destroy(); }

bool Window::create(int width, int height, const std::string& title, bool decorated)
{
    if (!glfwInit())
        return false;

    glfwWindowHint(GLFW_DECORATED, decorated ? GLFW_TRUE : GLFW_FALSE);

#if defined(__APPLE__)
    const char* glslVersion = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    // share GL objects with the first window so textures work everywhere
    impl->window =
        glfwCreateWindow(width, height, title.c_str(), nullptr, gShareWindow);
    if (!impl->window) {
        if (gWindowCount == 0)
            glfwTerminate();
        return false;
    }
    if (!gShareWindow)
        gShareWindow = impl->window;
    ++gWindowCount;

    glfwSetWindowUserPointer(impl->window, impl.get());
    glfwSetDropCallback(impl->window, Impl::dropCallback);
    glfwMakeContextCurrent(impl->window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    impl->ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(impl->ctx);
    loadFonts(); // text + embedded Material/Lucide icon fonts (this context)
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(impl->window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    return true;
}

void Window::destroy()
{
    if (!impl->window)
        return;
    glfwMakeContextCurrent(impl->window);
    ImGui::SetCurrentContext(impl->ctx);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(impl->ctx);
    impl->ctx = nullptr;
    if (impl->window == gShareWindow)
        gShareWindow = nullptr;
    glfwDestroyWindow(impl->window);
    impl->window = nullptr;
    if (--gWindowCount == 0)
        glfwTerminate();
}

bool Window::shouldClose() const
{
    return !impl->window || glfwWindowShouldClose(impl->window);
}

void Window::setShouldClose(bool close)
{
    if (impl->window)
        glfwSetWindowShouldClose(impl->window, close ? GLFW_TRUE : GLFW_FALSE);
}

bool Window::beginFrame()
{
    if (!impl->window)
        return false;
    glfwPollEvents();
    snd::platform::processMainQueue(); // runOnMain() jobs + frame-pumped timers
    glfwMakeContextCurrent(impl->window);
    ImGui::SetCurrentContext(impl->ctx); // per-window ImGui context
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    return true;
}

void Window::endFrame()
{
    glfwMakeContextCurrent(impl->window);
    ImGui::SetCurrentContext(impl->ctx);
    ImGui::Render();
    int w, h;
    glfwGetFramebufferSize(impl->window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(impl->window);
}

void Window::setTitle(const std::string& title)
{
    if (impl->window)
        glfwSetWindowTitle(impl->window, title.c_str());
}

int Window::width() const
{
    int w = 0, h = 0;
    if (impl->window) glfwGetWindowSize(impl->window, &w, &h);
    return w;
}

int Window::height() const
{
    int w = 0, h = 0;
    if (impl->window) glfwGetWindowSize(impl->window, &w, &h);
    return h;
}

void Window::getPosition(int& x, int& y) const
{
    x = y = 0;
    if (impl->window)
        glfwGetWindowPos(impl->window, &x, &y);
}

void Window::setPosition(int x, int y)
{
    if (impl->window)
        glfwSetWindowPos(impl->window, x, y);
}

void Window::setSize(int width, int height)
{
    if (impl->window)
        glfwSetWindowSize(impl->window, std::max(320, width), std::max(240, height));
}

void Window::minimize()
{
    if (impl->window)
        glfwIconifyWindow(impl->window);
}

#if defined(__APPLE__)
void nativeDragImpl(GLFWwindow* window); // ui_mac.mm
#endif

void Window::beginNativeDrag()
{
    if (!impl->window)
        return;
#if defined(__APPLE__)
    nativeDragImpl(impl->window);
#elif defined(_WIN32)
    // Standard Win32 trick: pretend the press hit the title bar.
    HWND hwnd = glfwGetWin32Window(impl->window);
    ReleaseCapture();
    SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
#endif
}

void Window::toggleMaximize()
{
    if (!impl->window)
        return;
    if (glfwGetWindowAttrib(impl->window, GLFW_MAXIMIZED))
        glfwRestoreWindow(impl->window);
    else
        glfwMaximizeWindow(impl->window);
}

std::vector<std::string> Window::takeDroppedFiles()
{
    auto out = std::move(impl->droppedFiles);
    impl->droppedFiles.clear();
    return out;
}

// --- custom widgets ---------------------------------------------------------

void gradientPanel(const ImVec2& size, ImU32 tl, ImU32 tr, ImU32 br, ImU32 bl)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    paint::drawGradientPanel(ImGui::GetWindowDrawList(), p, size, tl, tr, br, bl);
    ImGui::Dummy(size);
}

bool gradientButton(const char* label, const ImVec2& size, ImU32 top, ImU32 bottom)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, size);
    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = ImGui::IsItemFocused();
    paint::drawAnimatedButton(ImGui::GetWindowDrawList(), ImGui::GetFont(), p, size,
                              label, top, bottom, palette(), state, 0.0f);
    return pressed;
}

bool animatedButton(const char* label, const ImVec2& size, ImU32 top, ImU32 bottom)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, size);
    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = ImGui::IsItemFocused();
    const float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 3.6f);
    paint::drawAnimatedButton(ImGui::GetWindowDrawList(), ImGui::GetFont(), p, size,
                              label, top, bottom, palette(), state, pulse);
    return pressed;
}

bool button(const char* label, const ImVec2& size,
            const paint::ButtonPainter& painter)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, size);
    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = ImGui::IsItemFocused();

    paint::ButtonPaintArgs args;
    args.drawList = ImGui::GetWindowDrawList();
    args.font = ImGui::GetFont();
    args.topLeft = p;
    args.size = size;
    args.text = label;
    args.palette = &palette();
    args.state = &state;
    paint::drawButtonWithPainter(args, painter);
    return pressed;
}

bool iconButton(const char* id, Icon icon, const ImVec2& size, ImU32 accent, bool active)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = ImGui::IsItemFocused();
    paint::drawVectorIconButton(ImGui::GetWindowDrawList(), p, size, icon, accent,
                                palette(), state, active);
    return pressed;
}

} // namespace snd::ui
