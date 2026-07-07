// ImGui editor inside a host's plugin window (Linux/X11). Compiled into each
// plugin by snd_add_plugin(). The host hands us an X Window id
// (kPlatformTypeX11EmbedWindowID) and -- crucially -- an IRunLoop through the
// IPlugFrame; we render on its timer and read X events off its fd watcher,
// so we never spin a thread inside somebody else's process.

#include "snd/plugin_client.h"
#include "snd/ui.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <algorithm>
#include <chrono>
#include <cstdint>

using namespace Steinberg;

namespace snd::plugin::client {

namespace {

class EditorViewX11 final
    : public U::Implements<U::Directly<IPlugView, Steinberg::Linux::ITimerHandler,
                                       Steinberg::Linux::IEventHandler>> {
public:
    EditorViewX11(Processor& proc, UiHost& host, int width, int height)
        : proc_(proc), host_(host), width_(width), height_(height)
    {
    }

    ~EditorViewX11() override { removed(); }

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
        return type && strcmp(type, kPlatformTypeX11EmbedWindowID) == 0
                   ? kResultTrue
                   : kResultFalse;
    }

    tresult PLUGIN_API attached(void* parent, FIDString type) override
    {
        if (isPlatformTypeSupported(type) != kResultTrue || !parent)
            return kResultFalse;

        dpy_ = XOpenDisplay(nullptr);
        if (!dpy_)
            return kResultFalse;

        ::Window parentWin = (::Window)(uintptr_t)parent;

        static int visAttribs[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8,
                                   GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None};
        XVisualInfo* vi = glXChooseVisual(dpy_, DefaultScreen(dpy_), visAttribs);
        if (!vi) {
            XCloseDisplay(dpy_);
            dpy_ = nullptr;
            return kResultFalse;
        }

        XSetWindowAttributes swa{};
        swa.colormap =
            XCreateColormap(dpy_, parentWin, vi->visual, AllocNone);
        swa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                         PointerMotionMask | KeyPressMask | KeyReleaseMask |
                         StructureNotifyMask | LeaveWindowMask;
        win_ = XCreateWindow(dpy_, parentWin, 0, 0, (unsigned)width_,
                             (unsigned)height_, 0, vi->depth, InputOutput,
                             vi->visual, CWColormap | CWEventMask, &swa);
        XMapWindow(dpy_, win_);

        glctx_ = glXCreateContext(dpy_, vi, nullptr, True);
        XFree(vi);
        if (!glctx_) {
            removed();
            return kResultFalse;
        }

        glXMakeCurrent(dpy_, win_, glctx_);
        imctx_ = ImGui::CreateContext();
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imctx_);
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui_ImplOpenGL3_Init("#version 130");
        ImGui::SetCurrentContext(prev);
        last_ = std::chrono::steady_clock::now();

        // the host's run loop drives us: ~60Hz timer + the X connection fd
        if (frame_) {
            frame_->queryInterface(Steinberg::Linux::IRunLoop::iid,
                                   (void**)&runLoop_);
            if (runLoop_) {
                runLoop_->registerTimer(this, 16);
                runLoop_->registerEventHandler(this, ConnectionNumber(dpy_));
            }
        }
        return kResultOk;
    }

    tresult PLUGIN_API removed() override
    {
        if (runLoop_) {
            runLoop_->unregisterTimer(this);
            runLoop_->unregisterEventHandler(this);
            runLoop_->release();
            runLoop_ = nullptr;
        }
        if (imctx_) {
            glXMakeCurrent(dpy_, win_, glctx_);
            ImGuiContext* prev = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(imctx_);
            ImGui_ImplOpenGL3_Shutdown();
            ImGui::DestroyContext(imctx_);
            if (prev != imctx_)
                ImGui::SetCurrentContext(prev);
            imctx_ = nullptr;
        }
        if (glctx_) {
            glXMakeCurrent(dpy_, None, nullptr);
            glXDestroyContext(dpy_, glctx_);
            glctx_ = nullptr;
        }
        if (win_) {
            XDestroyWindow(dpy_, win_);
            win_ = 0;
        }
        if (dpy_) {
            XCloseDisplay(dpy_);
            dpy_ = nullptr;
        }
        return kResultOk;
    }

    // --- run-loop callbacks --------------------------------------------------

    void PLUGIN_API onTimer() override
    {
        drainXEvents();
        render();
    }

    void PLUGIN_API onFDIsSet(Steinberg::Linux::FileDescriptor) override
    {
        drainXEvents();
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
        if (newSize && dpy_ && win_) {
            width_ = newSize->getWidth();
            height_ = newSize->getHeight();
            XResizeWindow(dpy_, win_, (unsigned)width_, (unsigned)height_);
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
    void drainXEvents()
    {
        if (!dpy_ || !imctx_)
            return;
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imctx_);
        ImGuiIO& io = ImGui::GetIO();
        while (XPending(dpy_)) {
            XEvent ev;
            XNextEvent(dpy_, &ev);
            switch (ev.type) {
            case MotionNotify:
                io.AddMousePosEvent((float)ev.xmotion.x, (float)ev.xmotion.y);
                break;
            case ButtonPress:
            case ButtonRelease: {
                bool down = ev.type == ButtonPress;
                switch (ev.xbutton.button) {
                case Button1: io.AddMouseButtonEvent(0, down); break;
                case Button3: io.AddMouseButtonEvent(1, down); break;
                case Button4:
                    if (down) io.AddMouseWheelEvent(0.0f, 1.0f);
                    break;
                case Button5:
                    if (down) io.AddMouseWheelEvent(0.0f, -1.0f);
                    break;
                }
                io.AddMousePosEvent((float)ev.xbutton.x, (float)ev.xbutton.y);
                break;
            }
            case LeaveNotify:
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                break;
            case KeyPress: {
                char text[8] = {};
                KeySym ks = 0;
                int n = XLookupString(&ev.xkey, text, sizeof(text) - 1, &ks, nullptr);
                switch (ks) {
                case XK_BackSpace: tap(io, ImGuiKey_Backspace); break;
                case XK_Return: tap(io, ImGuiKey_Enter); break;
                case XK_Escape: tap(io, ImGuiKey_Escape); break;
                case XK_Left: tap(io, ImGuiKey_LeftArrow); break;
                case XK_Right: tap(io, ImGuiKey_RightArrow); break;
                case XK_Tab: tap(io, ImGuiKey_Tab); break;
                default:
                    if (n > 0 && (unsigned char)text[0] >= 0x20)
                        io.AddInputCharactersUTF8(text);
                }
                break;
            }
            case ConfigureNotify:
                width_ = ev.xconfigure.width;
                height_ = ev.xconfigure.height;
                break;
            default:
                break;
            }
        }
        ImGui::SetCurrentContext(prev);
    }

    static void tap(ImGuiIO& io, ImGuiKey k)
    {
        io.AddKeyEvent(k, true);
        io.AddKeyEvent(k, false);
    }

    void render()
    {
        if (!dpy_ || !imctx_ || !glctx_)
            return;
        glXMakeCurrent(dpy_, win_, glctx_);
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
        glXSwapBuffers(dpy_, win_);
        ImGui::SetCurrentContext(prev);
    }

    Processor& proc_;
    UiHost& host_;
    int width_, height_;
    Display* dpy_ = nullptr;
    ::Window win_ = 0;
    GLXContext glctx_ = nullptr;
    ImGuiContext* imctx_ = nullptr;
    IPlugFrame* frame_ = nullptr;
    Steinberg::Linux::IRunLoop* runLoop_ = nullptr;
    std::chrono::steady_clock::time_point last_;
};

} // namespace

IPlugView* createEditorView(Processor& proc, UiHost& host, int width, int height,
                            bool /*resizable*/, int /*minW*/, int /*minH*/)
{
    return new EditorViewX11(proc, host, width, height);
}

} // namespace snd::plugin::client
