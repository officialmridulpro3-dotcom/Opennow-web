#include "onow/Fs.h"

#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/Time.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(_WIN32)
#include <shlobj.h>
#include <windows.h>
#endif

namespace onow {

namespace {

const char* kAppName = "OpenNOW";

std::string home_dir() {
  const char* home = std::getenv("HOME");
  if (home && *home) return home;
#if defined(_WIN32)
  const char* drive = std::getenv("HOMEDRIVE");
  const char* path = std::getenv("HOMEPATH");
  if (drive && path) return std::string(drive) + path;
#endif
  return ".";
}

} // namespace

std::string fs_app_data_dir() {
#if defined(_WIN32)
  char buffer[MAX_PATH] = {0};
  if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, buffer))) {
    return fs_join(buffer, kAppName);
  }
  return fs_join(home_dir(), std::string("AppData/Roaming/") + kAppName);
#elif defined(__APPLE__)
  return fs_join(home_dir(), std::string("Library/Application Support/") + kAppName);
#else
  const char* xdg = std::getenv("XDG_DATA_HOME");
  if (xdg && *xdg) return fs_join(xdg, kAppName);
  return fs_join(home_dir(), std::string(".local/share/") + kAppName);
#endif
}

std::string fs_cache_dir() { return fs_join(fs_app_data_dir(), "cache"); }

std::string fs_join(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  std::string left = a;
  std::string right = b;
  while (left.size() > 1 && (left.back() == '/' || left.back() == '\\')) left.pop_back();
  while (!right.empty() && (right.front() == '/' || right.front() == '\\')) right.erase(right.begin());
  return left + "/" + right;
}

std::string fs_extension(const std::string& path) {
  const std::string::size_type slash = path.find_last_of("/\\");
  const std::string::size_type dot = path.find_last_of('.');
  if (dot == std::string::npos) return std::string();
  if (slash != std::string::npos && dot < slash) return std::string();
  return path.substr(dot + 1);
}

std::string fs_basename(const std::string& path) {
  const std::string::size_type slash = path.find_last_of("/\\");
  if (slash == std::string::npos) return path;
  return path.substr(slash + 1);
}

bool fs_exists(const std::string& path) {
  struct stat st {};
  return ::stat(path.c_str(), &st) == 0;
}

bool fs_is_file(const std::string& path) {
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) return false;
  return S_ISREG(st.st_mode);
}

bool fs_ensure_dir(const std::string& path) {
  if (path.empty()) return false;
  if (fs_exists(path)) return true;
  std::string current;
  std::string normalized = path;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  // Keep the path absolute: a leading '/' (or a Windows drive prefix) must
  // survive the walk, or the whole tree gets created relative to the cwd.
  const bool absolute = !normalized.empty() && normalized[0] == '/';
  std::string drive;
  if (!absolute && normalized.size() > 1 && normalized[1] == ':') {
    drive = normalized.substr(0, 2);
    normalized = normalized.substr(2);
  }
  if (absolute) current = "/";
  else if (!drive.empty()) current = drive;
  std::vector<std::string> parts = str_split(normalized, '/');
  for (const std::string& part : parts) {
    if (part.empty() || part == ".") continue;
    if (part == "..") {
      current = fs_join(current, part);
      continue;
    }
    current = (current.empty() || current == "/") ? current + part : fs_join(current, part);
    if (::mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
      // Might already exist as a symlink or be created concurrently.
      if (!fs_exists(current)) return false;
    }
  }
  return fs_exists(path);
}

bool fs_read_text(const std::string& path, std::string& out) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return false;
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  out = buffer.str();
  return true;
}

bool fs_write_text(const std::string& path, const std::string& content) {
  const std::string::size_type slash = path.find_last_of("/\\");
  if (slash != std::string::npos) fs_ensure_dir(path.substr(0, slash));
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) return false;
  stream.write(content.data(), static_cast<std::streamsize>(content.size()));
  return stream.good();
}

bool fs_read_binary(const std::string& path, std::vector<uint8_t>& out) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return false;
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  if (size < 0) return false;
  out.resize(static_cast<size_t>(size));
  stream.seekg(0, std::ios::beg);
  if (size > 0) stream.read(reinterpret_cast<char*>(out.data()), size);
  return stream.good() || stream.eof();
}

bool fs_write_binary(const std::string& path, const uint8_t* data, size_t len) {
  const std::string::size_type slash = path.find_last_of("/\\");
  if (slash != std::string::npos) fs_ensure_dir(path.substr(0, slash));
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) return false;
  if (len > 0) stream.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
  return stream.good();
}

bool fs_remove(const std::string& path) { return ::unlink(path.c_str()) == 0 || !fs_exists(path); }

bool fs_remove_tree(const std::string& path) {
  DIR* dir = ::opendir(path.c_str());
  if (!dir) return fs_remove(path);
  while (struct dirent* entry = ::readdir(dir)) {
    if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
    const std::string child = fs_join(path, entry->d_name);
    struct stat st {};
    if (::stat(child.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
      fs_remove_tree(child);
    } else {
      fs_remove(child);
    }
  }
  ::closedir(dir);
  return ::rmdir(path.c_str()) == 0 || !fs_exists(path);
}

std::vector<std::string> fs_list_dir(const std::string& path) {
  std::vector<std::string> out;
  DIR* dir = ::opendir(path.c_str());
  if (!dir) return out;
  while (struct dirent* entry = ::readdir(dir)) {
    if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
    out.push_back(entry->d_name);
  }
  ::closedir(dir);
  std::sort(out.begin(), out.end());
  return out;
}

int64_t fs_file_size(const std::string& path) {
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) return -1;
  return static_cast<int64_t>(st.st_size);
}

int64_t fs_mtime_ms(const std::string& path) {
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) return 0;
  return static_cast<int64_t>(st.st_mtime) * 1000;
}

std::string fs_unique_file(const std::string& dir, const std::string& prefix,
                           const std::string& extension) {
  fs_ensure_dir(dir);
  for (int attempt = 0; attempt < 1000; ++attempt) {
    char suffix[32];
    std::snprintf(suffix, sizeof(suffix), "-%lld", static_cast<long long>(now_ms()));
    if (attempt > 0) std::snprintf(suffix, sizeof(suffix), "-%lld-%d",
                                   static_cast<long long>(now_ms()), attempt);
    const std::string name = prefix + suffix + "." + extension;
    const std::string full = fs_join(dir, name);
    if (!fs_exists(full)) return full;
  }
  return fs_join(dir, prefix + "." + extension);
}

void fs_open_external(const std::string& path) {
#if defined(_WIN32)
  ::ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  const std::string command = "open \"" + path + "\" >/dev/null 2>&1";
  std::system(command.c_str());
#else
  const std::string command = "xdg-open \"" + path + "\" >/dev/null 2>&1";
  std::system(command.c_str());
#endif
}

void fs_reveal_in_file_manager(const std::string& path) {
#if defined(_WIN32)
  const std::string command = "explorer /select,\"" + path + "\"";
  std::system(command.c_str());
#elif defined(__APPLE__)
  const std::string command = "open -R \"" + path + "\" >/dev/null 2>&1";
  std::system(command.c_str());
#else
  fs_open_external(fs_join(path, ".."));
#endif
}

} // namespace onow
