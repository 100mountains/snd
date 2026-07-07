// Linux floating window for plugin editors (X11). The "content view" handed
// to plugins is the X Window id (kPlatformTypeX11EmbedWindowID). Events are
// pumped from pump(), which hosts reach through Instance::idle() -- no
// dedicated thread, everything on the main thread like the mac path.

#include "editor_window.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <cstdint>
#include <unordered_map>

namespace snd::plugin::editorwin {

namespace {

Display* gDisplay = nullptr;
Atom gWmDelete = 0;

struct Handle {
    ::Window win = 0;
    Callbacks cbs;
    int w = 0, h = 0;
};

std::unordered_map<::Window, Handle*>& windows()
{
    static std::unordered_map<::Window, Handle*> map;
    return map;
}

Display* display()
{
    if (!gDisplay) {
        gDisplay = XOpenDisplay(nullptr);
        if (gDisplay)
            gWmDelete = XInternAtom(gDisplay, "WM_DELETE_WINDOW", False);
    }
    return gDisplay;
}

} // namespace

void* create(const std::string& title, int width, int height, bool resizable,
             Callbacks callbacks)
{
    Display* dpy = display();
    if (!dpy)
        return nullptr;
    (void)resizable; // WM hints could pin the size; keep it simple for now

    int screen = DefaultScreen(dpy);
    ::Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 100, 100,
                                       (unsigned)width, (unsigned)height, 0,
                                       BlackPixel(dpy, screen),
                                       0x16181D /* dark */);
    if (!win)
        return nullptr;

    XStoreName(dpy, win, title.c_str());
    XSetWMProtocols(dpy, win, &gWmDelete, 1);
    XSelectInput(dpy, win, StructureNotifyMask);
    XMapWindow(dpy, win);
    XFlush(dpy);

    auto* h = new Handle{win, std::move(callbacks), width, height};
    windows()[win] = h;
    return h;
}

void* contentView(void* handle)
{
    auto* h = (Handle*)handle;
    return h ? (void*)(uintptr_t)h->win : nullptr;
}

void attachView(void* handle, void* nsView)
{
    (void)handle;
    (void)nsView; // AU-style view attachment has no X11 counterpart
}

void setContentSize(void* handle, int width, int height)
{
    auto* h = (Handle*)handle;
    if (h && display()) {
        XResizeWindow(display(), h->win, (unsigned)width, (unsigned)height);
        XFlush(display());
    }
}

void destroy(void* handle)
{
    auto* h = (Handle*)handle;
    if (!h)
        return;
    windows().erase(h->win);
    if (display()) {
        XDestroyWindow(display(), h->win);
        XFlush(display());
    }
    delete h;
}

// Drain pending X events for every editor window. Cheap when idle; hosts
// reach this via Instance::idle() each frame.
void pump()
{
    Display* dpy = gDisplay;
    if (!dpy)
        return;
    while (XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        ::Window w = ev.xany.window;
        auto it = windows().find(w);
        if (it == windows().end())
            continue;
        Handle* h = it->second;
        if (ev.type == ClientMessage &&
            (Atom)ev.xclient.data.l[0] == gWmDelete) {
            if (h->cbs.onClose)
                h->cbs.onClose(); // closes + destroys synchronously, like mac
        } else if (ev.type == ConfigureNotify) {
            if (ev.xconfigure.width != h->w || ev.xconfigure.height != h->h) {
                h->w = ev.xconfigure.width;
                h->h = ev.xconfigure.height;
                if (h->cbs.onResized)
                    h->cbs.onResized(h->w, h->h);
            }
        }
    }
}

} // namespace snd::plugin::editorwin
