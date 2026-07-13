// snd::ui implementation: GLFW + OpenGL3 + Dear ImGui shell.

#include "snd/ui.h"
#include "snd/platform.h"
#include "snd/ui_paint.h"
#include "ui_glfw_shared.h"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui_internal.h" // ImGuiContext::NavCursorVisible (focus-visible)
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#endif

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace snd::ui {

namespace {
// ImGui labels may carry an "##id" suffix; painted button text stops there.
// Returns `label` unchanged when there is no suffix (no allocation), else the
// visible prefix copied into `storage`.
const char* visibleButtonText(const char* label, std::string& storage)
{
    if (!label)
        return "";
    const char* end = label;
    while (*end && !(end[0] == '#' && end[1] == '#'))
        ++end;
    if (*end == 0)
        return label;
    storage.assign(label, end);
    return storage.c_str();
}

float menuItemHeight(const MenuOptions& options)
{
    return options.itemHeight > 0.0f ? options.itemHeight
                                     : std::max(22.0f, ImGui::GetFontSize() * 1.65f);
}

float menuSeparatorHeight()
{
    return 7.0f;
}

bool menuSelectable(const MenuItem& item)
{
    return !item.separator && item.enabled;
}

std::string menuItemKey(const MenuItem& item, int index)
{
    if (!item.id.empty())
        return item.id;
    if (!item.label.empty())
        return item.label;
    return std::to_string(index);
}

int firstMenuIndex(const std::vector<MenuItem>& items)
{
    for (int i = 0; i < (int)items.size(); ++i)
        if (menuSelectable(items[(std::size_t)i]))
            return i;
    return -1;
}

int checkedMenuIndex(const std::vector<MenuItem>& items)
{
    for (int i = 0; i < (int)items.size(); ++i)
        if (menuSelectable(items[(std::size_t)i]) && items[(std::size_t)i].checked)
            return i;
    return -1;
}

int nextMenuIndex(const std::vector<MenuItem>& items, int current, int direction)
{
    if (items.empty())
        return -1;
    if (current < 0 || current >= (int)items.size())
        current = direction < 0 ? 0 : -1;
    for (int step = 0; step < (int)items.size(); ++step) {
        current += direction;
        if (current < 0)
            current = (int)items.size() - 1;
        else if (current >= (int)items.size())
            current = 0;
        if (menuSelectable(items[(std::size_t)current]))
            return current;
    }
    return -1;
}

struct FlatMenuItem {
    const MenuItem* item = nullptr;
    int sourceIndex = -1;
    int depth = 0;
    std::string path;
};

bool pathOpen(const std::vector<std::string>& openPaths, const std::string& path)
{
    return std::find(openPaths.begin(), openPaths.end(), path) != openPaths.end();
}

void setPathOpen(std::vector<std::string>& openPaths, const std::string& path,
                 bool open)
{
    auto it = std::find(openPaths.begin(), openPaths.end(), path);
    if (open) {
        if (it == openPaths.end())
            openPaths.push_back(path);
        return;
    }
    if (it != openPaths.end())
        openPaths.erase(it);
    openPaths.erase(std::remove_if(openPaths.begin(), openPaths.end(),
                                   [&](const std::string& openPath) {
                                       return openPath.size() > path.size() &&
                                              openPath.compare(0, path.size(), path) == 0 &&
                                              openPath[path.size()] == '/';
                                   }),
                    openPaths.end());
}

void flattenMenuItems(const std::vector<MenuItem>& items,
                      const std::vector<std::string>& openPaths,
                      std::vector<FlatMenuItem>& out, int depth = 0,
                      const std::string& parentPath = {})
{
    for (int i = 0; i < (int)items.size(); ++i) {
        const MenuItem& item = items[(std::size_t)i];
        const std::string path = parentPath.empty()
                                     ? menuItemKey(item, i)
                                     : parentPath + "/" + menuItemKey(item, i);
        out.push_back({&item, i, depth, path});
        if (!item.children.empty() && pathOpen(openPaths, path))
            flattenMenuItems(item.children, openPaths, out, depth + 1, path);
    }
}

int firstFlatMenuIndex(const std::vector<FlatMenuItem>& rows)
{
    for (int i = 0; i < (int)rows.size(); ++i)
        if (rows[(std::size_t)i].item && menuSelectable(*rows[(std::size_t)i].item))
            return i;
    return -1;
}

int checkedFlatMenuIndex(const std::vector<FlatMenuItem>& rows)
{
    for (int i = 0; i < (int)rows.size(); ++i)
        if (rows[(std::size_t)i].item &&
            menuSelectable(*rows[(std::size_t)i].item) &&
            rows[(std::size_t)i].item->checked)
            return i;
    return -1;
}

int nextFlatMenuIndex(const std::vector<FlatMenuItem>& rows, int current,
                      int direction)
{
    if (rows.empty())
        return -1;
    if (current < 0 || current >= (int)rows.size())
        current = direction < 0 ? 0 : -1;
    for (std::size_t step = 0; step < rows.size(); ++step) {
        current += direction;
        if (current < 0)
            current = (int)rows.size() - 1;
        else if (current >= (int)rows.size())
            current = 0;
        const FlatMenuItem& row = rows[(std::size_t)current];
        if (row.item && menuSelectable(*row.item))
            return current;
    }
    return -1;
}

std::string lowercaseAscii(std::string text)
{
    for (char& ch : text)
        ch = (char)std::tolower((unsigned char)ch);
    return text;
}

int typeaheadFlatMenuIndex(const std::vector<FlatMenuItem>& rows, int current,
                           const std::string& query)
{
    if (rows.empty() || query.empty())
        return -1;
    const std::string lowerQuery = lowercaseAscii(query);
    for (int step = 1; step <= (int)rows.size(); ++step) {
        const int idx = (std::max(0, current) + step) % (int)rows.size();
        const FlatMenuItem& row = rows[(std::size_t)idx];
        if (!row.item || !menuSelectable(*row.item))
            continue;
        const std::string label = lowercaseAscii(row.item->label);
        if (label.size() >= lowerQuery.size() &&
            std::equal(lowerQuery.begin(), lowerQuery.end(), label.begin()))
            return idx;
    }
    return -1;
}

void collectMenuMetrics(const std::vector<MenuItem>& items, const MenuOptions& options,
                        int depth, float& width, float& height)
{
    for (const MenuItem& item : items) {
        if (item.separator) {
            height += menuSeparatorHeight();
        } else {
            height += menuItemHeight(options);
            const float labelW = ImGui::CalcTextSize(item.label.c_str()).x;
            const float rightW = item.rightText.empty()
                                     ? 0.0f
                                     : ImGui::CalcTextSize(item.rightText.c_str()).x + 20.0f;
            const float iconW = item.icon.empty() ? 0.0f : ImGui::GetFontSize() + 12.0f;
            const float submenuW = item.children.empty() ? 0.0f : 18.0f;
            width = std::max(width, 46.0f + iconW + labelW + rightW + submenuW +
                                        (float)depth * 16.0f);
        }
        collectMenuMetrics(item.children, options, depth + 1, width, height);
    }
}

ImVec2 menuPopupSize(const std::vector<MenuItem>& items, const MenuOptions& options)
{
    float width = options.width > 0.0f ? options.width : 0.0f;
    float height = 0.0f;
    collectMenuMetrics(items, options, 0, width, height);
    return ImVec2(std::max(width, 144.0f) + 8.0f, height + 8.0f);
}

bool beginMenuPopup(const char* popupId, const std::vector<MenuItem>& items,
                    const MenuOptions& options)
{
    const ImVec2 popupSize = menuPopupSize(items, options);
    ImGui::SetNextWindowSize(ImVec2(popupSize.x, 0.0f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    // SND owns the whole menu look: square corners, exactly one border (the
    // SND panel paint), and no ImGui keyboard-nav ring on rows.
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
    const bool open = ImGui::BeginPopup(
        popupId, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!open) {
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
    }
    return open;
}

void endMenuPopup()
{
    ImGui::EndPopup();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}

MenuResult drawMenuContents(const char* popupId, const std::vector<MenuItem>& items,
                            const MenuOptions& options)
{
    MenuResult result;
    if (items.empty())
        return result;

    struct TypeaheadState {
        std::string text;
        double time = 0.0;
    };
    static std::unordered_map<ImGuiID, std::vector<std::string>> openPathsByPopup;
    static std::unordered_map<ImGuiID, TypeaheadState> typeaheadByPopup;

    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID activeKey = ImGui::GetID("##snd-menu-active");
    const ImGuiID popupKey = ImGui::GetID(popupId ? popupId : "##snd-menu");
    std::vector<std::string>& openPaths = openPathsByPopup[popupKey];
    std::vector<FlatMenuItem> rows;
    flattenMenuItems(items, openPaths, rows);

    int active = store->GetInt(activeKey, -2);
    if (ImGui::IsWindowAppearing() || active == -2) {
        active = checkedFlatMenuIndex(rows);
        if (active < 0)
            active = firstFlatMenuIndex(rows);
        store->SetInt(activeKey, active);
    }
    if (active >= (int)rows.size()) {
        active = firstFlatMenuIndex(rows);
        store->SetInt(activeKey, active);
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
            active = nextFlatMenuIndex(rows, active, 1);
            store->SetInt(activeKey, active);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
            active = nextFlatMenuIndex(rows, active, -1);
            store->SetInt(activeKey, active);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) &&
            active >= 0 && active < (int)rows.size()) {
            const FlatMenuItem& row = rows[(std::size_t)active];
            if (row.item && !row.item->children.empty())
                setPathOpen(openPaths, row.path, true);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) && !openPaths.empty()) {
            openPaths.pop_back();
            active = firstFlatMenuIndex(rows);
            store->SetInt(activeKey, active);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
            openPaths.clear();
            active = -1;
            store->SetInt(activeKey, active);
        }
        ImGuiIO& io = ImGui::GetIO();
        TypeaheadState& typeahead = typeaheadByPopup[popupKey];
        if (ImGui::GetTime() - typeahead.time > 0.80)
            typeahead.text.clear();
        for (ImWchar ch : io.InputQueueCharacters) {
            if (ch < 32 || ch > 126 || io.KeyCtrl || io.KeyAlt || io.KeySuper)
                continue;
            typeahead.text.push_back((char)ch);
            typeahead.time = ImGui::GetTime();
            const int match = typeaheadFlatMenuIndex(rows, active, typeahead.text);
            if (match >= 0) {
                active = match;
                store->SetInt(activeKey, active);
            }
        }
    }

    paint::drawMenuPanel(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(),
                         ImGui::GetWindowSize(), palette());

    const float rowH = menuItemHeight(options);
    const float sepH = menuSeparatorHeight();
    const float rowW = std::max(0.0f, ImGui::GetContentRegionAvail().x);
    ImFont* iconFont = options.iconFont ? options.iconFont : iconFontMaterial();

    for (int i = 0; i < (int)rows.size(); ++i) {
        const FlatMenuItem& row = rows[(std::size_t)i];
        const MenuItem& item = *row.item;
        const float h = item.separator ? sepH : rowH;
        const ImVec2 p = ImGui::GetCursorScreenPos();

        ImGui::PushID(i);
        bool clicked = false;
        if (item.separator) {
            ImGui::Dummy(ImVec2(rowW, h));
        } else {
            ImGui::InvisibleButton("##menu-item", ImVec2(rowW, h));
            if (ImGui::IsItemHovered() && item.enabled) {
                active = i;
                store->SetInt(activeKey, active);
            }
            clicked = item.enabled && ImGui::IsItemClicked(ImGuiMouseButton_Left);
        }
        ImGui::PopID();

        paint::ControlState state;
        state.hovered = !item.separator && active == i;
        state.focused = state.hovered;
        state.active = !item.separator && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                       ImGui::IsMouseHoveringRect(p, ImVec2(p.x + rowW, p.y + h));
        state.disabled = !item.enabled;
        state.selected = item.checked || active == i ||
                         (!item.children.empty() && pathOpen(openPaths, row.path));
        MenuItem paintItem = item;
        if (row.depth > 0)
            paintItem.label = std::string((std::size_t)row.depth * 2, ' ') +
                              paintItem.label;
        paint::drawMenuItem(ImGui::GetWindowDrawList(), ImGui::GetFont(), iconFont,
                            p, ImVec2(rowW, h), paintItem, palette(), state);

        const bool keyActivated = active == i && menuSelectable(item) &&
                                  (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                                   ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
                                   ImGui::IsKeyPressed(ImGuiKey_Space, false));
        if (clicked || keyActivated) {
            if (!item.children.empty()) {
                setPathOpen(openPaths, row.path, !pathOpen(openPaths, row.path));
            } else {
                result.activated = true;
                result.index = row.sourceIndex;
                result.id = item.id.empty() ? item.label : item.id;
                result.targetId = row.path;
                openPaths.clear();
                ImGui::CloseCurrentPopup();
            }
        }
    }

    (void)popupId;
    return result;
}
} // namespace

struct Window::Impl {
    GLFWwindow* window = nullptr;
    ImGuiContext* ctx = nullptr; // one ImGui context per window
    std::vector<std::string> droppedFiles;
    int windowedX = 0, windowedY = 0; // frame to restore after fullscreen
    int windowedW = 0, windowedH = 0;
    float clearColor[3] = {0.10f, 0.10f, 0.12f}; // setClearColor overrides
    int swapInterval = 1; // vsync; secondary windows set 0 so waits don't stack
    bool mouseCaptured = false;
    double captureX = 0.0, captureY = 0.0; // cursor pos to restore after capture

    static void dropCallback(GLFWwindow* w, int count, const char** paths)
    {
        auto* self = (Impl*)glfwGetWindowUserPointer(w);
        for (int i = 0; i < count; ++i)
            self->droppedFiles.push_back(paths[i]);
    }
};

Window::Window() : impl(new Impl) {}
Window::~Window() { destroy(); }

#if defined(__APPLE__)
void macPrepareFramelessImpl(GLFWwindow* window); // ui_mac.mm
#endif

bool Window::create(int width, int height, const std::string& title, bool decorated)
{
    if (!detail::ensureGlfwInitialized())
        return false;

    glfwWindowHint(GLFW_DECORATED, decorated ? GLFW_TRUE : GLFW_FALSE);

#if defined(__APPLE__)
    const char* glslVersion = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    // share GL objects with the first window so textures work everywhere
    impl->window =
        glfwCreateWindow(width, height, title.c_str(), nullptr,
                         detail::sharedGlfwContext());
    if (!impl->window) {
        detail::terminateGlfwIfIdle();
        return false;
    }
    detail::registerGlfwWindow(impl->window);

    glfwSetWindowUserPointer(impl->window, impl.get());
    glfwSetDropCallback(impl->window, Impl::dropCallback);
#if defined(__APPLE__)
    if (!decorated)
        macPrepareFramelessImpl(impl->window); // zoom + fullscreen Space work
#endif
    glfwMakeContextCurrent(impl->window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    impl->ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(impl->ctx);
    loadFonts(); // text + embedded Material/Lucide icon fonts (this context)

    // SND style baseline: keyboard-nav cursor in the palette accent instead
    // of ImGui's white ring, square popups. Apps can re-theme afterwards.
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_NavCursor] =
            paint::toVec4(paint::withAlpha(palette().accent, 0x90));
        style.PopupRounding = 0.0f;
    }
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(impl->window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    return true;
}

void Window::destroy()
{
    if (!impl->window)
        return;
    glfwMakeContextCurrent(impl->window);
    ImGui::SetCurrentContext(impl->ctx);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(impl->ctx);
    impl->ctx = nullptr;
    GLFWwindow* window = impl->window;
    glfwDestroyWindow(window);
    impl->window = nullptr;
    detail::unregisterGlfwWindow(window);
}

bool Window::shouldClose() const
{
    return !impl->window || glfwWindowShouldClose(impl->window);
}

void Window::setShouldClose(bool close)
{
    if (impl->window)
        glfwSetWindowShouldClose(impl->window, close ? GLFW_TRUE : GLFW_FALSE);
}

bool Window::beginFrame()
{
    if (!impl->window)
        return false;
    glfwPollEvents();
    snd::platform::processMainQueue(); // runOnMain() jobs + frame-pumped timers
    glfwMakeContextCurrent(impl->window);
    ImGui::SetCurrentContext(impl->ctx); // per-window ImGui context
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    return true;
}

void Window::endFrame()
{
    glfwMakeContextCurrent(impl->window);
    glfwSwapInterval(impl->swapInterval);
    ImGui::SetCurrentContext(impl->ctx);
    ImGui::Render();
    int w, h;
    glfwGetFramebufferSize(impl->window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(impl->clearColor[0], impl->clearColor[1], impl->clearColor[2],
                 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(impl->window);
}

void Window::setSwapInterval(int interval)
{
    impl->swapInterval = interval;
}

void Window::setMouseCaptured(bool captured)
{
    if (!impl->window || captured == impl->mouseCaptured)
        return;
    if (captured) {
        glfwGetCursorPos(impl->window, &impl->captureX, &impl->captureY);
        glfwSetInputMode(impl->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(impl->window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    } else {
        glfwSetInputMode(impl->window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwSetCursorPos(impl->window, impl->captureX, impl->captureY);
    }
    impl->mouseCaptured = captured;
}

void Window::setClearColor(unsigned int rgba)
{
    impl->clearColor[0] = (float)(rgba & 0xFFu) / 255.0f;
    impl->clearColor[1] = (float)((rgba >> 8) & 0xFFu) / 255.0f;
    impl->clearColor[2] = (float)((rgba >> 16) & 0xFFu) / 255.0f;
}

void Window::setTitle(const std::string& title)
{
    if (impl->window)
        glfwSetWindowTitle(impl->window, title.c_str());
}

int Window::width() const
{
    int w = 0, h = 0;
    if (impl->window) glfwGetWindowSize(impl->window, &w, &h);
    return w;
}

int Window::height() const
{
    int w = 0, h = 0;
    if (impl->window) glfwGetWindowSize(impl->window, &w, &h);
    return h;
}

void Window::getPosition(int& x, int& y) const
{
    x = y = 0;
    if (impl->window)
        glfwGetWindowPos(impl->window, &x, &y);
}

void Window::setPosition(int x, int y)
{
    if (impl->window)
        glfwSetWindowPos(impl->window, x, y);
}

void Window::setSize(int width, int height)
{
    if (impl->window)
        glfwSetWindowSize(impl->window, std::max(320, width), std::max(240, height));
}

void Window::minimize()
{
    if (impl->window)
        glfwIconifyWindow(impl->window);
}

#if defined(__APPLE__)
void nativeDragImpl(GLFWwindow* window);        // ui_mac.mm
void macToggleZoomImpl(GLFWwindow* window);     // ui_mac.mm
bool macIsZoomedImpl(GLFWwindow* window);       // ui_mac.mm
void macToggleFullscreenImpl(GLFWwindow* window); // ui_mac.mm
bool macIsFullscreenImpl(GLFWwindow* window);   // ui_mac.mm
#endif

void Window::beginNativeDrag()
{
    if (!impl->window)
        return;
#if defined(__APPLE__)
    nativeDragImpl(impl->window);
#elif defined(_WIN32)
    // Standard Win32 trick: pretend the press hit the title bar.
    HWND hwnd = glfwGetWin32Window(impl->window);
    ReleaseCapture();
    SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
#endif
}

void Window::toggleMaximize()
{
    if (!impl->window)
        return;
#if defined(__APPLE__)
    macToggleZoomImpl(impl->window); // borderless Cocoa: GLFW maximize no-ops
#else
    if (glfwGetWindowAttrib(impl->window, GLFW_MAXIMIZED))
        glfwRestoreWindow(impl->window);
    else
        glfwMaximizeWindow(impl->window);
#endif
}

bool Window::isZoomed() const
{
    if (!impl->window)
        return false;
#if defined(__APPLE__)
    return macIsZoomedImpl(impl->window);
#else
    return glfwGetWindowAttrib(impl->window, GLFW_MAXIMIZED) != 0;
#endif
}

void Window::toggleFullscreen()
{
    if (!impl->window)
        return;
#if defined(__APPLE__)
    macToggleFullscreenImpl(impl->window);
#else
    if (glfwGetWindowMonitor(impl->window)) {
        glfwSetWindowMonitor(impl->window, nullptr, impl->windowedX,
                             impl->windowedY, impl->windowedW,
                             impl->windowedH, 0);
    } else {
        glfwGetWindowPos(impl->window, &impl->windowedX, &impl->windowedY);
        glfwGetWindowSize(impl->window, &impl->windowedW, &impl->windowedH);
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (mode)
            glfwSetWindowMonitor(impl->window, monitor, 0, 0, mode->width,
                                 mode->height, mode->refreshRate);
    }
#endif
}

bool Window::isFullscreen() const
{
    if (!impl->window)
        return false;
#if defined(__APPLE__)
    return macIsFullscreenImpl(impl->window);
#else
    return glfwGetWindowMonitor(impl->window) != nullptr;
#endif
}

std::vector<std::string> Window::takeDroppedFiles()
{
    auto out = std::move(impl->droppedFiles);
    impl->droppedFiles.clear();
    return out;
}

// --- custom widgets ---------------------------------------------------------

void gradientPanel(const ImVec2& size, ImU32 tl, ImU32 tr, ImU32 br, ImU32 bl)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    paint::drawGradientPanel(ImGui::GetWindowDrawList(), p, size, tl, tr, br, bl);
    ImGui::Dummy(size);
}

bool gradientButton(const char* label, const ImVec2& size, ImU32 top, ImU32 bottom)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, size);
    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = itemFocusVisible();
    std::string visible;
    paint::drawAnimatedButton(ImGui::GetWindowDrawList(), ImGui::GetFont(), p, size,
                              visibleButtonText(label, visible), top, bottom,
                              palette(), state, 0.0f);
    return pressed;
}

bool animatedButton(const char* label, const ImVec2& size, ImU32 top, ImU32 bottom)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, size);
    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = itemFocusVisible();
    const float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 3.6f);
    std::string visible;
    paint::drawAnimatedButton(ImGui::GetWindowDrawList(), ImGui::GetFont(), p, size,
                              visibleButtonText(label, visible), top, bottom,
                              palette(), state, pulse);
    return pressed;
}

bool button(const char* label, const ImVec2& size,
            const paint::ButtonPainter& painter)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, size);
    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = itemFocusVisible();

    std::string visible;
    paint::ButtonPaintArgs args;
    args.drawList = ImGui::GetWindowDrawList();
    args.font = ImGui::GetFont();
    args.topLeft = p;
    args.size = size;
    args.text = visibleButtonText(label, visible);
    args.palette = &palette();
    args.state = &state;
    paint::drawButtonWithPainter(args, painter);
    return pressed;
}

bool itemFocusVisible()
{
    // Nav cursor is hidden after a mouse click and shown after a nav move, so
    // this is true only for keyboard/gamepad focus on the item just submitted.
    ImGuiContext* g = ImGui::GetCurrentContext();
    return g && g->NavCursorVisible && ImGui::IsItemFocused();
}

bool outlineButton(const char* label, const ImVec2& size)
{
    paint::OutlineButtonStyle style;
    return outlineButton(label, size, style);
}

bool outlineButton(const char* label, const ImVec2& size,
                   const paint::OutlineButtonStyle& style, bool selected)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, size);
    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = itemFocusVisible();
    state.selected = selected;
    std::string visible;
    paint::drawOutlineButton(ImGui::GetWindowDrawList(), ImGui::GetFont(), p, size,
                             visibleButtonText(label, visible), palette(), state,
                             style);
    return pressed;
}

bool rangeSlider(const char* id, float* lo, float* hi, float minV, float maxV,
                 const ImVec2& size)
{
    if (!lo || !hi)
        return false;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz =
        (size.x > 0.0f && size.y > 0.0f) ? size : ImVec2(140.0f, 20.0f);
    const ImGuiID gid = ImGui::GetID(id);
    ImGui::InvisibleButton(id, sz);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool focused = itemFocusVisible();

    const float range = maxV - minV;
    const float span = std::fabs(range) < 1e-9f ? 1.0f : range;
    const float handleW = 8.0f;
    const float usableL = p.x + handleW * 0.5f;
    const float usableW = std::max(1.0f, sz.x - handleW);
    const auto valToX = [&](float v) {
        return usableL + std::clamp((v - minV) / span, 0.0f, 1.0f) * usableW;
    };
    const auto xToVal = [&](float x) {
        return minV + std::clamp((x - usableL) / usableW, 0.0f, 1.0f) * range;
    };

    ImGuiStorage* store = ImGui::GetStateStorage();
    int grabbed = store->GetInt(gid, -1);
    if (ImGui::IsItemActivated()) {
        const float mx = ImGui::GetIO().MousePos.x;
        grabbed = std::fabs(mx - valToX(*lo)) <= std::fabs(mx - valToX(*hi)) ? 0 : 1;
        store->SetInt(gid, grabbed);
    }

    bool changed = false;
    if (active && grabbed >= 0) {
        float v = xToVal(ImGui::GetIO().MousePos.x);
        if (grabbed == 0) {
            v = std::min(v, *hi);
            if (v != *lo) { *lo = v; changed = true; }
        } else {
            v = std::max(v, *lo);
            if (v != *hi) { *hi = v; changed = true; }
        }
    }
    if (focused) {
        const float step = span / 100.0f * (ImGui::GetIO().KeyShift ? 0.25f : 1.0f);
        const int kh = grabbed >= 0 ? grabbed : 1;
        float delta = 0.0f;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
            delta -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
            delta += step;
        if (delta != 0.0f) {
            if (kh == 0)
                *lo = std::clamp(*lo + delta, minV, *hi);
            else
                *hi = std::clamp(*hi + delta, *lo, maxV);
            store->SetInt(gid, kh);
            changed = true;
        }
    }

    paint::ControlState state;
    state.hovered = hovered;
    state.active = active;
    state.focused = focused;
    const float nlo = std::clamp((*lo - minV) / span, 0.0f, 1.0f);
    const float nhi = std::clamp((*hi - minV) / span, 0.0f, 1.0f);
    paint::drawRangeSlider(ImGui::GetWindowDrawList(), p, sz, nlo, nhi, palette(),
                           state, active ? grabbed : -1);
    return changed;
}

void progressBar(const char* id, float progress, const ImVec2& size)
{
    (void)id;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz =
        (size.x > 0.0f && size.y > 0.0f) ? size : ImVec2(160.0f, 10.0f);
    ImGui::Dummy(sz);
    paint::drawProgressBar(ImGui::GetWindowDrawList(), p, sz, progress, palette(),
                           progress < 0.0f, ImGui::GetTime());
}

void waveformView(const char* id, const float* samples, int count,
                  const ImVec2& size, float playhead)
{
    (void)id;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz =
        (size.x > 0.0f && size.y > 0.0f) ? size : ImVec2(240.0f, 60.0f);
    ImGui::Dummy(sz);
    paint::drawWaveform(ImGui::GetWindowDrawList(), p, sz, samples, count,
                        palette(), playhead);
}

void spectrumView(const char* id, const float* mags, int bins,
                  const ImVec2& size)
{
    (void)id;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz =
        (size.x > 0.0f && size.y > 0.0f) ? size : ImVec2(240.0f, 80.0f);
    ImGui::Dummy(sz);
    paint::drawSpectrum(ImGui::GetWindowDrawList(), p, sz, mags, bins, palette());
}

void timelineRuler(const char* id, double startBeat, double endBeat,
                   double beatsPerBar, const ImVec2& size, float playhead)
{
    (void)id;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz =
        (size.x > 0.0f && size.y > 0.0f) ? size : ImVec2(320.0f, 22.0f);
    ImGui::Dummy(sz);
    paint::drawTimelineRuler(ImGui::GetWindowDrawList(), ImGui::GetFont(), p, sz,
                             startBeat, endBeat, beatsPerBar, palette(), playhead);
}

bool automationLane(const char* id, std::vector<AutoPoint>& points,
                    const ImVec2& size)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz =
        (size.x > 0.0f && size.y > 0.0f) ? size : ImVec2(320.0f, 100.0f);
    const ImGuiID gid = ImGui::GetID(id);
    ImGui::InvisibleButton(id, sz,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight);
    const bool active = ImGui::IsItemActive();
    ImGuiStorage* store = ImGui::GetStateStorage();
    int grabbed = store->GetInt(gid, -1);
    const ImVec2 m = ImGui::GetIO().MousePos;
    const auto toX = [&](float t) { return p.x + std::clamp(t, 0.0f, 1.0f) * sz.x; };
    const auto toY = [&](float v) {
        return p.y + sz.y - std::clamp(v, 0.0f, 1.0f) * sz.y;
    };
    const auto fromXY = [&](ImVec2 q) {
        return AutoPoint{std::clamp((q.x - p.x) / sz.x, 0.0f, 1.0f),
                         std::clamp(1.0f - (q.y - p.y) / sz.y, 0.0f, 1.0f)};
    };
    const auto nearest = [&](ImVec2 q) {
        int idx = -1;
        float best = 10.0f;
        for (size_t i = 0; i < points.size(); ++i) {
            const float dx = toX(points[i].time) - q.x;
            const float dy = toY(points[i].value) - q.y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d < best) {
                best = d;
                idx = (int)i;
            }
        }
        return idx;
    };
    const auto resort = [&] {
        std::sort(points.begin(), points.end(),
                  [](const AutoPoint& a, const AutoPoint& b) {
                      return a.time < b.time;
                  });
    };

    bool changed = false;
    if (ImGui::IsItemActivated()) {
        const int n = nearest(m);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (n >= 0)
                points.erase(points.begin() + n);
            else {
                points.push_back(fromXY(m));
                resort();
            }
            changed = true;
            grabbed = -1;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && n >= 0) {
            points.erase(points.begin() + n);
            changed = true;
            grabbed = -1;
        } else {
            grabbed = n;
        }
        store->SetInt(gid, grabbed);
    }
    if (active && grabbed >= 0 && grabbed < (int)points.size()) {
        AutoPoint np = fromXY(m);
        const float lo = grabbed > 0 ? points[(size_t)grabbed - 1].time : 0.0f;
        const float hi = grabbed + 1 < (int)points.size()
                             ? points[(size_t)grabbed + 1].time
                             : 1.0f;
        np.time = std::clamp(np.time, lo, hi);
        if (np.time != points[(size_t)grabbed].time ||
            np.value != points[(size_t)grabbed].value) {
            points[(size_t)grabbed] = np;
            changed = true;
        }
    }
    if (!active && grabbed >= 0) {
        store->SetInt(gid, -1);
        grabbed = -1;
    }

    paint::drawAutomationLane(ImGui::GetWindowDrawList(), p, sz, points.data(),
                              (int)points.size(), palette(),
                              active ? grabbed : -1);
    return changed;
}

bool colorPicker(const char* id, float* h, float* s, float* v,
                 const ImVec2& size)
{
    if (!h || !s || !v)
        return false;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz =
        (size.x > 0.0f && size.y > 0.0f) ? size : ImVec2(180.0f, 160.0f);
    const ImGuiID gid = ImGui::GetID(id);
    ImGui::InvisibleButton(id, sz);
    const bool active = ImGui::IsItemActive();
    ImGuiStorage* store = ImGui::GetStateStorage();
    int region = store->GetInt(gid, 0); // 0 none, 1 sv square, 2 hue bar
    const float gap = 4.0f;
    const float hueBarH = std::clamp(sz.y * 0.18f, 10.0f, 18.0f);
    const float svH = std::max(1.0f, sz.y - hueBarH - gap);
    const ImVec2 m = ImGui::GetIO().MousePos;
    if (ImGui::IsItemActivated()) {
        region = (m.y - p.y <= svH) ? 1 : 2;
        store->SetInt(gid, region);
    }
    bool changed = false;
    if (active && region == 1) {
        const float ns = std::clamp((m.x - p.x) / sz.x, 0.0f, 1.0f);
        const float nv = std::clamp(1.0f - (m.y - p.y) / svH, 0.0f, 1.0f);
        if (ns != *s || nv != *v) {
            *s = ns;
            *v = nv;
            changed = true;
        }
    } else if (active && region == 2) {
        const float nh = std::clamp((m.x - p.x) / sz.x, 0.0f, 1.0f);
        if (nh != *h) {
            *h = nh;
            changed = true;
        }
    }
    if (!active && region != 0)
        store->SetInt(gid, 0);
    paint::drawColorPicker(ImGui::GetWindowDrawList(), p, sz, *h, *s, *v,
                           palette());
    return changed;
}

void pushToast(ToastStack& stack, std::string text, double now, double seconds)
{
    stack.items.push_back({std::move(text), now, now + seconds});
}

void toasts(ToastStack& stack, const ImVec2& anchorBottomRight, double now)
{
    stack.items.erase(
        std::remove_if(stack.items.begin(), stack.items.end(),
                       [now](const ToastStack::Item& it) {
                           return now >= it.expiry;
                       }),
        stack.items.end());
    if (stack.items.empty())
        return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float w = 240.0f, hgt = 30.0f, gap = 6.0f;
    float y = anchorBottomRight.y - hgt;
    for (auto it = stack.items.rbegin(); it != stack.items.rend(); ++it) {
        const double life = it->expiry - now;
        const float fadeIn = (float)std::clamp((now - it->bornAt) / 0.25, 0.0, 1.0);
        const float fadeOut = (float)std::clamp(life / 0.4, 0.0, 1.0);
        const float alpha = std::min(fadeIn, fadeOut);
        paint::drawToast(dl, ImGui::GetFont(), ImVec2(anchorBottomRight.x - w, y),
                         ImVec2(w, hgt), it->text.c_str(), palette(), alpha);
        y -= (hgt + gap);
    }
}

void popover(const char* id, const ImVec2& topLeft, const ImVec2& size,
             float beakCenterX, bool beakOnTop)
{
    (void)id;
    paint::drawPopover(ImGui::GetForegroundDrawList(), topLeft, size, palette(),
                       beakCenterX, beakOnTop);
}

void propertyRow(const char* label, const ImVec2& size, float labelWidth,
                 bool alt)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz =
        (size.x > 0.0f && size.y > 0.0f) ? size : ImVec2(220.0f, 24.0f);
    paint::drawPropertyRow(ImGui::GetWindowDrawList(), ImGui::GetFont(), p, sz,
                           label, palette(), alt, labelWidth);
    ImGui::Dummy(ImVec2(labelWidth, sz.y));
    ImGui::SameLine(0.0f, 4.0f);
}

int table(const char* id, const TableModel& model, const ImVec2& size,
          int selectedRow)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz =
        (size.x > 0.0f && size.y > 0.0f) ? size : ImVec2(320.0f, 200.0f);
    ImGui::InvisibleButton(id, sz);
    const ImGuiID syKey = ImGui::GetID((std::string(id) + "##sy").c_str());
    ImGuiStorage* store = ImGui::GetStateStorage();
    float scrollY = store->GetFloat(syKey, 0.0f);
    const float rowH = 20.0f;
    const float bodyTop = p.y + rowH;
    const float viewH = std::max(0.0f, sz.y - rowH);
    const float maxScroll = std::max(0.0f, (float)model.rows * rowH - viewH);
    int sel = selectedRow;
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f)
        scrollY = std::clamp(scrollY - ImGui::GetIO().MouseWheel * rowH * 2.0f,
                             0.0f, maxScroll);
    if (ImGui::IsItemClicked()) {
        const float my = ImGui::GetIO().MousePos.y;
        if (my >= bodyTop) {
            const int r = (int)((my - bodyTop + scrollY) / rowH);
            if (r >= 0 && r < model.rows)
                sel = r;
        }
    }
    if (ImGui::IsItemFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            sel = std::min(sel + 1, model.rows - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && sel > 0)
            sel = sel - 1;
    }
    if (sel >= 0 && model.rows > 0) {
        const float selTop = (float)sel * rowH, selBot = selTop + rowH;
        if (selTop < scrollY)
            scrollY = selTop;
        else if (selBot > scrollY + viewH)
            scrollY = selBot - viewH;
        scrollY = std::clamp(scrollY, 0.0f, maxScroll);
    }
    store->SetFloat(syKey, scrollY);
    paint::drawTable(ImGui::GetWindowDrawList(), ImGui::GetFont(), p, sz, model,
                     palette(), sel, scrollY);
    return sel;
}

void tooltip(const char* text, float maxWidth)
{
    if (!text || !text[0])
        return;
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        return;

    const Palette& pal = palette();
    ImGui::PushStyleColor(ImGuiCol_PopupBg, paint::toVec4(pal.frame));
    ImGui::PushStyleColor(ImGuiCol_Border, paint::toVec4(pal.frameBright));
    ImGui::PushStyleColor(ImGuiCol_Text, paint::toVec4(pal.text));
    if (ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(maxWidth > 0.0f ? maxWidth : 400.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleColor(3);
}

bool iconButton(const char* id, Icon icon, const ImVec2& size, ImU32 accent, bool active)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, size);
    paint::ControlState state;
    state.hovered = ImGui::IsItemHovered();
    state.active = ImGui::IsItemActive();
    state.focused = itemFocusVisible();
    paint::drawVectorIconButton(ImGui::GetWindowDrawList(), p, size, icon, accent,
                                palette(), state, active);
    return pressed;
}

bool transportButton(const char* id, Icon icon, const ImVec2& size,
                     bool selected, bool actOnPress)
{
    paint::OutlineButtonStyle style;
    return transportButton(id, icon, size, style, selected, actOnPress);
}

bool transportButton(const char* id, Icon icon, const ImVec2& size,
                     const paint::OutlineButtonStyle& style, bool selected,
                     bool actOnPress)
{
    ImVec2 sz = size;
    if (sz.x <= 0.0f)
        sz.x = 36.0f;
    if (sz.y <= 0.0f)
        sz.y = 18.0f;

    ImGuiButtonFlags flags = ImGuiButtonFlags_EnableNav;
    if (actOnPress)
        flags |= ImGuiButtonFlags_PressedOnClick;

    ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, sz, flags);
    const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
    paint::ControlState state;
    state.hovered = !disabled && ImGui::IsItemHovered();
    state.active = !disabled && ImGui::IsItemActive();
    state.focused = !disabled && itemFocusVisible();
    state.disabled = disabled;
    state.selected = selected;
    paint::drawTransportButton(ImGui::GetWindowDrawList(), icon, p, sz, palette(),
                               state, style);
    return pressed && !disabled;
}

void openPopupMenu(const char* popupId)
{
    if (popupId && popupId[0])
        ImGui::OpenPopup(popupId);
}

MenuResult popupMenu(const char* popupId, const std::vector<MenuItem>& items,
                     const MenuOptions& options)
{
    if (!popupId || !popupId[0] || !beginMenuPopup(popupId, items, options))
        return {};
    MenuResult result = drawMenuContents(popupId, items, options);
    endMenuPopup();
    return result;
}

MenuResult dropdownMenu(const char* id, const char* currentLabel,
                        const std::vector<MenuItem>& items, int* selectedIndex,
                        const ImVec2& size, const MenuOptions& options)
{
    paint::OutlineButtonStyle style;
    style.hoverBorder = palette().accent;
    style.activeFill = paint::withAlpha(palette().accent, 0x28);
    return dropdownMenu(id, currentLabel, items, selectedIndex, style, size,
                        options);
}

MenuResult dropdownMenu(const char* id, const char* currentLabel,
                        const std::vector<MenuItem>& items, int* selectedIndex,
                        const paint::OutlineButtonStyle& buttonStyle,
                        const ImVec2& size, const MenuOptions& options)
{
    MenuResult result;
    if (!id || !id[0])
        return result;
    const char* label = currentLabel ? currentLabel : "";
    ImVec2 buttonSize = size;
    if (buttonSize.x <= 0.0f)
        buttonSize.x = options.width > 0.0f ? options.width : 160.0f;
    if (buttonSize.y <= 0.0f)
        buttonSize.y = std::max(24.0f, ImGui::GetFrameHeight());

    std::string buttonLabel = std::string(label) + "##" + (id ? id : "dropdown");
    const bool pressed = outlineButton(buttonLabel.c_str(), buttonSize, buttonStyle);
    if (pressed)
        ImGui::OpenPopup(id);

    MenuOptions popupOptions = options;
    popupOptions.width = popupOptions.width > 0.0f ? popupOptions.width : buttonSize.x;
    if (beginMenuPopup(id, items, popupOptions)) {
        result = drawMenuContents(id, items, popupOptions);
        endMenuPopup();
    }

    if (result.activated && selectedIndex)
        *selectedIndex = result.index;
    return result;
}

MenuResult contextMenu(const char* popupId, const std::vector<MenuItem>& items,
                       const MenuOptions& options)
{
    if (!popupId || !popupId[0])
        return {};

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing);
        ImGui::OpenPopup(popupId);
    }

    if (!beginMenuPopup(popupId, items, options))
        return {};

    MenuResult result = drawMenuContents(popupId, items, options);
    endMenuPopup();
    return result;
}

} // namespace snd::ui
