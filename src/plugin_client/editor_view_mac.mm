// ImGui editor inside a host's plugin window (macOS). Compiled into each
// plugin by snd_add_plugin(). One NSOpenGLView + one ImGui context PER
// INSTANCE (no globals shared with the host or other instances), with
// bespoke event forwarding -- no event monitors that could fight the host.

#include "snd/plugin_client.h"
#include "snd/ui.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

#import <Cocoa/Cocoa.h>
#import <OpenGL/gl3.h>
#import <QuartzCore/QuartzCore.h>

using namespace Steinberg;

// ---------------------------------------------------------------------------

@interface SndPluginGLView : NSOpenGLView {
@public
    ImGuiContext* imctx_;
    snd::plugin::client::Processor* proc_;
    snd::plugin::client::UiHost* host_;
    NSTimer* timer_;
    double lastTime_;
    bool backendReady_;
}
@end

@implementation SndPluginGLView

- (instancetype)initWithFrame:(NSRect)frame
{
    NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
        NSOpenGLPFADoubleBuffer,  NSOpenGLPFAColorSize, 24,
        NSOpenGLPFADepthSize, 0, 0};
    NSOpenGLPixelFormat* pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    self = [super initWithFrame:frame pixelFormat:pf];
    if (self) {
        imctx_ = ImGui::CreateContext();
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imctx_);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // plugins don't scatter ini files
        io.DisplaySize = ImVec2((float)frame.size.width, (float)frame.size.height);
        ImGui::StyleColorsDark();
        ImGui::SetCurrentContext(prev);
        lastTime_ = CACurrentMediaTime();
        backendReady_ = false;
        self.wantsBestResolutionOpenGLSurface = YES;
    }
    return self;
}

- (BOOL)isFlipped { return YES; } // ImGui is y-down
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)e { return YES; }

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    if (self.window && !timer_) {
        timer_ = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0
                                                  target:self
                                                selector:@selector(tick)
                                                userInfo:nil
                                                 repeats:YES];
        [[NSRunLoop currentRunLoop] addTimer:timer_ forMode:NSEventTrackingRunLoopMode];
    } else if (!self.window && timer_) {
        [timer_ invalidate];
        timer_ = nil;
    }
}

- (void)tick { self.needsDisplay = YES; }

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    for (NSTrackingArea* a in self.trackingAreas)
        [self removeTrackingArea:a];
    [self addTrackingArea:[[NSTrackingArea alloc]
                              initWithRect:NSZeroRect
                                   options:NSTrackingMouseMoved |
                                           NSTrackingMouseEnteredAndExited |
                                           NSTrackingActiveAlways |
                                           NSTrackingInVisibleRect
                                     owner:self
                                  userInfo:nil]];
}

// --- input forwarding (per-context, no monitors) ---------------------------

- (ImGuiIO*)ioBegin
{
    if (!imctx_)
        return nullptr;
    ImGui::SetCurrentContext(imctx_);
    return &ImGui::GetIO();
}

- (void)forwardMouse:(NSEvent*)event
{
    if (auto* io = [self ioBegin]) {
        NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
        io->AddMousePosEvent((float)p.x, (float)p.y);
    }
}

- (void)mouseMoved:(NSEvent*)e { [self forwardMouse:e]; }
- (void)mouseDragged:(NSEvent*)e { [self forwardMouse:e]; }
- (void)rightMouseDragged:(NSEvent*)e { [self forwardMouse:e]; }
- (void)mouseExited:(NSEvent*)e
{
    if (auto* io = [self ioBegin])
        io->AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}

- (void)mouseDown:(NSEvent*)e
{
    [self.window makeFirstResponder:self];
    [self forwardMouse:e];
    if (auto* io = [self ioBegin])
        io->AddMouseButtonEvent(0, true);
}
- (void)mouseUp:(NSEvent*)e
{
    [self forwardMouse:e];
    if (auto* io = [self ioBegin])
        io->AddMouseButtonEvent(0, false);
}
- (void)rightMouseDown:(NSEvent*)e
{
    [self forwardMouse:e];
    if (auto* io = [self ioBegin])
        io->AddMouseButtonEvent(1, true);
}
- (void)rightMouseUp:(NSEvent*)e
{
    [self forwardMouse:e];
    if (auto* io = [self ioBegin])
        io->AddMouseButtonEvent(1, false);
}

- (void)scrollWheel:(NSEvent*)e
{
    if (auto* io = [self ioBegin]) {
        double dx = e.scrollingDeltaX, dy = e.scrollingDeltaY;
        if (e.hasPreciseScrollingDeltas) {
            dx *= 0.1;
            dy *= 0.1;
        }
        io->AddMouseWheelEvent((float)dx, (float)dy);
    }
}

- (void)keyDown:(NSEvent*)e
{
    if (auto* io = [self ioBegin]) {
        NSString* chars = e.characters;
        if (chars.length > 0) {
            unichar c = [chars characterAtIndex:0];
            // basic edit keys; everything else lands as text
            switch (c) {
            case NSDeleteCharacter: // 0x7F
                io->AddKeyEvent(ImGuiKey_Backspace, true);
                io->AddKeyEvent(ImGuiKey_Backspace, false);
                break;
            case NSCarriageReturnCharacter:
            case NSEnterCharacter:
                io->AddKeyEvent(ImGuiKey_Enter, true);
                io->AddKeyEvent(ImGuiKey_Enter, false);
                break;
            case 0x1B:
                io->AddKeyEvent(ImGuiKey_Escape, true);
                io->AddKeyEvent(ImGuiKey_Escape, false);
                break;
            case NSLeftArrowFunctionKey:
                io->AddKeyEvent(ImGuiKey_LeftArrow, true);
                io->AddKeyEvent(ImGuiKey_LeftArrow, false);
                break;
            case NSRightArrowFunctionKey:
                io->AddKeyEvent(ImGuiKey_RightArrow, true);
                io->AddKeyEvent(ImGuiKey_RightArrow, false);
                break;
            case NSTabCharacter:
                io->AddKeyEvent(ImGuiKey_Tab, true);
                io->AddKeyEvent(ImGuiKey_Tab, false);
                break;
            default:
                if (c >= 0x20)
                    io->AddInputCharactersUTF8(chars.UTF8String);
                break;
            }
        }
    }
}
- (void)keyUp:(NSEvent*)e { (void)e; }

// --- drawing ----------------------------------------------------------------

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    if (!imctx_ || !proc_)
        return;
    [[self openGLContext] makeCurrentContext];
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(imctx_);

    if (!backendReady_) {
        ImGui_ImplOpenGL3_Init("#version 150");
        backendReady_ = true;
    }

    ImGuiIO& io = ImGui::GetIO();
    NSSize sz = self.bounds.size;
    float scale = (float)(self.window ? self.window.backingScaleFactor : 1.0);
    io.DisplaySize = ImVec2((float)sz.width, (float)sz.height);
    io.DisplayFramebufferScale = ImVec2(scale, scale);
    double now = CACurrentMediaTime();
    io.DeltaTime = (float)std::min(0.1, std::max(1e-4, now - lastTime_));
    lastTime_ = now;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##snd-plugin", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    proc_->drawUi(*host_);
    ImGui::End();

    ImGui::Render();
    glViewport(0, 0, (GLsizei)(sz.width * scale), (GLsizei)(sz.height * scale));
    glClearColor(0.07f, 0.07f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    [[self openGLContext] flushBuffer];

    ImGui::SetCurrentContext(prev);
}

- (void)shutdownImGui
{
    if (timer_) {
        [timer_ invalidate];
        timer_ = nil;
    }
    if (imctx_) {
        [[self openGLContext] makeCurrentContext];
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imctx_);
        if (backendReady_)
            ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext(imctx_);
        imctx_ = nullptr;
        if (prev != imctx_)
            ImGui::SetCurrentContext(prev == imctx_ ? nullptr : prev);
    }
}

@end

// ---------------------------------------------------------------------------
// IPlugView bridging the GL view into whatever NSView the host provides.

namespace snd::plugin::client {

namespace {

class EditorView final : public U::Implements<U::Directly<IPlugView>> {
public:
    EditorView(Processor& proc, UiHost& host, int width, int height)
        : proc_(proc), host_(host), width_(width), height_(height)
    {
    }

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
        return type && strcmp(type, kPlatformTypeNSView) == 0 ? kResultTrue
                                                              : kResultFalse;
    }

    tresult PLUGIN_API attached(void* parent, FIDString type) override
    {
        if (isPlatformTypeSupported(type) != kResultTrue || !parent)
            return kResultFalse;
        NSView* parentView = (__bridge NSView*)parent;
        SndPluginGLView* gl =
            [[SndPluginGLView alloc] initWithFrame:parentView.bounds];
        if (!gl)
            return kResultFalse;
        gl->proc_ = &proc_;
        gl->host_ = &host_;
        gl.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [parentView addSubview:gl];
        view_ = (__bridge_retained void*)gl;
        return kResultOk;
    }

    tresult PLUGIN_API removed() override
    {
        if (view_) {
            SndPluginGLView* gl = (__bridge_transfer SndPluginGLView*)view_;
            [gl shutdownImGui];
            [gl removeFromSuperview];
            view_ = nullptr;
        }
        return kResultOk;
    }

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
        (void)newSize; // autoresizing tracks the parent
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

    ~EditorView() override { removed(); }

private:
    Processor& proc_;
    UiHost& host_;
    int width_, height_;
    void* view_ = nullptr;
    IPlugFrame* frame_ = nullptr;
};

} // namespace

IPlugView* createEditorView(Processor& proc, UiHost& host, int width, int height)
{
    return new EditorView(proc, host, width, height);
}

} // namespace snd::plugin::client
