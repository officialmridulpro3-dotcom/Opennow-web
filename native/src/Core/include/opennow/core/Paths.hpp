// OpenNOW native client — file system locations.
//
// Windows: %LOCALAPPDATA%\OpenNOW\{settings,cache,logs}
// Everything the app writes lives under that one tree so the installer and the
// uninstaller stay trivial, and so settings can be roamed by copying one folder.

#pragma once

#include <string>

namespace opennow::core {

class Paths {
public:
    // Root of all writable state. Creates the directory on first use.
    static std::string RootDirectory();
    static std::string SettingsFile();
    static std::string CacheDirectory();
    static std::string LogDirectory();
    static std::string ScreenshotDirectory();
    static std::string TokenFile();

    // Overrides used by the unit tests so they never touch the real profile.
    static void SetTestRoot(std::string directory);
    static void ClearTestRoot();

private:
    static std::string EnsureDirectory(const std::string& path);
};

}  // namespace opennow::core
