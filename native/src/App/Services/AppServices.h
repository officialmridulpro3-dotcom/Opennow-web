// OpenNOW native client — application services.
//
// Plain C++ (no WinRT projection) so the UI can call it from background
// coroutines and so the same code can be exercised from tests. WinUI pages wrap
// every call in `co_await winrt::resume_background()` and marshal results back
// through the window's DispatcherQueue.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <opennow/core/Http.hpp>
#include <opennow/core/Result.hpp>
#include <opennow/core/Settings.hpp>
#include <opennow/core/Types.hpp>
#include <opennow/core/gfn/Auth.hpp>
#include <opennow/core/gfn/Catalog.hpp>
#include <opennow/core/gfn/CloudMatch.hpp>
#include <opennow/core/gfn/DeviceId.hpp>
#include <opennow/core/gfn/Signaling.hpp>
#include <opennow/core/gfn/Subscription.hpp>

namespace opennow::app {

// The domain model lives in Core; re-export the handful of types the UI layer
// touches on every line so the call sites stay readable.
using core::CatalogShelf;
using core::Game;
using core::StreamStats;
using core::Subscription;


// Progress shown on the stream loading screen and in the status bar.
enum class StreamPhase {
    Idle,
    CreatingSession,
    NetworkTest,
    Queued,
    Provisioning,
    Connecting,
    Streaming,
    AdPlaying,
    Stopping,
    Failed,
};

std::wstring_view PhaseLabel(StreamPhase phase);

struct StreamProgress {
    StreamPhase phase = StreamPhase::Idle;
    std::string detail;                     // human-readable line for the UI
    std::optional<int> queuePosition;
    std::optional<int> estimatedWaitSeconds;
    std::optional<std::string> adMediaUrl;
    std::optional<int> adDurationMs;
};

// A decoded video frame handed to the DirectX surface.
struct VideoFrameData {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> luma;
    std::vector<std::uint8_t> chroma;
    int lumaStride = 0;
    int chromaStride = 0;
    bool fullRange = true;
};

struct StreamCallbacks {
    std::function<void(const StreamProgress&)> onProgress;
    std::function<void(VideoFrameData&&)> onFrame;
    std::function<void(const StreamStats&)> onStats;
    std::function<void(const std::string&)> onError;
};

// ---- transport ---------------------------------------------------------------------
// The wire protocol (CloudMatch + signaling) is fully ported. The media
// transport is pluggable: libwebrtc is the production implementation and is
// linked through vcpkg (see BUILD.md); until it is present the client builds
// with the diagnostic transport, which still completes a session so the queue,
// signaling and UI paths can be exercised end to end.

class IStreamTransport {
public:
    virtual ~IStreamTransport() = default;
    virtual bool Start(const gfn::SignalingInfo& signaling, const std::string& sessionId,
                       const gfn::SessionPollState& seat) = 0;
    virtual void Stop() = 0;
    virtual void SetFrameSink(std::function<void(VideoFrameData&&)> sink) = 0;
    virtual void SetStatsSink(std::function<void(const StreamStats&)> sink) = 0;
    virtual StreamStats CurrentStats() const = 0;
};

std::unique_ptr<IStreamTransport> CreateStreamTransport();

// ---- session controller ---------------------------------------------------------------

class StreamController {
public:
    explicit StreamController(core::HttpClientPtr http);
    ~StreamController();

    void Start(const Game& game, const core::StreamSettings& settings,
               const std::string& token, const std::string& streamingBaseUrl,
               const std::string& zone, StreamCallbacks callbacks);
    void Stop();
    bool running() const;
    StreamPhase phase() const;
    StreamStats stats() const;

private:
    void Run(const Game& game, core::StreamSettings settings, std::string token,
             std::string streamingBaseUrl, std::string zone);
    void Emit(StreamProgress progress);
    void Fail(std::string message);

    core::HttpClientPtr http_;
    std::unique_ptr<gfn::CloudMatchService> cloudMatch_;
    std::unique_ptr<IStreamTransport> transport_;
    StreamCallbacks callbacks_;
    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};
    std::atomic<StreamPhase> phase_{StreamPhase::Idle};
    std::atomic<int> statsFps_{0};
    mutable std::mutex mutex_;
};

// ---- app services -----------------------------------------------------------------------

class AppServices {
public:
    static AppServices& Instance();

    void Initialize();
    void Shutdown();

    // ---- settings ------------------------------------------------------------------
    core::AppSettings& settings();
    void SaveSettings();

    // ---- session ----------------------------------------------------------------------
    bool signedIn() const;
    core::UserProfile profile() const;
    core::AuthTokens tokens() const;
    // Loads stored credentials, refreshing them when they are near expiry.
    core::Result<void> RestoreSession();
    void SignOut();

    // ---- sign-in ------------------------------------------------------------------------
    std::vector<gfn::LoginProvider> providers() const;
    core::Result<gfn::DeviceLoginChallenge> StartDeviceLogin(std::size_t providerIndex = 0);
    gfn::DevicePollResult PollForTokens(const gfn::DeviceLoginChallenge& challenge);
    core::Result<void> CompleteSignIn(const core::AuthTokens& tokens);

    // ---- catalog -----------------------------------------------------------------------
    core::Result<std::vector<CatalogShelf>> RefreshHome();
    core::Result<std::vector<Game>> RefreshLibrary();
    core::Result<std::vector<Game>> Search(const std::string& query);
    core::Result<Game> LoadGameDetails(const std::string& id);
    std::vector<CatalogShelf> shelves() const;
    std::vector<Game> library() const;

    // ---- account -------------------------------------------------------------------------
    core::Result<core::Subscription> RefreshSubscription();
    core::Subscription subscription() const;
    core::Result<std::vector<core::Region>> RefreshRegions();
    std::vector<core::Region> regions() const;
    // Bounded latency probe for one region; nullopt when it does not answer.
    std::optional<int> LatencyFor(const core::Region& region) const;

    // The region chosen on the details page, used by the next stream start.
    void SetPreferredRegion(const std::string& streamingBaseUrl);
    std::string preferredRegionUrl() const;
    std::string streamingBaseUrl() const;
    // Measures every region in parallel; reports through the callback.
    void MeasureRegionLatency(std::function<void(const core::Region&)> onMeasured);

    // ---- playtime ----------------------------------------------------------------------------
    std::vector<core::PlaytimeEntry> playtime() const;
    void RecordSession(const std::string& gameId, const std::string& title,
                       std::int64_t seconds);

    // ---- streaming ---------------------------------------------------------------------------
    StreamController& stream();

private:
    AppServices() = default;

    core::Result<void> EnsureVpcId();

    core::HttpClientPtr http_;
    std::unique_ptr<gfn::AuthService> auth_;
    std::unique_ptr<gfn::CatalogService> catalog_;
    std::unique_ptr<gfn::CloudMatchService> cloudMatch_;
    std::unique_ptr<gfn::SubscriptionService> subscriptionService_;
    std::unique_ptr<StreamController> stream_;

    core::AppSettings settings_;
    core::AuthTokens tokens_;
    core::UserProfile profile_;
    core::Subscription subscription_;
    std::vector<gfn::LoginProvider> providers_;
    std::vector<CatalogShelf> shelves_;
    std::vector<Game> library_;
    std::vector<core::Region> regions_;
    std::vector<core::PlaytimeEntry> playtime_;
    std::string vpcId_;
    std::string streamingBaseUrl_;
    bool signedIn_ = false;

    mutable std::mutex mutex_;
};

}  // namespace opennow::app
