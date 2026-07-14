// snd::platform -- OS-specific glue: native file dialogs, standard paths.
// Apps never write per-OS code themselves; it lives here.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace snd::platform {

// One filter, e.g. {"Audio files", "wav,flac,mp3,ogg"}.
using FileFilter = std::pair<std::string, std::string>;

// Native "Open File" dialog. Empty optional = user cancelled.
std::optional<std::string> openFileDialog(const std::vector<FileFilter>& filters = {});

// Native "Choose Folder" dialog.
std::optional<std::string> openFolderDialog();

// Native "Save File" dialog.
std::optional<std::string> saveFileDialog(const std::string& defaultName,
                                          const std::vector<FileFilter>& filters = {});

// Reveal a file in the platform file manager, or open the directory itself.
bool revealPath(const std::string& path);

// Per-OS config directory for this app, created if missing:
// mac ~/Library/Application Support/<app>, win %APPDATA%/<app>, linux ~/.config/<app>
std::string configDir(const std::string& appName);

// Absolute path of the running executable (empty if the OS won't say).
// Lets an app re-run itself, e.g. as a plugin-scan worker process.
std::string executablePath();

// --- main-thread dispatch + timers -------------------------------------------
// Queue work from any thread; the app's frame loop drains it. snd::ui's
// Window::beginFrame() calls processMainQueue() automatically, so windowed
// apps get this for free; headless code pumps it explicitly.
void runOnMain(std::function<void()> fn);
void processMainQueue();

// Frame-pumped repeating timer: fires during processMainQueue() when due.
class Timer {
public:
    Timer();
    ~Timer();
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    void start(uint32_t intervalMs, std::function<void()> fn);
    void stop();
    bool running() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl;
};

// --- worker pool ---------------------------------------------------------------
// Background jobs (file scans, renders). Jobs must not touch UI; hand results
// back through runOnMain().
class ThreadPool {
public:
    explicit ThreadPool(unsigned threads = 0); // 0 = hardware concurrency
    ~ThreadPool();                             // waits for the queue to drain
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void submit(std::function<void()> job);
    void wait(); // block until every submitted job has finished

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace snd::platform
