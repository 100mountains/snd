// snd::plugin::HostManager -- format registry, scanning with crash-loop
// protection: either the "dead man's pedal" file (JUCE audit) or, better,
// out-of-process enumeration per plugin.

#include "snd/plugin_host.h"
#include "plugin_host/editor_window.h"
#include "plugin_host/vst3/vst3_format.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#if defined(__APPLE__)
namespace snd::plugin { std::unique_ptr<Format> createAUFormat(); }
#endif

#if defined(__APPLE__)
namespace snd::plugin::editorwin {
void pump() {} // Cocoa pumps itself
} // namespace snd::plugin::editorwin
#endif
// _WIN32 + Linux provide the full editorwin backend in their own TUs
// (editor_window_win.cpp / editor_window_x11.cpp).

namespace snd::plugin {

namespace fs = std::filesystem;

// -- description (de)serialization for the worker's out-file ----------------
// One description per line, tab-separated fields, tab/newline/backslash
// escaped. Deliberately dumb: both ends are this same library.

namespace {

std::string escapeField(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string unescapeField(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            out += n == 't' ? '\t' : n == 'n' ? '\n' : n;
        } else
            out += s[i];
    }
    return out;
}

void writeDescriptions(std::ostream& out, const std::vector<Description>& descs)
{
    for (auto& d : descs)
        out << escapeField(d.format) << '\t' << escapeField(d.identifier) << '\t'
            << escapeField(d.path) << '\t' << escapeField(d.name) << '\t'
            << escapeField(d.vendor) << '\t' << escapeField(d.version) << '\t'
            << escapeField(d.category) << '\n';
}

std::vector<Description> readDescriptions(std::istream& in)
{
    std::vector<Description> descs;
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> fields;
        std::string cur;
        for (size_t i = 0; i <= line.size(); ++i) {
            if (i == line.size() || line[i] == '\t') {
                fields.push_back(unescapeField(cur));
                cur.clear();
            } else
                cur += line[i];
        }
        if (fields.size() != 7)
            continue;
        descs.push_back({fields[0], fields[1], fields[2], fields[3], fields[4],
                         fields[5], fields[6]});
    }
    return descs;
}

// Launch a child process and wait for it, with a hard timeout so a hanging
// plugin can't stall the scan. Returns false only if it couldn't launch.
bool runChildWithTimeout(const std::vector<std::string>& args, uint32_t timeoutMs)
{
#if defined(_WIN32)
    std::string cmd;
    for (auto& a : args)
        cmd += "\"" + a + "\" ";
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &si, &pi))
        return false;
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_TIMEOUT)
        TerminateProcess(pi.hProcess, 1); // hung -- kill it
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
#else
    std::vector<char*> argv;
    for (auto& a : args)
        argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = -1;
    if (posix_spawn(&pid, args[0].c_str(), nullptr, nullptr, argv.data(), environ) != 0)
        return false;

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    int status = 0;
    for (;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
            return true; // exited -- cleanly or by crashing; the out-file tells
        if (r < 0)
            return true; // can't observe it any more; carry on
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL); // hung -- kill it; this plugin yields nothing
            waitpid(pid, &status, 0);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#endif
}

} // namespace

int runScanWorker(const std::string& formatName, const std::string& identifier,
                  const std::string& outFile)
{
    HostManager manager;
    manager.addDefaultFormats();

    Format* format = nullptr;
    for (auto& f : manager.formats())
        if (formatName == f->name())
            format = f.get();
    if (!format)
        return 2;

    // May crash or hang right here -- isolated to this child, which is the point.
    auto found = format->scan(identifier);

    std::ofstream out(outFile, std::ios::trunc);
    if (!out)
        return 3;
    writeDescriptions(out, found); // empty file if nothing was found
    return 0;
}

std::vector<Description> scanViaWorker(const std::string& workerExe,
                                       const std::string& workerFlag,
                                       const std::string& formatName,
                                       const std::string& fileOrIdentifier,
                                       uint32_t timeoutMs)
{
    std::error_code ec;
    auto tmpDir = fs::temp_directory_path(ec);
    if (ec)
        return {};
    static std::atomic<uint32_t> counter{0};
    std::ostringstream name;
    name << "snd-scan-"
#if defined(_WIN32)
         << GetCurrentProcessId()
#else
         << getpid()
#endif
         << "-" << counter.fetch_add(1) << ".txt";
    auto outFile = tmpDir / name.str();

    std::vector<Description> found;
    if (runChildWithTimeout({workerExe, workerFlag, formatName, fileOrIdentifier,
                             outFile.string()},
                            timeoutMs)) {
        std::ifstream in(outFile);
        if (in)
            found = readDescriptions(in);
    }
    fs::remove(outFile, ec);
    return found;
}

struct HostManager::Impl {
    std::vector<std::unique_ptr<Format>> formats;
    std::string pedalFile;
    std::string workerExe, workerFlag;
    uint32_t workerTimeoutMs = 30000;

    std::set<std::string> readPedal() const
    {
        std::set<std::string> entries;
        if (pedalFile.empty())
            return entries;
        std::ifstream in(pedalFile);
        std::string line;
        while (std::getline(in, line))
            if (!line.empty())
                entries.insert(line);
        return entries;
    }

    void writePedal(const std::set<std::string>& entries) const
    {
        if (pedalFile.empty())
            return;
        std::ofstream out(pedalFile, std::ios::trunc);
        for (auto& e : entries)
            out << e << "\n";
    }
};

HostManager::HostManager() : impl(new Impl) {}
HostManager::~HostManager() = default;

void HostManager::addFormat(std::unique_ptr<Format> format)
{
    impl->formats.push_back(std::move(format));
}

void HostManager::addDefaultFormats()
{
    addFormat(createVST3Format());
#if defined(__APPLE__)
    addFormat(createAUFormat());
#endif
}

const std::vector<std::unique_ptr<Format>>& HostManager::formats() const
{
    return impl->formats;
}

void HostManager::setDeadMansPedalFile(const std::string& path)
{
    impl->pedalFile = path;
}

void HostManager::setScanWorker(const std::string& exePath, const std::string& flag)
{
    impl->workerExe = exePath;
    impl->workerFlag = flag;
}

void HostManager::setScanWorkerTimeout(uint32_t milliseconds)
{
    impl->workerTimeoutMs = milliseconds;
}

HostManager::ScanResult HostManager::scanAll(const std::vector<std::string>& extraPaths)
{
    ScanResult result;
    auto blacklist = impl->readPedal();
    for (auto& entry : blacklist)
        result.blacklisted.push_back(entry);

    for (auto& format : impl->formats) {
        auto searchPaths = format->defaultSearchPaths();
        searchPaths.insert(searchPaths.end(), extraPaths.begin(), extraPaths.end());

        if (searchPaths.empty()) {
            // Registry-based format (AU): one enumerate-everything call.
            auto found = format->scan("");
            result.plugins.insert(result.plugins.end(), found.begin(), found.end());
            continue;
        }

        // Path-based format: walk the directories for candidate files.
        std::vector<std::string> candidates;
        for (auto& dir : searchPaths) {
            std::error_code ec;
            if (!fs::exists(dir, ec))
                continue;
            for (auto it = fs::recursive_directory_iterator(dir, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec)
                    break;
                auto p = it->path().string();
                if (format->fileMightBePlugin(p)) {
                    candidates.push_back(p);
                    // A .vst3 bundle is a directory: don't descend into it.
                    if (it->is_directory(ec))
                        it.disable_recursion_pending();
                }
            }
        }

        for (auto& file : candidates) {
            std::vector<Description> found;
            if (!impl->workerExe.empty()) {
                // Out-of-process: a crash or hang dies in a throwaway child.
                // No pedal, no permanent blacklist -- a plugin that fails
                // today just yields nothing and gets retried next scan.
                found = scanViaWorker(impl->workerExe, impl->workerFlag,
                                      format->name(), file, impl->workerTimeoutMs);
            } else {
                if (blacklist.count(file))
                    continue; // crashed a previous scan; skip until pedal is cleared

                // Mark before loading; clear only after surviving the scan.
                auto pedal = impl->readPedal();
                pedal.insert(file);
                impl->writePedal(pedal);

                found = format->scan(file);

                pedal = impl->readPedal();
                pedal.erase(file);
                impl->writePedal(pedal);
            }

            if (found.empty())
                result.failed.push_back(file);
            else
                result.plugins.insert(result.plugins.end(), found.begin(), found.end());
        }
    }
    return result;
}

std::unique_ptr<Instance> HostManager::create(const Description& desc)
{
    for (auto& format : impl->formats)
        if (desc.format == format->name())
            return format->create(desc);
    return nullptr;
}

} // namespace snd::plugin
