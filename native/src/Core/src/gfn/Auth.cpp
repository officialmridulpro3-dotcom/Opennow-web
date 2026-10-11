// OpenNOW native client — NVIDIA authentication implementation.

#include "opennow/core/gfn/Auth.hpp"

#include <algorithm>
#include <cctype>

#include "opennow/core/Hash.hpp"
#include "opennow/core/Json.hpp"
#include "opennow/core/Logging.hpp"
#include "opennow/core/gfn/Constants.hpp"
#include "opennow/core/gfn/DeviceId.hpp"

namespace opennow::core::gfn {
namespace {

std::int64_t NowSeconds() {
    return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());
}

std::string Trim(std::string_view value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return std::string(value.substr(begin, end - begin));
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// Safe accessors: NVIDIA omits optional fields regularly (and error payloads
// only carry `error`), so no read may dereference an absent member.
std::string GetString(const json::Value& value, std::string_view key, std::string fallback = {}) {
    if (const json::Value* found = value.Find(key)) return found->AsString(fallback);
    return fallback;
}

std::int64_t GetInt(const json::Value& value, std::string_view key, std::int64_t fallback = 0) {
    if (const json::Value* found = value.Find(key)) return found->AsInt(fallback);
    return fallback;
}

// The web server picks the Steam Deck client id for device authorization: it is
// the id NVIDIA's own Steam Deck client uses for the code flow and the only one
// that consistently returns a verification_uri_complete.
constexpr std::string_view kDeviceFlowClientId = kSteamDeckClientId;

constexpr int kDefaultExpiresIn = 600;

}  // namespace

bool DeviceLoginChallenge::expired() const { return NowSeconds() >= expiresAt; }

bool DevicePollResult::retryable() const {
    // Pending covers authorization_pending and slow_down: the code is still
    // valid and the user has simply not approved it yet.
    return status == DevicePollStatus::Pending;
}

// ---- providers --------------------------------------------------------------------------

Result<std::vector<LoginProvider>> AuthService::FetchProviders(bool forceRefresh) {
    if (providersLoaded_ && !forceRefresh && !providers_.empty()) {
        return Result<std::vector<LoginProvider>>::Ok(providers_);
    }

    HttpRequest request;
    request.method = "GET";
    request.url = std::string(kServiceUrlsEndpoint);
    request.headers.push_back(HttpHeader{"Accept", "application/json"});
    request.headers.push_back(HttpHeader{"User-Agent", std::string(kUserAgent)});

    const HttpResponse response = http_->Send(request);
    if (!response.ok()) {
        LogWarn("Auth", "Provider discovery failed, using NVIDIA default.");
        std::vector<LoginProvider> fallback{DefaultProvider()};
        providers_ = fallback;
        providersLoaded_ = true;
        return Result<std::vector<LoginProvider>>::Ok(fallback);
    }

    const json::ParseResult parsed = json::Value::Parse(response.body);
    if (!parsed.ok) {
        LogWarn("Auth", "Provider payload was not JSON, using NVIDIA default.");
        std::vector<LoginProvider> fallback{DefaultProvider()};
        providers_ = fallback;
        providersLoaded_ = true;
        return Result<std::vector<LoginProvider>>::Ok(fallback);
    }

    std::vector<LoginProvider> providers;
    if (const json::Value* serviceInfo = parsed.value.Find("gfnServiceInfo")) {
        if (const json::Value* endpoints = serviceInfo->Find("gfnServiceEndpoints")) {
            for (const json::Value& entry : endpoints->Items()) {
                LoginProvider provider;
                provider.idpId = GetString(entry, "idpId");
                provider.code = GetString(entry, "loginProviderCode");
                provider.displayName =
                    provider.code == "BPC" ? "bro.game"
                                           : GetString(entry, "loginProviderDisplayName");
                provider.streamingServiceUrl = GetString(entry, "streamingServiceUrl");
                provider.priority = static_cast<int>(GetInt(entry, "loginProviderPriority", 0));
                if (provider.idpId.empty() || provider.streamingServiceUrl.empty()) continue;
                if (!provider.streamingServiceUrl.empty() &&
                    provider.streamingServiceUrl.back() != '/') {
                    provider.streamingServiceUrl.push_back('/');
                }
                providers.push_back(std::move(provider));
            }
        }
    }

    if (providers.empty()) providers.push_back(DefaultProvider());
    std::stable_sort(providers.begin(), providers.end(),
                     [](const LoginProvider& a, const LoginProvider& b) {
                         return a.priority < b.priority;
                     });

    providers_ = providers;
    providersLoaded_ = true;
    return Result<std::vector<LoginProvider>>::Ok(providers);
}

LoginProvider AuthService::DefaultProvider() const {
    LoginProvider provider;
    provider.idpId = std::string(kDefaultIdpId);
    provider.code = "NVIDIA";
    provider.displayName = "NVIDIA";
    provider.streamingServiceUrl = std::string(kDefaultStreamingServiceUrl);
    provider.priority = 0;
    return provider;
}

// ---- device login --------------------------------------------------------------------------

Result<DeviceLoginChallenge> AuthService::StartDeviceLogin(const LoginProvider& provider) {
    const std::string deviceId = DeviceId::Get();

    const std::string body = FormEncode({
        {"client_id", std::string(kDeviceFlowClientId)},
        {"scope", std::string(kScopes)},
        {"device_id", deviceId},
        {"display_name", "OpenNOW"},
        {"idp_id", provider.idpId},
    });

    HttpRequest request;
    request.method = "POST";
    request.url = std::string(kDeviceAuthorizeEndpoint);
    request.body = body;
    request.headers = BuildAuthHeaders(kDeviceFlowClientId,
                                       "application/x-www-form-urlencoded; charset=UTF-8",
                                       deviceId);
    request.headers.push_back(HttpHeader{"nv-client-streamer", "WEBRTC"});
    request.headers.push_back(HttpHeader{"nv-client-type", "BROWSER"});
    request.headers.push_back(HttpHeader{"nv-client-platform-name", "browser"});
    request.headers.push_back(HttpHeader{"nv-browser-type", "CHROME"});
    request.headers.push_back(HttpHeader{"nv-device-os", "STEAMOS"});
    request.headers.push_back(HttpHeader{"nv-device-type", "CONSOLE"});
    request.headers.push_back(HttpHeader{"nv-device-model", "STEAMDECK"});
    request.headers.push_back(HttpHeader{"nv-device-make", "VALVE"});

    const HttpResponse response = http_->Send(request);
    if (!response.ok()) {
        return Result<DeviceLoginChallenge>::Fail(
            "Device authorization failed (" + std::to_string(response.status) + "): " +
            response.body.substr(0, 400));
    }

    const json::ParseResult parsed = json::Value::Parse(response.body);
    if (!parsed.ok) {
        return Result<DeviceLoginChallenge>::Fail("Device authorization returned invalid JSON.");
    }

    const json::Value* deviceCode = parsed.value.Find("device_code");
    const json::Value* userCode = parsed.value.Find("user_code");
    const json::Value* verificationUri = parsed.value.Find("verification_uri");
    const json::Value* verificationUriComplete = parsed.value.Find("verification_uri_complete");
    if (deviceCode == nullptr || userCode == nullptr || verificationUri == nullptr) {
        return Result<DeviceLoginChallenge>::Fail(
            "Device authorization response did not include sign-in link data.");
    }

    DeviceLoginChallenge challenge;
    challenge.deviceCode = deviceCode->AsString();
    challenge.userCode = userCode->AsString();
    challenge.verificationUri = verificationUri->AsString();
    challenge.verificationUriComplete =
        verificationUriComplete != nullptr ? verificationUriComplete->AsString() : std::string();

    if (challenge.deviceCode.empty() || challenge.userCode.empty() ||
        challenge.verificationUri.empty()) {
        return Result<DeviceLoginChallenge>::Fail(
            "Device authorization response did not include sign-in link data.");
    }

    challenge.expiresAt = NowSeconds() + GetInt(parsed.value, "expires_in", kDefaultExpiresIn);
    const std::int64_t interval = GetInt(parsed.value, "interval", 5);
    challenge.intervalSeconds = static_cast<int>(interval < 1 ? 1 : interval);

    return Result<DeviceLoginChallenge>::Ok(challenge);
}

DevicePollResult AuthService::PollForTokens(const DeviceLoginChallenge& challenge) {
    DevicePollResult result;

    const std::string body = FormEncode({
        {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
        {"device_code", challenge.deviceCode},
        {"client_id", std::string(kDeviceFlowClientId)},
    });

    HttpRequest request;
    request.method = "POST";
    request.url = std::string(kTokenEndpoint);
    request.body = body;
    request.headers = BuildAuthHeaders(kDeviceFlowClientId,
                                       "application/x-www-form-urlencoded; charset=UTF-8");

    const HttpResponse response = http_->Send(request);
    const json::ParseResult parsed = json::Value::Parse(response.body);

    if (!response.ok()) {
        if (parsed.ok) {
            result.error = GetString(parsed.value, "error");
            result.errorDescription = GetString(parsed.value, "error_description");
            // authorization_pending / slow_down are normal while the code waits.
            const bool waiting = result.error == "authorization_pending" ||
                                 result.error == "slow_down";
            result.status = waiting ? DevicePollStatus::Pending : DevicePollStatus::Error;
            if (result.errorDescription.empty()) {
                result.errorDescription = waiting
                    ? "Waiting for approval in your browser."
                    : "Sign-in was not approved (" + result.error + ").";
            }
            return result;
        }
        result.status = DevicePollStatus::Error;
        result.error = "device_token_exchange_failed";
        result.errorDescription =
            "Device token exchange failed (" + std::to_string(response.status) + ").";
        return result;
    }

    if (!parsed.ok) {
        result.status = DevicePollStatus::Error;
        result.error = "invalid_token_response";
        result.errorDescription = "Token response was not valid JSON.";
        return result;
    }

    const std::string accessToken = GetString(parsed.value, "access_token");
    if (accessToken.empty()) {
        result.status = DevicePollStatus::Error;
        result.error = "invalid_token_response";
        result.errorDescription = "Device token response did not include access_token.";
        return result;
    }

    AuthTokens tokens;
    tokens.accessToken = accessToken;
    tokens.refreshToken = GetString(parsed.value, "refresh_token");
    tokens.idToken = GetString(parsed.value, "id_token");
    tokens.expiresAt = NowSeconds() + GetInt(parsed.value, "expires_in", 86400);

    result.status = DevicePollStatus::Success;
    result.tokens = std::move(tokens);
    return result;
}

// ---- token refresh --------------------------------------------------------------------------

Result<AuthTokens> AuthService::Refresh(AuthTokens tokens) {
    if (tokens.refreshToken.empty()) {
        return Result<AuthTokens>::Fail("No refresh token is stored; sign in again.");
    }

    const std::string body = FormEncode({
        {"grant_type", "refresh_token"},
        {"refresh_token", tokens.refreshToken},
        {"client_id", std::string(kDeviceFlowClientId)},
    });

    HttpRequest request;
    request.method = "POST";
    request.url = std::string(kTokenEndpoint);
    request.body = body;
    request.headers = BuildAuthHeaders(kDeviceFlowClientId,
                                       "application/x-www-form-urlencoded; charset=UTF-8");

    const HttpResponse response = http_->Send(request);
    if (!response.ok()) {
        return Result<AuthTokens>::Fail("Token refresh failed (" +
                                        std::to_string(response.status) + "): " +
                                        response.body.substr(0, 300));
    }

    const json::ParseResult parsed = json::Value::Parse(response.body);
    if (!parsed.ok || GetString(parsed.value, "access_token").empty()) {
        return Result<AuthTokens>::Fail("Token refresh response was unusable.");
    }

    tokens.accessToken = GetString(parsed.value, "access_token");
    tokens.idToken = GetString(parsed.value, "id_token", tokens.idToken);
    if (const json::Value* refresh = parsed.value.Find("refresh_token")) {
        // NVIDIA rotates the refresh token; keep the new one when present.
        const std::string rotated = refresh->AsString();
        if (!rotated.empty()) tokens.refreshToken = rotated;
    }
    tokens.expiresAt = NowSeconds() + GetInt(parsed.value, "expires_in", 86400);

    return Result<AuthTokens>::Ok(tokens);
}

Result<AuthTokens> AuthService::EnsureClientToken(AuthTokens tokens) {
    if (!tokens.clientToken.empty() &&
        NowSeconds() + kClientTokenRefreshWindowSeconds < tokens.clientTokenExpiresAt) {
        return Result<AuthTokens>::Ok(tokens);
    }

    const std::string body = FormEncode({
        {"grant_type", "client_credentials"},
        {"client_id", std::string(kDeviceFlowClientId)},
        {"scope", std::string(kScopes)},
    });

    HttpRequest request;
    request.method = "POST";
    request.url = std::string(kClientTokenEndpoint);
    request.body = body;
    request.headers = BuildAuthHeaders(kDeviceFlowClientId,
                                       "application/x-www-form-urlencoded; charset=UTF-8");

    const HttpResponse response = http_->Send(request);
    if (!response.ok()) {
        return Result<AuthTokens>::Fail("Client token request failed (" +
                                        std::to_string(response.status) + ").");
    }

    const json::ParseResult parsed = json::Value::Parse(response.body);
    if (!parsed.ok) {
        return Result<AuthTokens>::Fail("Client token response was not valid JSON.");
    }

    tokens.clientToken = GetString(parsed.value, "access_token");
    tokens.clientTokenExpiresAt = NowSeconds() + GetInt(parsed.value, "expires_in", 86400);
    return Result<AuthTokens>::Ok(tokens);
}

// ---- profile ---------------------------------------------------------------------------------

std::optional<JwtClaims> AuthService::ParseIdToken(const std::string& token) {
    std::size_t firstDot = token.find('.');
    if (firstDot == std::string::npos) return std::nullopt;
    const std::size_t secondDot = token.find('.', firstDot + 1);
    if (secondDot == std::string::npos) return std::nullopt;

    const std::string segment = token.substr(firstDot + 1, secondDot - firstDot - 1);
    const std::string decoded = Base64UrlDecodeToString(segment);
    const json::ParseResult parsed = json::Value::Parse(decoded);
    if (!parsed.ok || !parsed.value.isObject()) return std::nullopt;

    JwtClaims claims;
    claims.subject = GetString(parsed.value, "sub");
    claims.email = GetString(parsed.value, "email");
    claims.picture = GetString(parsed.value, "picture");
    claims.name = GetString(parsed.value, "name");
    claims.expiresAt = GetInt(parsed.value, "exp", 0);
    return claims;
}

std::string AuthService::GravatarUrl(const std::string& email, int size) {
    std::string normalized = Lower(email);
    const std::size_t at = normalized.find('@');
    // Strip "+" tags before hashing: so gfn+opennow@x.com matches gfn@x.com,
    // which is what mail providers treat as the same mailbox.
    if (at != std::string::npos) {
        const std::size_t plus = normalized.find('+');
        if (plus != std::string::npos && plus < at) {
            normalized.erase(plus, at - plus);
        }
        normalized = Trim(normalized);
    }
    return "https://www.gravatar.com/avatar/" + Md5(normalized).Hex() +
           "?s=" + std::to_string(size) + "&d=identicon";
}

Result<UserProfile> AuthService::FetchProfile(const AuthTokens& tokens) {
    UserProfile profile;

    if (const std::optional<JwtClaims> claims = ParseIdToken(
            tokens.idToken.empty() ? tokens.accessToken : tokens.idToken)) {
        profile.email = claims->email;
        profile.displayName = claims->name;
        profile.userId = claims->subject;
        profile.avatarUrl =
            claims->picture.empty() && !claims->email.empty()
                ? GravatarUrl(claims->email)
                : claims->picture;
        if (!claims->email.empty() && !claims->name.empty() && !claims->picture.empty()) {
            // The JWT carried everything the UI needs; skip the network call.
            return Result<UserProfile>::Ok(profile);
        }
    }

    HttpRequest request;
    request.method = "GET";
    request.url = std::string(kUserInfoEndpoint);
    request.headers = BuildAuthHeaders(kDeviceFlowClientId);
    request.headers.push_back(HttpHeader{"Authorization", "Bearer " + tokens.accessToken});

    const HttpResponse response = http_->Send(request);
    if (!response.ok()) {
        // A partial profile is better than no session: the nav bar can fall
        // back to the email from the JWT.
        if (!profile.email.empty() || !profile.displayName.empty()) {
            return Result<UserProfile>::Ok(profile);
        }
        return Result<UserProfile>::Fail("User info request failed (" +
                                         std::to_string(response.status) + ").");
    }

    const json::ParseResult parsed = json::Value::Parse(response.body);
    if (!parsed.ok) {
        return Result<UserProfile>::Ok(profile);
    }

    const std::string email = GetString(parsed.value, "email", profile.email);
    profile.email = email;
    profile.displayName = GetString(parsed.value, "name", profile.displayName);
    if (profile.displayName.empty()) {
        profile.displayName = GetString(parsed.value, "preferred_username");
    }
    profile.userId = GetString(parsed.value, "sub", profile.userId);
    const std::string picture = GetString(parsed.value, "picture");
    profile.avatarUrl = picture.empty() && !email.empty() ? GravatarUrl(email) : picture;

    return Result<UserProfile>::Ok(profile);
}

}  // namespace opennow::core::gfn
