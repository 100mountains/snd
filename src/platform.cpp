// snd::platform over nativefiledialog-extended + std::filesystem.

#include "snd/platform.h"

#include <nfd.h>

#include <cstdlib>
#include <filesystem>

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

} // namespace snd::platform
