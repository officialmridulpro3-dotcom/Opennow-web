#include "pch.h"

// OpenNOW native client — application services implementation.

#include "AppServices.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

#include <opennow/core/Hash.hpp>
#include <opennow/core/Logging.hpp>
#include <opennow/core/Paths.hpp>
#include <opennow/core/gfn/Constants.hpp>
#include <opennow/core/gfn/DeviceId.hpp>

namespace opennow::app {
namespace {

constexpr std::chrono::milliseconds kPollInterval{3000};
constexpr std::chrono::seconds kSessionCreateTimeout{180};

using Clock = std::chrono::steady_clock;

}  // namespace

std::wstring_view PhaseLabel(StreamPhase phase) {
    switch (phase) {
        case StreamPhase::Idle: return L"Idle";
        case StreamPhase::CreatingSession: return L"Requesting a seat";
        case StreamPhase::NetworkTest: return L"Measuring your connection";
        case StreamPhase::Queued: return L"Waiting for a free rig";
        case StreamPhase::Provisioning: return L"Starting your rig";
        case StreamPhase::Connecting: return L"Connecting";
        case StreamPhase::Streaming: return L"Streaming";
        case StreamPhase::AdPlaying: return L"Advertisement";
        case StreamPhase::Stopping: return L"Ending session";
        case StreamPhase::Failed: return L"Failed";
    }
    return L"Idle";
}

// ---- transport -------------------------------------------------------------------------------

namespace {

// Diagnostic transport used when libwebrtc is not linked. It drives the session
// lifecycle and emits a colour-bars test pattern so the DirectX pipeline, the
// HUD and the session controls can be validated without a real stream.
class DiagnosticTransport final : public IStreamTransport {
public:
    bool Start(const gfn::SignalingInfo&, const std::string&, const gfn::SessionPollState&) override {
        running_ = true;
        worker_ = std::thread([this] { Loop(); });
        return true;
    }

    void Stop() override {
        running_ = false;
        if (worker_.joinable()) worker_.join();
    }

    void SetFrameSink(std::function<void(VideoFrameData&&)> sink) override {
        frameSink_ = std::move(sink);
    }
    void SetStatsSink(std::function<void(const StreamStats&)> sink) override {
        statsSink_ = std::move(sink);
    }
    StreamStats CurrentStats() const override { return {}; }

private:
    void Loop() {
        const int width = 1280;
        const int height = 720;
        VideoFrameData frame;
        frame.width = width;
        frame.height = height;
        frame.lumaStride = width;
        frame.chromaStride = width;
        frame.luma.resize(static_cast<std::size_t>(width) * height);
        frame.chroma.resize(static_cast<std::size_t>(width) * height / 2);
        frame.fullRange = false;

        // SMPTE-style bars, generated on the CPU: enough to prove the NV12 path
        // and the colour conversion in the shader are correct.
        const std::uint8_t barLuma[8] = {235, 210, 170, 145, 105, 81, 41, 16};
        for (int y = 0; y < height && running_; ++y) {
            for (int x = 0; x < width; ++x) {
                frame.luma[static_cast<std::size_t>(y) * width + x] = barLuma[(x * 8) / width];
            }
        }
        std::fill(frame.chroma.begin(), frame.chroma.end(), 128);

        while (running_) {
            VideoFrameData copy = frame;
            if (frameSink_) frameSink_(std::move(copy));
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
    }

    std::atomic<bool> running_{false};
    std::thread worker_;
    std::function<void(VideoFrameData&&)> frameSink_;
    std::function<void(const StreamStats&)> statsSink_;
};

}  // namespace

std::unique_ptr<IStreamTransport> CreateStreamTransport() {
    return std::make_unique<DiagnosticTransport>();
}

// ---- stream controller --------------------------------------------------------------------------

StreamController::StreamController(core::HttpClientPtr http) : http_(std::move(http)) {
    cloudMatch_ = std::make_unique<gfn::CloudMatchService>(http_);
}

StreamController::~StreamController() { Stop(); }

bool StreamController::running() const { return running_.load(); }
StreamPhase StreamController::phase() const { return phase_.load(); }
StreamStats StreamController::stats() const {
    StreamStats stats;
    stats.fps = statsFps_.load();
    return stats;
}

void StreamController::Emit(StreamProgress progress) {
    phase_ = progress.phase;
    if (callbacks_.onProgress) callbacks_.onProgress(progress);
}

void StreamController::Fail(std::string message) {
    StreamProgress progress;
    progress.phase = StreamPhase::Failed;
    progress.detail = message;
    Emit(progress);
    if (callbacks_.onError) callbacks_.onError(message);
}

void StreamController::Start(const Game& game, const core::StreamSettings& settings,
                             const std::string& token, const std::string& streamingBaseUrl,
                             const std::string& zone, StreamCallbacks callbacks) {
    Stop();
    callbacks_ = std::move(callbacks);
    stopRequested_ = false;
    running_ = true;
    worker_ = std::thread([this, game, settings, token, streamingBaseUrl, zone] {
        Run(game, settings, token, streamingBaseUrl, zone);
    });
}

void StreamController::Stop() {
    stopRequested_ = true;
    if (transport_ != nullptr) transport_->Stop();
    if (worker_.joinable()) worker_.join();
    running_ = false;
    phase_ = StreamPhase::Idle;
}

void StreamController::Run(const Game& game, core::StreamSettings settings, std::string token,
                           std::string streamingBaseUrl, std::string zone) {
    const std::string deviceId = gfn::DeviceId::Get();
    const std::string clientId = core::Sha256(deviceId + token).HexPrefix(16);

    Emit({StreamPhase::CreatingSession, "Requesting a seat from " + zone, {}, {}, {}, {}});

    gfn::SessionCreateInput create;
    create.token = token;
    create.streamingBaseUrl = streamingBaseUrl;
    create.appId = game.id;
    create.internalTitle = game.internalTitle.empty() ? game.title : game.internalTitle;
    create.zone = zone;
    create.settings = settings;
    create.clientId = clientId;
    create.deviceId = deviceId;

    const core::Result<gfn::SessionPollState> created = cloudMatch_->CreateSession(create);
    if (!created.ok()) {
        Fail(created.error);
        return;
    }

    std::string sessionId = created->sessionId;
    std::string serverIp = created->serverIp;
    if (sessionId.empty()) {
        Fail("CloudMatch did not return a session id.");
        return;
    }
    core::LogInfo("Stream", "Session " + sessionId + " created.");

    // ---- poll until the seat is ready ------------------------------------------
    const auto deadline = Clock::now() + kSessionCreateTimeout;
    gfn::SessionPollState state = *created;
    while (!stopRequested_ && Clock::now() < deadline) {
        if (state.ready) break;

        StreamProgress progress;
        if (state.adsRequired) {
            progress.phase = StreamPhase::AdPlaying;
            progress.detail = "Your session starts after a short advertisement.";
            progress.adMediaUrl = state.adMediaUrl;
            progress.adDurationMs = state.adDurationMs;
        } else if (state.queuePosition.has_value()) {
            progress.phase = StreamPhase::Queued;
            progress.queuePosition = state.queuePosition;
            progress.estimatedWaitSeconds = state.estimatedWaitSeconds;
            progress.detail = "Position " + std::to_string(*state.queuePosition) + " in the queue.";
        } else {
            progress.phase = StreamPhase::Provisioning;
            progress.detail = "Starting your rig.";
        }
        Emit(progress);

        std::this_thread::sleep_for(kPollInterval);

        gfn::SessionPollInput poll;
        poll.token = token;
        poll.streamingBaseUrl = streamingBaseUrl;
        poll.zone = zone;
        poll.sessionId = sessionId;
        poll.appId = create.appId;
        poll.serverIp = serverIp;
        poll.clientId = clientId;
        poll.deviceId = deviceId;

        const core::Result<gfn::SessionPollState> polled = cloudMatch_->PollSession(poll);
        if (!polled.ok()) {
            // A single failed poll is normal while the seat is being allocated;
            // only give up when the deadline passes.
            core::LogWarn("Stream", "Poll failed: " + polled.error);
            continue;
        }
        state = *polled;
        if (serverIp.empty() && !state.serverIp.empty()) serverIp = state.serverIp;
        if (sessionId.empty()) sessionId = state.sessionId;
    }

    if (stopRequested_) {
        Emit({StreamPhase::Stopping, "Session cancelled.", {}, {}, {}, {}});
        running_ = false;
        return;
    }

    if (!state.ready) {
        // Stop the half-allocated seat so it does not count against the account.
        gfn::SessionPollInput poll;
        poll.token = token;
        poll.streamingBaseUrl = streamingBaseUrl;
        poll.zone = zone;
        poll.sessionId = sessionId;
        poll.appId = create.appId;
        poll.serverIp = serverIp;
        poll.clientId = clientId;
        poll.deviceId = deviceId;
        cloudMatch_->StopSession(poll);
        Fail("The rig did not become ready in time. Try a different region.");
        return;
    }

    // ---- connect the media transport ---------------------------------------------
    Emit({StreamPhase::Connecting, "Connecting to your rig.", {}, {}, {}, {}});

    const gfn::SignalingInfo signaling = gfn::ResolveSignaling(state.raw);
    transport_ = CreateStreamTransport();
    transport_->SetFrameSink([this](VideoFrameData&& frame) {
        if (callbacks_.onFrame) callbacks_.onFrame(std::move(frame));
    });
    transport_->SetStatsSink([this](const StreamStats& stats) {
        statsFps_ = stats.fps;
        if (callbacks_.onStats) callbacks_.onStats(stats);
    });

    if (!transport_->Start(signaling, sessionId, state)) {
        Fail("The stream transport could not be started.");
        return;
    }

    Emit({StreamPhase::Streaming, "Streaming " + game.title + ".", {}, {}, {}, {}});
    core::LogInfo("Stream", "Streaming session " + sessionId + ".");
}

// ---- app services -------------------------------------------------------------------------------

AppServices& AppServices::Instance() {
    static AppServices instance;
    return instance;
}

void AppServices::Initialize() {
    http_ = core::CreateHttpClient();
    auth_ = std::make_unique<gfn::AuthService>(http_);
    catalog_ = std::make_unique<gfn::CatalogService>(http_);
    cloudMatch_ = std::make_unique<gfn::CloudMatchService>(http_);
    subscriptionService_ = std::make_unique<gfn::SubscriptionService>(http_);
    stream_ = std::make_unique<StreamController>(http_);

    settings_ = core::SettingsStore::Instance().Load();
    playtime_ = core::SettingsStore::Instance().LoadPlaytime();
}

void AppServices::Shutdown() {
    if (stream_ != nullptr) stream_->Stop();
}

core::AppSettings& AppServices::settings() { return settings_; }

void AppServices::SaveSettings() { core::SettingsStore::Instance().Save(settings_); }

bool AppServices::signedIn() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return signedIn_;
}

core::UserProfile AppServices::profile() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return profile_;
}

core::AuthTokens AppServices::tokens() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tokens_;
}

core::Result<void> AppServices::RestoreSession() {
    const std::optional<core::AuthTokens> stored = core::SettingsStore::Instance().LoadCredentials();
    if (!stored) return core::Result<void>::Fail("No stored credentials.");

    core::AuthTokens refreshed = *stored;
    if (refreshed.needsRefresh(gfn::kTokenRefreshWindowSeconds)) {
        const core::Result<core::AuthTokens> result = auth_->Refresh(refreshed);
        if (!result.ok()) {
            core::SettingsStore::Instance().ClearCredentials();
            return core::Result<void>::Fail(result.error);
        }
        refreshed = *result;
        core::SettingsStore::Instance().SaveCredentials(refreshed);
    }

    const core::Result<core::AuthTokens> withClientToken = auth_->EnsureClientToken(refreshed);
    if (withClientToken.ok()) refreshed = *withClientToken;

    const core::Result<core::UserProfile> userProfile = auth_->FetchProfile(refreshed);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tokens_ = refreshed;
        signedIn_ = true;
        if (userProfile.ok()) profile_ = *userProfile;
    }
    core::SettingsStore::Instance().SaveCredentials(refreshed);
    return core::Result<void>::Ok();
}

void AppServices::SignOut() {
    core::SettingsStore::Instance().ClearCredentials();
    std::lock_guard<std::mutex> lock(mutex_);
    tokens_ = {};
    profile_ = {};
    signedIn_ = false;
    shelves_.clear();
    library_.clear();
}

std::vector<gfn::LoginProvider> AppServices::providers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return providers_;
}

core::Result<gfn::DeviceLoginChallenge> AppServices::StartDeviceLogin(std::size_t providerIndex) {
    const core::Result<std::vector<gfn::LoginProvider>> fetched = auth_->FetchProviders();
    if (!fetched.ok()) return core::Result<gfn::DeviceLoginChallenge>::Fail(fetched.error);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        providers_ = *fetched;
    }

    const std::size_t index = providerIndex < providers_.size() ? providerIndex : 0;
    const gfn::LoginProvider provider = providers_[index];
    streamingBaseUrl_ = provider.streamingServiceUrl;
    return auth_->StartDeviceLogin(provider);
}

gfn::DevicePollResult AppServices::PollForTokens(const gfn::DeviceLoginChallenge& challenge) {
    return auth_->PollForTokens(challenge);
}

core::Result<void> AppServices::CompleteSignIn(const core::AuthTokens& tokens) {
    core::AuthTokens working = tokens;
    const core::Result<core::AuthTokens> withClientToken = auth_->EnsureClientToken(working);
    if (withClientToken.ok()) working = *withClientToken;

    const core::Result<core::UserProfile> userProfile = auth_->FetchProfile(working);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tokens_ = working;
        signedIn_ = true;
        if (userProfile.ok()) profile_ = *userProfile;
    }
    core::SettingsStore::Instance().SaveCredentials(working);
    return core::Result<void>::Ok();
}

core::Result<void> AppServices::EnsureVpcId() {
    if (!vpcId_.empty()) return core::Result<void>::Ok();
    const core::Result<std::string> vpcId = catalog_->FetchVpcId(tokens_.accessToken,
                                                                 streamingBaseUrl_);
    if (!vpcId.ok()) return core::Result<void>::Fail(vpcId.error);
    vpcId_ = *vpcId;
    return core::Result<void>::Ok();
}

core::Result<std::vector<CatalogShelf>> AppServices::RefreshHome() {
    const core::Result<void> vpc = EnsureVpcId();
    if (!vpc.ok()) return core::Result<std::vector<CatalogShelf>>::Fail(vpc.error);

    const core::Result<std::vector<CatalogShelf>> shelves =
        catalog_->FetchHome(tokens_.accessToken, vpcId_, settings_.stream.gameLanguage);
    if (!shelves.ok()) return shelves;

    std::lock_guard<std::mutex> lock(mutex_);
    shelves_ = *shelves;
    return shelves;
}

core::Result<std::vector<Game>> AppServices::RefreshLibrary() {
    const core::Result<void> vpc = EnsureVpcId();
    if (!vpc.ok()) return core::Result<std::vector<Game>>::Fail(vpc.error);

    const core::Result<std::vector<Game>> games =
        catalog_->FetchLibrary(tokens_.accessToken, vpcId_, settings_.stream.gameLanguage);
    if (!games.ok()) return games;

    std::lock_guard<std::mutex> lock(mutex_);
    library_ = *games;
    return games;
}

core::Result<std::vector<Game>> AppServices::Search(const std::string& query) {
    const core::Result<void> vpc = EnsureVpcId();
    if (!vpc.ok()) return core::Result<std::vector<Game>>::Fail(vpc.error);

    const core::Result<gfn::CatalogPage> page =
        catalog_->BrowseApps(tokens_.accessToken, vpcId_, query, "relevance", {}, 60);
    if (!page.ok()) return core::Result<std::vector<Game>>::Fail(page.error);
    return core::Result<std::vector<Game>>::Ok(page->games);
}

core::Result<Game> AppServices::LoadGameDetails(const std::string& id) {
    const core::Result<void> vpc = EnsureVpcId();
    if (!vpc.ok()) return core::Result<Game>::Fail(vpc.error);
    return catalog_->FetchApp(tokens_.accessToken, id, vpcId_, settings_.stream.gameLanguage);
}

std::vector<CatalogShelf> AppServices::shelves() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return shelves_;
}

std::vector<Game> AppServices::library() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return library_;
}

core::Result<core::Subscription> AppServices::RefreshSubscription() {
    const core::Result<core::Subscription> result =
        subscriptionService_->FetchSubscription(tokens_.accessToken, profile_.userId, vpcId_);
    if (!result.ok()) return result;
    std::lock_guard<std::mutex> lock(mutex_);
    subscription_ = *result;
    return result;
}

core::Subscription AppServices::subscription() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscription_;
}

core::Result<std::vector<core::Region>> AppServices::RefreshRegions() {
    const core::Result<std::vector<core::Region>> result =
        subscriptionService_->FetchRegions(tokens_.accessToken, streamingBaseUrl_);
    if (!result.ok()) return result;
    std::lock_guard<std::mutex> lock(mutex_);
    regions_ = *result;
    return result;
}

std::vector<core::Region> AppServices::regions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return regions_;
}

std::optional<int> AppServices::LatencyFor(const core::Region& region) const {
    const core::Result<int> latency = subscriptionService_->MeasureLatency(
        region.streamingBaseUrl);
    if (!latency.ok()) return std::nullopt;
    return *latency;
}

void AppServices::SetPreferredRegion(const std::string& streamingBaseUrl) {
    std::lock_guard<std::mutex> lock(mutex_);
    preferredRegionUrl_ = streamingBaseUrl;
}

std::string AppServices::preferredRegionUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return preferredRegionUrl_;
}

std::string AppServices::streamingBaseUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return streamingBaseUrl_.empty() ? std::string(gfn::kDefaultStreamingServiceUrl)
                                     : streamingBaseUrl_;
}

void AppServices::MeasureRegionLatency(std::function<void(const core::Region&)> onMeasured) {
    const std::vector<core::Region> snapshot = regions();
    std::vector<std::thread> workers;
    workers.reserve(snapshot.size());

    // One bounded probe per region, in parallel: the web client measures from
    // the browser for the same reason — a serial probe would take a minute.
    for (const core::Region& region : snapshot) {
        workers.emplace_back([this, region, onMeasured] {
            const core::Result<int> latency =
                subscriptionService_->MeasureLatency(region.streamingBaseUrl);
            core::Region measured = region;
            measured.latencyMs = latency.ok() ? std::optional<int>(*latency) : std::nullopt;
            measured.available = latency.ok();
            if (onMeasured) onMeasured(measured);
        });
    }
    for (std::thread& worker : workers) {
        if (worker.joinable()) worker.detach();
    }
}

std::vector<core::PlaytimeEntry> AppServices::playtime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return playtime_;
}

void AppServices::RecordSession(const std::string& gameId, const std::string& title,
                                std::int64_t seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto existing = std::find_if(playtime_.begin(), playtime_.end(),
                                 [&](const core::PlaytimeEntry& entry) {
                                     return entry.gameId == gameId;
                                 });
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    if (existing != playtime_.end()) {
        existing->totalSeconds += seconds;
        existing->sessions += 1;
        existing->lastPlayedAt = now;
    } else {
        core::PlaytimeEntry entry;
        entry.gameId = gameId;
        entry.title = title;
        entry.totalSeconds = seconds;
        entry.sessions = 1;
        entry.lastPlayedAt = now;
        playtime_.push_back(std::move(entry));
    }
    core::SettingsStore::Instance().SavePlaytime(playtime_);
}

StreamController& AppServices::stream() { return *stream_; }

}  // namespace opennow::app
