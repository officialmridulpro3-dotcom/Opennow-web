// The application: owns the platform, the services and the views, and runs the
// frame loop.
//
// Where the web client had one `App` React component that did everything, this
// splits the same responsibilities into services that own their own threads
// (auth, catalog, session, stream) plus a single-threaded UI that reads their
// results. Nothing in the UI blocks on the network, and nothing in the
// services touches ImGui.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "onow/app/AppState.h"
#include "onow/platform/Platform.h"
#include "onow/stream/StreamEngine.h"
#include "onow/ui/ImGuiLayer.h"

namespace onow {

namespace net {
class WebSocket;
}

namespace stream {
class StreamEngine;
}

namespace services {
class GfnClient;
class AuthService;
class CatalogService;
class SessionService;
class PlaytimeService;
} // namespace services

namespace app {

class App {
public:
  App();
  ~App();

  // Parses argv, initialises the platform and services. Returns false with
  // `error` filled when the app cannot start (no display, no GL, ...).
  bool init(int argc, char** argv, std::string& error);

  // Runs until the user quits. Returns the process exit code.
  int run();

  void request_quit() { quit_requested_ = true; }

  // ---------------------------------------------------------------- accessors
  Platform& platform() { return *platform_; }
  ImGuiLayer& ui() { return *ui_; }
  AppState& state() { return state_; }
  const AppState& state() const { return state_; }

  services::GfnClient& gfn() { return *gfn_; }
  services::AuthService& auth() { return *auth_service_; }
  services::CatalogService& catalog() { return *catalog_service_; }
  services::SessionService& session() { return *session_service_; }
  services::PlaytimeService& playtime() { return *playtime_service_; }
  stream::StreamEngine* stream_engine() { return stream_engine_.get(); }

  // ---------------------------------------------------------------- commands
  // These are the same handlers the React app wired to buttons and chords;
  // views call them, and so does the stream engine's callback thread.
  void start_device_login();
  void cancel_device_login();
  void logout();
  void switch_account(const std::string& user_id);

  void refresh_regions();
  void refresh_subscription();
  void load_catalog(bool force = false);
  void load_library();
  void mark_game_owned(const model::GameInfo& game, const std::string& variant_id);
  void toggle_favorite(const std::string& game_id);
  void buy_game(const model::GameInfo& game, const std::string& variant_id);

  void initiate_play(const model::GameInfo& game);
  void play_game(const model::GameInfo& game, const std::string& variant_id = "",
                 const std::string& streaming_base_url = "");
  void stop_stream();
  void resume_navbar_session();
  void terminate_navbar_session();

  void update_setting_bool(const std::string& key, bool value);
  void update_setting_int(const std::string& key, int value);
  void update_setting_float(const std::string& key, float value);
  void update_setting_string(const std::string& key, const std::string& value);

  void open_store_url(const std::string& url);

  // Helpers shared by the command implementations.
  std::string effective_streaming_base_url() const;
  Json build_stream_settings_json() const;
  // Normalizes a claimed session into the engine's SessionContext.
  stream::SessionContext build_session_context(const model::SessionInfo& session) const;
  // Creates the engine the settings ask for. Returns nullptr when the settings
  // name a transport this build cannot run.
  std::unique_ptr<stream::StreamEngine> create_stream_engine() const;

  // Diagnostics forwarded to the backend, batched like the web client.
  void client_log(const std::string& line);

  // Re-reads the accent / locale settings and applies them.
  void apply_accent();
  void apply_locale(const std::string& code);

  bool headless() const { return headless_; }

private:
  struct Options {
    bool headless = false;
    int max_frames = 0;
    bool self_test = false;
    std::string locale;
    std::string backend_url; // default http://127.0.0.1:3000
  };

  bool init_platform(std::string& error);
  bool init_services(std::string& error);
  void frame();
  void pump_services();
  void handle_shortcuts();
  void draw();
  // Rebuilds the engine when the transport setting changed, so a user can flip
  // nvst <-> webrtc without restarting.
  void ensure_stream_engine();
  void save_runtime_snapshot();
  void load_runtime_snapshot();
  void clear_runtime_snapshot();

  Options options_;
  std::unique_ptr<Platform> platform_;
  std::unique_ptr<ImGuiLayer> ui_;
  std::unique_ptr<services::GfnClient> gfn_;
  std::unique_ptr<services::AuthService> auth_service_;
  std::unique_ptr<services::CatalogService> catalog_service_;
  std::unique_ptr<services::SessionService> session_service_;
  std::unique_ptr<services::PlaytimeService> playtime_service_;
  std::unique_ptr<stream::StreamEngine> stream_engine_;
  std::string stream_engine_transport_;

  AppState state_;
  bool headless_ = false;
  bool quit_requested_ = false;
  bool services_ready_ = false;
  bool smoke_reported_ = false;

  // Diagnostics batching (the web client posted these once a second).
  std::vector<std::string> client_log_buffer_;
  int64_t client_log_flush_at_ms_ = 0;

  std::atomic<bool> stream_thread_busy_{false};
  mutable std::mutex region_mutex_;
};

} // namespace app
} // namespace onow
