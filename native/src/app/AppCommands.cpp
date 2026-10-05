// The command surface: everything a view can ask the app to do.
//
// These are the handlers the React app wired to buttons, keyboard chords and
// signaling events. They are plain member functions here, which means a unit
// test can drive a whole launch without a window.
#include "onow/app/App.h"

#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/Time.h"
#include "onow/i18n.h"
#include "onow/net/Url.h"
#include "onow/Result.h"
#include "onow/services/AuthService.h"
#include "onow/services/CatalogService.h"
#include "onow/services/GfnClient.h"
#include "onow/services/PlaytimeService.h"
#include "onow/services/SessionService.h"

#include <algorithm>
#include <thread>

namespace onow {
namespace app {

using services::LaunchProgress;
using services::LaunchStage;

// --------------------------------------------------------------------- auth --

void App::start_device_login() {
  state_.login_error.clear();
  state_.device_challenge.reset();
  state_.device_login_pending = true;
  auth_service_->start_device_login(state_.selected_provider_id);

  // The challenge arrives asynchronously; poll the service once per frame
  // through `pump_services`, which promotes it into `state_.device_challenge`.
  // Poll immediately too so the screen does not flash empty.
  for (int i = 0; i < 40 && !state_.device_challenge; ++i) {
    onow::sleep_ms(25);
    model::DeviceLoginPoll poll;
    auth_service_->poll_device_login(poll);
    if (!auth_service_->last_error().empty()) {
      state_.login_error = auth_service_->last_error();
      state_.device_login_pending = false;
      return;
    }
  }
}

void App::cancel_device_login() {
  auth_service_->cancel_device_login();
  state_.device_login_pending = false;
  state_.device_challenge.reset();
}

void App::logout() {
  auth_service_->logout();
  state_.auth_session.reset();
  state_.view = View::Login;
  state_.all_games.clear();
  state_.library_games.clear();
  state_.filtered_games.clear();
  state_.filtered_library_games.clear();
  clear_runtime_snapshot();
}

void App::switch_account(const std::string& user_id) {
  (void)user_id;
  logout();
}

// ------------------------------------------------------------------- regions --

void App::refresh_regions() {
  if (state_.regions_loading) return;
  state_.regions_loading = true;
  Result<std::vector<model::StreamRegion>> regions = gfn_->regions();
  state_.regions_loading = false;
  if (!regions.ok()) {
    ONOW_WARN("App", "region refresh failed: %s", regions.error().message.c_str());
    return;
  }
  state_.regions = regions.value();
  // Ping each region on a worker thread; the web client did this from the
  // browser with six concurrent probes.
  std::thread([this, list = state_.regions]() mutable {
    for (model::StreamRegion& region : list) {
      if (region.url.empty()) continue;
      Result<services::PingResult> ping = gfn_->ping_region(region.url);
      if (ping.ok()) {
        region.ping_ms = ping->ping_ms;
        region.ping_failed = ping->failed;
      } else {
        region.ping_failed = true;
      }
    }
    std::lock_guard<std::mutex> lock(region_mutex_);
    state_.regions = list;
  }).detach();
}

void App::refresh_subscription() {
  if (state_.subscription_loading) return;
  state_.subscription_loading = true;
  Result<std::optional<model::SubscriptionInfo>> subscription = gfn_->subscription();
  state_.subscription_loading = false;
  if (!subscription.ok()) {
    ONOW_WARN("App", "subscription refresh failed: %s", subscription.error().message.c_str());
    return;
  }
  state_.subscription = subscription.value();
  if (state_.subscription) {
    // Clamp the requested profile to what the account is entitled to.
    const model::StreamProfile entitled = state_.entitled_profile();
    if (entitled.resolution != state_.settings().resolution) {
      update_setting_string("resolution", entitled.resolution);
    }
    if (entitled.fps != state_.settings().fps) {
      update_setting_int("fps", entitled.fps);
    }
  }
}

// ------------------------------------------------------------------ catalog --

void App::load_catalog(bool force) {
  std::string user_id;
  if (state_.auth_session) user_id = state_.auth_session->user.user_id;
  const std::string base_url = effective_streaming_base_url();
  state_.loading_catalog = true;
  catalog_service_->load(user_id, base_url, "", force);

  // The service runs on its own thread; copy its results in on the next pump.
  std::thread([this, user_id, base_url]() {
    for (int i = 0; i < 200; ++i) {
      onow::sleep_ms(50);
      if (!catalog_service_->loading()) break;
    }
    // Results are picked up in pump_services via the service's snapshot.
    (void)user_id;
    (void)base_url;
  }).detach();
}

void App::load_library() {
  const std::string base_url = effective_streaming_base_url();
  state_.loading_library = true;
  catalog_service_->load_library(state_.auth_session ? state_.auth_session->user.user_id : "",
                                 base_url, "");
}

std::string App::effective_streaming_base_url() const {
  const Settings& settings = state_.settings();
  if (!settings.region.empty()) return settings.region;
  if (state_.auth_session && !state_.auth_session->provider.streaming_service_url.empty()) {
    return state_.auth_session->provider.streaming_service_url;
  }
  return std::string();
}

Json App::build_stream_settings_json() const {
  const Settings& settings = state_.settings();
  const model::StreamProfile profile = state_.entitled_profile();
  Json json = Json::object();
  json["resolution"] = profile.resolution;
  json["fps"] = profile.fps;
  json["maxBitrateMbps"] = settings.max_bitrate_mbps;
  json["codec"] = settings.codec;
  json["colorQuality"] = settings.color_quality;
  json["keyboardLayout"] = settings.keyboard_layout;
  json["gameLanguage"] = settings.game_language;
  json["enableL4S"] = settings.enable_l4s;
  json["enableCloudGsync"] = settings.enable_cloud_gsync;
  json["clientMode"] = settings.stream_client_mode == "native" ? "native" : "web";
  json["nativeStreamerBackend"] = "gstreamer";
  json["transportMode"] = settings.transport_mode;
  json["nativeCloudGsyncMode"] = settings.native_cloud_gsync_mode;
  json["appLaunchMode"] =
      (settings.controller_mode || settings.launch_in_console_mode) ? "gamepadFriendly" : "default";
  return json;
}

// -------------------------------------------------------------------- launch --

void App::initiate_play(const model::GameInfo& game) {
  // Free-tier accounts get the server picker first; NVIDIA accounts on a
  // non-alliance zone skip straight to launch.
  if (!model::is_game_in_library(game)) {
    state_.details_game = game;
    state_.details_variant_id = catalog::default_variant_id(game);
    state_.view = View::GameDetails;
    return;
  }
  const Settings& settings = state_.settings();
  const std::string tier = state_.subscription ? state_.subscription->membership_tier
                                               : (state_.auth_session
                                                      ? state_.auth_session->user.membership_tier
                                                      : std::string());
  const bool is_nvidia = state_.auth_session &&
                         str_upper(state_.auth_session->provider.code) == "NVIDIA";
  if (!is_nvidia || settings.hide_server_selector || tier != "FREE") {
    play_game(game);
    return;
  }
  // Free tier on an NVIDIA zone: fetch the community queue snapshot and show the
  // picker. Any failure (service down, blocked, no eligible zone) falls back to
  // launching with the default routing, which is what the web client did.
  Result<Json> queue = gfn_->printed_waste_queue();
  if (!queue.ok() || !queue.value().is_object() || queue.value().size() == 0) {
    play_game(game);
    return;
  }
  std::vector<QueueServer> servers;
  for (const std::string& zone : queue.value().keys()) {
    const Json& entry = queue.value()[zone];
    // Only standard zones are eligible; "NPA-*" are internal.
    if (!str_starts_with(zone, "NP-") || str_starts_with(zone, "NPA-")) continue;
    const Json* eta = entry.find("eta");
    QueueServer server;
    server.id = zone;
    server.label = zone;
    server.latency_ms = eta ? eta->as_int(-1) : -1;
    servers.push_back(server);
  }
  if (servers.empty()) {
    play_game(game);
    return;
  }
  state_.queue_servers = servers;
  state_.queue_modal_data = queue.value();
  state_.queue_modal_game = game;
  state_.queue_modal_open = true;
}

void App::play_game(const model::GameInfo& game, const std::string& variant_id_override,
                    const std::string& streaming_base_url_override) {
  if (state_.launch_in_flight || state_.stream_active()) return;

  const std::string variant_id =
      variant_id_override.empty() ? (state_.variant_by_game_id.count(game.id)
                                         ? state_.variant_by_game_id[game.id]
                                         : catalog::default_variant_id(game))
                                  : variant_id_override;
  const model::GameVariant* variant = catalog::selected_variant(game, variant_id);

  // Epic Store variants that are not linked must be linked first.
  if (variant && model::is_epic_store(variant->store) && variant->library_status.empty()) {
    state_.launch_error.active = true;
    state_.launch_error.stage = StreamStatus::Queue;
    state_.launch_error.title = tr("errors.markOwnedFailed");
    state_.launch_error.description = tr("errors.markOwnedMissingVariant");
    return;
  }

  state_.launch_in_flight = true;
  state_.launch_abort_requested = false;
  state_.launch_error = LaunchErrorState();
  state_.queue_position_known = false;
  state_.queue_position = 0;
  state_.session_started_at_ms = 0;
  state_.stream_warning = StreamWarningState();
  state_.streaming_game = game;
  state_.streaming_store = variant ? variant->store : std::string();
  state_.stream_status = StreamStatus::Queue;
  playtime_service_->mark_launch_start(game.id, game.title);

  // Resolve the numeric app id: variant id first, then launchAppId, then the
  // backend's resolver for UUID-only entries.
  std::string app_id;
  if (catalog::is_numeric_id(variant_id)) app_id = variant_id;
  else if (catalog::is_numeric_id(game.launch_app_id)) app_id = game.launch_app_id;
  if (app_id.empty()) {
    services::ResolveLaunchIdInput input;
    input.app_id_or_uuid = game.uuid.empty() ? variant_id : game.uuid;
    input.streaming_base_url = effective_streaming_base_url();
    Result<std::string> resolved = gfn_->resolve_launch_app_id(input);
    if (resolved.ok() && catalog::is_numeric_id(resolved.value())) {
      app_id = resolved.value();
    }
  }
  if (app_id.empty()) {
    state_.launch_error.active = true;
    state_.launch_error.stage = StreamStatus::Queue;
    state_.launch_error.title = tr("errors.launchFailedTitle");
    state_.launch_error.description = tr("errors.launchUnknown");
    state_.launch_in_flight = false;
    state_.stream_status = StreamStatus::Idle;
    return;
  }

  const std::string base_url =
      streaming_base_url_override.empty() ? effective_streaming_base_url()
                                          : streaming_base_url_override;
  state_.recovery_app_id = catalog::parse_numeric_id(app_id, 0);

  const bool account_linked = !variant || variant->store.empty() ||
                              !model::is_epic_store(variant->store);
  const bool supports_persistence = variant && variant->supports_in_game_settings_persistence;

  session_service_->launch(
      app_id, game.title, base_url, "", account_linked, supports_persistence,
      build_stream_settings_json(),
      [this](const LaunchProgress& progress) {
        state_.queue_position = progress.queue_position;
        state_.queue_position_known = progress.queue_position_known;
        state_.launch_poll_diagnostic = progress.diagnostic;
        switch (progress.stage) {
          case LaunchStage::Queue: state_.stream_status = StreamStatus::Queue; break;
          case LaunchStage::Setup: state_.stream_status = StreamStatus::Setup; break;
          case LaunchStage::Starting: state_.stream_status = StreamStatus::Starting; break;
          case LaunchStage::Connecting: state_.stream_status = StreamStatus::Connecting; break;
          default: break;
        }
      },
      [this](const Result<model::SessionInfo>& result) {
        state_.launch_in_flight = false;
        if (!result.ok()) {
          state_.launch_error.active = true;
          state_.launch_error.stage = state_.stream_status;
          state_.launch_error.title = tr("errors.launchFailedTitle");
          state_.launch_error.description = result.error().message;
          state_.stream_status = StreamStatus::Idle;
          return;
        }
        state_.session = result.value();
        state_.stream_status = StreamStatus::Connecting;
        state_.launch_poll_diagnostic = "seat ready · claiming session " + result->session_id;

        // Claim, then hand the seat to the stream engine.
        Result<model::SessionInfo> claimed = session_service_->claim(
            result->session_id, result->server_ip,
            result->streaming_base_url.empty() ? effective_streaming_base_url()
                                               : result->streaming_base_url,
            state_.recovery_app_id > 0 ? to_string(state_.recovery_app_id) : result->app_id,
            result->app_launch_mode, result->enable_persisting_in_game_settings, false,
            build_stream_settings_json(), result->client_id, result->device_id);
        if (!claimed.ok()) {
          state_.launch_error.active = true;
          state_.launch_error.stage = StreamStatus::Connecting;
          state_.launch_error.title = tr("errors.launchFailedTitle");
          state_.launch_error.description = claimed.error().message;
          state_.stream_status = StreamStatus::Idle;
          return;
        }
        state_.session = claimed.value();
        ensure_stream_engine();
        if (stream_engine_) {
          stream::SessionContext context = build_session_context(*state_.session);
          if (!stream_engine_->start(context)) {
            ONOW_ERROR("App", "stream engine refused to start for session %s",
                       state_.session->session_id.c_str());
          }
        }
      });
}

void App::stop_stream() {
  if (state_.stream_status == StreamStatus::Idle) return;
  state_.launch_abort_requested = true;
  session_service_->abort(true);

  if (stream_engine_) stream_engine_->stop();

  if (state_.session) {
    session_service_->stop(*state_.session);
  }
  if (state_.streaming_game) playtime_service_->end_session(state_.streaming_game->id);
  state_.session.reset();
  state_.stream_status = StreamStatus::Idle;
  state_.queue_position = 0;
  state_.queue_position_known = false;
  state_.launch_error = LaunchErrorState();
  state_.launch_poll_diagnostic.clear();
  state_.session_started_at_ms = 0;
  state_.show_stats_overlay = state_.settings().show_stats_on_launch;
  state_.navbar_active_session.reset();
  clear_runtime_snapshot();
}

void App::resume_navbar_session() {
  if (!state_.navbar_active_session || state_.stream_active()) return;
  const model::ActiveSessionInfo& active = *state_.navbar_active_session;
  state_.navbar_session_resuming = true;
  state_.stream_status = StreamStatus::Connecting;
  if (state_.recovery_app_id <= 0) state_.recovery_app_id = active.app_id;

  const model::GameInfo* game = catalog::find_by_app_id(state_.all_games, active.app_id,
                                                        state_.variant_by_game_id);
  if (game) {
    state_.streaming_game = *game;
    state_.streaming_store =
        catalog::selected_variant(*game, state_.variant_by_game_id.count(game->id)
                                             ? state_.variant_by_game_id[game->id]
                                             : std::string())
            ? catalog::selected_variant(*game, std::string())->store
            : std::string();
  }

  Result<model::SessionInfo> claimed = session_service_->claim(
      active.session_id, active.server_ip,
      active.streaming_base_url.empty() ? effective_streaming_base_url()
                                        : active.streaming_base_url,
      to_string(active.app_id), active.app_launch_mode,
      active.enable_persisting_in_game_settings, true, build_stream_settings_json());
  state_.navbar_session_resuming = false;
  if (!claimed.ok()) {
    state_.launch_error.active = true;
    state_.launch_error.stage = StreamStatus::Connecting;
    state_.launch_error.title = tr("errors.sessionConnectionLostTitle");
    state_.launch_error.description = claimed.error().message;
    state_.stream_status = StreamStatus::Idle;
    return;
  }
  state_.session = claimed.value();
  state_.stream_status = StreamStatus::Connecting;
  ensure_stream_engine();
  if (stream_engine_) {
    stream::SessionContext context = build_session_context(*state_.session);
    if (!stream_engine_->start(context)) {
      ONOW_ERROR("App", "stream engine refused to start for resumed session %s",
                 state_.session->session_id.c_str());
    }
  }
}

void App::terminate_navbar_session() {
  if (!state_.navbar_active_session) return;
  state_.navbar_session_terminating = true;
  session_service_->stop(*state_.navbar_active_session);
  state_.navbar_session_terminating = false;
  state_.navbar_active_session.reset();
}

// ------------------------------------------------------------------ catalog --

void App::mark_game_owned(const model::GameInfo& game, const std::string& variant_id) {
  state_.mark_owned_in_flight[variant_id] = true;
  Result<bool> result =
      catalog_service_->mark_owned(game, variant_id, effective_streaming_base_url(), "");
  state_.mark_owned_in_flight[variant_id] = false;
  if (!result.ok()) {
    state_.push_toast(ToastKind::Error, tr("errors.markOwnedFailed"),
                      result.error().message);
    return;
  }
  state_.push_toast(ToastKind::Success, tr("app.status.saved"), game.title);
  // Refresh so the card moves into the library.
  load_catalog(true);
}

void App::toggle_favorite(const std::string& game_id) {
  std::vector<std::string> favorites = state_.settings().favorite_game_ids;
  const auto it = std::find(favorites.begin(), favorites.end(), game_id);
  if (it != favorites.end()) favorites.erase(it);
  else favorites.push_back(game_id);
  // Settings are persisted as a JSON array; rebuild it through the store.
  Json json = Json::array();
  for (const std::string& id : favorites) json.push_back(Json(id));
  SettingsStore::instance().set_json("favoriteGameIds", json);
}

void App::buy_game(const model::GameInfo& game, const std::string& variant_id) {
  const model::GameVariant* variant =
      catalog::selected_variant(game, variant_id.empty() ? catalog::default_variant_id(game)
                                                         : variant_id);
  if (variant && !variant->store_url.empty()) {
    open_store_url(variant->store_url);
    return;
  }
  services::ResolveStoreUrlInput input;
  input.app_id_or_uuid = game.uuid.empty() ? game.id : game.uuid;
  input.variant_id = variant ? variant->id : variant_id;
  input.store = variant ? variant->store : std::string();
  input.streaming_base_url = effective_streaming_base_url();
  Result<std::string> url = gfn_->resolve_store_url(input);
  if (url.ok() && !url.value().empty()) open_store_url(url.value());
  else state_.push_toast(ToastKind::Warning, tr("errors.launchUnknown"));
}

void App::open_store_url(const std::string& url) {
  Url parsed;
  if (!url_parse(url, parsed) || (parsed.scheme != "http" && parsed.scheme != "https")) {
    state_.push_toast(ToastKind::Warning, tr("errors.launchUnknown"));
    return;
  }
  platform_->open_url(url);
}

// ----------------------------------------------------------------- settings --

void App::update_setting_bool(const std::string& key, bool value) {
  SettingsStore::instance().set_bool(key, value);
  if (key == "appAccentColor") apply_accent();
  if (key == "windowWidth" || key == "windowHeight") {
    // Window size changes are applied by the platform on the next resize.
  }
}

void App::update_setting_int(const std::string& key, int value) {
  SettingsStore::instance().set_int(key, value);
}

void App::update_setting_float(const std::string& key, float value) {
  SettingsStore::instance().set_float(key, value);
}

void App::update_setting_string(const std::string& key, const std::string& value) {
  SettingsStore::instance().set_string(key, value);
  if (key == "region") refresh_regions();
}

} // namespace app
} // namespace onow
