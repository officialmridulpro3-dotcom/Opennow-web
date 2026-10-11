// OpenNOW native client — CloudMatch session protocol implementation.

#include "opennow/core/gfn/CloudMatch.hpp"

#include <random>
#include <sstream>

#include "opennow/core/Hash.hpp"
#include "opennow/core/Logging.hpp"
#include "opennow/core/gfn/Constants.hpp"
#include "opennow/core/gfn/DeviceId.hpp"

namespace opennow::core::gfn {
namespace {

std::string UuidV4() {
    static std::mt19937_64 engine{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> distribution;
    const std::uint64_t a = distribution(engine);
    const std::uint64_t b = distribution(engine);
    char buffer[40];
    // Version 4, variant 1 — the shape NVIDIA's logs expect; cryptographic
    // quality is not required for a sub-session id.
    std::snprintf(buffer, sizeof(buffer), "%08x-%04x-4%03x-%04x-%012llx",
                  static_cast<unsigned>(a >> 32), static_cast<unsigned>((a >> 16) & 0xFFFF),
                  static_cast<unsigned>(a & 0x0FFF),
                  static_cast<unsigned>(0x8000 | (b & 0x3FFF)),
                  static_cast<unsigned long long>(b >> 16));
    buffer[8] = '-';
    return std::string(buffer);
}

std::int64_t TimezoneOffsetMs() {
    // CloudMatch wants milliseconds; std::chrono has no portable zone offset
    // before C++20's tzdb, so derive it from the local/UTC time_t difference.
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    std::tm local{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
    localtime_s(&local, &now);
#else
    gmtime_r(&now, &utc);
    localtime_r(&now, &local);
#endif
    const std::time_t utcTime = std::mktime(&utc);
    const std::time_t localTime = std::mktime(&local);
    return static_cast<std::int64_t>(std::difftime(localTime, utcTime)) * 1000;
}

std::string JoinUrl(const std::string& base, const std::string& path) {
    if (base.empty()) return path;
    if (base.back() != '/' && path.front() != '/') return base + "/" + path;
    if (base.back() == '/' && path.front() == '/') return base + path.substr(1);
    return base + path;
}

json::Value Metadata(const std::vector<std::pair<std::string, std::string>>& entries) {
    json::Value array = json::ArrayValue();
    for (const auto& [key, value] : entries) {
        json::Value entry = json::ObjectValue();
        entry.Set("key", key);
        entry.Set("value", value);
        array.Push(std::move(entry));
    }
    return array;
}

json::Value StreamingFeatures(const StreamSettings& settings, int bitDepth, int chromaFormat,
                              bool nativeTransport) {
    json::Value features = json::ObjectValue();
    const bool cloudGsync = settings.enableCloudGsync;
    const int codec = CodecWireValue(settings.codec);

    if (!nativeTransport) {
        // WebRTC: the codec is negotiated later through SDP, so only colour
        // quality and the latency features are fixed at allocation.
        features.Set("reflex", settings.reflex);
        features.Set("bitDepth", bitDepth);
        features.Set("cloudGsync", cloudGsync);
        features.Set("enabledL4S", settings.enableL4S);
        features.Set("supportedHidDevices", 0);
        features.Set("profile", 0);
        features.Set("fallbackToLogicalResolution", false);
        features.Set("chromaFormat", chromaFormat);
        features.Set("prefilterMode", 0);
        features.Set("prefilterSharpness", 0);
        features.Set("prefilterNoiseReduction", 0);
        features.Set("hudStreamingMode", 0);
        return features;
    }

    // Native seats configure the encoder from these fields at allocation.
    // Without codec and maxBitrateKbps the seat never becomes ready.
    const int depth = codec == 1 ? 0 : bitDepth;
    const int chroma = (codec == 1 || codec == 3) ? 0 : chromaFormat;
    int maxBitrate = settings.maxBitrateMbps;
    if (maxBitrate < 1) maxBitrate = 1;
    if (maxBitrate > 200) maxBitrate = 200;

    features.Set("reflex", cloudGsync || settings.fps >= 120);
    features.Set("bitDepth", depth);
    features.Set("cloudGsync", cloudGsync);
    features.Set("enabledL4S", settings.enableL4S);
    features.Set("codec", codec);
    features.Set("maxBitrateKbps", maxBitrate * 1000);
    features.Set("supportedHidDevices", 0);
    features.Set("profile", 0);
    features.Set("fallbackToLogicalResolution", false);
    features.Set("chromaFormat", chroma);
    features.Set("prefilterMode", 0);
    features.Set("prefilterSharpness", 0);
    features.Set("prefilterNoiseReduction", 0);
    features.Set("hudStreamingMode", 0);
    features.Set("vsync", settings.vsync);
    features.Set("audioChannelCount", 2);
    features.Set("qosPolicy", 0);
    features.Set("dynamicStreamingMode", settings.adjustForPoorNetwork ? 1 : 0);
    return features;
}

int OptionalInt(const json::Value& value, std::string_view key, int fallback) {
    if (const json::Value* found = value.Find(key)) {
        const std::int64_t raw = found->AsInt(fallback);
        return static_cast<int>(raw);
    }
    return fallback;
}

std::optional<int> ToOptionalPositiveInt(const json::Value* value) {
    if (value == nullptr) return std::nullopt;
    const std::int64_t raw = value->AsInt(0);
    if (raw <= 0) return std::nullopt;
    return static_cast<int>(raw);
}

std::optional<std::string> ToOptionalString(const json::Value* value) {
    if (value == nullptr || !value->isString()) return std::nullopt;
    const std::string text = value->AsString();
    if (text.empty()) return std::nullopt;
    return text;
}

std::string ExtractString(const json::Value& value, std::initializer_list<std::string_view> keys) {
    for (std::string_view key : keys) {
        if (const json::Value* found = value.Find(key)) {
            const std::string text = found->AsString();
            if (!text.empty()) return text;
        }
    }
    return {};
}

}  // namespace

Resolution ParseResolution(const std::string& value) {
    Resolution out;
    const std::size_t separator = value.find('x');
    if (separator == std::string::npos) return out;
    const long width = std::strtol(value.substr(0, separator).c_str(), nullptr, 10);
    const long height = std::strtol(value.substr(separator + 1).c_str(), nullptr, 10);
    if (width <= 0 || height <= 0) return out;
    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    return out;
}

int ColorQualityBitDepth(ColorQuality quality) {
    return quality == ColorQuality::TenBit420 || quality == ColorQuality::TenBit444 ? 1 : 0;
}

int ColorQualityChromaFormat(ColorQuality quality) {
    return quality == ColorQuality::EightBit444 || quality == ColorQuality::TenBit444 ? 1 : 0;
}

int CodecWireValue(VideoCodec codec) {
    switch (codec) {
        case VideoCodec::H264: return 1;
        case VideoCodec::Hevc: return 2;
        case VideoCodec::Av1: return 3;
        case VideoCodec::Auto: return 0;
    }
    return 0;
}

int AppLaunchModeWireValue(AppLaunchMode mode) {
    switch (mode) {
        case AppLaunchMode::Default: return 1;
        case AppLaunchMode::GamepadFriendly: return 2;
    }
    return 1;
}

// ---- builders ------------------------------------------------------------------------------

json::Value CloudMatchService::BuildSessionRequestBody(
    const SessionCreateInput& input, const std::string& deviceHashId,
    const std::optional<std::string>& netTestSessionId) {
    const Resolution resolution = ParseResolution(input.settings.resolution);
    const int bitDepth = ColorQualityBitDepth(input.settings.colorQuality);
    const int chromaFormat = ColorQualityChromaFormat(input.settings.colorQuality);
    const bool nativeTransport = input.settings.transport == StreamTransport::Nvst;
    const std::string subSessionId = UuidV4();

    json::Value monitor = json::ObjectValue();
    monitor.Set("monitorId", 0);
    monitor.Set("positionX", 0);
    monitor.Set("positionY", 0);
    monitor.Set("widthInPixels", resolution.width);
    monitor.Set("heightInPixels", resolution.height);
    monitor.Set("framesPerSecond", input.settings.fps);
    monitor.Set("sdrHdrMode", 0);          // HDR is a separate toggle; off here.
    monitor.Set("displayData", json::ObjectValue());
    monitor.Set("hdr10PlusGamingData", json::Value());
    monitor.Set("dpi", 0);

    json::Value monitors = json::ArrayValue();
    monitors.Push(std::move(monitor));

    // The metadata list locks the transport at allocation: sending
    // GSStreamerType=WebRTC and then opening NVST yields HTTP 501 on RTSPS.
    // clientPhysicalResolution is a JSON string nested inside the metadata
    // value, not an object — NVIDIA parses it as text.
    json::Value physical = json::ObjectValue();
    physical.Set("horizontalPixels", resolution.width);
    physical.Set("verticalPixels", resolution.height);
    const std::string physicalJson = physical.Dump();

    json::Value metaData =
        nativeTransport
            ? Metadata({{"ClientImeSupport", "0"},
                        {"SubSessionId", subSessionId},
                        {"clientPhysicalResolution", physicalJson},
                        {"networkType", "Unknown"},
                        {"wssignaling", "1"},
                        {"surroundAudioInfo", "2"}})
            : Metadata({{"SubSessionId", subSessionId},
                        {"wssignaling", "1"},
                        {"GSStreamerType", "WebRTC"},
                        {"networkType", "Unknown"},
                        {"ClientImeSupport", "0"},
                        {"clientPhysicalResolution", physicalJson},
                        {"surroundAudioInfo", "2"}});

    json::Value request = json::ObjectValue();
    request.Set("appId", input.appId);
    request.Set("internalTitle", input.internalTitle.empty() ? json::Value()
                                                            : json::Value(input.internalTitle));
    request.Set("availableSupportedControllers", json::ArrayValue());
    request.Set("networkTestSessionId",
                netTestSessionId && !netTestSessionId->empty() ? json::Value(*netTestSessionId)
                                                              : json::Value());
    request.Set("parentSessionId", json::Value());
    request.Set("clientIdentification", "GFN-PC");
    request.Set("deviceHashId", deviceHashId);
    request.Set("clientVersion", "30.0");
    request.Set("sdkVersion", "1.0");
    request.Set("streamerVersion", 1);
    request.Set("clientPlatformName", "windows");
    request.Set("clientRequestMonitorSettings", std::move(monitors));
    request.Set("useOps", true);
    request.Set("audioMode", 2);
    request.Set("metaData", std::move(metaData));
    request.Set("sdrHdrMode", 0);
    request.Set("clientDisplayHdrCapabilities", json::Value());
    request.Set("surroundAudioInfo", 0);
    request.Set("remoteControllersBitmap", 0);
    request.Set("clientTimezoneOffset", TimezoneOffsetMs());
    request.Set("enhancedStreamMode", 1);
    request.Set("appLaunchMode", AppLaunchModeWireValue(input.settings.appLaunchMode));
    request.Set("secureRTSPSupported", nativeTransport);
    request.Set("transport", json::Value());
    request.Set("partnerCustomData", "");
    request.Set("accountLinked", input.accountLinked);
    request.Set("enablePersistingInGameSettings", input.enablePersistingInGameSettings);
    request.Set("userAge", 26);
    request.Set("requestedStreamingFeatures",
                StreamingFeatures(input.settings, bitDepth, chromaFormat, nativeTransport));

    json::Value body = json::ObjectValue();
    body.Set("sessionRequestData", std::move(request));
    return body;
}

json::Value CloudMatchService::BuildClaimRequestBody(const std::string& appId,
                                                     const StreamSettings& settings,
                                                     const std::string& deviceHashId,
                                                     std::optional<int> sessionAppLaunchMode) {
    // A resume claim must NOT renegotiate streaming parameters: fps, resolution
    // and codec are immutable on a running session and re-sending them is a
    // reliable HTTP 400.
    const bool nativeTransport = settings.transport == StreamTransport::Nvst;
    const std::string subSessionId = UuidV4();

    const json::Value metaData =
        nativeTransport
            ? Metadata({{"SubSessionId", UuidV4()},
                        {"wssignaling", "1"},
                        {"networkType", "Unknown"},
                        {"ClientImeSupport", "0"},
                        {"surroundAudioInfo", "2"}})
            : Metadata({{"SubSessionId", subSessionId},
                        {"wssignaling", "1"},
                        {"GSStreamerType", "WebRTC"},
                        {"networkType", "Unknown"},
                        {"ClientImeSupport", "0"},
                        {"surroundAudioInfo", "2"}});

    json::Value request = json::ObjectValue();
    request.Set("audioMode", 2);
    request.Set("remoteControllersBitmap", 0);
    request.Set("sdrHdrMode", 0);
    request.Set("networkTestSessionId", json::Value());
    request.Set("availableSupportedControllers", json::ArrayValue());
    request.Set("clientVersion", "30.0");
    request.Set("deviceHashId", deviceHashId);
    request.Set("internalTitle", json::Value());
    request.Set("clientPlatformName", "windows");
    request.Set("metaData", metaData);
    request.Set("surroundAudioInfo", 0);
    request.Set("clientTimezoneOffset", TimezoneOffsetMs());
    request.Set("clientIdentification", "GFN-PC");
    request.Set("parentSessionId", json::Value());
    request.Set("appId", json::Value(static_cast<std::int64_t>(
        std::strtoll(appId.c_str(), nullptr, 10))));
    request.Set("streamerVersion", 1);
    request.Set("appLaunchMode", sessionAppLaunchMode.value_or(
                                     AppLaunchModeWireValue(settings.appLaunchMode)));
    request.Set("sdkVersion", "1.0");
    request.Set("enhancedStreamMode", 1);
    request.Set("useOps", true);
    request.Set("clientDisplayHdrCapabilities", json::Value());
    request.Set("accountLinked", true);
    request.Set("partnerCustomData", "");
    request.Set("enablePersistingInGameSettings", settings.persistInGameSettings);
    request.Set("secureRTSPSupported", nativeTransport);
    request.Set("userAge", 26);

    json::Value body = json::ObjectValue();
    body.Set("action", 2);
    body.Set("data", "RESUME");
    body.Set("sessionRequestData", std::move(request));
    body.Set("metaData", json::ArrayValue());
    return body;
}

SessionPollState CloudMatchService::ParseSessionResponse(const json::Value& payload) {
    SessionPollState state;
    state.raw = payload;
    state.rawIncluded = true;

    if (const json::Value* status = payload.Find("requestStatus")) {
        state.statusCode = OptionalInt(*status, "statusCode", 0);
        state.statusDescription = ExtractString(*status, {"statusDescription"});
    }

    const json::Value* session = payload.Find("session");
    if (session == nullptr) {
        // Some error payloads only carry requestStatus; keep what we have.
        state.valid = state.statusCode != 0;
        return state;
    }

    state.valid = true;
    state.sessionId = ExtractString(*session, {"sessionId", "id"});
    state.appId = ExtractString(*session, {"appId"});
    if (state.appId.empty() && session->Find("appId")) {
        state.appId = std::to_string(session->Find("appId")->AsInt(0));
    }
    state.serverIp = ExtractString(*session, {"serverIp", "streamingServerIp", "ip"});
    state.signalingUrl = ExtractString(*session, {"signalingUrl", "webRtcSignalingUrl"});
    state.signalingServer = ExtractString(*session, {"signalingServer", "signalingServerHost"});
    state.peerId = ExtractString(*session, {"peerId", "clientId"});

    if (const json::Value* sdp = session->Find("sdpOffer")) {
        const std::string offer = sdp->AsString();
        if (!offer.empty()) state.sdpOffer = offer;
    }

    if (const json::Value* ice = session->Find("iceServers")) {
        for (const json::Value& entry : ice->Items()) {
            if (const json::Value* url = entry.Find("url")) {
                state.iceServers.push_back(url->AsString());
                continue;
            }
            if (const json::Value* urls = entry.Find("urls")) {
                if (urls->isString()) state.iceServers.push_back(urls->AsString());
                else if (urls->isArray()) {
                    for (const json::Value& item : urls->Items()) {
                        state.iceServers.push_back(item.AsString());
                    }
                }
            }
        }
    }

    // Queue position lives in several places depending on the server build.
    state.queuePosition = ToOptionalPositiveInt(session->Find("queuePosition"));
    if (!state.queuePosition) {
        if (const json::Value* progress = session->Find("sessionProgress")) {
            state.queuePosition = ToOptionalPositiveInt(progress->Find("queuePosition"));
            if (!state.queuePosition) {
                state.estimatedWaitSeconds =
                    ToOptionalPositiveInt(progress->Find("estimatedWaitTimeSeconds"));
            }
        }
    }
    if (!state.queuePosition) {
        if (const json::Value* progress = session->Find("progressInfo")) {
            state.queuePosition = ToOptionalPositiveInt(progress->Find("queuePosition"));
        }
    }

    if (const json::Value* setup = session->Find("seatSetupInfo")) {
        state.seatSetupStep = ToOptionalPositiveInt(setup->Find("seatSetupStep"));
    }

    const std::string state_ = ExtractString(*session, {"sessionState", "state", "status"});
    const bool stateSaysReady = state_ == "READY" || state_ == "ready" ||
                                state_ == "STREAMING" || state_ == "streaming";
    // An SDP offer in the poll response is the practical "seat is waiting for
    // you" signal; some builds never set a sessionState string.
    state.ready = stateSaysReady || state.sdpOffer.has_value();

    // Ad-supported queue: the server pauses the queue until the ad finishes.
    const json::Value* adsRequired = session->Find("sessionAdsRequired");
    if (adsRequired == nullptr) adsRequired = session->Find("isAdsRequired");
    state.adsRequired = adsRequired != nullptr && adsRequired->AsBool(false);

    if (const json::Value* opportunity = session->Find("opportunity")) {
        state.queuePaused = opportunity->Find("queuePaused")->AsBool(false);
        if (const json::Value* paused = opportunity->Find("state")) {
            std::string text = paused->AsString();
            for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (text == "graceperiodstart") state.queuePaused = true;
        }
    }

    if (const json::Value* ads = session->Find("sessionAds")) {
        for (const json::Value& ad : ads->Items()) {
            if (const auto url = ToOptionalString(ad.Find("mediaFileUrl"))) {
                state.adMediaUrl = url;
                break;
            }
            if (const auto url = ToOptionalString(ad.Find("adUrl"))) {
                state.adMediaUrl = url;
                break;
            }
        }
        if (const json::Value* first = ads->Items().empty() ? nullptr : &ads->Items()[0]) {
            if (const json::Value* seconds = first->Find("adLengthInSeconds")) {
                const std::int64_t value = seconds->AsInt(0);
                if (value > 0) state.adDurationMs = static_cast<int>(value * 1000);
            }
        }
        if (!state.adDurationMs && state.adMediaUrl) state.adsRequired = true;
    }

    return state;
}

// ---- service --------------------------------------------------------------------------------

std::string CloudMatchService::EnsureClientId(const std::string& supplied) {
    if (!supplied.empty()) {
        clientId_ = supplied;
        return clientId_;
    }
    if (clientId_.empty()) clientId_ = UuidV4();
    return clientId_;
}

Result<std::string> CloudMatchService::CreateNetworkTestSession(const std::string& base,
                                                                const StreamSettings& settings,
                                                                const std::string& token,
                                                                const std::string& clientId,
                                                                const std::string& deviceId) {
    const Resolution resolution = ParseResolution(settings.resolution);

    json::Value profile = json::ObjectValue();
    profile.Set("widthInPixels", resolution.width);
    profile.Set("heightInPixels", resolution.height);
    profile.Set("framesPerSecond", settings.fps);

    json::Value netTest = json::ObjectValue();
    netTest.Set("clientPlatformName", "windows");
    netTest.Set("netTestProfile", std::move(profile));

    json::Value body = json::ObjectValue();
    body.Set("netTestRequestData", std::move(netTest));

    HttpRequest request;
    request.method = "POST";
    request.url = JoinUrl(base, "v2/nettestsession");
    request.body = body.Dump();
    request.timeout = std::chrono::milliseconds(8000);
    request.headers = BuildCloudMatchHeaders(token, EnsureClientId(clientId),
                                             deviceId.empty() ? DeviceId::Get() : deviceId, true);

    const HttpResponse response = http_->Send(request);
    if (!response.ok()) {
        // A missing net-test session only removes a bandwidth hint; never fail
        // the launch because of it.
        LogWarn("CloudMatch", "nettestsession failed (HTTP " + std::to_string(response.status) +
                                  "); continuing without it.");
        return Result<std::string>::Fail("nettestsession unavailable");
    }

    const json::ParseResult parsed = json::Value::Parse(response.body);
    if (!parsed.ok) return Result<std::string>::Fail("nettestsession returned invalid JSON");

    if (const json::Value* status = parsed.value.Find("requestStatus")) {
        if (OptionalInt(*status, "statusCode", 0) != 1) {
            return Result<std::string>::Fail("nettestsession rejected the request");
        }
    }

    if (const json::Value* netTestSession = parsed.value.Find("netTestSession")) {
        const std::string id = ExtractString(*netTestSession, {"sessionId"});
        if (!id.empty()) return Result<std::string>::Ok(id);
    }
    return Result<std::string>::Fail("nettestsession response had no sessionId");
}

Result<SessionPollState> CloudMatchService::CreateSession(const SessionCreateInput& input) {
    const std::string deviceHashId = input.deviceId.empty() ? DeviceId::Get() : input.deviceId;
    const std::string clientId = EnsureClientId(input.clientId);

    std::optional<std::string> netTestSessionId = input.networkTestSessionId;
    if (!netTestSessionId) {
        const Result<std::string> created = CreateNetworkTestSession(
            input.streamingBaseUrl, input.settings, input.token, clientId, deviceHashId);
        if (created.ok()) netTestSessionId = *created;
    }

    const json::Value body =
        BuildSessionRequestBody(input, deviceHashId, netTestSessionId);

    HttpRequest request;
    request.method = "POST";
    request.url = JoinUrl(input.streamingBaseUrl, "v2/session");
    request.body = body.Dump();
    request.headers = BuildCloudMatchHeaders(input.token, clientId, deviceHashId, true);

    const HttpResponse response = http_->Send(request);
    const json::ParseResult parsed = json::Value::Parse(response.body);
    if (!parsed.ok) {
        return Result<SessionPollState>::Fail(
            "Session create failed (" + std::to_string(response.status) + "): " +
            response.body.substr(0, 300));
    }

    SessionPollState state = ParseSessionResponse(parsed.value);
    state.streamingBaseUrl = input.streamingBaseUrl;
    if (!response.ok() && state.statusCode == 0) {
        return Result<SessionPollState>::Fail(
            "Session create failed (HTTP " + std::to_string(response.status) + ")");
    }
    return Result<SessionPollState>::Ok(state);
}

Result<SessionPollState> CloudMatchService::PollSession(const SessionPollInput& input) {
    const std::string deviceHashId = input.deviceId.empty() ? DeviceId::Get() : input.deviceId;
    const std::string clientId = EnsureClientId(input.clientId);

    std::string base = input.streamingBaseUrl;
    if (!input.serverIp.empty()) {
        // Once the seat reports its own IP, poll it directly: the zone endpoint
        // can lag behind the allocation by several seconds.
        base = "https://" + input.serverIp;
    }

    HttpRequest request;
    request.method = "GET";
    request.url = JoinUrl(base, "v2/session/" + input.sessionId);
    request.headers = BuildCloudMatchHeaders(input.token, clientId, deviceHashId, false);
    request.strictTls = false;  // seat certificates are pinned to the raw IP

    const HttpResponse response = http_->Send(request);
    const json::ParseResult parsed = json::Value::Parse(response.body);
    if (!parsed.ok) {
        return Result<SessionPollState>::Fail(
            "Session poll failed (" + std::to_string(response.status) + ")");
    }

    SessionPollState state = ParseSessionResponse(parsed.value);
    state.streamingBaseUrl = input.streamingBaseUrl;
    if (state.sessionId.empty()) state.sessionId = input.sessionId;
    return Result<SessionPollState>::Ok(state);
}

Result<SessionPollState> CloudMatchService::ClaimSession(const SessionPollInput& input,
                                                          const StreamSettings& settings,
                                                          std::optional<int> sessionAppLaunchMode) {
    const std::string deviceHashId = input.deviceId.empty() ? DeviceId::Get() : input.deviceId;
    const std::string clientId = EnsureClientId(input.clientId);
    const json::Value body = BuildClaimRequestBody(input.appId, settings,
                                                   deviceHashId, sessionAppLaunchMode);

    HttpRequest request;
    request.method = "PUT";
    request.url = JoinUrl(input.streamingBaseUrl, "v2/session/" + input.sessionId);
    request.body = body.Dump();
    request.headers = BuildCloudMatchHeaders(input.token, clientId, deviceHashId, false);
    request.strictTls = false;

    const HttpResponse response = http_->Send(request);
    const json::ParseResult parsed = json::Value::Parse(response.body);
    if (!parsed.ok || !response.ok()) {
        return Result<SessionPollState>::Fail("Session claim failed (HTTP " +
                                              std::to_string(response.status) + ")");
    }
    return Result<SessionPollState>::Ok(ParseSessionResponse(parsed.value));
}

Result<void> CloudMatchService::StopSession(const SessionPollInput& input) {
    const std::string deviceHashId = input.deviceId.empty() ? DeviceId::Get() : input.deviceId;
    const std::string clientId = EnsureClientId(input.clientId);

    json::Value body = json::ObjectValue();
    body.Set("action", 3);
    body.Set("data", "TERMINATE");

    HttpRequest request;
    request.method = "PUT";
    request.url = JoinUrl(input.streamingBaseUrl, "v2/session/" + input.sessionId);
    request.body = body.Dump();
    request.headers = BuildCloudMatchHeaders(input.token, clientId, deviceHashId, false);
    request.strictTls = false;

    const HttpResponse response = http_->Send(request);
    if (!response.ok()) {
        return Result<void>::Fail("Session stop failed (HTTP " + std::to_string(response.status) +
                                  ")");
    }
    return Result<void>::Ok();
}

Result<void> CloudMatchService::ReportAd(const SessionPollInput& input, int adState,
                                         std::int64_t clientTimestamp) {
    const std::string deviceHashId = input.deviceId.empty() ? DeviceId::Get() : input.deviceId;
    const std::string clientId = EnsureClientId(input.clientId);
    const std::int64_t timestamp =
        clientTimestamp != 0 ? clientTimestamp
                             : static_cast<std::int64_t>(std::time(nullptr));

    json::Value adUpdate = json::ObjectValue();
    adUpdate.Set("adState", adState);
    adUpdate.Set("clientTimestamp", timestamp);

    json::Value body = json::ObjectValue();
    body.Set("action", 6);
    body.Set("data", "AD_UPDATE");
    body.Set("adUpdate", std::move(adUpdate));

    HttpRequest request;
    request.method = "PUT";
    request.url = JoinUrl(input.streamingBaseUrl, "v2/session/" + input.sessionId);
    request.body = body.Dump();
    request.headers = BuildCloudMatchHeaders(input.token, clientId, deviceHashId, false);
    request.strictTls = false;

    const HttpResponse response = http_->Send(request);
    if (!response.ok()) {
        return Result<void>::Fail("Ad report failed (HTTP " + std::to_string(response.status) +
                                  ")");
    }
    return Result<void>::Ok();
}

}  // namespace opennow::core::gfn
