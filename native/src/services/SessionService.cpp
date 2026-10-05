#include "onow/services/GfnClient.h"
#include "onow/services/SessionService.h"

#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/Time.h"

#include <algorithm>

namespace onow {
namespace services {

SessionService::SessionService(GfnClient& client) : client_(client) {}

SessionService::~SessionService() {
  abort_requested_.store(true);
  if (worker_.joinable()) worker_.join();
}

void SessionService::abort(bool stop_session) {
  abort_requested_.store(true);
  if (stop_session) {
    std::optional<model::SessionInfo> session = launched_session();
    if (session) stop(*session);
  }
}

std::optional<model::SessionInfo> SessionService::launched_session() {
  std::lock_guard<std::mutex> lock(mutex_);
  return launched_;
}

void SessionService::launch(const std::string& app_id, const std::string& internal_title,
                            const std::string& streaming_base_url, const std::string& proxy_url,
                            const bool account_linked,
                            const bool supports_in_game_settings_persistence,
                            const Json& stream_settings,
                            const std::function<void(const LaunchProgress&)>& on_progress,
                            const std::function<void(const Result<model::SessionInfo>&)>& on_done) {
  if (in_flight_.exchange(true)) return;
  if (worker_.joinable()) worker_.join();
  abort_requested_.store(false);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    launched_.reset();
    request_ = LaunchRequest();
    request_.app_id = app_id;
    request_.internal_title = internal_title;
    request_.streaming_base_url = streaming_base_url;
    request_.proxy_url = proxy_url;
    request_.account_linked = account_linked;
    request_.supports_in_game_settings_persistence = supports_in_game_settings_persistence;
    request_.stream_settings = stream_settings;
    request_.on_progress = on_progress;
    request_.on_done = on_done;
  }
  worker_ = std::thread([this] { run_launch(); });
}

void SessionService::run_launch() {
  LaunchRequest request;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    request = request_;
  }

  LaunchProgress progress;
  auto report = [&](LaunchStage stage) {
    progress.stage = stage;
    if (request.on_progress) request.on_progress(progress);
  };

  // --- stop leftover queued sessions ---------------------------------------
  // NVIDIA enforces one session per account; an orphaned queued seat gets the
  // new one torn down mid-queue (404 INVALID_SESSION_ID_NOT_FOUND).
  {
    Result<std::vector<model::ActiveSessionInfo>> active =
        client_.active_sessions();
    if (active.ok()) {
      for (const model::ActiveSessionInfo& entry : active.value()) {
        if (entry.status != 1) continue;
        if (abort_requested_.load()) break;
        StopSessionInput stop;
        stop.session_id = entry.session_id;
        stop.server_ip = entry.server_ip;
        stop.zone = "prod";
        stop.streaming_base_url = entry.streaming_base_url.empty() ? request.streaming_base_url
                                                                   : entry.streaming_base_url;
        client_.stop_session(stop);
        progress.diagnostic = "stopped leftover queued session " + entry.session_id;
        report(LaunchStage::Setup);
      }
    }
  }

  // --- create --------------------------------------------------------------
  CreateSessionInput create;
  create.app_id = request.app_id;
  create.internal_title = request.internal_title;
  create.account_linked = request.account_linked;
  create.supports_in_game_settings_persistence = request.supports_in_game_settings_persistence;
  create.enable_persisting_in_game_settings = false;
  create.zone = "prod";
  create.streaming_base_url = request.streaming_base_url;
  create.proxy_url = request.proxy_url;
  create.settings = request.stream_settings;

  Result<model::SessionInfo> created = client_.create_session(create);
  if (!created.ok()) {
    in_flight_.store(false);
    if (request.on_done) request.on_done(created.error());
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    launched_ = created.value();
  }
  progress.queue_position = created->queue_position;
  progress.queue_position_known = created->queue_position > 0;
  progress.in_queue = model::is_session_in_queue(created.value());
  report(progress.in_queue ? LaunchStage::Queue : LaunchStage::Setup);

  // --- poll ----------------------------------------------------------------
  model::SessionInfo latest = created.value();
  model::SessionInfo final_session;
  bool ready = false;
  const int kMaxAttempts = 1200; // ~40 minutes at the ad-poll interval
  int stuck_polls = 0;
  for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
    if (abort_requested_.load()) {
      in_flight_.store(false);
      if (request.on_done) request.on_done(Error::fail("Launch cancelled"));
      return;
    }
    progress.attempts = attempt;

    // Queue-ad sessions poll on the slower interval; the web client slept in
    // small ticks so a cancel or an ad completion reacted immediately.
    const bool ads_required = model::is_session_ads_required(latest.ad_state);
    const int interval_ms = ads_required ? 30000 : 2000;
    int slept = 0;
    while (slept < interval_ms) {
      const int tick = std::min(500, interval_ms - slept);
      onow::sleep_ms(tick);
      slept += tick;
      if (abort_requested_.load()) {
        in_flight_.store(false);
        if (request.on_done) request.on_done(Error::fail("Launch cancelled"));
        return;
      }
    }

    PollSessionInput poll;
    poll.session_id = latest.session_id;
    poll.server_ip = latest.server_ip.empty() ? created->server_ip : latest.server_ip;
    poll.zone = latest.zone.empty() ? "prod" : latest.zone;
    poll.streaming_base_url = latest.streaming_base_url.empty() ? request.streaming_base_url
                                                                : latest.streaming_base_url;
    poll.client_id = latest.client_id;
    poll.device_id = latest.device_id;
    poll.proxy_url = request.proxy_url;

    // Tolerate transient poll failures with a bounded retry instead of failing
    // the whole launch: a network blip is not a dead session.
    Result<model::SessionInfo> polled = Error::fail("poll not attempted");
    for (int retry = 0; retry < 3; ++retry) {
      polled = client_.poll_session(poll);
      if (polled.ok()) break;
      onow::sleep_ms(1500);
      if (abort_requested_.load()) {
        in_flight_.store(false);
        if (request.on_done) request.on_done(Error::fail("Launch cancelled"));
        return;
      }
    }
    if (!polled.ok()) {
      in_flight_.store(false);
      if (request.on_done) request.on_done(polled.error());
      return;
    }

    // Merge: keep the newest values for fields the poll response omits.
    model::SessionInfo merged = polled.value();
    if (merged.server_ip.empty()) merged.server_ip = latest.server_ip;
    if (merged.streaming_base_url.empty()) merged.streaming_base_url = latest.streaming_base_url;
    if (merged.signaling_url.empty()) merged.signaling_url = latest.signaling_url;
    if (merged.signaling_server.empty()) merged.signaling_server = latest.signaling_server;
    if (merged.client_id.empty()) merged.client_id = latest.client_id;
    if (merged.device_id.empty()) merged.device_id = latest.device_id;
    if (!model::is_session_ads_required(merged.ad_state) &&
        model::is_session_ads_required(latest.ad_state)) {
      merged.ad_state = latest.ad_state;
    }
    latest = merged;

    progress.queue_position = latest.queue_position;
    progress.queue_position_known = latest.queue_position > 0;
    progress.seat_setup_step = latest.seat_setup_step;
    progress.in_queue = model::is_session_in_queue(latest);
    progress.diagnostic = "poll #" + to_string(attempt) + " · seat status " +
                          to_string(latest.status) + " · setup step " +
                          to_string(latest.seat_setup_step) + " · queue " +
                          (latest.queue_position > 0 ? to_string(latest.queue_position)
                                                     : std::string("n/a")) +
                          " · endpoints " + std::to_string(latest.rtsps_endpoints.size());
    report(progress.in_queue ? LaunchStage::Queue : LaunchStage::Setup);

    if (model::is_session_ready_for_connect(latest.status)) {
      final_session = latest;
      ready = true;
      break;
    }

    // Seats that never leave setup: warn and keep waiting rather than
    // abandoning the queue, which is what the reference client does.
    if (!progress.in_queue) {
      ++stuck_polls;
      if (stuck_polls > 150) {
        progress.diagnostic = "poll #" + to_string(attempt) + " · still in setup (" +
                              to_string(stuck_polls) + " polls) · waiting…";
        report(LaunchStage::Setup);
      }
    } else {
      stuck_polls = 0;
    }
  }

  if (!ready) {
    in_flight_.store(false);
    if (request.on_done) {
      request.on_done(Error::fail("The session never became ready. Try another server."));
    }
    return;
  }

  in_flight_.store(false);
  if (request.on_done) request.on_done(final_session);
}

Result<model::SessionInfo> SessionService::claim(
    const std::string& session_id, const std::string& server_ip,
    const std::string& streaming_base_url, const std::string& app_id, const int app_launch_mode,
    const bool enable_persisting_in_game_settings, const bool recovery_mode,
    const Json& stream_settings, const std::string& client_id, const std::string& device_id) {
  ClaimSessionInput input;
  input.session_id = session_id;
  input.server_ip = server_ip;
  input.streaming_base_url = streaming_base_url;
  input.client_id = client_id;
  input.device_id = device_id;
  input.app_id = app_id;
  input.app_launch_mode = app_launch_mode;
  input.enable_persisting_in_game_settings = enable_persisting_in_game_settings;
  input.recovery_mode = recovery_mode;
  input.settings = stream_settings;
  return client_.claim_session(input);
}

Result<bool> SessionService::stop(const model::SessionInfo& session) {
  StopSessionInput input;
  input.session_id = session.session_id;
  input.server_ip = session.server_ip;
  input.zone = session.zone.empty() ? "prod" : session.zone;
  input.streaming_base_url = session.streaming_base_url;
  input.client_id = session.client_id;
  input.device_id = session.device_id;
  return client_.stop_session(input);
}

Result<bool> SessionService::stop(const model::ActiveSessionInfo& session) {
  StopSessionInput input;
  input.session_id = session.session_id;
  input.server_ip = session.server_ip;
  input.zone = "prod";
  input.streaming_base_url = session.streaming_base_url;
  return client_.stop_session(input);
}

Result<std::vector<model::ActiveSessionInfo>> SessionService::active_sessions(
    const std::string& streaming_base_url) {
  (void)streaming_base_url;
  return client_.active_sessions();
}

} // namespace services
} // namespace onow
