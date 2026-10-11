// OpenNOW native client — Core unit tests.
//
// Runs on Linux CI (g++/clang++) and on Windows (MSVC) because the portable
// Core has no platform dependencies. The scripted HTTP client below is what
// lets the GFN protocol code be tested without touching NVIDIA's servers.

#include <cassert>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

#include "opennow/core/Hash.hpp"
#include "opennow/core/Http.hpp"
#include "opennow/core/Json.hpp"
#include "opennow/core/Logging.hpp"
#include "opennow/core/Paths.hpp"
#include "opennow/core/Settings.hpp"
#include "opennow/core/gfn/Auth.hpp"
#include "opennow/core/gfn/Catalog.hpp"
#include "opennow/core/gfn/CloudMatch.hpp"
#include "opennow/core/gfn/DeviceId.hpp"
#include "opennow/core/gfn/Signaling.hpp"
#include "opennow/core/gfn/Signaling.hpp"

using namespace opennow::core;
using namespace opennow::core::gfn;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::cout << "  FAIL: " << what << "\n";
    }
}

void Section(const std::string& name) { std::cout << "[" << name << "]\n"; }

// Records requests and replays scripted responses.
class ScriptedHttpClient final : public IHttpClient {
public:
    struct Call {
        std::string method;
        std::string url;
        std::string body;
    };

    std::vector<Call> calls;
    std::vector<HttpResponse> responses;

    HttpResponse Send(const HttpRequest& request) override {
        calls.push_back(Call{request.method, request.url, request.body});
        if (!responses.empty()) {
            HttpResponse response = responses.front();
            responses.erase(responses.begin());
            return response;
        }
        HttpResponse response;
        response.status = 200;
        response.body = fallbackBody.empty() ? "{}" : fallbackBody;
        return response;
    }

    std::string fallbackBody;
};

std::shared_ptr<ScriptedHttpClient> MakeClient() { return std::make_shared<ScriptedHttpClient>(); }

HttpResponse JsonResponse(int status, const std::string& body) {
    HttpResponse response;
    response.status = status;
    response.body = body;
    return response;
}

void TestJson() {
    Section("json");
    const json::ParseResult parsed =
        json::Value::Parse(R"({"a":[1,2,3],"b":{"c":"d"},"e":1.25,"f":null,"g":true})");
    Check(parsed.ok, "parse object");
    Check(parsed.value.Find("a")->Items().size() == 3, "array length");
    Check(parsed.value.Find("b")->Find("c")->AsString() == "d", "nested string");
    Check(parsed.value.Find("e")->AsDouble() == 1.25, "double");
    Check(parsed.value.Find("f")->isNull(), "null");
    Check(parsed.value.Find("g")->AsBool() == true, "bool");

    Check(!json::Value::Parse("{").ok, "reject truncated object");
    Check(!json::Value::Parse("[1,2,]").ok, "reject trailing comma");
    Check(!json::Value::Parse("01").ok, "reject leading zero");
    Check(!json::Value::Parse(R"({"a":01})").ok, "reject leading zero in object");

    // Round trip
    json::Value built = json::ObjectValue();
    built.Set("s", std::string("quote\" back\\ slash"));
    built.Set("i", static_cast<std::int64_t>(42));
    built.Set("arr", json::ArrayValue());
    const json::ParseResult again = json::Value::Parse(built.Dump());
    Check(again.ok && again.value == built, "round trip is stable");

    // UTF-8 including astral plane
    const json::ParseResult emoji = json::Value::Parse(R"({"t":"héllo 😀"})");
    Check(emoji.ok && emoji.value.Find("t")->AsString() == "héllo 😀", "utf8 + astral plane");

    // Numbers NVIDIA sends as strings must coerce.
    Check(json::Value::Parse("{\"n\":\"1234\"}").value.Find("n")->AsInt() == 1234,
          "numeric string coerces to int");
}

void TestHash() {
    Section("hash");
    Check(Md5("hello").Hex() == "5d41402abc4b2a76b9719d911017c592", "md5(hello)");
    Check(Sha256("abc").Hex() ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "sha256(abc)");
    Check(Base64UrlEncode("a") == "YQ", "base64url no padding");
    Check(Base64UrlDecodeToString("YQ") == "a", "base64url decode");
    Check(Base64Encode("OpenNOW") == "T3Blbk5PVw==", "base64 with padding");
}

void TestCloudMatch() {
    Section("cloudmatch");
    const Resolution resolution = ParseResolution("2560x1440");
    Check(resolution.width == 2560 && resolution.height == 1440, "parse resolution");
    Check(ParseResolution("nonsense").width == 1920, "resolution fallback");

    Check(ColorQualityBitDepth(ColorQuality::TenBit420) == 1, "10-bit depth");
    Check(ColorQualityBitDepth(ColorQuality::EightBit420) == 0, "8-bit depth");
    Check(ColorQualityChromaFormat(ColorQuality::TenBit444) == 1, "4:4:4 chroma");
    Check(CodecWireValue(VideoCodec::Av1) == 3, "AV1 wire value");
    Check(AppLaunchModeWireValue(AppLaunchMode::GamepadFriendly) == 2, "gamepad launch mode");

    SessionCreateInput input;
    input.appId = "12345";
    input.internalTitle = "Cyberpunk 2077";
    input.streamingBaseUrl = "https://prod.cloudmatchbeta.nvidiagrid.net/";
    input.settings.resolution = "1920x1080";
    input.settings.fps = 60;

    const json::Value body = CloudMatchService::BuildSessionRequestBody(input, "deadbeef", {});
    const json::Value* request = body.Find("sessionRequestData");
    Check(request != nullptr, "session body has sessionRequestData");
    Check(request->Find("appId")->AsString() == "12345", "appId on wire");
    Check(request->Find("clientVersion")->AsString() == "30.0", "client version");
    Check(request->Find("clientPlatformName")->AsString() == "windows", "platform name");
    Check(request->Find("userAge")->AsInt() == 26, "user age");

    const json::Value* monitors = request->Find("clientRequestMonitorSettings");
    Check(monitors != nullptr && monitors->Items().size() == 1, "one monitor");
    Check(monitors->Items()[0].Find("widthInPixels")->AsInt() == 1920, "monitor width");

    const json::Value* meta = request->Find("metaData");
    bool hasWebRtc = false;
    bool hasResolution = false;
    for (const json::Value& entry : meta->Items()) {
        if (entry.Find("key")->AsString() == "GSStreamerType") {
            hasWebRtc = entry.Find("value")->AsString() == "WebRTC";
        }
        if (entry.Find("key")->AsString() == "clientPhysicalResolution") {
            hasResolution = entry.Find("value")->AsString().find("\"horizontalPixels\":1920") !=
                            std::string::npos;
        }
    }
    Check(hasWebRtc, "WebRTC transport marker present");
    Check(hasResolution, "clientPhysicalResolution is a JSON string");

    // A native (NVST) request must NOT carry the WebRTC marker.
    SessionCreateInput native = input;
    native.settings.transport = StreamTransport::Nvst;
    const json::Value nativeBody =
        CloudMatchService::BuildSessionRequestBody(native, "deadbeef", {});
    const json::Value* nativeMeta =
        nativeBody.Find("sessionRequestData")->Find("metaData");
    bool leakedWebRtc = false;
    for (const json::Value& entry : nativeMeta->Items()) {
        if (entry.Find("key")->AsString() == "GSStreamerType") leakedWebRtc = true;
    }
    Check(!leakedWebRtc, "NVST request omits GSStreamerType");

    // Resume must not renegotiate streaming parameters.
    const json::Value claim =
        CloudMatchService::BuildClaimRequestBody("12345", input.settings, "deadbeef", std::nullopt);
    Check(claim.Find("action")->AsInt() == 2, "claim action 2");
    Check(claim.Find("data")->AsString() == "RESUME", "claim data RESUME");
    Check(claim.Find("sessionRequestData")->Find("appId")->AsInt() == 12345,
          "claim appId is numeric");
    Check(claim.Find("sessionRequestData")->Find("widthInPixels") == nullptr,
          "claim omits resolution");

    // Poll parsing
    const json::ParseResult poll = json::Value::Parse(R"({
      "requestStatus": {"statusCode": 1},
      "session": {
        "sessionId": "abc-123",
        "appId": 12345,
        "serverIp": "1.2.3.4",
        "sessionProgress": {"queuePosition": 7},
        "connectionInfo": [{"usage": 14, "ip": "1.2.3.4", "resourcePath": "rtsps://seat:443/x"}],
        "iceServers": [{"urls": ["stun:stun.example:3478"]}],
        "sdpOffer": "v=0..."
      }
    })");
    const SessionPollState state = CloudMatchService::ParseSessionResponse(poll.value);
    Check(state.valid, "poll parsed");
    Check(state.sessionId == "abc-123", "session id");
    Check(state.queuePosition.has_value() && *state.queuePosition == 7, "queue position");
    Check(state.sdpOffer.has_value(), "sdp offer present");
    Check(state.ready, "ready when SDP is present");
    Check(state.iceServers.size() == 1, "ice servers");
}

void TestSignaling() {
    Section("signaling");
    std::string host;
    Check(BuildSignalingUrl("rtsps://seat.example:443/nvst/", "1.2.3.4", &host) ==
              "wss://seat.example:443/nvst/",
          "rtsps resource path becomes wss");
    Check(host == "seat.example", "signaling host extracted from rtsps URL");
    Check(BuildSignalingUrl("/custom/", "9.9.9.9") == "wss://9.9.9.9:443/custom/",
          "path-only resource uses seat ip");
    Check(BuildSignalingUrl("", "9.9.9.9") == "wss://9.9.9.9:443/nvst/", "default /nvst/");

    const json::ParseResult payload = json::Value::Parse(R"({
      "session": {"serverIp": "5.6.7.8",
                  "connectionInfo": [{"usage": 14, "ip": "5.6.7.8",
                                      "resourcePath": "rtsps://5.6.7.8:443/nvst/"}]}
    })");
    const SignalingInfo info = ResolveSignaling(payload.value);
    Check(info.serverIp == "5.6.7.8", "resolved server ip");
    Check(info.signalingUrl == "wss://5.6.7.8:443/nvst/", "resolved signaling url");
    Check(info.signalingServer == "5.6.7.8:443", "signaling server host:port");

    Check(SignalingClient::PeerInfoMessage("peer-1").Dump() == R"({"peer_info":{"id":"peer-1"}})",
          "peer_info framing");
    Check(SignalingClient::IceMessage("candidate:1", "0", 0).Dump() ==
              R"({"peer_msg":{"msg":"candidate","candidate":"candidate:1","sdpMid":"0","sdpMLineIndex":0}})",
          "ice framing (field order preserved)");
}

void TestAuth() {
    Section("auth");
    auto http = MakeClient();
    AuthService auth(http);

    // Provider discovery, including the BPC rename and priority sort.
    http->responses.push_back(JsonResponse(200, R"({
      "gfnServiceInfo": {"gfnServiceEndpoints": [
        {"idpId": "second", "loginProviderCode": "BPC", "loginProviderDisplayName": "Pentanet",
         "streamingServiceUrl": "https://b.example", "loginProviderPriority": 5},
        {"idpId": "first", "loginProviderCode": "NVIDIA", "loginProviderDisplayName": "NVIDIA",
         "streamingServiceUrl": "https://a.example", "loginProviderPriority": 1}
      ]}
    })"));
    const Result<std::vector<LoginProvider>> providers = auth.FetchProviders();
    Check(providers.ok(), "providers fetched");
    Check((*providers).size() == 2, "two providers");
    Check((*providers)[0].idpId == "first", "sorted by priority");
    Check((*providers)[1].displayName == "bro.game", "BPC renamed to bro.game");
    Check((*providers)[0].streamingServiceUrl == "https://a.example/", "trailing slash appended");

    // Device authorization
    http->responses.push_back(JsonResponse(200, R"({
      "device_code": "dev-1", "user_code": "ABCD-EFGH",
      "verification_uri": "https://www.nvidia.com/device",
      "verification_uri_complete": "https://www.nvidia.com/device?code=ABCD-EFGH",
      "expires_in": 600, "interval": 5
    })"));
    const Result<DeviceLoginChallenge> challenge = auth.StartDeviceLogin((*providers)[0]);
    Check(challenge.ok(), "device login started");
    Check((*challenge).userCode == "ABCD-EFGH", "user code");
    Check((*challenge).intervalSeconds == 5, "poll interval");
    Check(!(*challenge).expired(), "challenge not expired");
    Check(http->calls.back().url == "https://login.nvidia.com/device/authorize",
          "device authorize endpoint");
    Check(http->calls.back().body.find("client_id=q61ddeJr") != std::string::npos,
          "uses the device-flow client id");

    // Pending poll
    http->responses.push_back(
        JsonResponse(400, R"({"error":"authorization_pending"})"));
    const DevicePollResult pending = auth.PollForTokens(*challenge);
    Check(pending.status == DevicePollStatus::Pending, "pending keeps polling");
    Check(pending.retryable(), "pending is retryable");

    // Successful poll
    http->responses.push_back(JsonResponse(200, R"({
      "access_token": "access-1", "refresh_token": "refresh-1",
      "id_token": "header.eyJzdWIiOiJ1c2VyLTEiLCJlbWFpbCI6InRAZS5jb20ifQ.sig",
      "expires_in": 3600
    })"));
    const DevicePollResult success = auth.PollForTokens(*challenge);
    Check(success.status == DevicePollStatus::Success, "poll succeeded");
    Check(success.tokens.accessToken == "access-1", "access token");
    Check(!success.tokens.expired(), "token not expired");

    // JWT claims survive a payload without padding.
    const std::optional<JwtClaims> claims =
        AuthService::ParseIdToken(success.tokens.idToken);
    Check(claims.has_value(), "id token parsed");
    Check(claims->subject == "user-1", "jwt subject");
    Check(claims->email == "t@e.com", "jwt email");

    // Device-id stability
    DeviceId::SetOverride("test-machine");
    const std::string first = DeviceId::Get();
    Check(first == DeviceId::Get(), "device id is stable");
    Check(first.size() == 32, "device id is 32 hex chars");
    DeviceId::ClearOverride();
}

void TestCatalog() {
    Section("catalog");
    auto http = MakeClient();
    CatalogService catalog(http);

    const json::ParseResult panels = json::Value::Parse(R"({
      "data": {"panels": [{"id": "p1", "name": "Main", "sections": [
        {"id": "s1", "title": "Featured", "items": [
          {"__typename": "FilterItem", "id": "f1"},
          {"__typename": "GameItem", "app": {
            "id": "1001", "title": "Test Game", "developerName": "Dev",
            "publisherName": "Pub", "genres": ["Action", "RPG"],
            "images": {"GAME_BOX_ART": "https://cdn/box-{width}.jpg",
                       "HERO_IMAGE": "https://cdn/hero.jpg"},
            "variants": [{"id": "v1", "appStore": "Steam", "supportedControls": ["Gamepad"],
                          "gfn": {"status": "OWNED", "library": {"status": "OWNED",
                                                                 "selected": true}}}]
          }}
        ]}
      ]}]}
    })");

    const std::vector<CatalogShelf> shelves = CatalogService::ShelvesFromPanels(panels.value);
    Check(shelves.size() == 1, "one shelf");
    Check(shelves[0].games.size() == 1, "filter items skipped, one game");
    const Game& game = shelves[0].games[0];
    Check(game.id == "1001", "game id");
    Check(game.title == "Test Game", "game title");
    Check(game.owned, "ownership detected from variant");
    Check(game.controllerSupport, "controller support detected");
    Check(game.platform == StorePlatform::Steam, "store platform");
    Check(game.posterUrl.find("900") != std::string::npos, "poster width substituted");
    Check(game.heroUrl.find("w=1200") != std::string::npos, "hero width query appended");
    Check(game.genres.size() == 2, "genres");

    // VPC id comes from serverInfo, and only trusted hosts are used.
    http->responses.push_back(JsonResponse(200, R"({"vpcId": "NP-AMS-08"})"));
    const Result<std::string> vpc = catalog.FetchVpcId("token", "https://evil.example/");
    Check(vpc.ok() && *vpc == "NP-AMS-08", "vpc id");
    Check(http->calls.back().url.find("prod.cloudmatchbeta.nvidiagrid.net") != std::string::npos,
          "untrusted provider url replaced with the default host");
}

void TestSettings() {
    Section("settings");
    const std::string testRoot = std::string(Paths::RootDirectory()) + "/settings-test";
    Paths::SetTestRoot(testRoot);

    AppSettings settings;
    settings.stream.resolution = "2560x1440";
    settings.stream.fps = 120;
    settings.stream.colorQuality = ColorQuality::TenBit444;
    settings.interface_.accent = AccentTheme::Lime;
    settings.interface_.locale = "de";
    settings.audio.volume = 55;
    settings.input.shortcuts.push_back(ShortcutBinding{"screenshot", "Ctrl+Shift+S"});

    Check(SettingsStore::Instance().Save(settings).ok(), "settings saved");
    const AppSettings reloaded = SettingsStore::Instance().Load();
    Check(reloaded.stream.resolution == "2560x1440", "resolution round trip");
    Check(reloaded.stream.fps == 120, "fps round trip");
    Check(reloaded.stream.colorQuality == ColorQuality::TenBit444, "color quality round trip");
    Check(reloaded.interface_.accent == AccentTheme::Lime, "accent round trip");
    Check(reloaded.audio.volume == 55, "volume round trip");
    Check(reloaded.input.shortcuts.size() == 1 &&
              reloaded.input.shortcuts[0].chord == "Ctrl+Shift+S",
          "shortcut round trip");

    SettingsStore::Instance().SavePlaytime({PlaytimeEntry{"1", "Game", 3600, 2, 1700000000}});
    const std::vector<PlaytimeEntry> playtime = SettingsStore::Instance().LoadPlaytime();
    Check(playtime.size() == 1 && playtime[0].totalSeconds == 3600, "playtime round trip");

    Paths::ClearTestRoot();
}

}  // namespace

int main() {
    TestJson();
    TestHash();
    TestCloudMatch();
    TestSignaling();
    TestAuth();
    TestCatalog();
    TestSettings();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    if (g_failures != 0) {
        std::cout << g_failures << " FAILED\n";
        return 1;
    }
    std::cout << "core: OK\n";
    return 0;
}
