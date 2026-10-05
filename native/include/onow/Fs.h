// Filesystem + process paths. The web client kept everything in cookies and
// localStorage; the native client owns a real app-data directory:
//
//   Windows: %APPDATA%/OpenNOW
//   macOS:   ~/Library/Application Support/OpenNOW
//   Linux:   $XDG_DATA_HOME/OpenNOW  (fallback ~/.local/share/OpenNOW)
//
// Layout:
//   settings.json     persisted settings
//   session.json      encrypted credential vault (tokens, accounts)
//   catalog.json      cached catalog snapshot
//   playtime.json     local playtime ledger
//   diagnostics.log   rotating log
//   cache/            decoded cover textures
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace onow {

std::string fs_app_data_dir();
std::string fs_cache_dir();
bool fs_ensure_dir(const std::string& path);
bool fs_exists(const std::string& path);
bool fs_is_file(const std::string& path);

bool fs_read_text(const std::string& path, std::string& out);
bool fs_write_text(const std::string& path, const std::string& content);
bool fs_read_binary(const std::string& path, std::vector<uint8_t>& out);
bool fs_write_binary(const std::string& path, const uint8_t* data, size_t len);

bool fs_remove(const std::string& path);
bool fs_remove_tree(const std::string& path);
std::vector<std::string> fs_list_dir(const std::string& path);

int64_t fs_file_size(const std::string& path);
int64_t fs_mtime_ms(const std::string& path);

// Joins with the platform separator, collapsing duplicates.
std::string fs_join(const std::string& a, const std::string& b);
std::string fs_extension(const std::string& path);
std::string fs_basename(const std::string& path);

// Creates a uniquely named file inside `dir` (used by the screenshot/recording
// paths). Returns the full path.
std::string fs_unique_file(const std::string& dir, const std::string& prefix,
                           const std::string& extension);

// Opens a path in the OS shell (store URLs, screenshots folder). Best effort.
void fs_open_external(const std::string& path);
void fs_reveal_in_file_manager(const std::string& path);

} // namespace onow
