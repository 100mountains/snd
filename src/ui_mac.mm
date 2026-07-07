// macOS native window dragging for frameless windows: hand the current
// mouse-down event to AppKit so the OS moves the window (no manual math,
// no feedback wobble).

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>

namespace snd::ui {

void nativeDragImpl(GLFWwindow* window)
{
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    NSEvent* event = [NSApp currentEvent];
    if (nsWindow && event)
        [nsWindow performWindowDragWithEvent:event];
}

} // namespace snd::ui
