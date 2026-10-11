// OpenNOW native client — file system locations.

#include "opennow/core/Paths.hpp"

#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>          // EXTERN_C and the base types knownfolders.h needs
#include <knownfolders.h>
#include <shlobj.h>
#endif

namespace opennow::core {
namespace {

std::string& TestRoot() {
    static std::string root;
    return root;
}

std::string DefaultRoot() {
#ifdef _WIN32
    PWSTR path = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)) &&
        path != nullptr) {
        std::wstring wide(path);
        ::CoTaskMemFree(path);
        const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                               static_cast<int>(wide.size()), nullptr, 0, nullptr,
                                               nullptr);
        std::string out(static_cast<std::size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(),
                            needed, nullptr, nullptr);
        return out + "\\OpenNOW";
    }
    if (path != nullptr) ::CoTaskMemFree(path);
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData != nullptr) return std::string(localAppData) + "\\OpenNOW";
    return "OpenNOW";
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr) return std::string(xdg) + "/opennow";
    const char* home = std::getenv("HOME");
    if (home != nullptr) return std::string(home) + "/.local/share/opennow";
    return "./opennow-profile";
#endif
}

}  // namespace

void Paths::SetTestRoot(std::string directory) { TestRoot() = std::move(directory); }
void Paths::ClearTestRoot() { TestRoot().clear(); }

std::string Paths::EnsureDirectory(const std::string& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return path;
}

std::string Paths::RootDirectory() {
    return EnsureDirectory(TestRoot().empty() ? DefaultRoot() : TestRoot());
}

std::string Paths::SettingsFile() { return RootDirectory() + "/settings.json"; }

std::string Paths::CacheDirectory() {
    return EnsureDirectory(RootDirectory() + "/cache");
}

std::string Paths::LogDirectory() { return EnsureDirectory(RootDirectory() + "/logs"); }

std::string Paths::ScreenshotDirectory() {
    return EnsureDirectory(RootDirectory() + "/screenshots");
}

std::string Paths::TokenFile() { return RootDirectory() + "/account.token"; }

}  // namespace opennow::core
