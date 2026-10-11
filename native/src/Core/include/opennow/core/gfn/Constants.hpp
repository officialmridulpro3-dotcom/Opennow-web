// OpenNOW native client — GFN wire constants.
//
// Ported verbatim from `src/server/gfn/auth/constants.ts` and
// `src/server/gfn/clientHeaders.ts`. These values are the client identity; if
// NVIDIA changes them the web client breaks in exactly the same way, which is
// what keeps the two clients interchangeable.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "opennow/core/Http.hpp"

namespace opennow::core::gfn {

// ---- Endpoints ------------------------------------------------------------------

inline constexpr std::string_view kServiceUrlsEndpoint =
    "https://pcs.geforcenow.com/v1/serviceUrls";
inline constexpr std::string_view kTokenEndpoint = "https://login.nvidia.com/token";
inline constexpr std::string_view kClientTokenEndpoint = "https://login.nvidia.com/client_token";
inline constexpr std::string_view kUserInfoEndpoint = "https://login.nvidia.com/userinfo";
inline constexpr std::string_view kAuthorizeEndpoint = "https://login.nvidia.com/authorize";
inline constexpr std::string_view kDeviceAuthorizeEndpoint =
    "https://login.nvidia.com/device/authorize";

inline constexpr std::string_view kDefaultStreamingServiceUrl =
    "https://prod.cloudmatchbeta.nvidiagrid.net/";

// ---- Client identity --------------------------------------------------------------

inline constexpr std::string_view kDesktopClientId = "ZU7sPN-miLujMD95LfOQ453IB0AtjM8sMyvgJ9wCXEQ";
inline constexpr std::string_view kSteamDeckClientId =
    "q61ddeJr-T7O90Nl-P-N7I36yctih4Ml6FyXLrb6j-U";
inline constexpr std::string_view kScopes = "openid consent email tk_client age";
inline constexpr std::string_view kDefaultIdpId =
    "PDiAhv2kJTFeQ7WOPqiQ2tRZ7lGhR2X11dXvM4TZSxg";
inline constexpr std::string_view kLcarsClientId = "ec7e38d4-03af-4b58-b131-cfb0495903ab";
inline constexpr std::string_view kClientVersion = "2.0.80.173";

inline constexpr std::string_view kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/128.0.0.0 Safari/537.36 NVIDIACEFClient/HEAD/debb5919f6 GFN-PC/2.0.80.173";

inline constexpr std::string_view kNvidiaFileOrigin = "https://nvfile";
inline constexpr std::string_view kNvidiaFileReferer = "https://nvfile/";
inline constexpr std::string_view kGfnPlayOrigin = "https://play.geforcenow.com";
inline constexpr std::string_view kGfnPlayReferer = "https://play.geforcenow.com/";

inline constexpr int kTokenRefreshWindowSeconds = 10 * 60;
inline constexpr int kClientTokenRefreshWindowSeconds = 5 * 60;

// ---- Header builders ----------------------------------------------------------------

// Headers for the OAuth endpoints (login.nvidia.com).
std::vector<HttpHeader> BuildAuthHeaders(std::string_view clientId,
                                         std::string_view contentType = {},
                                         std::string_view deviceId = {});

// Headers for LCARS / catalog GraphQL calls.
std::vector<HttpHeader> BuildLcarsHeaders(std::string_view token, std::string_view clientId,
                                          std::string_view contentType = {});

// Headers for CloudMatch session calls.
std::vector<HttpHeader> BuildCloudMatchHeaders(std::string_view token,
                                               std::string_view clientId,
                                               std::string_view deviceId,
                                               bool includeOrigin = true);

}  // namespace opennow::core::gfn
