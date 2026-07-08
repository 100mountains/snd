// ImGui editor inside a host's plugin window (Windows/HWND). Compiled into each
// plugin by snd_add_plugin(). The host hands us a parent HWND
// (kPlatformTypeHWND); we make a child window with a WGL OpenGL context and
// render it from a WM_TIMER, pumped by the host's own message loop -- no
// background thread inside somebody else's process.

#include "snd/plugin_client.h"
#include "snd/ui.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

#include <windows.h>
#include <windowsx.h>
#include <GL/gl.h>

#include <algorithm>
#include <chrono>
#include <cstring>

using namespace Steinberg;

namespace snd::plugin::client {

namespace {

const wchar_t* kChildClass = L"SndPluginGLChild";

class EditorViewWin final : public U::Implements<U::Directly<IPlugView>> {
public:
    EditorViewWin(Processor& proc, UiHost& host, int width, int height)
        : proc_(proc), host_(host), width_(width), height_(height)
    {
    }

    ~EditorViewWin() override { removed(); }

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
        return type && strcmp(type, kPlatformTypeHWND) == 0 ? kResultTrue
                                                            : kResultFalse;
    }

    tresult PLUGIN_API attached(void* parent, FIDString type) override
    {
        if (isPlatformTypeSupported(type) != kResultTrue || !parent)
            return kResultFalse;
        ensureClass();

        hwnd_ = CreateWindowExW(0, kChildClass, L"", WS_CHILD | WS_VISIBLE, 0, 0,
                                width_, height_, (HWND)parent, nullptr,
                                GetModuleHandleW(nullptr), this);
        if (!hwnd_)
            return kResultFalse;
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, (LONG_PTR)this);

        dc_ = GetDC(hwnd_);
        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 24;
        pfd.cDepthBits = 16;
        pfd.iLayerType = PFD_MAIN_PLANE;
        int pf = ChoosePixelFormat(dc_, &pfd);
        SetPixelFormat(dc_, pf, &pfd);
        glrc_ = wglCreateContext(dc_);
        if (!glrc_) {
            removed();
            return kResultFalse;
        }
        wglMakeCurrent(dc_, glrc_);

        imctx_ = ImGui::CreateContext();
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imctx_);
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui_ImplOpenGL3_Init("#version 130");
        ImGui::SetCurrentContext(prev);
        last_ = std::chrono::steady_clock::now();

        SetTimer(hwnd_, 1, 16, nullptr); // ~60Hz, serviced by the host's pump
        return kResultOk;
    }

    tresult PLUGIN_API removed() override
    {
        if (hwnd_)
            KillTimer(hwnd_, 1);
        if (imctx_) {
            if (dc_ && glrc_)
                wglMakeCurrent(dc_, glrc_);
            ImGuiContext* prev = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(imctx_);
            ImGui_ImplOpenGL3_Shutdown();
            ImGui::DestroyContext(imctx_);
            if (prev != imctx_)
                ImGui::SetCurrentContext(prev);
            imctx_ = nullptr;
        }
        if (glrc_) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(glrc_);
            glrc_ = nullptr;
        }
        if (dc_ && hwnd_) {
            ReleaseDC(hwnd_, dc_);
            dc_ = nullptr;
        }
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        return kResultOk;
    }

    // --- IPlugView boilerplate ------------------------------------------------
    tresult PLUGIN_API onWheel(float) override { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) override { return kResultFalse; }

    tresult PLUGIN_API getSize(ViewRect* size) override
    {
        if (!size)
            return kResultFalse;
        size->left = size->top = 0;
        size->right = width_;
        size->bottom = height_;
        return kResultOk;
    }

    tresult PLUGIN_API onSize(ViewRect* newSize) override
    {
        if (newSize && hwnd_) {
            width_ = newSize->getWidth();
            height_ = newSize->getHeight();
            SetWindowPos(hwnd_, nullptr, 0, 0, width_, height_,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return kResultOk;
    }

    tresult PLUGIN_API onFocus(TBool) override { return kResultOk; }
    tresult PLUGIN_API setFrame(IPlugFrame* frame) override
    {
        frame_ = frame;
        return kResultOk;
    }
    tresult PLUGIN_API canResize() override { return kResultFalse; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect*) override { return kResultOk; }

private:
    static void ensureClass()
    {
        static bool done = false;
        if (done)
            return;
        done = true;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = childProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = kChildClass;
        RegisterClassExW(&wc);
    }

    static LRESULT CALLBACK childProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto* self =
            (EditorViewWin*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (self && self->imctx_) {
            ImGuiContext* prev = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(self->imctx_);
            ImGuiIO& io = ImGui::GetIO();
            switch (msg) {
            case WM_TIMER:
            case WM_PAINT:
                self->render();
                if (msg == WM_PAINT)
                    ValidateRect(hwnd, nullptr);
                ImGui::SetCurrentContext(prev);
                return 0;
            case WM_MOUSEMOVE:
                io.AddMousePosEvent((float)GET_X_LPARAM(lp),
                                    (float)GET_Y_LPARAM(lp));
                break;
            case WM_LBUTTONDOWN: io.AddMouseButtonEvent(0, true); break;
            case WM_LBUTTONUP: io.AddMouseButtonEvent(0, false); break;
            case WM_RBUTTONDOWN: io.AddMouseButtonEvent(1, true); break;
            case WM_RBUTTONUP: io.AddMouseButtonEvent(1, false); break;
            case WM_MBUTTONDOWN: io.AddMouseButtonEvent(2, true); break;
            case WM_MBUTTONUP: io.AddMouseButtonEvent(2, false); break;
            case WM_MOUSEWHEEL:
                io.AddMouseWheelEvent(
                    0.0f, (float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA);
                break;
            case WM_CHAR:
                if (wp >= 0x20)
                    io.AddInputCharacter((unsigned)wp);
                break;
            case WM_KEYDOWN:
            case WM_KEYUP: {
                bool down = msg == WM_KEYDOWN;
                ImGuiKey k = mapKey((int)wp);
                if (k != ImGuiKey_None)
                    io.AddKeyEvent(k, down);
                break;
            }
            default: break;
            }
            ImGui::SetCurrentContext(prev);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    static ImGuiKey mapKey(int vk)
    {
        switch (vk) {
        case VK_BACK: return ImGuiKey_Backspace;
        case VK_RETURN: return ImGuiKey_Enter;
        case VK_ESCAPE: return ImGuiKey_Escape;
        case VK_LEFT: return ImGuiKey_LeftArrow;
        case VK_RIGHT: return ImGuiKey_RightArrow;
        case VK_UP: return ImGuiKey_UpArrow;
        case VK_DOWN: return ImGuiKey_DownArrow;
        case VK_TAB: return ImGuiKey_Tab;
        case VK_DELETE: return ImGuiKey_Delete;
        default: return ImGuiKey_None;
        }
    }

    void render()
    {
        if (!dc_ || !glrc_ || !imctx_)
            return;
        wglMakeCurrent(dc_, glrc_);
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imctx_);

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)width_, (float)height_);
        auto now = std::chrono::steady_clock::now();
        io.DeltaTime = std::clamp(
            std::chrono::duration<float>(now - last_).count(), 1e-4f, 0.1f);
        last_ = now;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##snd-plugin", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        proc_.drawUi(host_);
        ImGui::End();
        ImGui::Render();

        glViewport(0, 0, width_, height_);
        glClearColor(0.07f, 0.07f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(dc_);
        ImGui::SetCurrentContext(prev);
    }

    Processor& proc_;
    UiHost& host_;
    int width_, height_;
    HWND hwnd_ = nullptr;
    HDC dc_ = nullptr;
    HGLRC glrc_ = nullptr;
    ImGuiContext* imctx_ = nullptr;
    IPlugFrame* frame_ = nullptr;
    std::chrono::steady_clock::time_point last_;
};

} // namespace

IPlugView* createEditorView(Processor& proc, UiHost& host, int width, int height,
                            bool /*resizable*/, int /*minW*/, int /*minH*/)
{
    return new EditorViewWin(proc, host, width, height);
}

} // namespace snd::plugin::client
