// OpenNOW native client — shared domain model.
//
// Mirrors `src/shared/gfn/*.ts` and `src/server/gfn/types.ts` field for field so
// the native client and the web client stay wire-compatible.

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace opennow::core {

// ---- Catalog -----------------------------------------------------------------

enum class StorePlatform {
    Unknown,
    Steam,
    Epic,
    Ubisoft,
    Xbox,
    Gog,
    Battlenet,
    Origin,
    Rockstar,
    Amazon,
    Itch,
};

std::string_view ToString(StorePlatform platform);
StorePlatform ParseStorePlatform(std::string_view value);

struct Game {
    std::string id;                 // GFN application id
    std::string title;
    std::string internalTitle;      // what CloudMatch actually launches
    StorePlatform platform = StorePlatform::Unknown;
    std::string platformLabel;      // "Steam", "Epic Games", ...
    int64_t storeAppId = 0;         // Steam/Epic app id when known
    std::string posterUrl;
    std::string heroUrl;
    std::string logoUrl;
    std::string publisher;
    std::string developer;
    std::vector<std::string> genres;
    std::optional<std::string> releaseDate;
    std::optional<std::string> description;
    bool owned = false;             // present in the user's linked library
    bool cloudSave = false;
    bool controllerSupport = false;
    bool keyboardMouse = true;
    int64_t playerCount = 0;
    double rating = 0.0;
    std::string launchUrl;          // direct-launch deep link when available
    std::vector<std::string> variants;  // e.g. different editions/regions
};

struct CatalogShelf {
    std::string id;
    std::string titleKey;   // i18n key
    std::string title;
    std::vector<Game> games;
};

// ---- Account -----------------------------------------------------------------------

struct Region {
    std::string id;             // zone, e.g. "NA_CENTRAL_01"
    std::string label;
    std::string streamingBaseUrl;
    std::optional<int> latencyMs;   // measured, nullopt = not measured yet
    bool available = true;
};

struct Subscription {
    std::string tierId;
    std::string tierName;       // Free / Priority / Ultimate ...
    int64_t sessionTimeMinutes = 0;
    int64_t maxSessionMinutes = 0;
    int64_t priorityLevel = 0;
    std::optional<std::string> renewalDate;
    bool active = false;
};

struct UserProfile {
    std::string displayName;
    std::string email;
    std::string avatarUrl;
    std::string userId;
};

struct AuthTokens {
    std::string accessToken;
    std::string refreshToken;
    std::string idToken;
    std::string clientToken;
    int64_t expiresAt = 0;             // unix seconds
    int64_t clientTokenExpiresAt = 0;
    bool expired(std::chrono::seconds skew = std::chrono::seconds(60)) const;
    bool needsRefresh(int64_t windowSeconds = 600) const;
    bool empty() const { return accessToken.empty(); }
};

// ---- Streaming ----------------------------------------------------------------------

enum class VideoCodec { H264, Hevc, Av1, Auto };
// Wire values: "8bit_420" | "8bit_444" | "10bit_420" | "10bit_444".
// 10-bit does NOT imply HDR — they are independent toggles on the CloudMatch
// request. Conflating them made the server provision an HDR pipeline that
// downscaled the stream, so keep them separate here too.
enum class ColorQuality {
    EightBit420,
    EightBit444,
    TenBit420,
    TenBit444,
};

std::string_view ToString(ColorQuality quality);
ColorQuality ParseColorQuality(std::string_view value);
enum class StreamTransport { Webrtc, Nvst };
enum class AppLaunchMode { Default, GamepadFriendly };

struct StreamSettings {
    std::string resolution = "1920x1080";
    int fps = 60;
    int maxBitrateMbps = 25;
    VideoCodec codec = VideoCodec::H264;
    ColorQuality colorQuality = ColorQuality::EightBit420;
    std::string keyboardLayout = "en_US";
    std::string gameLanguage = "en_US";
    bool enableL4S = false;
    bool enableCloudGsync = false;
    bool vsync = true;
    bool reflex = true;
    bool adjustForPoorNetwork = true;
    AppLaunchMode appLaunchMode = AppLaunchMode::Default;
    StreamTransport transport = StreamTransport::Webrtc;
    bool microphone = false;
    bool enableHdr = false;
    bool persistInGameSettings = false;
    int sharpening = 0;             // 0..10, applied in the DirectX pipeline
};

struct SessionInfo {
    std::string sessionId;
    std::string appId;
    std::string serverIp;
    std::string zone;
    std::string signalingUrl;
    std::string signalingServer;
    std::string peerId;
    std::string clientId;
    std::string deviceId;
    std::string streamingBaseUrl;
    std::optional<std::string> sdpOffer;
    std::vector<std::string> iceServers;
    int64_t queuePosition = 0;
    int64_t estimatedWaitSeconds = 0;
    bool ready = false;
    bool running = false;
};

enum class StreamStatus {
    Idle,
    Creating,
    NetworkTest,
    Queued,
    Provisioning,
    Connecting,
    Streaming,
    Stopping,
    Error,
};

std::string_view ToString(StreamStatus status);

struct StreamStats {
    int fps = 0;
    int width = 0;
    int height = 0;
    int bitrateKbps = 0;
    int latencyMs = 0;
    int packetLossPercent = 0;
    int decodedFrames = 0;
    int droppedFrames = 0;
    std::string codec;
};

// ---- Interface preferences -----------------------------------------------------------

enum class AccentTheme { Cyan, Lime, Violet, Amber, Rose };
enum class LibraryView { Grid, List };
enum class EffectsProfile { Full, Reduced };

struct InterfaceSettings {
    AccentTheme accent = AccentTheme::Cyan;
    LibraryView libraryView = LibraryView::Grid;
    EffectsProfile effects = EffectsProfile::Full;
    std::string locale = "en";
    bool showStatusBar = true;
    bool showPlaytime = true;
    bool lowEffectsAuto = true;     // auto-downgrade on old GPUs (DX10 class)
    double cardSize = 1.0;
};

struct ShortcutBinding {
    std::string id;                 // "screenshot", "toggleStats", ...
    std::string chord;              // "Ctrl+Shift+S"
};

struct InputSettings {
    bool gamepadEnabled = true;
    bool keyboardLock = true;
    bool pointerLock = false;
    double mouseSensitivity = 1.0;
    bool invertY = false;
    std::vector<ShortcutBinding> shortcuts;
};

struct AudioSettings {
    int volume = 100;
    bool microphone = false;
    int microphoneVolume = 80;
    std::string inputDeviceId;
    bool surround51 = false;
};

struct AppSettings {
    StreamSettings stream;
    InterfaceSettings interface_;
    InputSettings input;
    AudioSettings audio;
    std::string preferredZone;
    bool discordPresence = false;
    bool antiAfk = false;
};

// ---- Playtime -------------------------------------------------------------------------

struct PlaytimeEntry {
    std::string gameId;
    std::string title;
    int64_t totalSeconds = 0;
    int64_t sessions = 0;
    int64_t lastPlayedAt = 0;   // unix seconds
};

}  // namespace opennow::core
