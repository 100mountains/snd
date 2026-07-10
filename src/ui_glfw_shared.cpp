#include "ui_glfw_shared.h"

#include <GLFW/glfw3.h>

#include <algorithm>
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
