// OpenNOW native client — settings and credential persistence.

#include "opennow/core/Settings.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "opennow/core/Hash.hpp"
#include "opennow/core/Logging.hpp"
#include "opennow/core/Paths.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dpapi.h>
#pragma comment(lib, "crypt32.lib")
#endif

namespace opennow::core {
namespace {

constexpr std::string_view kSettingsVersionKey = "version";
constexpr int kSettingsVersion = 1;

std::optional<std::string> ReadFile(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) return std::nullopt;
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

Result<void> WriteFileAtomic(const std::string& path, const std::string& contents) {
    const std::string temporary = path + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) return Result<void>::Fail("cannot open " + temporary);
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return Result<void>::Fail("cannot replace " + path + ": " + error.message());
    }
    return Result<void>::Ok();
}

#ifdef _WIN32

std::optional<std::string> Protect(const std::string& plaintext) {
    DATA_BLOB input{};
    input.cbData = static_cast<DWORD>(plaintext.size());
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));
    DATA_BLOB output{};
    if (!::CryptProtectData(&input, L"OpenNOW account tokens", nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return std::nullopt;
    }
    std::string result(reinterpret_cast<const char*>(output.pbData), output.cbData);
    ::LocalFree(output.pbData);
    return result;
}

std::optional<std::string> Unprotect(const std::string& ciphertext) {
    DATA_BLOB input{};
    input.cbData = static_cast<DWORD>(ciphertext.size());
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(ciphertext.data()));
    DATA_BLOB output{};
    if (!::CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return std::nullopt;
    }
    std::string result(reinterpret_cast<const char*>(output.pbData), output.cbData);
    ::LocalFree(output.pbData);
    return result;
}

#endif

std::string CachePath(const std::string& key) {
    // Key is hashed so arbitrary cache names can never escape the cache dir.
    return Paths::CacheDirectory() + "/" + Sha256(key).Hex() + ".json";
}


// Safe field accessors: NVIDIA payloads and hand-edited settings files both
// omit keys, so every read falls back to the current default instead of
// dereferencing a missing member.
std::string GetString(const json::Value& value, std::string_view key, std::string fallback) {
    if (const json::Value* found = value.Find(key)) return found->AsString(fallback);
    return fallback;
}

std::int64_t GetInt(const json::Value& value, std::string_view key, std::int64_t fallback) {
    if (const json::Value* found = value.Find(key)) return found->AsInt(fallback);
    return fallback;
}

bool GetBool(const json::Value& value, std::string_view key, bool fallback) {
    if (const json::Value* found = value.Find(key)) return found->AsBool(fallback);
    return fallback;
}

double GetDouble(const json::Value& value, std::string_view key, double fallback) {
    if (const json::Value* found = value.Find(key)) return found->AsDouble(fallback);
    return fallback;
}

}  // namespace

SettingsStore& SettingsStore::Instance() {
    static SettingsStore instance;
    return instance;
}

// ---- conversions ---------------------------------------------------------------------

json::Value ToJson(const StreamSettings& value) {
    json::Value out = json::ObjectValue();
    out.Set("resolution", value.resolution);
    out.Set("fps", value.fps);
    out.Set("maxBitrateMbps", value.maxBitrateMbps);
    out.Set("codec", static_cast<int>(value.codec));
    out.Set("colorQuality", std::string(ToString(value.colorQuality)));
    out.Set("keyboardLayout", value.keyboardLayout);
    out.Set("gameLanguage", value.gameLanguage);
    out.Set("enableL4S", value.enableL4S);
    out.Set("enableCloudGsync", value.enableCloudGsync);
    out.Set("vsync", value.vsync);
    out.Set("reflex", value.reflex);
    out.Set("adjustForPoorNetwork", value.adjustForPoorNetwork);
    out.Set("appLaunchMode", static_cast<int>(value.appLaunchMode));
    out.Set("transport", static_cast<int>(value.transport));
    out.Set("microphone", value.microphone);
    out.Set("enableHdr", value.enableHdr);
    out.Set("persistInGameSettings", value.persistInGameSettings);
    out.Set("sharpening", value.sharpening);
    return out;
}

StreamSettings StreamSettingsFromJson(const json::Value& value) {
    StreamSettings out;
    out.resolution = GetString(value, "resolution", out.resolution);
    out.fps = static_cast<int>(GetInt(value, "fps", out.fps));
    out.maxBitrateMbps = static_cast<int>(GetInt(value, "maxBitrateMbps", out.maxBitrateMbps));
    out.codec = static_cast<VideoCodec>(GetInt(value, "codec", 0));
    out.colorQuality = ParseColorQuality(GetString(value, "colorQuality", "8bit_420"));
    out.keyboardLayout = GetString(value, "keyboardLayout", out.keyboardLayout);
    out.gameLanguage = GetString(value, "gameLanguage", out.gameLanguage);
    out.enableL4S = GetBool(value, "enableL4S", out.enableL4S);
    out.enableCloudGsync = GetBool(value, "enableCloudGsync", out.enableCloudGsync);
    out.vsync = GetBool(value, "vsync", out.vsync);
    out.reflex = GetBool(value, "reflex", out.reflex);
    out.adjustForPoorNetwork =
        GetBool(value, "adjustForPoorNetwork", out.adjustForPoorNetwork);
    out.appLaunchMode = static_cast<AppLaunchMode>(GetInt(value, "appLaunchMode", 0));
    out.transport = static_cast<StreamTransport>(GetInt(value, "transport", 0));
    out.microphone = GetBool(value, "microphone", out.microphone);
    out.enableHdr = GetBool(value, "enableHdr", out.enableHdr);
    out.persistInGameSettings =
        GetBool(value, "persistInGameSettings", out.persistInGameSettings);
    out.sharpening = static_cast<int>(GetInt(value, "sharpening", out.sharpening));
    return out;
}

json::Value ToJson(const InterfaceSettings& value) {
    json::Value out = json::ObjectValue();
    out.Set("accent", static_cast<int>(value.accent));
    out.Set("libraryView", static_cast<int>(value.libraryView));
    out.Set("effects", static_cast<int>(value.effects));
    out.Set("locale", value.locale);
    out.Set("showStatusBar", value.showStatusBar);
    out.Set("showPlaytime", value.showPlaytime);
    out.Set("lowEffectsAuto", value.lowEffectsAuto);
    out.Set("cardSize", value.cardSize);
    return out;
}

InterfaceSettings InterfaceSettingsFromJson(const json::Value& value) {
    InterfaceSettings out;
    out.accent = static_cast<AccentTheme>(GetInt(value, "accent", 0));
    out.libraryView = static_cast<LibraryView>(GetInt(value, "libraryView", 0));
    out.effects = static_cast<EffectsProfile>(GetInt(value, "effects", 0));
    out.locale = GetString(value, "locale", out.locale);
    out.showStatusBar = GetBool(value, "showStatusBar", out.showStatusBar);
    out.showPlaytime = GetBool(value, "showPlaytime", out.showPlaytime);
    out.lowEffectsAuto = GetBool(value, "lowEffectsAuto", out.lowEffectsAuto);
    out.cardSize = GetDouble(value, "cardSize", out.cardSize);
    return out;
}

json::Value ToJson(const InputSettings& value) {
    json::Value out = json::ObjectValue();
    out.Set("gamepadEnabled", value.gamepadEnabled);
    out.Set("keyboardLock", value.keyboardLock);
    out.Set("pointerLock", value.pointerLock);
    out.Set("mouseSensitivity", value.mouseSensitivity);
    out.Set("invertY", value.invertY);
    json::Value shortcuts = json::ArrayValue();
    for (const ShortcutBinding& binding : value.shortcuts) {
        json::Value entry = json::ObjectValue();
        entry.Set("id", binding.id);
        entry.Set("chord", binding.chord);
        shortcuts.Push(std::move(entry));
    }
    out.Set("shortcuts", std::move(shortcuts));
    return out;
}

InputSettings InputSettingsFromJson(const json::Value& value) {
    InputSettings out;
    out.gamepadEnabled = GetBool(value, "gamepadEnabled", out.gamepadEnabled);
    out.keyboardLock = GetBool(value, "keyboardLock", out.keyboardLock);
    out.pointerLock = GetBool(value, "pointerLock", out.pointerLock);
    out.mouseSensitivity = GetDouble(value, "mouseSensitivity", out.mouseSensitivity);
    out.invertY = GetBool(value, "invertY", out.invertY);
    if (const json::Value* shortcuts = value.Find("shortcuts")) {
        for (const json::Value& entry : shortcuts->Items()) {
            ShortcutBinding binding;
            binding.id = GetString(entry, "id", "");
            binding.chord = GetString(entry, "chord", "");
            out.shortcuts.push_back(std::move(binding));
        }
    }
    return out;
}

json::Value ToJson(const AudioSettings& value) {
    json::Value out = json::ObjectValue();
    out.Set("volume", value.volume);
    out.Set("microphone", value.microphone);
    out.Set("microphoneVolume", value.microphoneVolume);
    out.Set("inputDeviceId", value.inputDeviceId);
    out.Set("surround51", value.surround51);
    return out;
}

AudioSettings AudioSettingsFromJson(const json::Value& value) {
    AudioSettings out;
    out.volume = static_cast<int>(GetInt(value, "volume", out.volume));
    out.microphone = GetBool(value, "microphone", out.microphone);
    out.microphoneVolume =
        static_cast<int>(GetInt(value, "microphoneVolume", out.microphoneVolume));
    out.inputDeviceId = GetString(value, "inputDeviceId", out.inputDeviceId);
    out.surround51 = GetBool(value, "surround51", out.surround51);
    return out;
}

json::Value ToJson(const AppSettings& value) {
    json::Value out = json::ObjectValue();
    out.Set(std::string(kSettingsVersionKey), kSettingsVersion);
    out.Set("stream", ToJson(value.stream));
    out.Set("interface", ToJson(value.interface_));
    out.Set("input", ToJson(value.input));
    out.Set("audio", ToJson(value.audio));
    out.Set("preferredZone", value.preferredZone);
    out.Set("discordPresence", value.discordPresence);
    out.Set("antiAfk", value.antiAfk);
    return out;
}

AppSettings AppSettingsFromJson(const json::Value& value) {
    AppSettings out;
    if (const json::Value* stream = value.Find("stream")) out.stream = StreamSettingsFromJson(*stream);
    if (const json::Value* ui = value.Find("interface")) {
        out.interface_ = InterfaceSettingsFromJson(*ui);
    }
    if (const json::Value* input = value.Find("input")) out.input = InputSettingsFromJson(*input);
    if (const json::Value* audio = value.Find("audio")) out.audio = AudioSettingsFromJson(*audio);
    out.preferredZone = GetString(value, "preferredZone", out.preferredZone);
    out.discordPresence = GetBool(value, "discordPresence", out.discordPresence);
    out.antiAfk = GetBool(value, "antiAfk", out.antiAfk);
    return out;
}

json::Value ToJson(const AuthTokens& value) {
    json::Value out = json::ObjectValue();
    out.Set("accessToken", value.accessToken);
    out.Set("refreshToken", value.refreshToken);
    out.Set("idToken", value.idToken);
    out.Set("clientToken", value.clientToken);
    out.Set("expiresAt", value.expiresAt);
    out.Set("clientTokenExpiresAt", value.clientTokenExpiresAt);
    return out;
}

AuthTokens AuthTokensFromJson(const json::Value& value) {
    AuthTokens out;
    out.accessToken = GetString(value, "accessToken", "");
    out.refreshToken = GetString(value, "refreshToken", "");
    out.idToken = GetString(value, "idToken", "");
    out.clientToken = GetString(value, "clientToken", "");
    out.expiresAt = GetInt(value, "expiresAt", 0);
    out.clientTokenExpiresAt = GetInt(value, "clientTokenExpiresAt", 0);
    return out;
}

json::Value ToJson(const PlaytimeEntry& value) {
    json::Value out = json::ObjectValue();
    out.Set("gameId", value.gameId);
    out.Set("title", value.title);
    out.Set("totalSeconds", value.totalSeconds);
    out.Set("sessions", value.sessions);
    out.Set("lastPlayedAt", value.lastPlayedAt);
    return out;
}

PlaytimeEntry PlaytimeEntryFromJson(const json::Value& value) {
    PlaytimeEntry out;
    out.gameId = GetString(value, "gameId", "");
    out.title = GetString(value, "title", "");
    out.totalSeconds = GetInt(value, "totalSeconds", 0);
    out.sessions = GetInt(value, "sessions", 0);
    out.lastPlayedAt = GetInt(value, "lastPlayedAt", 0);
    return out;
}

// ---- settings --------------------------------------------------------------------------

AppSettings SettingsStore::Load() {
    const std::optional<std::string> text = ReadFile(Paths::SettingsFile());
    if (!text) return AppSettings{};
    const json::ParseResult parsed = json::Value::Parse(*text);
    if (!parsed.ok) {
        LogWarn("Settings", "settings.json is corrupt, starting from defaults: " +
                                parsed.message);
        return AppSettings{};
    }
    return AppSettingsFromJson(parsed.value);
}

Result<void> SettingsStore::Save(const AppSettings& settings) {
    return WriteFileAtomic(Paths::SettingsFile(), ToJson(settings).Dump(true));
}

// ---- credentials ------------------------------------------------------------------------

Result<void> SettingsStore::SaveCredentials(const AuthTokens& tokens) {
    const std::string plaintext = ToJson(tokens).Dump();
#ifdef _WIN32
    const std::optional<std::string> protected_ = Protect(plaintext);
    if (!protected_) {
        return Result<void>::Fail("DPAPI protection failed: " + std::to_string(::GetLastError()));
    }
    return WriteFileAtomic(Paths::TokenFile(), *protected_);
#else
    LogWarn("Settings", "Credential encryption unavailable off Windows; storing unprotected.");
    return WriteFileAtomic(Paths::TokenFile(), plaintext);
#endif
}

std::optional<AuthTokens> SettingsStore::LoadCredentials() {
    const std::optional<std::string> stored = ReadFile(Paths::TokenFile());
    if (!stored) return std::nullopt;
#ifdef _WIN32
    const std::optional<std::string> plain = Unprotect(*stored);
    if (!plain) {
        LogWarn("Settings", "Stored credentials could not be decrypted (different Windows "
                            "account or machine).");
        return std::nullopt;
    }
#else
    const std::optional<std::string> plain = stored;
#endif
    const json::ParseResult parsed = json::Value::Parse(*plain);
    if (!parsed.ok || !parsed.value.isObject()) return std::nullopt;
    const AuthTokens tokens = AuthTokensFromJson(parsed.value);
    if (tokens.accessToken.empty()) return std::nullopt;
    return tokens;
}

Result<void> SettingsStore::ClearCredentials() {
    std::error_code error;
    std::filesystem::remove(Paths::TokenFile(), error);
    if (error && error != std::errc::no_such_file_or_directory) {
        return Result<void>::Fail(error.message());
    }
    return Result<void>::Ok();
}

// ---- playtime ----------------------------------------------------------------------------

std::vector<PlaytimeEntry> SettingsStore::LoadPlaytime() {
    const std::optional<std::string> text = ReadFile(Paths::RootDirectory() + "/playtime.json");
    if (!text) return {};
    const json::ParseResult parsed = json::Value::Parse(*text);
    if (!parsed.ok || !parsed.value.isArray()) return {};
    std::vector<PlaytimeEntry> out;
    for (const json::Value& entry : parsed.value.Items()) {
        out.push_back(PlaytimeEntryFromJson(entry));
    }
    return out;
}

Result<void> SettingsStore::SavePlaytime(const std::vector<PlaytimeEntry>& entries) {
    json::Value array = json::ArrayValue();
    for (const PlaytimeEntry& entry : entries) array.Push(ToJson(entry));
    return WriteFileAtomic(Paths::RootDirectory() + "/playtime.json", array.Dump(true));
}

// ---- cache --------------------------------------------------------------------------------

std::optional<std::string> SettingsStore::ReadCache(const std::string& key) {
    return ReadFile(CachePath(key));
}

Result<void> SettingsStore::WriteCache(const std::string& key, const std::string& payload) {
    return WriteFileAtomic(CachePath(key), payload);
}

}  // namespace opennow::core
