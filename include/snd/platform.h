// snd::platform -- OS-specific glue: native file dialogs, standard paths.
// Apps never write per-OS code themselves; it lives here.
#pragma once

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

// Per-OS config directory for this app, created if missing:
// mac ~/Library/Application Support/<app>, win %APPDATA%/<app>, linux ~/.config/<app>
std::string configDir(const std::string& appName);

// Absolute path of the running executable (empty if the OS won't say).
// Lets an app re-run itself, e.g. as a plugin-scan worker process.
std::string executablePath();

} // namespace snd::platform
