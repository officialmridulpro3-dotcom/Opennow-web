// OpenNOW native client — domain model helpers.

#include "opennow/core/Types.hpp"

#include <algorithm>

namespace opennow::core {

std::string_view ToString(ColorQuality quality) {
    switch (quality) {
        case ColorQuality::EightBit420: return "8bit_420";
        case ColorQuality::EightBit444: return "8bit_444";
        case ColorQuality::TenBit420: return "10bit_420";
        case ColorQuality::TenBit444: return "10bit_444";
    }
    return "8bit_420";
}

ColorQuality ParseColorQuality(std::string_view value) {
    if (value == "8bit_444") return ColorQuality::EightBit444;
    if (value == "10bit_420") return ColorQuality::TenBit420;
    if (value == "10bit_444") return ColorQuality::TenBit444;
    return ColorQuality::EightBit420;
}

std::string_view ToString(StorePlatform platform) {
    switch (platform) {
        case StorePlatform::Unknown: return "Unknown";
        case StorePlatform::Steam: return "Steam";
        case StorePlatform::Epic: return "Epic Games";
        case StorePlatform::Ubisoft: return "Ubisoft Connect";
        case StorePlatform::Xbox: return "Xbox";
        case StorePlatform::Gog: return "GOG";
        case StorePlatform::Battlenet: return "Battle.net";
        case StorePlatform::Origin: return "EA App";
        case StorePlatform::Rockstar: return "Rockstar";
        case StorePlatform::Amazon: return "Amazon";
        case StorePlatform::Itch: return "itch.io";
    }
    return "Unknown";
}

StorePlatform ParseStorePlatform(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lowered.find("steam") != std::string::npos) return StorePlatform::Steam;
    if (lowered.find("epic") != std::string::npos) return StorePlatform::Epic;
    if (lowered.find("ubisoft") != std::string::npos || lowered.find("uplay") != std::string::npos) {
        return StorePlatform::Ubisoft;
    }
    if (lowered.find("xbox") != std::string::npos || lowered.find("microsoft") != std::string::npos) {
        return StorePlatform::Xbox;
    }
    if (lowered.find("gog") != std::string::npos) return StorePlatform::Gog;
    if (lowered.find("battle") != std::string::npos) return StorePlatform::Battlenet;
    if (lowered.find("origin") != std::string::npos || lowered.find("ea") == 0) {
        return StorePlatform::Origin;
    }
    if (lowered.find("rockstar") != std::string::npos) return StorePlatform::Rockstar;
    if (lowered.find("amazon") != std::string::npos) return StorePlatform::Amazon;
    if (lowered.find("itch") != std::string::npos) return StorePlatform::Itch;
    return StorePlatform::Unknown;
}

std::string_view ToString(StreamStatus status) {
    switch (status) {
        case StreamStatus::Idle: return "Idle";
        case StreamStatus::Creating: return "Creating";
        case StreamStatus::NetworkTest: return "NetworkTest";
        case StreamStatus::Queued: return "Queued";
        case StreamStatus::Provisioning: return "Provisioning";
        case StreamStatus::Connecting: return "Connecting";
        case StreamStatus::Streaming: return "Streaming";
        case StreamStatus::Stopping: return "Stopping";
        case StreamStatus::Error: return "Error";
    }
    return "Unknown";
}

namespace {
std::int64_t NowSeconds() {
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}
}  // namespace

bool AuthTokens::expired(std::chrono::seconds skew) const {
    if (expiresAt <= 0) return false;  // NVIDIA occasionally omits expiry
    return NowSeconds() + skew.count() >= expiresAt;
}

bool AuthTokens::needsRefresh(std::int64_t windowSeconds) const {
    if (expiresAt <= 0) return false;
    return NowSeconds() + windowSeconds >= expiresAt;
}

}  // namespace opennow::core
