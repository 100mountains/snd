// snd::plugin::HostManager -- format registry, scanning with crash-loop
// protection (the "dead man's pedal" pattern from the JUCE audit).

#include "snd/plugin_host.h"
#include "plugin_host/vst3/vst3_format.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

#if defined(__APPLE__)
namespace snd::plugin { std::unique_ptr<Format> createAUFormat(); }
#endif

namespace snd::plugin {

namespace fs = std::filesystem;

struct HostManager::Impl {
    std::vector<std::unique_ptr<Format>> formats;
    std::string pedalFile;

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
            if (blacklist.count(file))
                continue; // crashed a previous scan; skip until pedal is cleared

            // Mark before loading; clear only after surviving the scan.
            auto pedal = impl->readPedal();
            pedal.insert(file);
            impl->writePedal(pedal);

            auto found = format->scan(file);

            pedal = impl->readPedal();
            pedal.erase(file);
            impl->writePedal(pedal);

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
