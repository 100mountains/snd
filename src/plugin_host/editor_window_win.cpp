// Windows floating window for plugin editors (Win32/HWND). The "content view"
// handed to plugins is the HWND (kPlatformTypeHWND). Events are pumped from
// pump(), which hosts reach through Instance::idle() -- everything on the main
// thread, like the mac/X11 paths.

#include "editor_window.h"

#include <windows.h>

#include <unordered_map>

namespace snd::plugin::editorwin {

namespace {

const wchar_t* kClassName = L"SndPluginEditorWindow";

struct Handle {
    HWND hwnd = nullptr;
    Callbacks cbs;
    int w = 0, h = 0;
};

std::unordered_map<HWND, Handle*>& windows()
{
    static std::unordered_map<HWND, Handle*> map;
    return map;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto it = windows().find(hwnd);
    Handle* h = it == windows().end() ? nullptr : it->second;
    switch (msg) {
    case WM_CLOSE:
        if (h && h->cbs.onClose)
            h->cbs.onClose(); // closes + destroys synchronously, like mac/X11
        return 0;
    case WM_SIZE:
        if (h) {
            int w = LOWORD(lp), hh = HIWORD(lp);
            if ((w != h->w || hh != h->h) && w > 0 && hh > 0) {
                h->w = w;
                h->h = hh;
                if (h->cbs.onResized)
                    h->cbs.onResized(w, hh);
            }
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void ensureClass()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
}

// Grow a client size to the outer window size for the given style.
void clientToWindow(DWORD style, int cw, int ch, int& ww, int& wh)
{
    RECT r{0, 0, cw, ch};
    AdjustWindowRect(&r, style, FALSE);
    ww = r.right - r.left;
    wh = r.bottom - r.top;
}

} // namespace

void* create(const std::string& title, int width, int height, bool resizable,
             Callbacks callbacks)
{
    ensureClass();
    DWORD style = resizable ? WS_OVERLAPPEDWINDOW
                            : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU);
    int ww, wh;
    clientToWindow(style, width, height, ww, wh);

    // widen the UTF-8 title
    std::wstring wtitle;
    int n = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
    if (n > 0) {
        wtitle.resize((size_t)n - 1);
        MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, wtitle.data(), n);
    }

    HWND hwnd =
        CreateWindowExW(0, kClassName, wtitle.c_str(), style, CW_USEDEFAULT,
                        CW_USEDEFAULT, ww, wh, nullptr, nullptr,
                        GetModuleHandleW(nullptr), nullptr);
    if (!hwnd)
        return nullptr;

    auto* h = new Handle{hwnd, std::move(callbacks), width, height};
    windows()[hwnd] = h;
    ShowWindow(hwnd, SW_SHOW);
    return h;
}

void* contentView(void* handle)
{
    auto* h = (Handle*)handle;
    return h ? (void*)h->hwnd : nullptr; // plugins attach into this HWND
}

void attachView(void* handle, void* childHwnd)
{
    // View-object style (AU on mac) has no Windows counterpart; VST3 plugins
    // create their own child in contentView()'s HWND. Parent it if given.
    auto* h = (Handle*)handle;
    if (h && childHwnd)
        SetParent((HWND)childHwnd, h->hwnd);
}

void setContentSize(void* handle, int width, int height)
{
    auto* h = (Handle*)handle;
    if (!h || !h->hwnd)
        return;
    DWORD style = (DWORD)GetWindowLongPtrW(h->hwnd, GWL_STYLE);
    int ww, wh;
    clientToWindow(style, width, height, ww, wh);
    SetWindowPos(h->hwnd, nullptr, 0, 0, ww, wh,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    h->w = width;
    h->h = height;
}

void destroy(void* handle)
{
    auto* h = (Handle*)handle;
    if (!h)
        return;
    windows().erase(h->hwnd);
    if (h->hwnd)
        DestroyWindow(h->hwnd);
    delete h;
}

// Drain pending messages for every editor window. Cheap when idle; hosts reach
// this via Instance::idle() each frame.
void pump()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

} // namespace snd::plugin::editorwin
