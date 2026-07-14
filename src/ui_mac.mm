// macOS native window dragging for frameless windows: hand the current
// mouse-down event to AppKit so the OS moves the window (no manual math,
// no feedback wobble).

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include <cmath>

namespace snd::ui {

void nativeDragImpl(GLFWwindow* window)
{
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    NSEvent* event = [NSApp currentEvent];
    if (nsWindow && event)
        [nsWindow performWindowDragWithEvent:event];
}

// Frameless windows need the resizable style bit or AppKit refuses both the
// native fullscreen Space transition and smooth zoom; GLFW leaves borderless
// windows non-resizable on macOS. Keeping the mask borderless preserves the
// app-drawn title bar.
void macPrepareFramelessImpl(GLFWwindow* window)
{
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    if (!nsWindow)
        return;
    [nsWindow setStyleMask:[nsWindow styleMask] | NSWindowStyleMaskResizable];
    [nsWindow setPreservesContentDuringLiveResize:NO];
    [nsWindow setCollectionBehavior:[nsWindow collectionBehavior] |
                                    NSWindowCollectionBehaviorFullScreenPrimary];
}

// Mac zoom for frameless windows: fill the screen's visible frame, keeping
// the app-drawn title bar; toggling restores each window's saved frame. GLFW's
// own maximize is a no-op for borderless Cocoa windows, so we do it directly.
static char kSavedZoomFrameKey;

bool macIsZoomedImpl(GLFWwindow* window)
{
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    if (!nsWindow)
        return false;
    const NSRect visible = [[nsWindow screen] visibleFrame];
    const NSRect frame = [nsWindow frame];
    return std::abs(frame.origin.x - visible.origin.x) < 1.0 &&
           std::abs(frame.origin.y - visible.origin.y) < 1.0 &&
           std::abs(frame.size.width - visible.size.width) < 1.0 &&
           std::abs(frame.size.height - visible.size.height) < 1.0;
}

void macToggleZoomImpl(GLFWwindow* window)
{
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    if (!nsWindow)
        return;
    if (macIsZoomedImpl(window)) {
        NSValue* saved = (NSValue*)objc_getAssociatedObject(
            nsWindow, &kSavedZoomFrameKey);
        if (saved)
            [nsWindow setFrame:[saved rectValue] display:YES animate:YES];
        return;
    }
    objc_setAssociatedObject(nsWindow, &kSavedZoomFrameKey,
                             [NSValue valueWithRect:[nsWindow frame]],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [nsWindow setFrame:[[nsWindow screen] visibleFrame] display:YES animate:YES];
}

bool macIsFullscreenImpl(GLFWwindow* window)
{
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    return nsWindow && ([nsWindow styleMask] & NSWindowStyleMaskFullScreen) != 0;
}

void macToggleFullscreenImpl(GLFWwindow* window)
{
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    if (!nsWindow)
        return;
    // Frameless windows need the collection behaviour before the first toggle.
    [nsWindow setCollectionBehavior:[nsWindow collectionBehavior] |
                                    NSWindowCollectionBehaviorFullScreenPrimary];
    [nsWindow toggleFullScreen:nil];
}

} // namespace snd::ui
