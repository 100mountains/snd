// Internal helper: a floating native window that hosts a plugin's editor
// view. Implemented per-OS (editor_window_mac.mm); the stub in
// plugin_host.cpp reports "unsupported" elsewhere. Main thread only.
#pragma once

#include <functional>
#include <string>

namespace snd::plugin::editorwin {

struct Callbacks {
    // The user clicked the window's close button. Do a full synchronous
    // teardown here (detach the plugin view, then destroy() this handle) --
    // the window object stays alive until the callback returns.
    std::function<void()> onClose;
    // The user resized the window; new content size in points.
    std::function<void(int, int)> onResized;
};

// Returns an opaque handle, or nullptr where unsupported.
void* create(const std::string& title, int width, int height, bool resizable,
             Callbacks callbacks);

// The native view plugin UIs attach into (NSView* on macOS).
void* contentView(void* handle);

// Convenience for view-object UIs (AU): adds the native view as a subview
// sized to fill the window, following resizes. The window keeps it alive.
void attachView(void* handle, void* nsView);

void setContentSize(void* handle, int width, int height);

// Close the window and free the handle. Safe on nullptr, and safe to call
// from inside onClose.
void destroy(void* handle);

} // namespace snd::plugin::editorwin
