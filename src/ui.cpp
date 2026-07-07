// snd::ui implementation: GLFW + OpenGL3 + Dear ImGui shell.

#include "snd/ui.h"

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

struct Window::Impl {
    GLFWwindow* window = nullptr;
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

    impl->window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!impl->window) {
        glfwTerminate();
        return false;
    }
    glfwSetWindowUserPointer(impl->window, impl.get());
    glfwSetDropCallback(impl->window, Impl::dropCallback);
    glfwMakeContextCurrent(impl->window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(impl->window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    return true;
}

void Window::destroy()
{
    if (!impl->window)
        return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(impl->window);
    glfwTerminate();
    impl->window = nullptr;
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
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    return true;
}

void Window::endFrame()
{
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
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
        p, ImVec2(p.x + size.x, p.y + size.y), tl, tr, br, bl);
    ImGui::Dummy(size);
}

bool gradientButton(const char* label, const ImVec2& size, ImU32 top, ImU32 bottom)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, size);
    bool hovered = ImGui::IsItemHovered();

    ImU32 t = top, b = bottom;
    if (hovered) {
        // brighten slightly on hover
        auto lift = [](ImU32 c) {
            ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
            v.x = v.x + (1.0f - v.x) * 0.15f;
            v.y = v.y + (1.0f - v.y) * 0.15f;
            v.z = v.z + (1.0f - v.z) * 0.15f;
            return ImGui::ColorConvertFloat4ToU32(v);
        };
        t = lift(t);
        b = lift(b);
    }

    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor(p, ImVec2(p.x + size.x, p.y + size.y), t, t, b, b);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(0, 0, 0, 120));

    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f),
                IM_COL32_WHITE, label);
    return pressed;
}

bool iconButton(const char* id, Icon icon, const ImVec2& size, ImU32 accent, bool active)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();

    auto* dl = ImGui::GetWindowDrawList();
    const ImGuiStyle& st = ImGui::GetStyle();
    ImU32 bg = ImGui::GetColorU32(held      ? st.Colors[ImGuiCol_ButtonActive]
                                  : hovered ? st.Colors[ImGuiCol_ButtonHovered]
                                            : st.Colors[ImGuiCol_Button]);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), bg, 4.0f);
    if (active)
        dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), accent, 4.0f, 0, 2.0f);

    ImU32 fg = active ? accent : ImGui::GetColorU32(st.Colors[ImGuiCol_Text]);
    ImVec2 c(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    float r = std::min(size.x, size.y) * 0.28f; // icon radius

    switch (icon) {
    case Icon::Play:
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.7f, c.y - r), ImVec2(c.x - r * 0.7f, c.y + r),
                              ImVec2(c.x + r, c.y), fg);
        break;
    case Icon::Stop:
        dl->AddRectFilled(ImVec2(c.x - r * 0.8f, c.y - r * 0.8f),
                          ImVec2(c.x + r * 0.8f, c.y + r * 0.8f), fg, 2.0f);
        break;
    case Icon::Record:
        dl->AddCircleFilled(c, r * 0.9f, fg, 24);
        break;
    case Icon::SkipToStart:
        dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x - r + 2.5f, c.y + r), fg);
        dl->AddTriangleFilled(ImVec2(c.x + r, c.y - r), ImVec2(c.x + r, c.y + r),
                              ImVec2(c.x - r * 0.5f, c.y), fg);
        break;
    case Icon::SkipToEnd:
        dl->AddRectFilled(ImVec2(c.x + r - 2.5f, c.y - r), ImVec2(c.x + r, c.y + r), fg);
        dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x - r, c.y + r),
                              ImVec2(c.x + r * 0.5f, c.y), fg);
        break;
    case Icon::Loop: {
        dl->PathArcTo(c, r, 0.3f, 2.0f * (float)M_PI - 0.6f, 24);
        dl->PathStroke(fg, 0, 2.0f);
        // arrowhead at the arc's open end
        float ax = c.x + r * std::cos(0.3f);
        float ay = c.y + r * std::sin(0.3f);
        dl->AddTriangleFilled(ImVec2(ax - 4, ay - 1), ImVec2(ax + 3, ay + 3),
                              ImVec2(ax + 2, ay - 5), fg);
        break;
    }
    case Icon::Waveform: {
        // stylised min/max columns
        const float heights[7] = {0.35f, 0.75f, 0.5f, 1.0f, 0.6f, 0.85f, 0.4f};
        float step = (r * 2.2f) / 7.0f;
        for (int i = 0; i < 7; ++i) {
            float x = c.x - r * 1.1f + step * (i + 0.5f);
            float h = r * heights[i];
            dl->AddLine(ImVec2(x, c.y - h), ImVec2(x, c.y + h), fg, 2.0f);
        }
        break;
    }
    case Icon::Spectrum: {
        // rising frequency sweep
        for (int i = 0; i < 12; ++i) {
            float t = i / 11.0f;
            float x = c.x - r * 1.1f + t * r * 2.2f;
            float h = r * (0.2f + 0.8f * t);
            dl->AddLine(ImVec2(x, c.y + r * 0.9f), ImVec2(x, c.y + r * 0.9f - h * 1.8f), fg,
                        1.5f);
        }
        break;
    }
    }
    return pressed;
}

} // namespace snd::ui
