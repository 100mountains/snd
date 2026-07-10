#include "snd/ui_retained_gl.h"

#include "snd/platform.h"
#include "snd/ui_retained_widgets.h"
#include "ui_draw_gl.h"

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

namespace snd::ui::retained {

namespace {

int gWindowCount = 0;
GLFWwindow* gShareWindow = nullptr;

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
};

GlWindow::GlWindow() : impl_(new Impl) {}

GlWindow::~GlWindow()
{
    destroy();
}

bool GlWindow::create(int width, int height, const std::string& title,
                      bool decorated)
{
    if (!glfwInit())
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
                                     gShareWindow);
    if (!impl_->window) {
        if (gWindowCount == 0)
            glfwTerminate();
        return false;
    }
    if (!gShareWindow)
        gShareWindow = impl_->window;
    ++gWindowCount;

    glfwSetWindowUserPointer(impl_->window, impl_.get());
    glfwSetCursorPosCallback(impl_->window, Impl::cursorCallback);
    glfwSetMouseButtonCallback(impl_->window, Impl::mouseButtonCallback);
    glfwSetScrollCallback(impl_->window, Impl::scrollCallback);
    glfwSetKeyCallback(impl_->window, Impl::keyCallback);
    glfwSetCharCallback(impl_->window, Impl::charCallback);
    glfwSetDropCallback(impl_->window, Impl::dropCallback);
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
    return true;
}

void GlWindow::destroy()
{
    if (!impl_ || !impl_->window)
        return;
    glfwMakeContextCurrent(impl_->window);
    impl_->surface.destroyGl();
    impl_->fonts.destroyGl();
    if (impl_->window == gShareWindow)
        gShareWindow = nullptr;
    glfwDestroyWindow(impl_->window);
    impl_->window = nullptr;
    if (--gWindowCount == 0)
        glfwTerminate();
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

    glfwPollEvents();
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

    int windowW = 0;
    int windowH = 0;
    int fbW = 0;
    int fbH = 0;
    glfwGetWindowSize(impl_->window, &windowW, &windowH);
    glfwGetFramebufferSize(impl_->window, &fbW, &fbH);
    const Vec2 logical{(float)std::max(1, windowW),
                       (float)std::max(1, windowH)};

    impl_->context.font = impl_->fonts.defaultFontRef();
    impl_->context.iconFontLucide = impl_->fonts.iconFontLucideRef();
    impl_->context.fontSizePx = 13.0f;
    impl_->context.timeSeconds = glfwGetTime();
    impl_->context.pointer = {impl_->pointer.x, impl_->pointer.y};
    impl_->context.pointerValid = impl_->pointerValid;

    bool changed = tree.refreshBoundValues();
    tree.layout(logical);
    renderer.prepareOpenPopups(tree);

    bool consumed = false;
    for (const Event& event : impl_->events) {
        if (event.type == EventType::MouseDown) {
            renderer.dismissOpenPopupsOutside(
                tree, ImVec2(0.0f, 0.0f),
                ImVec2(event.position.x, event.position.y));
        }
        consumed = tree.dispatch(event) || consumed;
    }
    impl_->events.clear();
    if (consumed) {
        changed = tree.refreshBoundValues() || changed;
        tree.layout(logical);
        renderer.prepareOpenPopups(tree);
    }
    (void)changed;

    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    colorFloats(impl_->clearColor, r, g, b, a);
    glViewport(0, 0, fbW, fbH);
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);

    impl_->surface.beginFrame({(float)std::max(1, windowW),
                               (float)std::max(1, windowH)},
                              std::max(1, fbW), std::max(1, fbH));
    renderer.render(tree, impl_->surface, impl_->context);
    impl_->surface.endFrame();
    impl_->frameOpen = true;
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
