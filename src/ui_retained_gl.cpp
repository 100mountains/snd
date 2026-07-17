#include "snd/ui_retained_gl.h"

#include "snd/platform.h"
#include "snd/ui_retained_widgets.h"
#include "ui_draw_gl.h"
#include "ui_glfw_shared.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#if defined(__APPLE__)
namespace snd::ui {
void nativeDragImpl(GLFWwindow* window);
void macPrepareWindowResizeImpl(GLFWwindow* window);
void macPrepareFramelessImpl(GLFWwindow* window);
void macToggleZoomImpl(GLFWwindow* window);
void macToggleFullscreenImpl(GLFWwindow* window);
} // namespace snd::ui
#endif

namespace snd::ui::retained {

namespace {

draw::Color gDefaultClear = 0xFF1F1A18u;

MouseButton mapMouseButton(int button)
{
    switch (button) {
    case GLFW_MOUSE_BUTTON_RIGHT:
        return MouseButton::Right;
    case GLFW_MOUSE_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case GLFW_MOUSE_BUTTON_LEFT:
    default:
        return MouseButton::Left;
    }
}

Key mapKey(int key)
{
    switch (key) {
    case GLFW_KEY_TAB:
        return Key::Tab;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
        return Key::Enter;
    case GLFW_KEY_SPACE:
        return Key::Space;
    case GLFW_KEY_LEFT:
        return Key::Left;
    case GLFW_KEY_RIGHT:
        return Key::Right;
    case GLFW_KEY_UP:
        return Key::Up;
    case GLFW_KEY_DOWN:
        return Key::Down;
    case GLFW_KEY_ESCAPE:
        return Key::Escape;
    case GLFW_KEY_BACKSPACE:
        return Key::Backspace;
    case GLFW_KEY_DELETE:
        return Key::Delete;
    case GLFW_KEY_HOME:
        return Key::Home;
    case GLFW_KEY_END:
        return Key::End;
    case GLFW_KEY_A: return Key::A;
    case GLFW_KEY_B: return Key::B;
    case GLFW_KEY_C: return Key::C;
    case GLFW_KEY_D: return Key::D;
    case GLFW_KEY_E: return Key::E;
    case GLFW_KEY_F: return Key::F;
    case GLFW_KEY_G: return Key::G;
    case GLFW_KEY_H: return Key::H;
    case GLFW_KEY_I: return Key::I;
    case GLFW_KEY_J: return Key::J;
    case GLFW_KEY_K: return Key::K;
    case GLFW_KEY_L: return Key::L;
    case GLFW_KEY_M: return Key::M;
    case GLFW_KEY_N: return Key::N;
    case GLFW_KEY_O: return Key::O;
    case GLFW_KEY_P: return Key::P;
    case GLFW_KEY_Q: return Key::Q;
    case GLFW_KEY_R: return Key::R;
    case GLFW_KEY_S: return Key::S;
    case GLFW_KEY_T: return Key::T;
    case GLFW_KEY_U: return Key::U;
    case GLFW_KEY_V: return Key::V;
    case GLFW_KEY_W: return Key::W;
    case GLFW_KEY_X: return Key::X;
    case GLFW_KEY_Y: return Key::Y;
    case GLFW_KEY_Z: return Key::Z;
    default:
        return Key::Unknown;
    }
}

void applyMods(Event& event, int mods)
{
    event.shift = (mods & GLFW_MOD_SHIFT) != 0;
    event.ctrl = (mods & GLFW_MOD_CONTROL) != 0;
    event.alt = (mods & GLFW_MOD_ALT) != 0;
    event.super = (mods & GLFW_MOD_SUPER) != 0;
}

void appendUtf8(std::string& out, unsigned int cp)
{
    if (cp <= 0x7F) {
        out.push_back((char)cp);
    } else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

void colorFloats(draw::Color color, float& r, float& g, float& b, float& a)
{
    r = (float)(color & 0xFFu) / 255.0f;
    g = (float)((color >> 8) & 0xFFu) / 255.0f;
    b = (float)((color >> 16) & 0xFFu) / 255.0f;
    a = (float)((color >> 24) & 0xFFu) / 255.0f;
}

} // namespace

struct GlWindow::Impl {
    GLFWwindow* window = nullptr;
    draw::StbFontAtlas fonts;
    draw::OpenGLSurface surface{fonts};
    draw::FrameContext context;
    draw::Color clearColor = gDefaultClear;
    std::vector<Event> events;
    std::vector<std::string> droppedFiles;
    Vec2 pointer;
    bool pointerValid = false;
    std::array<double, 3> lastClickTime{{0.0, 0.0, 0.0}};
    std::array<Vec2, 3> lastClickPos{};
    bool frameOpen = false;
    bool surfaceReady = false;
    bool rendering = false;
    Tree* activeTree = nullptr;
    PaintRenderer* activeRenderer = nullptr;
    // undecorated-window management (title-bar-driven; see GlWindow::beginNativeDrag)
    bool dragging = false;
    double dragStartCursorX = 0.0, dragStartCursorY = 0.0; // window-relative cursor at drag start
    bool fullscreen = false;
    int savedX = 0, savedY = 0, savedW = 0, savedH = 0;    // windowed geometry, for fullscreen restore
    GLFWcursor* crosshairCursor = nullptr;
    GLFWcursor* handCursor = nullptr;
    GLFWcursor* resizeHorizontalCursor = nullptr;
    CursorStyle activeCursor = CursorStyle::Default;
    bool mouseCaptured = false;

    static Impl* from(GLFWwindow* w)
    {
        return static_cast<Impl*>(glfwGetWindowUserPointer(w));
    }

    Vec2 cursorLocal() const
    {
        double x = 0.0;
        double y = 0.0;
        if (window)
            glfwGetCursorPos(window, &x, &y);
        return {(float)x, (float)y};
    }

    int mods() const
    {
        int out = 0;
        if (!window)
            return out;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
            out |= GLFW_MOD_SHIFT;
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
            out |= GLFW_MOD_CONTROL;
        if (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS)
            out |= GLFW_MOD_ALT;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS)
            out |= GLFW_MOD_SUPER;
        return out;
    }

    Event pointerEvent(EventType type, Vec2 pos, Vec2 delta,
                       MouseButton button, int modifierMask) const
    {
        Event event;
        event.type = type;
        event.position = pos;
        event.delta = delta;
        event.button = button;
        applyMods(event, modifierMask);
        return event;
    }

    GLFWcursor* cursorFor(CursorStyle style)
    {
        if (style == CursorStyle::Crosshair) {
            if (!crosshairCursor)
                crosshairCursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
            return crosshairCursor;
        }
        if (style == CursorStyle::Hand) {
            if (!handCursor)
                handCursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
            return handCursor;
        }
        if (style == CursorStyle::ResizeHorizontal) {
            if (!resizeHorizontalCursor)
                resizeHorizontalCursor = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
            return resizeHorizontalCursor;
        }
        return nullptr;
    }

    void updateCursor(const Tree& tree)
    {
        if (!window || mouseCaptured)
            return;
        const Node* hovered = tree.hovered();
        const CursorStyle next = hovered ? hovered->cursorStyle()
                                         : CursorStyle::Default;
        if (next == activeCursor)
            return;
        activeCursor = next;
        glfwSetCursor(window, cursorFor(next));
    }

    void destroyCursors()
    {
        if (crosshairCursor)
            glfwDestroyCursor(crosshairCursor);
        if (handCursor)
            glfwDestroyCursor(handCursor);
        if (resizeHorizontalCursor)
            glfwDestroyCursor(resizeHorizontalCursor);
        crosshairCursor = nullptr;
        handCursor = nullptr;
        resizeHorizontalCursor = nullptr;
        activeCursor = CursorStyle::Default;
    }

    int clickCountFor(int button, Vec2 pos)
    {
        const int idx = std::clamp(button, 0, 2);
        const double now = glfwGetTime();
        const Vec2 prev = lastClickPos[(std::size_t)idx];
        const float dx = pos.x - prev.x;
        const float dy = pos.y - prev.y;
        const bool close = dx * dx + dy * dy <= 16.0f;
        const bool fast = now - lastClickTime[(std::size_t)idx] <= 0.35;
        lastClickTime[(std::size_t)idx] = now;
        lastClickPos[(std::size_t)idx] = pos;
        return close && fast ? 2 : 1;
    }

    static void cursorCallback(GLFWwindow* w, double x, double y)
    {
        Impl* self = from(w);
        if (!self)
            return;
        const Vec2 next{(float)x, (float)y};
        const Vec2 delta{next.x - self->pointer.x, next.y - self->pointer.y};
        self->pointer = next;
        self->pointerValid = true;
        self->events.push_back(self->pointerEvent(EventType::MouseMove, next,
                                                  delta, MouseButton::None,
                                                  self->mods()));
    }

    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods)
    {
        Impl* self = from(w);
        if (!self)
            return;
        const Vec2 pos = self->cursorLocal();
        self->pointer = pos;
        self->pointerValid = true;
        if (action == GLFW_PRESS) {
            Event event = self->pointerEvent(EventType::MouseDown, pos, {},
                                             mapMouseButton(button), mods);
            event.clickCount = self->clickCountFor(button, pos);
            self->events.push_back(event);
        } else if (action == GLFW_RELEASE) {
            Event event = self->pointerEvent(EventType::MouseUp, pos, {},
                                             mapMouseButton(button), mods);
            event.clickCount = 1;
            self->events.push_back(event);
            if (button == GLFW_MOUSE_BUTTON_RIGHT) {
                Event context = self->pointerEvent(EventType::ContextMenu, pos, {},
                                                   MouseButton::Right, mods);
                context.clickCount = 1;
                self->events.push_back(context);
            }
        }
    }

    static void scrollCallback(GLFWwindow* w, double x, double y)
    {
        Impl* self = from(w);
        if (!self)
            return;
        const Vec2 pos = self->cursorLocal();
        Event event = self->pointerEvent(EventType::MouseWheel, pos, {},
                                         MouseButton::None, self->mods());
        event.wheelDelta = {(float)x, (float)y};
        self->events.push_back(event);
    }

    static void keyCallback(GLFWwindow* w, int key, int, int action, int mods)
    {
        Impl* self = from(w);
        if (!self || (action != GLFW_PRESS && action != GLFW_REPEAT &&
                      action != GLFW_RELEASE))
            return;
        Event event;
        event.type = action == GLFW_RELEASE ? EventType::KeyUp : EventType::KeyDown;
        event.position = self->cursorLocal();
        event.button = MouseButton::None;
        event.key = mapKey(key);
        applyMods(event, mods);
        if (event.key != Key::Unknown)
            self->events.push_back(event);
    }

    static void charCallback(GLFWwindow* w, unsigned int codepoint)
    {
        Impl* self = from(w);
        if (!self || codepoint < 32)
            return;
        Event event;
        event.type = EventType::TextInput;
        event.position = self->cursorLocal();
        event.button = MouseButton::None;
        applyMods(event, self->mods());
        if (event.ctrl || event.alt || event.super)
            return;
        appendUtf8(event.text, codepoint);
        if (!event.text.empty())
            self->events.push_back(std::move(event));
    }

    static void dropCallback(GLFWwindow* w, int count, const char** paths)
    {
        Impl* self = from(w);
        if (!self)
            return;
        for (int i = 0; i < count; ++i)
            self->droppedFiles.push_back(paths[i]);
    }

    static void windowRefreshCallback(GLFWwindow* w)
    {
        if (Impl* self = from(w))
            self->redrawActiveTree(true);
    }

    Vec2 logicalSize() const
    {
        int windowW = 0;
        int windowH = 0;
        if (window)
            glfwGetWindowSize(window, &windowW, &windowH);
        return {(float)std::max(1, windowW), (float)std::max(1, windowH)};
    }

    void updateFrameContext()
    {
        context.font = fonts.defaultFontRef();
        context.iconFontLucide = fonts.iconFontLucideRef();
        context.fontSizePx = 13.0f;
        context.timeSeconds = glfwGetTime();
        context.pointer = {pointer.x, pointer.y};
        context.pointerValid = pointerValid;
    }

    void drawTree(Tree& tree, PaintRenderer& renderer, bool present,
                  bool refreshAndLayout)
    {
        if (!window || !surfaceReady || rendering)
            return;

        rendering = true;
        glfwMakeContextCurrent(window);

        int windowW = 0;
        int windowH = 0;
        int fbW = 0;
        int fbH = 0;
        glfwGetWindowSize(window, &windowW, &windowH);
        glfwGetFramebufferSize(window, &fbW, &fbH);
        const Vec2 logical{(float)std::max(1, windowW),
                           (float)std::max(1, windowH)};

        updateFrameContext();
        if (refreshAndLayout) {
            tree.refreshBoundValues();
            tree.layout(logical);
            renderer.prepareOpenPopups(tree);
        }

        float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
        colorFloats(clearColor, r, g, b, a);
        glViewport(0, 0, std::max(1, fbW), std::max(1, fbH));
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);

        surface.beginFrame({(float)std::max(1, windowW),
                            (float)std::max(1, windowH)},
                           std::max(1, fbW), std::max(1, fbH));
        renderer.render(tree, surface, context);
        surface.endFrame();

        if (present) {
            glfwSwapBuffers(window);
            frameOpen = false;
        } else {
            frameOpen = true;
        }
        rendering = false;
    }

    void redrawActiveTree(bool present)
    {
        if (!activeTree || !activeRenderer)
            return;
        drawTree(*activeTree, *activeRenderer, present, true);
    }
};

GlWindow::GlWindow() : impl_(new Impl) {}

GlWindow::~GlWindow()
{
    destroy();
}

bool GlWindow::create(int width, int height, const std::string& title,
                      bool decorated)
{
    if (!snd::ui::detail::ensureGlfwInitialized())
        return false;

    glfwWindowHint(GLFW_DECORATED, decorated ? GLFW_TRUE : GLFW_FALSE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    impl_->window = glfwCreateWindow(width, height, title.c_str(), nullptr,
                                     snd::ui::detail::sharedGlfwContext());
    if (!impl_->window) {
        snd::ui::detail::terminateGlfwIfIdle();
        return false;
    }
    snd::ui::detail::registerGlfwWindow(impl_->window);
#if defined(__APPLE__)
    snd::ui::macPrepareWindowResizeImpl(impl_->window);
    if (!decorated)
        snd::ui::macPrepareFramelessImpl(impl_->window);
#endif

    glfwSetWindowUserPointer(impl_->window, impl_.get());
    glfwSetCursorPosCallback(impl_->window, Impl::cursorCallback);
    glfwSetMouseButtonCallback(impl_->window, Impl::mouseButtonCallback);
    glfwSetScrollCallback(impl_->window, Impl::scrollCallback);
    glfwSetKeyCallback(impl_->window, Impl::keyCallback);
    glfwSetCharCallback(impl_->window, Impl::charCallback);
    glfwSetDropCallback(impl_->window, Impl::dropCallback);
    glfwSetWindowRefreshCallback(impl_->window, Impl::windowRefreshCallback);
    glfwMakeContextCurrent(impl_->window);
    glfwSwapInterval(1);

    std::string fontError;
    if (!impl_->fonts.build(&fontError))
    {
        destroy();
        return false;
    }
    if (!impl_->surface.init()) {
        destroy();
        return false;
    }
    impl_->fonts.upload();
    impl_->surfaceReady = true;
    return true;
}

void GlWindow::destroy()
{
    if (!impl_ || !impl_->window)
        return;
    impl_->surfaceReady = false;
    impl_->activeTree = nullptr;
    impl_->activeRenderer = nullptr;
    glfwMakeContextCurrent(impl_->window);
    impl_->surface.destroyGl();
    impl_->fonts.destroyGl();
    impl_->destroyCursors();
    GLFWwindow* window = impl_->window;
    glfwDestroyWindow(window);
    impl_->window = nullptr;
    snd::ui::detail::unregisterGlfwWindow(window);
}

bool GlWindow::shouldClose() const
{
    return !impl_->window || glfwWindowShouldClose(impl_->window);
}

void GlWindow::setShouldClose(bool close)
{
    if (impl_->window)
        glfwSetWindowShouldClose(impl_->window, close ? GLFW_TRUE : GLFW_FALSE);
}

bool GlWindow::beginFrame(Tree& tree, PaintRenderer& renderer)
{
    if (!impl_->window)
        return false;

    impl_->activeTree = &tree;
    impl_->activeRenderer = &renderer;
    glfwPollEvents();
    // native window drag: while the title bar is held, move the OS window so the
    // grabbed point stays under the cursor (self-correcting via the window-relative
    // delta, so it doesn't feed back as the window moves).
    if (impl_->dragging) {
        if (glfwGetMouseButton(impl_->window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS) {
            impl_->dragging = false;
        } else {
            double cx = 0.0, cy = 0.0;
            glfwGetCursorPos(impl_->window, &cx, &cy);
            int wx = 0, wy = 0;
            glfwGetWindowPos(impl_->window, &wx, &wy);
            glfwSetWindowPos(impl_->window, wx + (int)(cx - impl_->dragStartCursorX),
                             wy + (int)(cy - impl_->dragStartCursorY));
        }
    }
    snd::platform::processMainQueue();
    glfwMakeContextCurrent(impl_->window);

    const Vec2 currentPointer = impl_->cursorLocal();
    const Vec2 pointerDelta = impl_->pointerValid
                                  ? Vec2{currentPointer.x - impl_->pointer.x,
                                         currentPointer.y - impl_->pointer.y}
                                  : Vec2{};
    const bool hasQueuedMove =
        std::any_of(impl_->events.begin(), impl_->events.end(),
                    [](const Event& event) {
                        return event.type == EventType::MouseMove;
                    });
    impl_->pointer = currentPointer;
    impl_->pointerValid = true;
    if (!hasQueuedMove) {
        impl_->events.insert(impl_->events.begin(),
                             impl_->pointerEvent(EventType::MouseMove,
                                                 currentPointer, pointerDelta,
                                                 MouseButton::None,
                                                 impl_->mods()));
    }

    const Vec2 logical = impl_->logicalSize();
    impl_->updateFrameContext();

    bool changed = tree.refreshBoundValues();
    tree.layout(logical);
    renderer.prepareOpenPopups(tree);

    bool consumed = false;
    for (const Event& event : impl_->events) {
        bool popupDismissed = false;
        if (event.type == EventType::MouseDown) {
            popupDismissed = renderer.dismissOpenPopupsOutside(
                tree, ImVec2(0.0f, 0.0f),
                ImVec2(event.position.x, event.position.y));
        }
        if (popupDismissed) {
            consumed = true;
            continue;
        }
        consumed = tree.dispatch(event) || consumed;
    }
    impl_->updateCursor(tree);
    impl_->events.clear();
    if (consumed) {
        changed = tree.refreshBoundValues() || changed;
        tree.layout(logical);
        renderer.prepareOpenPopups(tree);
    }
    (void)changed;

    impl_->drawTree(tree, renderer, false, false);
    return true;
}

void GlWindow::endFrame()
{
    if (!impl_->window || !impl_->frameOpen)
        return;
    glfwMakeContextCurrent(impl_->window);
    glfwSwapBuffers(impl_->window);
    impl_->frameOpen = false;
}

int GlWindow::width() const
{
    int w = 0;
    int h = 0;
    if (impl_->window)
        glfwGetWindowSize(impl_->window, &w, &h);
    return w;
}

int GlWindow::height() const
{
    int w = 0;
    int h = 0;
    if (impl_->window)
        glfwGetWindowSize(impl_->window, &w, &h);
    return h;
}

Vec2 GlWindow::size() const
{
    return {(float)width(), (float)height()};
}

void GlWindow::setTitle(const std::string& title)
{
    if (impl_->window)
        glfwSetWindowTitle(impl_->window, title.c_str());
}

void GlWindow::setClearColor(draw::Color color)
{
    impl_->clearColor = color;
}

bool GlWindow::setUiFont(const std::string& nameOrPath, float sizePx)
{
    if (!impl_->window)
        return false;
    glfwMakeContextCurrent(impl_->window);
    // Keep the current atlas usable if the new face fails to load: build into a
    // fresh atlas, and only swap the live one (and its GL texture) on success.
    draw::StbFontAtlas probe;
    probe.setBaseFont(nameOrPath, sizePx);
    if (!probe.build(nullptr))
        return false;
    impl_->fonts.setBaseFont(nameOrPath, sizePx);
    impl_->fonts.destroyGl();
    impl_->fonts.build(nullptr);
    impl_->fonts.upload();
    return true;
}

std::string GlWindow::uiFont() const
{
    return impl_->fonts.baseFont();
}

void GlWindow::minimize()
{
    if (impl_ && impl_->window)
        glfwIconifyWindow(impl_->window);
}

void GlWindow::toggleMaximize()
{
    if (!impl_ || !impl_->window)
        return;
#if defined(__APPLE__)
    snd::ui::macToggleZoomImpl(impl_->window);
#else
    if (glfwGetWindowAttrib(impl_->window, GLFW_MAXIMIZED))
        glfwRestoreWindow(impl_->window);
    else
        glfwMaximizeWindow(impl_->window);
#endif
}

void GlWindow::toggleFullscreen()
{
    if (!impl_ || !impl_->window)
        return;
#if defined(__APPLE__)
    snd::ui::macToggleFullscreenImpl(impl_->window);
#else
    GLFWwindow* w = impl_->window;
    if (!impl_->fullscreen) {
        glfwGetWindowPos(w, &impl_->savedX, &impl_->savedY);
        glfwGetWindowSize(w, &impl_->savedW, &impl_->savedH);
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        if (const GLFWvidmode* mode = mon ? glfwGetVideoMode(mon) : nullptr) {
            glfwSetWindowMonitor(w, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
            impl_->fullscreen = true;
        }
    } else {
        glfwSetWindowMonitor(w, nullptr, impl_->savedX, impl_->savedY,
                             impl_->savedW > 0 ? impl_->savedW : 1280,
                             impl_->savedH > 0 ? impl_->savedH : 800, 0);
        impl_->fullscreen = false;
    }
#endif
}

void GlWindow::setMouseCaptured(bool captured)
{
    if (impl_ && impl_->window) {
        impl_->mouseCaptured = captured;
        glfwSetInputMode(impl_->window, GLFW_CURSOR,
                         captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (!captured) {
            glfwSetCursor(impl_->window, nullptr);
            impl_->activeCursor = CursorStyle::Default;
        }
    }
}

void GlWindow::beginNativeDrag()
{
    if (!impl_ || !impl_->window)
        return;
#if defined(__APPLE__)
    snd::ui::nativeDragImpl(impl_->window);
#else
    impl_->dragging = true;
    glfwGetCursorPos(impl_->window, &impl_->dragStartCursorX, &impl_->dragStartCursorY);
#endif
}

draw::FrameContext GlWindow::frameContext() const
{
    return impl_->context;
}

std::vector<std::string> GlWindow::takeDroppedFiles()
{
    auto out = std::move(impl_->droppedFiles);
    impl_->droppedFiles.clear();
    return out;
}

} // namespace snd::ui::retained
