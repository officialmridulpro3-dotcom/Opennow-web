// OpenNOW native client — logging.
//
// Writes to a rotating file under the app data directory and, when a debugger
// is attached, to the debug output. Deliberately lock-light: the stream loop
// logs at high frequency.

#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace opennow::core {

enum class LogLevel : unsigned char { Debug = 0, Info = 1, Warn = 2, Error = 3 };

class Logger {
public:
    static Logger& Instance();

    // Directory defaults to Paths::LogDirectory(). Call once at startup.
    void Configure(std::string logDirectory, LogLevel minimumLevel = LogLevel::Info);

    void Write(LogLevel level, std::string_view category, std::string_view message);

    // Session-scoped redaction: any occurrence of a registered secret is
    // replaced with "***" before the line reaches disk.
    void RegisterSecret(std::string secret);
    void ClearSecrets();

private:
    Logger() = default;
    std::string directory_;
    LogLevel minimum_ = LogLevel::Info;
    std::vector<std::string> secrets_;
    mutable std::mutex mutex_;
};

void LogDebug(std::string_view category, std::string_view message);
void LogInfo(std::string_view category, std::string_view message);
void LogWarn(std::string_view category, std::string_view message);
void LogError(std::string_view category, std::string_view message);

}  // namespace opennow::core
