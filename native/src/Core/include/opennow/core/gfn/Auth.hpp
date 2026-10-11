// OpenNOW native client — NVIDIA authentication.
//
// Port of `src/server/gfn/auth/*`. Desktop keeps the device-authorization flow
// the web client introduced (no redirect URI, no loopback listener): the user
// approves a short code in a browser, we poll the token endpoint.

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "opennow/core/Http.hpp"
#include "opennow/core/Result.hpp"
#include "opennow/core/Types.hpp"

namespace opennow::core::gfn {

struct LoginProvider {
    std::string idpId;
    std::string code;                 // "NVIDIA", "BPC", ...
    std::string displayName;
    std::string streamingServiceUrl;  // always ends with '/'
    int priority = 0;
};

struct DeviceLoginChallenge {
    std::string deviceCode;
    std::string userCode;                 // shown to the user, e.g. "ABCD-EFGH"
    std::string verificationUri;          // https://www.nvidia.com/...
    std::string verificationUriComplete;  // pre-filled link, used by the QR code
    std::int64_t expiresAt = 0;
    int intervalSeconds = 5;
    bool expired() const;
};

enum class DevicePollStatus { Pending, Success, Error };

struct DevicePollResult {
    DevicePollStatus status = DevicePollStatus::Pending;
    AuthTokens tokens;
    std::string error;              // machine code: authorization_pending, ...
    std::string errorDescription;   // human text for the login screen

    // NVIDIA returns these while the user has not approved the code yet; they
    // are expected, so the caller keeps polling instead of surfacing an error.
    bool retryable() const;
};

struct JwtClaims {
    std::string subject;
    std::string email;
    std::string picture;
    std::string name;
    std::int64_t expiresAt = 0;
};

class AuthService {
public:
    explicit AuthService(HttpClientPtr http) : http_(std::move(http)) {}

    // Provider list from https://pcs.geforcenow.com/v1/serviceUrls. Falls back
    // to the hard-coded NVIDIA provider when discovery fails, as the web server
    // does, so sign-in never hard-fails on a flaky endpoint.
    Result<std::vector<LoginProvider>> FetchProviders(bool forceRefresh = false);
    LoginProvider DefaultProvider() const;

    Result<DeviceLoginChallenge> StartDeviceLogin(const LoginProvider& provider);
    DevicePollResult PollForTokens(const DeviceLoginChallenge& challenge);

    Result<AuthTokens> Refresh(AuthTokens tokens);
    Result<AuthTokens> EnsureClientToken(AuthTokens tokens);
    Result<UserProfile> FetchProfile(const AuthTokens& tokens);

    // ---- helpers exposed for tests and Settings → About --------------------------
    static std::optional<JwtClaims> ParseIdToken(const std::string& token);
    static std::string GravatarUrl(const std::string& email, int size = 80);

private:
    HttpClientPtr http_;
    std::vector<LoginProvider> providers_;
    bool providersLoaded_ = false;
};

}  // namespace opennow::core::gfn
