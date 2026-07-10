#pragma once

struct GLFWwindow;

namespace snd::ui::detail {

bool ensureGlfwInitialized();
bool hasGlfwWindows();
GLFWwindow* sharedGlfwContext();
void registerGlfwWindow(GLFWwindow* window);
void unregisterGlfwWindow(GLFWwindow* window);
void terminateGlfwIfIdle();

} // namespace snd::ui::detail
