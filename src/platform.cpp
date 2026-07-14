// snd::platform over nativefiledialog-extended + std::filesystem.

#include "snd/platform.h"

#include <nfd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <spawn.h>
#elif defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#else
#include <spawn.h>
#endif

#if !defined(_WIN32)
extern char** environ;
#endif

namespace snd::platform {

namespace {

// NFD wants init/quit around usage; do it once lazily.
struct NfdSession {
    NfdSession() { ok = NFD_Init() == NFD_OKAY; }
    ~NfdSession()
    {
        if (ok)
            NFD_Quit();
    }
    bool ok = false;
};

bool nfdReady()
{
    static NfdSession session;
    return session.ok;
}

std::vector<nfdfilteritem_t> toNfdFilters(const std::vector<FileFilter>& filters)
{
    std::vector<nfdfilteritem_t> out;
    for (auto& f : filters)
        out.push_back({f.first.c_str(), f.second.c_str()});
    return out;
}

} // namespace

std::optional<std::string> openFileDialog(const std::vector<FileFilter>& filters)
{
    if (!nfdReady())
        return std::nullopt;
    auto nfdFilters = toNfdFilters(filters);
    nfdchar_t* path = nullptr;
    nfdresult_t r = NFD_OpenDialog(&path, nfdFilters.data(), (nfdfiltersize_t)nfdFilters.size(),
                                   nullptr);
    if (r != NFD_OKAY || !path)
        return std::nullopt;
    std::string result = path;
    NFD_FreePath(path);
    return result;
}

std::optional<std::string> openFolderDialog()
{
    if (!nfdReady())
        return std::nullopt;
    nfdchar_t* path = nullptr;
    nfdresult_t r = NFD_PickFolder(&path, nullptr);
    if (r != NFD_OKAY || !path)
        return std::nullopt;
    std::string result = path;
    NFD_FreePath(path);
    return result;
}

std::optional<std::string> saveFileDialog(const std::string& defaultName,
                                          const std::vector<FileFilter>& filters)
{
    if (!nfdReady())
        return std::nullopt;
    auto nfdFilters = toNfdFilters(filters);
    nfdchar_t* path = nullptr;
    nfdresult_t r = NFD_SaveDialog(&path, nfdFilters.data(), (nfdfiltersize_t)nfdFilters.size(),
                                   nullptr, defaultName.c_str());
    if (r != NFD_OKAY || !path)
        return std::nullopt;
    std::string result = path;
    NFD_FreePath(path);
    return result;
}

bool revealPath(const std::string& path)
{
    if (path.empty())
        return false;
    const std::filesystem::path target(path);
#if defined(_WIN32)
    std::error_code error;
    if (std::filesystem::is_directory(target, error))
        return reinterpret_cast<intptr_t>(ShellExecuteA(
                   nullptr, "open", target.string().c_str(), nullptr, nullptr,
                   SW_SHOWNORMAL)) > 32;
    const std::string argument = "/select,\"" + target.string() + "\"";
    return reinterpret_cast<intptr_t>(ShellExecuteA(
               nullptr, "open", "explorer.exe", argument.c_str(), nullptr,
               SW_SHOWNORMAL)) > 32;
#elif defined(__APPLE__)
    pid_t process = 0;
    const std::string value = target.string();
    char* const arguments[] = {
        const_cast<char*>("/usr/bin/open"), const_cast<char*>("-R"),
        const_cast<char*>(value.c_str()), nullptr};
    return posix_spawn(&process, arguments[0], nullptr, nullptr, arguments,
                       environ) == 0;
#else
    std::error_code error;
    const auto folder = std::filesystem::is_directory(target, error)
                            ? target
                            : target.parent_path();
    const std::string value = folder.string();
    pid_t process = 0;
    char* const arguments[] = {
        const_cast<char*>("xdg-open"), const_cast<char*>(value.c_str()), nullptr};
    return posix_spawnp(&process, arguments[0], nullptr, nullptr, arguments,
                        environ) == 0;
#endif
}

std::string configDir(const std::string& appName)
{
    std::filesystem::path base;
#if defined(__APPLE__)
    if (const char* home = std::getenv("HOME"))
        base = std::filesystem::path(home) / "Library" / "Application Support";
#elif defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"))
        base = appdata;
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"))
        base = xdg;
    else if (const char* home = std::getenv("HOME"))
        base = std::filesystem::path(home) / ".config";
#endif
    if (base.empty())
        base = std::filesystem::temp_directory_path();

    auto dir = base / appName;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

// --- main-thread dispatch + timers ------------------------------------------

namespace {

std::mutex gQueueMutex;
std::deque<std::function<void()>> gMainQueue;

struct TimerState {
    std::atomic<bool> active{false};
    uint32_t intervalMs = 0;
    std::chrono::steady_clock::time_point nextFire;
    std::function<void()> fn;
};

std::mutex gTimersMutex;
std::vector<std::weak_ptr<TimerState>> gTimers;

} // namespace

void runOnMain(std::function<void()> fn)
{
    std::lock_guard<std::mutex> g(gQueueMutex);
    gMainQueue.push_back(std::move(fn));
}

void processMainQueue()
{
    // drain what's queued NOW (jobs queued by jobs run next pump)
    std::deque<std::function<void()>> batch;
    {
        std::lock_guard<std::mutex> g(gQueueMutex);
        batch.swap(gMainQueue);
    }
    for (auto& fn : batch)
        fn();

    // due timers
    auto now = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<TimerState>> due;
    {
        std::lock_guard<std::mutex> g(gTimersMutex);
        for (auto it = gTimers.begin(); it != gTimers.end();) {
            auto t = it->lock();
            if (!t) {
                it = gTimers.erase(it);
                continue;
            }
            if (t->active.load() && now >= t->nextFire) {
                t->nextFire = now + std::chrono::milliseconds(t->intervalMs);
                due.push_back(std::move(t));
            }
            ++it;
        }
    }
    for (auto& t : due)
        if (t->active.load() && t->fn)
            t->fn();
}

struct Timer::Impl : TimerState {};

Timer::Timer() : impl(std::make_shared<Impl>()) {}

Timer::~Timer() { stop(); }

void Timer::start(uint32_t intervalMs, std::function<void()> fn)
{
    impl->intervalMs = intervalMs == 0 ? 1 : intervalMs;
    impl->fn = std::move(fn);
    impl->nextFire =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(impl->intervalMs);
    if (!impl->active.exchange(true)) {
        std::lock_guard<std::mutex> g(gTimersMutex);
        gTimers.push_back(impl);
    }
}

void Timer::stop() { impl->active.store(false); }

bool Timer::running() const { return impl->active.load(); }

// --- worker pool ---------------------------------------------------------------

struct ThreadPool::Impl {
    std::vector<std::thread> workers;
    std::deque<std::function<void()>> jobs;
    std::mutex mutex;
    std::condition_variable cv, idleCv;
    unsigned busy = 0;
    bool quit = false;

    void loop()
    {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&] { return quit || !jobs.empty(); });
                if (quit && jobs.empty())
                    return;
                job = std::move(jobs.front());
                jobs.pop_front();
                ++busy;
            }
            job();
            {
                std::lock_guard<std::mutex> lock(mutex);
                --busy;
            }
            idleCv.notify_all();
        }
    }
};

ThreadPool::ThreadPool(unsigned threads) : impl(new Impl)
{
    if (threads == 0)
        threads = std::max(1u, std::thread::hardware_concurrency());
    for (unsigned i = 0; i < threads; ++i)
        impl->workers.emplace_back([this] { impl->loop(); });
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->quit = true;
    }
    impl->cv.notify_all();
    for (auto& w : impl->workers)
        w.join();
}

void ThreadPool::submit(std::function<void()> job)
{
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->jobs.push_back(std::move(job));
    }
    impl->cv.notify_one();
}

void ThreadPool::wait()
{
    std::unique_lock<std::mutex> lock(impl->mutex);
    impl->idleCv.wait(lock, [&] { return impl->jobs.empty() && impl->busy == 0; });
}

std::string executablePath()
{
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size); // ask for the needed length
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0)
        return {};
    buf.resize(std::strlen(buf.c_str()));
    std::error_code ec;
    auto canon = std::filesystem::canonical(buf, ec);
    return ec ? buf : canon.string();
#elif defined(_WIN32)
    char buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return n > 0 && n < MAX_PATH ? std::string(buf, n) : std::string();
#else
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::string() : p.string();
#endif
}

} // namespace snd::platform
