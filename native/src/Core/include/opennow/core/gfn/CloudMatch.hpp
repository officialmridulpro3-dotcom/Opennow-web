// OpenNOW native client — CloudMatch session protocol.
//
// Port of `src/server/gfn/cloudmatch*.ts`. CloudMatch is the matchmaking and
// seat-allocation service: create → poll until the seat is ready → connect the
// WebRTC/NVST transport → stop. Every field below is on the wire, so the
// builders are pure functions that the unit tests can assert byte for byte.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "opennow/core/Http.hpp"
#include "opennow/core/Json.hpp"
#include "opennow/core/Result.hpp"
#include "opennow/core/Types.hpp"

namespace opennow::core::gfn {

struct Resolution {
    int width = 1920;
    int height = 1080;
};

// "1920x1080" -> {1920, 1080}. Falls back to 1080p on garbage, matching the
// server so a corrupt settings file can never wedge the launcher.
Resolution ParseResolution(const std::string& value);

int ColorQualityBitDepth(ColorQuality quality);     // 10-bit modes -> 1, else 0
int ColorQualityChromaFormat(ColorQuality quality); // *_444 -> 1, else 0
int CodecWireValue(VideoCodec codec);               // H264 1, H265 2, AV1 3
int AppLaunchModeWireValue(AppLaunchMode mode);     // default 1, gamepad 2, touch 3

struct SessionCreateInput {
    std::string token;
    std::string streamingBaseUrl;   // provider URL, ends with '/'
    std::string appId;
    std::string internalTitle;
    std::string zone;
    StreamSettings settings;
    bool accountLinked = true;
    bool enablePersistingInGameSettings = false;
    std::optional<std::string> networkTestSessionId;
    std::string clientId;
    std::string deviceId;
};

struct SessionPollInput {
    std::string token;
    std::string streamingBaseUrl;
    std::string zone;
    std::string sessionId;
    std::string appId;      // required by the resume/claim body
    std::string serverIp;
    std::string clientId;
    std::string deviceId;
};

// Minimal queue/advert state pulled out of a poll response. The full payload is
// far richer; the UI only needs these.
struct SessionPollState {
    bool valid = false;
    int statusCode = 0;
    std::string statusDescription;
    std::string sessionId;
    std::string appId;
    std::string serverIp;
    std::string streamingBaseUrl;
    std::string signalingUrl;
    std::string signalingServer;
    std::string peerId;
    std::optional<std::string> sdpOffer;
    std::vector<std::string> iceServers;
    std::optional<int> queuePosition;
    std::optional<int> estimatedWaitSeconds;
    std::optional<int> seatSetupStep;
    bool ready = false;
    bool adsRequired = false;
    bool queuePaused = false;
    std::optional<std::string> adMediaUrl;
    std::optional<int> adDurationMs;
    bool rawIncluded = false;   // true when the caller should keep the JSON
    json::Value raw;
};

class CloudMatchService {
public:
    explicit CloudMatchService(HttpClientPtr http) : http_(std::move(http)) {}

    // POST {base}/v2/nettestsession — optional; a failure is not fatal.
    Result<std::string> CreateNetworkTestSession(const std::string& base,
                                                 const StreamSettings& settings,
                                                 const std::string& token,
                                                 const std::string& clientId,
                                                 const std::string& deviceId);

    // POST {base}/v2/session
    Result<SessionPollState> CreateSession(const SessionCreateInput& input);

    // GET {base}/v2/session/{id}
    Result<SessionPollState> PollSession(const SessionPollInput& input);

    // PUT {base}/v2/session/{id} with action=3
    Result<void> StopSession(const SessionPollInput& input);

    // PUT {base}/v2/session/{id} with action=2 (resume/claim an existing seat)
    Result<SessionPollState> ClaimSession(const SessionPollInput& input,
                                          const StreamSettings& settings,
                                          std::optional<int> sessionAppLaunchMode = std::nullopt);

    // PUT {base}/v2/session/{id} with action=6 — acknowledge ad playback.
    Result<void> ReportAd(const SessionPollInput& input, int adState,
                          std::int64_t clientTimestamp = 0);

    // ---- pure builders, exposed for tests -------------------------------------
    static json::Value BuildSessionRequestBody(const SessionCreateInput& input,
                                               const std::string& deviceHashId,
                                               const std::optional<std::string>& netTestSessionId);
    static json::Value BuildClaimRequestBody(const std::string& appId,
                                             const StreamSettings& settings,
                                             const std::string& deviceHashId,
                                             std::optional<int> sessionAppLaunchMode);
    static SessionPollState ParseSessionResponse(const json::Value& payload);

private:
    HttpClientPtr http_;

    // Session ids are per-launch, but the client id must stay stable across
    // create → poll → stop or NVIDIA treats the poll as a different client.
    std::string EnsureClientId(const std::string& supplied);
    std::string clientId_;
};

}  // namespace opennow::core::gfn
