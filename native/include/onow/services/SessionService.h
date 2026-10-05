// CloudMatch session lifecycle: create → poll (queue / setup) → claim → stop.
//
// This is the state machine the web client ran inside `handlePlayGame`. Keeping
// it here (rather than in a React callback) means the queue polling loop, the
// ad-aware poll interval, and the leftover-session cleanup are all testable
// without a UI.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "onow/model/Types.h"
#include "onow/Result.h"

namespace onow {
namespace services {

class GfnClient;

// Where a launch currently is. Drives the loading screen's stage text.
enum class LaunchStage { Idle, ResolvingAppId, Queue, Setup, Starting, Connecting, Streaming, Failed };

struct LaunchProgress {
  LaunchStage stage = LaunchStage::Idle;
  int queue_position = 0;
  bool queue_position_known = false;
  std::string diagnostic;
  int attempts = 0;
  bool in_queue = false;
  int seat_setup_step = 0;
};

class SessionService {
public:
  explicit SessionService(GfnClient& client);
  ~SessionService();

  // Runs the whole create → poll → claim sequence on a worker thread and
  // reports progress through `on_progress`. The final claimed session (or an
  // error) arrives on completion.
  void launch(const std::string& app_id, const std::string& internal_title,
              const std::string& streaming_base_url, const std::string& proxy_url,
              bool account_linked, bool supports_in_game_settings_persistence,
              const Json& stream_settings,
              const std::function<void(const LaunchProgress&)>& on_progress,
              const std::function<void(const Result<model::SessionInfo>&)>& on_done);

  // Stops the launch loop; the cloud session is left alone unless `stop_session`
  // is true.
  void abort(bool stop_session);

  Result<model::SessionInfo> claim(const std::string& session_id, const std::string& server_ip,
                                   const std::string& streaming_base_url,
                                   const std::string& app_id, int app_launch_mode,
                                   bool enable_persisting_in_game_settings,
                                   bool recovery_mode, const Json& stream_settings,
                                   const std::string& client_id = "",
                                   const std::string& device_id = "");

  Result<bool> stop(const model::SessionInfo& session);
  Result<bool> stop(const model::ActiveSessionInfo& session);
  Result<std::vector<model::ActiveSessionInfo>> active_sessions(
      const std::string& streaming_base_url);

  bool in_flight() const { return in_flight_.load(); }
  bool abort_requested() const { return abort_requested_.load(); }

  // The session this launch created, so a cancelled launch can be torn down
  // instead of orphaning a queued seat.
  std::optional<model::SessionInfo> launched_session();

private:
  void run_launch();

  GfnClient& client_;
  std::atomic<bool> in_flight_{false};
  std::atomic<bool> abort_requested_{false};
  std::thread worker_;
  std::mutex mutex_;

  struct LaunchRequest {
    std::string app_id;
    std::string internal_title;
    std::string streaming_base_url;
    std::string proxy_url;
    bool account_linked = false;
    bool supports_in_game_settings_persistence = false;
    Json stream_settings;
    std::function<void(const LaunchProgress&)> on_progress;
    std::function<void(const Result<model::SessionInfo>&)> on_done;
  };
  LaunchRequest request_;
  std::optional<model::SessionInfo> launched_;
};

} // namespace services
} // namespace onow
