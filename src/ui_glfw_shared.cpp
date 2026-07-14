#include "ui_glfw_shared.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <string>
#include <vector>

namespace snd::ui::detail {

namespace {
std::vector<GLFWwindow*> gWindows;
}

bool ensureGlfwInitialized()
{
    return glfwInit() == GLFW_TRUE;
}

bool hasGlfwWindows()
{
    return !gWindows.empty();
}

GLFWwindow* sharedGlfwContext()
{
    return gWindows.empty() ? nullptr : gWindows.front();
}

void registerGlfwWindow(GLFWwindow* window)
{
    if (!window)
        return;
    if (std::find(gWindows.begin(), gWindows.end(), window) == gWindows.end())
        gWindows.push_back(window);
}

void unregisterGlfwWindow(GLFWwindow* window)
{
    gWindows.erase(std::remove(gWindows.begin(), gWindows.end(), window),
                   gWindows.end());
    terminateGlfwIfIdle();
}

void terminateGlfwIfIdle()
{
    if (gWindows.empty())
        glfwTerminate();
}

} // namespace snd::ui::detail

namespace snd::ui {

// GLFW's clipboard is process-global; the window handle is only a context
// token, so any live window works and none means no clipboard yet.
std::string getClipboardText()
{
    GLFWwindow* window = detail::sharedGlfwContext();
    if (!window)
        return {};
    const char* text = glfwGetClipboardString(window);
    return text ? std::string(text) : std::string();
}

void setClipboardText(const std::string& text)
{
    if (GLFWwindow* window = detail::sharedGlfwContext())
        glfwSetClipboardString(window, text.c_str());
}

} // namespace snd::ui
