// OpenNOW native client — settings and credential persistence.
//
// Settings live in a plain JSON file the user can inspect and back up.
// Credentials are stored separately and encrypted with the OS key store:
// CryptProtectData (DPAPI, per-user + per-machine entropy) on Windows. The web
// client used AES-256-GCM cookies; the desktop equivalent is DPAPI because it
// binds the ciphertext to the Windows account, so copying the file to another
// machine yields nothing.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "opennow/core/Json.hpp"
#include "opennow/core/Result.hpp"
#include "opennow/core/Types.hpp"

namespace opennow::core {

class SettingsStore {
public:
    static SettingsStore& Instance();

    // ---- settings ------------------------------------------------------------
    AppSettings Load();
    Result<void> Save(const AppSettings& settings);

    // ---- credentials -----------------------------------------------------------
    Result<void> SaveCredentials(const AuthTokens& tokens);
    std::optional<AuthTokens> LoadCredentials();
    Result<void> ClearCredentials();

    // ---- playtime ---------------------------------------------------------------
    std::vector<PlaytimeEntry> LoadPlaytime();
    Result<void> SavePlaytime(const std::vector<PlaytimeEntry>& entries);

    // ---- cache -------------------------------------------------------------------
    // Catalog snapshots are cached so cold start shows the previous library
    // instantly; same 24h TTL policy as the web server's games cache.
    std::optional<std::string> ReadCache(const std::string& key);
    Result<void> WriteCache(const std::string& key, const std::string& payload);

private:
    SettingsStore() = default;
};

// ---- JSON <-> model conversions (also used by the unit tests) ---------------------

json::Value ToJson(const StreamSettings& value);
StreamSettings StreamSettingsFromJson(const json::Value& value);
json::Value ToJson(const InterfaceSettings& value);
InterfaceSettings InterfaceSettingsFromJson(const json::Value& value);
json::Value ToJson(const InputSettings& value);
InputSettings InputSettingsFromJson(const json::Value& value);
json::Value ToJson(const AudioSettings& value);
AudioSettings AudioSettingsFromJson(const json::Value& value);
json::Value ToJson(const AppSettings& value);
AppSettings AppSettingsFromJson(const json::Value& value);
json::Value ToJson(const AuthTokens& value);
AuthTokens AuthTokensFromJson(const json::Value& value);
json::Value ToJson(const PlaytimeEntry& value);
PlaytimeEntry PlaytimeEntryFromJson(const json::Value& value);

}  // namespace opennow::core
