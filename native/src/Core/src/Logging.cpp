// OpenNOW native client — logging implementation.

#include "opennow/core/Logging.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "opennow/core/Paths.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace opennow::core {
namespace {

const char* LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DBG";
        case LogLevel::Info: return "INF";
        case LogLevel::Warn: return "WRN";
        case LogLevel::Error: return "ERR";
    }
    return "???";
}

std::string Timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm parts{};
#if defined(_WIN32)
    localtime_s(&parts, &time);
#else
    localtime_r(&time, &parts);
#endif
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                  parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday, parts.tm_hour,
                  parts.tm_min, parts.tm_sec);
    return std::string(buffer);
}

constexpr std::size_t kMaxLogBytes = 4u * 1024u * 1024u;

}  // namespace

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Configure(std::string logDirectory, LogLevel minimumLevel) {
    std::lock_guard<std::mutex> lock(mutex_);
    directory_ = std::move(logDirectory);
    minimum_ = minimumLevel;
}

void Logger::RegisterSecret(std::string secret) {
    if (secret.size() < 8) return;  // short values would redact too much
    std::lock_guard<std::mutex> lock(mutex_);
    secrets_.push_back(std::move(secret));
}

void Logger::ClearSecrets() {
    std::lock_guard<std::mutex> lock(mutex_);
    secrets_.clear();
}

void Logger::Write(LogLevel level, std::string_view category, std::string_view message) {
    std::string line;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (level < minimum_) return;

        std::string text(message);
        for (const std::string& secret : secrets_) {
            std::size_t position = 0;
            while ((position = text.find(secret, position)) != std::string::npos) {
                text.replace(position, secret.size(), "***");
                position += 3;
            }
        }

        line = Timestamp();
        line += " [";
        line += LevelName(level);
        line += "] [";
        line += category;
        line += "] ";
        line += text;
    }

#ifdef _WIN32
    if (::IsDebuggerPresent()) {
        std::string debugLine = line + "\n";
        ::OutputDebugStringA(debugLine.c_str());
    }
#endif

    if (directory_.empty()) return;
    const std::string path = directory_ + "/opennow.log";
    {
        std::error_code error;
        if (std::filesystem::exists(path, error)) {
            const auto size = std::filesystem::file_size(path, error);
            if (!error && size > kMaxLogBytes) {
                // One generation of rotation is enough for a client log.
                std::filesystem::rename(path, directory_ + "/opennow.old.log", error);
            }
        }
    }
    std::ofstream stream(path, std::ios::app);
    if (stream.is_open()) {
        stream << line << "\n";
    }
}

void LogDebug(std::string_view category, std::string_view message) {
    Logger::Instance().Write(LogLevel::Debug, category, message);
}
void LogInfo(std::string_view category, std::string_view message) {
    Logger::Instance().Write(LogLevel::Info, category, message);
}
void LogWarn(std::string_view category, std::string_view message) {
    Logger::Instance().Write(LogLevel::Warn, category, message);
}
void LogError(std::string_view category, std::string_view message) {
    Logger::Instance().Write(LogLevel::Error, category, message);
}

}  // namespace opennow::core
