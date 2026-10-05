// The whole application state, in one place.
//
// The web client spread this across forty-odd `useState` hooks inside a single
// 4,800-line App.tsx. Keeping it in a plain struct has two advantages the React
// version could not have: the state machine is inspectable from a unit test
// without rendering anything, and the stream engine can drive it from its own
// thread with a single lock.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "onow/Json.h"
#include "onow/app/Settings.h"
#include "onow/model/Types.h"
#include "onow/ui/Theme.h"
#include "onow/ui/Widgets.h"

namespace onow {
namespace app {

// ------------------------------------------------------------------- views --

enum class View {
  Loading,
  Login,
  Home,
  Library,
  GameDetails,
  Playtime,
  Settings,
  Stream,
};

enum class SettingsSection {
  Account,
  NativeStreamer,
  Audio,
  Game,
  Input,
  Interface,
  Stream,
  About,
  Thanks,
};

// The launch lifecycle. Mirrors the web client's `StreamStatus` union.
enum class StreamStatus {
  Idle,
  Queue,
  Setup,
  Starting,
  Connecting,
  Streaming,
};

struct LaunchErrorState {
  bool active = false;
  std::string title;
  std::string description;
  std::string code_label;
  std::string action_label;
  std::string action; // "" | "persistent-storage-settings"
  StreamStatus stage = StreamStatus::Queue;
};

struct StreamWarningState {
  std::string code;
  std::string message;
  std::string tone; // info | warning | critical
  int seconds_left = 0;
};

// --------------------------------------------------------------- diagnostics --

struct StreamDiagnostics {
  bool active = false;
  std::string codec = "H264";
  std::string resolution;
  int decoded_fps = 0;
  int render_fps = 0;
  double bitrate_kbps = 0.0;
  double target_bitrate_kbps = 0.0;
  int rtt_ms = 0;
  double packet_loss_percent = 0.0;
  long long frames_decoded = 0;
  long long frames_dropped = 0;
  std::string hardware_acceleration;
  bool zero_copy = false;
  std::string queue_mode;
  std::string transport; // "webrtc" | "nvst"
  int64_t updated_at_ms = 0;
};

// ------------------------------------------------------------------- state --

// One completed stream segment. The web client's Playtime page showed a
// timeline of recent sessions, so the ledger keeps them.
struct PlaytimeEvent {
  int64_t started_at_ms = 0;
  long long duration_ms = 0;
  std::string region;
  std::string client_mode; // "web" | "native"
};

struct PlaytimeRecord {
  std::string game_id;
  std::string game_title;
  long long total_ms = 0;
  int sessions = 0;
  int64_t last_played_ms = 0;
  long long longest_session_ms = 0;
  long long queue_ms = 0;
  std::vector<PlaytimeEvent> events;
};

// -------------------------------------------------------------- dialogs --

struct ModalOption {
  std::string id;
  std::string label;
  std::function<void()> action;
};

// Generic confirmation dialog (stop stream, reset settings, ...).
struct ModalState {
  bool open = false;
  std::string title;
  std::string body;
  std::vector<ModalOption> options;
};

// A CloudMatch server offered by the free-tier picker.
struct QueueServer {
  std::string id;
  std::string label;
  std::string url;
  int latency_ms = -1;
};

struct PlaytimeLedger {
  std::vector<PlaytimeRecord> records;
  bool recording = false;
  std::string recording_game_title;
  int64_t stream_started_at_ms = 0;
  int64_t queue_started_at_ms = 0;
};

struct ToastMessage {
  ToastKind kind = ToastKind::Info;
  std::string title;
  std::string detail;
  int64_t at_ms = 0;
  int64_t ttl_ms = 4000;
};

struct QueueAdRuntimeState {
  bool has_ad = false;
  model::SessionAdInfo ad;
  std::string media_url;
  int watched_ms = 0;
  bool paused = false;
};

struct RuntimeSnapshot {
  bool valid = false;
  int64_t updated_at_ms = 0;
  StreamStatus stream_status = StreamStatus::Idle;
  std::string session_id;
  int session_app_id = 0;
  std::string streaming_game_id;
  std::string streaming_store;
  // Enough of the session to re-attach after a crash.
  std::string resume_session_id;
  std::string resume_server_ip;
  std::string resume_streaming_base_url;
  std::string resume_signaling_url;
  int resume_app_id = 0;
  int resume_app_launch_mode = 0;
  bool resume_enable_persisting_in_game_settings = false;
};

class AppState {
public:
  // ---- navigation -----------------------------------------------------------
  View view = View::Loading;
  View page_before_settings = View::Home;
  SettingsSection settings_section = SettingsSection::Account;
  bool settings_focus_account = false;

  // ---- auth -----------------------------------------------------------------
  bool auth_initializing = true;
  std::string startup_status_message;
  std::optional<model::AuthSession> auth_session;
  std::vector<model::LoginProvider> providers;
  std::string selected_provider_id;
  bool device_login_pending = false;
  std::optional<model::DeviceLoginChallenge> device_challenge;
  int64_t device_challenge_poll_at_ms = 0;
  std::string login_error;
  bool logging_in = false;
  bool show_qr = false;

  // ---- catalog --------------------------------------------------------------
  std::vector<model::GameInfo> all_games;
  std::vector<model::GameInfo> library_games;
  std::vector<model::GameInfo> featured_games;
  std::vector<model::GameInfo> store_panel_games;
  std::vector<model::GameInfo> filtered_games;
  std::vector<model::GameInfo> filtered_library_games;
  std::map<std::string, std::string> variant_by_game_id;
  std::vector<model::CatalogFilterGroup> catalog_filter_groups;
  std::vector<model::CatalogSortOption> catalog_sort_options;
  std::string catalog_sort_id = "relevance";
  std::vector<std::string> catalog_selected_filter_ids;
  int catalog_total_count = 0;
  int catalog_supported_count = 0;
  std::string search_query;
  std::string selected_game_id;
  std::optional<model::GameInfo> details_game;
  std::string details_variant_id;
  bool loading_catalog = false;
  bool loading_library = false;
  std::string catalog_notice;
  std::string catalog_notice_tone;
  int64_t catalog_notice_at_ms = 0;
  int64_t catalog_snapshot_at_ms = 0;
  std::map<std::string, bool> mark_owned_in_flight;

  // ---- regions / subscription ----------------------------------------------
  std::vector<model::StreamRegion> regions;
  bool regions_loading = false;
  std::optional<model::SubscriptionInfo> subscription;
  bool subscription_loading = false;

  // ---- session --------------------------------------------------------------
  std::optional<model::SessionInfo> session;
  std::optional<model::ActiveSessionInfo> navbar_active_session;
  bool navbar_session_resuming = false;
  bool navbar_session_terminating = false;
  StreamStatus stream_status = StreamStatus::Idle;
  int queue_position = 0;
  int queue_position_known = false;
  std::string launch_poll_diagnostic;
  LaunchErrorState launch_error;
  StreamWarningState stream_warning;
  std::optional<model::GameInfo> streaming_game;
  std::string streaming_store;
  int64_t session_started_at_ms = 0;
  bool launch_in_flight = false;
  bool launch_abort_requested = false;
  QueueAdRuntimeState queue_ad;
  std::optional<model::GameInfo> queue_modal_game;
  Json queue_modal_data;
  bool exit_prompt_open = false;
  std::string exit_prompt_game_title;

  // ---- recovery -------------------------------------------------------------
  int signaling_recovery_attempts = 0;
  bool signaling_recovery_in_flight = false;
  bool signaling_explicit_shutdown = false;
  int recovery_app_id = 0;
  int64_t stable_recovery_reset_at_ms = 0;
  bool awaiting_recovery_remote_ice = false;
  bool has_confirmed_remote_ice = false;
  std::string latest_ice_state = "new";
  int pending_controlled_disconnects = 0;
  RuntimeSnapshot runtime_snapshot;

  // ---- stream chrome --------------------------------------------------------
  bool show_stats_overlay = false;
  bool anti_afk_enabled = false;
  int anti_afk_ack_nonce = 0;
  bool session_fullscreen = false;
  bool stream_reveal_complete = false;
  bool video_has_frame = false;
  float stream_volume = 1.0f;
  float stream_mic_level = 1.0f;
  bool microphone_active = false;
  bool input_capture_active = false;
  bool pointer_locked = false;
  StreamDiagnostics diagnostics;
  bool native_input_bridge_ready = false;
  int native_input_protocol_version = 0;

  // ---- playtime -------------------------------------------------------------
  PlaytimeLedger playtime;

  // ---- dialogs ---------------------------------------------------------------
  ModalState modal;
  bool queue_modal_open = false;
  std::vector<QueueServer> queue_servers;
  std::string selected_queue_server_id;

  // ---- stream chrome extras -------------------------------------------------
  bool show_stream_sidebar = false;

  // ---- ui -------------------------------------------------------------------
  std::vector<ToastMessage> toasts;
  std::string status_hint;
  int64_t status_hint_at_ms = 0;
  bool show_release_highlights = false;
  std::string release_highlights_body;
  bool controller_nav_enabled = false;
  bool search_focused = false;
  int controller_page_offset = 0;
  int64_t last_frame_ms = 0;
  float frame_dt = 0.0f;
  double app_time = 0.0f;
  float content_scale = 1.0f;
  int window_width = 1400;
  int window_height = 900;
  bool quit_requested = false;

  // ---- derived helpers ------------------------------------------------------
  bool is_streaming() const { return stream_status == StreamStatus::Streaming; }
  bool stream_active() const { return stream_status != StreamStatus::Idle; }
  bool is_authenticated() const { return auth_session.has_value(); }
  const Settings& settings() const { return SettingsStore::instance().get(); }
  Settings& settings() { return SettingsStore::instance().get(); }

  model::StreamProfile entitled_profile() const;
  long long session_elapsed_seconds() const;
  int session_limit_seconds() const;
  int session_time_remaining_seconds() const;
  bool free_tier_warnings_active() const;

  void push_toast(ToastKind kind, const std::string& title, const std::string& detail = "");
  void tick_toasts(int64_t now_ms);
};

// Catalog helpers, ported from `src/client/lib/gameCatalog.ts`.
namespace catalog {

std::string default_variant_id(const model::GameInfo& game);
const model::GameVariant* selected_variant(const model::GameInfo& game,
                                           const std::string& variant_id);
bool matches_search(const model::GameInfo& game, const std::string& query);
void sort_library(std::vector<model::GameInfo>& games, const std::string& sort_id,
                  const std::vector<PlaytimeRecord>& playtime);
void sort_catalog(std::vector<model::GameInfo>& games, const std::string& sort_id,
                  const std::vector<PlaytimeRecord>& playtime);
void apply_filters(std::vector<model::GameInfo>& games,
                   const std::vector<std::string>& filter_ids);
bool is_numeric_id(const std::string& text);
int parse_numeric_id(const std::string& text, int fallback = 0);
const model::GameInfo* find_by_app_id(const std::vector<model::GameInfo>& games, int app_id,
                                      const std::map<std::string, std::string>& variants);

} // namespace catalog
} // namespace app
} // namespace onow
