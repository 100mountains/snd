// macOS floating window for plugin editors. Compiled with ARC.

#include "editor_window.h"

#import <Cocoa/Cocoa.h>

@interface SndEditorWinCtl : NSObject <NSWindowDelegate> {
@public
    NSWindow* window_;
    snd::plugin::editorwin::Callbacks cbs_;
}
@end

@implementation SndEditorWinCtl
- (BOOL)windowShouldClose:(NSWindow*)sender
{
    // NSWindow.delegate is an unretained reference: the onClose callback may
    // destroy() us synchronously, so pin self until this method returns.
    SndEditorWinCtl* keepAlive = self;
    (void)keepAlive;
    if (cbs_.onClose)
        cbs_.onClose();
    return NO; // the callback closed the window via destroy()
}

- (void)windowDidResize:(NSNotification*)note
{
    if (cbs_.onResized && window_) {
        NSSize s = window_.contentView.frame.size;
        cbs_.onResized((int)s.width, (int)s.height);
    }
}
@end

namespace snd::plugin::editorwin {

struct Handle {
    SndEditorWinCtl* ctl;
};

void* create(const std::string& title, int width, int height, bool resizable,
             Callbacks callbacks)
{
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                       NSWindowStyleMaskMiniaturizable;
    if (resizable)
        style |= NSWindowStyleMaskResizable;

    NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, width, height)
                                                styleMask:style
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    if (!win)
        return nullptr;
    win.releasedWhenClosed = NO; // ownership is ours, via the Handle
    win.title = [NSString stringWithUTF8String:title.c_str()];

    SndEditorWinCtl* ctl = [SndEditorWinCtl new];
    ctl->window_ = win;
    ctl->cbs_ = std::move(callbacks);
    win.delegate = ctl;

    [win center];
    [win makeKeyAndOrderFront:nil];
    return new Handle{ctl};
}

void* contentView(void* handle)
{
    auto* h = (Handle*)handle;
    return h && h->ctl && h->ctl->window_ ? (__bridge void*)h->ctl->window_.contentView
                                          : nullptr;
}

void attachView(void* handle, void* nsView)
{
    auto* h = (Handle*)handle;
    if (!h || !h->ctl || !h->ctl->window_ || !nsView)
        return;
    NSView* view = (__bridge NSView*)nsView;
    NSView* content = h->ctl->window_.contentView;
    view.frame = content.bounds;
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [content addSubview:view];
}

void setContentSize(void* handle, int width, int height)
{
    auto* h = (Handle*)handle;
    if (h && h->ctl && h->ctl->window_)
        [h->ctl->window_ setContentSize:NSMakeSize(width, height)];
}

void destroy(void* handle)
{
    auto* h = (Handle*)handle;
    if (!h)
        return;
    if (h->ctl) {
        h->ctl->cbs_ = {};
        if (h->ctl->window_) {
            h->ctl->window_.delegate = nil;
            [h->ctl->window_ close];
            h->ctl->window_ = nil;
        }
        h->ctl = nil;
    }
    delete h;
}

} // namespace snd::plugin::editorwin
