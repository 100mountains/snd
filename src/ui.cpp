// snd::ui implementation: GLFW + OpenGL3 + Dear ImGui shell.

#include "snd/ui.h"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

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

bool Window::create(int width, int height, const std::string& title)
{
    if (!glfwInit())
        return false;

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

} // namespace snd::ui
