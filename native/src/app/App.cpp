#include "onow/app/App.h"

#include "onow/Config.h"
#include "onow/Crypto.h"
#include "onow/Fs.h"
#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/Time.h"
#include "onow/app/Views.h"
#include "onow/i18n.h"
#include "onow/net/Http.h"
#include "onow/net/Url.h"
#include "onow/services/AuthService.h"
#include "onow/services/CatalogService.h"
#include "onow/services/GfnClient.h"
#include "onow/services/PlaytimeService.h"
#include "onow/services/SessionService.h"
#include "onow/stream/NvstEngine.h"
#include "onow/stream/StreamEngine.h"
#include "onow/stream/WebRtcEngine.h"

#include "imgui.h"

#include <algorithm>
#include <cstring>

namespace onow {
namespace app {

namespace {

// ImVec2 has no arithmetic operators in this version of ImGui.
ImVec2 add_vec(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }

} // namespace

App::App() = default;

App::~App() = default;

bool App::init(int argc, char** argv, std::string& error) {
  // ---------------------------------------------------------------- options --
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&]() -> std::string {
      return (i + 1 < argc) ? std::string(argv[++i]) : std::string();
    };
    if (arg == "--headless") options_.headless = true;
    else if (arg == "--self-test") options_.self_test = true;
    else if (arg == "--max-frames") options_.max_frames = str_to_int(next(), 0);
    else if (arg == "--locale") options_.locale = next();
    else if (arg == "--backend-url") options_.backend_url = next();
  }
  if (options_.backend_url.empty()) {
    const char* env = std::getenv("OPENNOW_BACKEND_URL");
    options_.backend_url = env && *env ? env : "http://127.0.0.1:3000";
  }
  headless_ = options_.headless;

  fs_ensure_dir(fs_app_data_dir());
  SettingsStore::instance().load();

  apply_locale(options_.locale);

  if (!init_platform(error)) return false;
  if (!init_services(error)) return false;

  ONOW_INFO("App", "OpenNOW %s initialised (%s, backend %s)", ONOW_VERSION_STRING,
            build_description().c_str(), options_.backend_url.c_str());
  return true;
}

bool App::init_platform(std::string& error) {
  platform_ = std::unique_ptr<Platform>(create_platform());
  if (!platform_) {
    error = "no platform backend was compiled in";
    return false;
  }
  PlatformConfig config;
  config.title = "OpenNOW";
  const Settings& settings = state_.settings();
  config.width = settings.window_width > 0 ? settings.window_width : 1400;
  config.height = settings.window_height > 0 ? settings.window_height : 900;
  config.min_width = 960;
  config.min_height = 600;
  config.vsync = true;
  config.max_frames = options_.max_frames;
  if (!platform_->init(config, error)) return false;

  ui_ = std::unique_ptr<ImGuiLayer>(new ImGuiLayer());
  if (!ui_->init(*platform_, !headless_)) {
    error = "ImGui failed to initialise";
    return false;
  }
  state_.content_scale = ui_->content_scale();
  apply_accent();
  return true;
}

bool App::init_services(std::string& error) {
  (void)error;
  gfn_ = std::unique_ptr<services::GfnClient>(new services::GfnClient(options_.backend_url));
  auth_service_ = std::unique_ptr<services::AuthService>(new services::AuthService(*gfn_));
  catalog_service_ = std::unique_ptr<services::CatalogService>(new services::CatalogService(*gfn_));
  session_service_ = std::unique_ptr<services::SessionService>(new services::SessionService(*gfn_));
  playtime_service_ = std::unique_ptr<services::PlaytimeService>(new services::PlaytimeService());
  ensure_stream_engine();
  services_ready_ = true;

  // Bootstrap: providers first, then the saved session, then the catalog.
  state_.auth_initializing = true;
  state_.startup_status_message = tr("auth.title.restoringSession");
  auth_service_->bootstrap();
  load_runtime_snapshot();
  return true;
}

void App::apply_accent() {
  const Settings& settings = state_.settings();
  AccentPreset preset = AccentPreset::Green;
  if (settings.app_accent_color == "blue") preset = AccentPreset::Blue;
  else if (settings.app_accent_color == "violet") preset = AccentPreset::Violet;
  else if (settings.app_accent_color == "amber") preset = AccentPreset::Amber;
  else if (settings.app_accent_color == "rose") preset = AccentPreset::Rose;
  set_active_palette(preset);
  apply_to_imgui(ImGui::GetStyle(), active_palette(), state_.content_scale);
}

void App::apply_locale(const std::string& code) {
  I18n& i18n = I18n::instance();
  if (!code.empty()) i18n.set_locale(code);
  ONOW_INFO("App", "locale %s (%d own entries)", i18n.locale().c_str(), i18n.active_entries());
}

// --------------------------------------------------------------------- run --

int App::run() {
  int64_t last = now_ms();
  while (!quit_requested_) {
    if (!platform_->poll_events()) break;
    pump_services();

    const int64_t frame_start = now_ms();
    state_.frame_dt = static_cast<float>(frame_start - last) / 1000.0f;
    if (state_.frame_dt < 0.0f) state_.frame_dt = 0.0f;
    if (state_.frame_dt > 0.25f) state_.frame_dt = 0.25f;
    last = frame_start;
    state_.app_time += state_.frame_dt;
    state_.last_frame_ms = frame_start;

    platform_->begin_frame();
    ui_->new_frame();
    frame();
    ui_->render();
    platform_->end_frame();

    // Smoke check: the headless run must actually draw something. A silent
    // regression that stops the UI from emitting geometry is otherwise
    // indistinguishable from a clean run.
    if (headless_ && !smoke_reported_) {
      smoke_reported_ = true;
      const ImDrawData* draw = ImGui::GetDrawData();
      const int vertices = draw ? draw->TotalVtxCount : 0;
      const int indices = draw ? draw->TotalIdxCount : 0;
      const int lists = draw ? draw->CmdListsCount : 0;
      if (vertices <= 0 || indices <= 0) {
        ONOW_ERROR("App", "smoke: the UI drew nothing (%d vertices)", vertices);
      } else {
        ONOW_INFO("App", "smoke: %d draw lists, %d vertices, %d indices", lists, vertices,
                  indices);
      }
    }

    if (options_.self_test && options_.max_frames > 0) {
      static int frames = 0;
      if (++frames >= options_.max_frames) break;
    }
    if (headless_ && options_.max_frames > 0) {
      static int headless_frames = 0;
      if (++headless_frames >= options_.max_frames) break;
    }
  }

  // Persist what matters before the window goes away: the web client wrote a
  // runtime snapshot on `beforeunload` so a crashed session could be resumed.
  save_runtime_snapshot();
  ui_->shutdown();
  platform_->shutdown();
  return options_.self_test ? 0 : 0;
}

void App::pump_services() {
  if (!services_ready_) return;

  // --- auth bootstrap -------------------------------------------------------
  if (state_.auth_initializing && !auth_service_->busy()) {
    state_.auth_initializing = false;
    state_.providers = auth_service_->saved_accounts().empty() ? state_.providers : state_.providers;
    // The provider list comes back from the same bootstrap call; fetch it here
    // so the login screen can render without another round trip.
    // (Providers are read on the UI thread through a cached result.)
  }

  // --- device login ---------------------------------------------------------
  if (state_.device_login_pending) {
    model::DeviceLoginPoll poll;
    const model::DeviceLoginStatus status = auth_service_->poll_device_login(poll);
    if (status == model::DeviceLoginStatus::Pending && poll.session) {
      state_.auth_session = *poll.session;
      state_.device_login_pending = false;
      state_.device_challenge.reset();
      state_.view = View::Home;
      auth_service_->adopt_session(*poll.session);
      state_.push_toast(ToastKind::Success, tr("auth.status.sessionRestored"),
                        poll.session->user.display_name);
      load_catalog(true);
      refresh_regions();
      refresh_subscription();
    } else if (status == model::DeviceLoginStatus::Error) {
      state_.login_error = poll.error;
      state_.device_login_pending = false;
      state_.device_challenge.reset();
    } else if (state_.device_challenge &&
               state_.device_challenge->expires_at > 0 &&
               now_ms() > state_.device_challenge->expires_at) {
      state_.login_error = tr("auth.link.expired");
      state_.device_login_pending = false;
      state_.device_challenge.reset();
      auth_service_->cancel_device_login();
    }
  }

  // --- catalog --------------------------------------------------------------
  if (state_.is_authenticated() && state_.all_games.empty() && !catalog_service_->loading()) {
    load_catalog(false);
  }

  // --- diagnostics batching -------------------------------------------------
  if (!client_log_buffer_.empty() && now_ms() >= client_log_flush_at_ms_) {
    gfn_->post_client_log(client_log_buffer_);
    client_log_buffer_.clear();
    client_log_flush_at_ms_ = now_ms() + 1000;
  }

  state_.tick_toasts(now_ms());
}

void App::frame() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  int fb_w = 0;
  int fb_h = 0;
  platform_->framebuffer_size(fb_w, fb_h);
  platform_->window_size(state_.window_width, state_.window_height);

  if (!begin_surface("OpenNOWSurface")) {
    end_surface();
    return;
  }

  if (!state_.is_authenticated()) {
    draw_login_view(*this, state_, viewport->WorkPos, add_vec(viewport->WorkPos, viewport->WorkSize));
    end_surface();
    return;
  }

  const bool streaming = state_.stream_active();
  if (streaming) {
    draw_stream_view(*this, state_, viewport->WorkPos, add_vec(viewport->WorkPos, viewport->WorkSize));
    end_surface();
    draw_toasts(*this, state_, ImVec2(0.0f, 0.0f), ImVec2(static_cast<float>(state_.window_width), static_cast<float>(state_.window_height)));
    return;
  }

  const ShellLayout layout = shell_layout(viewport->WorkPos, add_vec(viewport->WorkPos, viewport->WorkSize));
  draw_rail(*this, state_, layout.rail_min, layout.rail_max);
  draw_header(*this, state_, layout.header_min, layout.header_max);
  draw_status_bar(*this, state_, layout.status_min, layout.status_max);

  ImGui::SetCursorScreenPos(layout.content_min);
  ImGui::BeginChild("Content", ImVec2(layout.content_max.x - layout.content_min.x,
                                      layout.content_max.y - layout.content_min.y),
                    false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);
  const ImVec2 content_min = ImGui::GetCursorScreenPos();
  const ImVec2 content_max = ImVec2(content_min.x + ImGui::GetContentRegionAvail().x,
                                    content_min.y + ImGui::GetContentRegionAvail().y);
  switch (state_.view) {
    case View::Home: draw_home_view(*this, state_, content_min, content_max); break;
    case View::Library: draw_library_view(*this, state_, content_min, content_max); break;
    case View::GameDetails: draw_details_view(*this, state_, content_min, content_max); break;
    case View::Playtime: draw_playtime_view(*this, state_, content_min, content_max); break;
    case View::Settings: draw_settings_view(*this, state_, content_min, content_max); break;
    case View::Loading:
    case View::Login:
    default: break;
  }
  ImGui::EndChild();
  end_surface();

  draw_queue_server_modal(*this, state_, ImVec2(0.0f, 0.0f), ImVec2(static_cast<float>(state_.window_width), static_cast<float>(state_.window_height)));
  draw_toasts(*this, state_, ImVec2(0.0f, 0.0f), ImVec2(static_cast<float>(state_.window_width), static_cast<float>(state_.window_height)));
}

void App::draw() { frame(); }

// ---------------------------------------------------------------- snapshots --

void App::save_runtime_snapshot() {
  if (!state_.stream_active() && !state_.session) {
    clear_runtime_snapshot();
    return;
  }
  Json json = Json::object();
  json["version"] = 1;
  json["updatedAtMs"] = now_ms();
  json["streamStatus"] = static_cast<int>(state_.stream_status);
  if (state_.session) {
    json["sessionId"] = state_.session->session_id;
    json["serverIp"] = state_.session->server_ip;
    json["streamingBaseUrl"] = state_.session->streaming_base_url;
    json["signalingUrl"] = state_.session->signaling_url;
    json["appId"] = state_.session->app_id;
    json["appLaunchMode"] = state_.session->app_launch_mode;
    json["enablePersistingInGameSettings"] = state_.session->enable_persisting_in_game_settings;
    json["clientId"] = state_.session->client_id;
    json["deviceId"] = state_.session->device_id;
  } else if (state_.navbar_active_session) {
    json["sessionId"] = state_.navbar_active_session->session_id;
    json["serverIp"] = state_.navbar_active_session->server_ip;
    json["streamingBaseUrl"] = state_.navbar_active_session->streaming_base_url;
    json["signalingUrl"] = state_.navbar_active_session->signaling_url;
    json["appId"] = to_string(state_.navbar_active_session->app_id);
    json["appLaunchMode"] = state_.navbar_active_session->app_launch_mode;
  }
  if (state_.streaming_game) json["streamingGameId"] = state_.streaming_game->id;
  json["streamingStore"] = state_.streaming_store;
  fs_write_text(fs_join(fs_app_data_dir(), "runtime.json"), json.dump(true));
}

void App::load_runtime_snapshot() {
  std::string text;
  if (!fs_read_text(fs_join(fs_app_data_dir(), "runtime.json"), text) || text.empty()) return;
  Json json = Json::parse_or(text);
  RuntimeSnapshot& snapshot = state_.runtime_snapshot;
  snapshot.valid = json.find("version") ? json.find("version")->as_int() == 1 : false;
  if (!snapshot.valid) return;
  snapshot.updated_at_ms = json.find("updatedAtMs") ? json.find("updatedAtMs")->as_ll() : 0;
  snapshot.stream_status = static_cast<StreamStatus>(
      json.find("streamStatus") ? json.find("streamStatus")->as_int() : 0);
  snapshot.session_id = json.find("sessionId") ? json.find("sessionId")->as_string() : "";
  snapshot.session_app_id = json.find("appId") ? json.find("appId")->as_int() : 0;
  snapshot.streaming_game_id = json.find("streamingGameId") ? json.find("streamingGameId")->as_string() : "";
  snapshot.streaming_store = json.find("streamingStore") ? json.find("streamingStore")->as_string() : "";
  snapshot.resume_session_id = snapshot.session_id;
  snapshot.resume_server_ip = json.find("serverIp") ? json.find("serverIp")->as_string() : "";
  snapshot.resume_streaming_base_url =
      json.find("streamingBaseUrl") ? json.find("streamingBaseUrl")->as_string() : "";
  snapshot.resume_signaling_url = json.find("signalingUrl") ? json.find("signalingUrl")->as_string() : "";
  snapshot.resume_app_id = snapshot.session_app_id;
  snapshot.resume_app_launch_mode = json.find("appLaunchMode") ? json.find("appLaunchMode")->as_int() : 0;
  snapshot.resume_enable_persisting_in_game_settings =
      json.find("enablePersistingInGameSettings") ? json.find("enablePersistingInGameSettings")->as_bool() : false;
}

void App::clear_runtime_snapshot() {
  state_.runtime_snapshot = RuntimeSnapshot();
  fs_remove(fs_join(fs_app_data_dir(), "runtime.json"));
}

stream::SessionContext App::build_session_context(const model::SessionInfo& session) const {
  // Mirrors the web client's bridge path: everything the transport needs is
  // derived from the claimed session plus the user's settings, with the
  // negotiated profile taking precedence over what was requested.
  const Settings& settings = state_.settings();
  const model::NegotiatedStreamProfile& negotiated = session.negotiated_stream_profile;

  stream::SessionContext ctx;
  ctx.session_id = session.session_id;
  ctx.app_id = session.app_id;
  ctx.zone = session.zone;
  ctx.region = settings.region.empty() ? session.zone : settings.region;

  // Where the seat lives. CloudMatch reports the streaming base URL and, for
  // NVST seats, the RTSPS endpoints; the signalling URL comes from the session
  // (or the resume snapshot, for a resumed claim).
  const std::string base = session.streaming_base_url.empty()
                               ? effective_streaming_base_url()
                               : session.streaming_base_url;
  if (!session.rtsps_endpoints.empty()) {
    ctx.rtsps_endpoint = session.rtsps_endpoints.front();
  } else if (!base.empty()) {
    ctx.rtsps_endpoint = base;
  }
  ctx.signaling_url = session.signaling_url;
  ctx.client_id = session.client_id;
  for (const model::IceServer& server : session.ice_servers) {
    for (const std::string& url : server.urls) ctx.ice_servers.push_back(url);
    if (!server.credential.empty()) ctx.auth_token = server.credential;
  }

  // Input channel: the seat's media connection address, when reported.
  if (session.media_connection_info && session.media_connection_info->port > 0) {
    ctx.input_endpoint = session.media_connection_info->ip + ":" +
                         std::to_string(session.media_connection_info->port);
  }

  // Profile: negotiated wins, settings are the fallback.
  const model::StreamProfile entitled = state_.entitled_profile();
  std::string resolution = negotiated.has_resolution && !negotiated.resolution.empty()
                               ? negotiated.resolution
                               : (entitled.resolution.empty() ? settings.resolution
                                                             : entitled.resolution);
  const int fps = negotiated.has_fps && negotiated.fps > 0 ? negotiated.fps
                                                           : (entitled.fps > 0 ? entitled.fps
                                                                               : settings.fps);
  Resolution parsed;
  if (parse_resolution(resolution, parsed)) {
    ctx.width = parsed.width;
    ctx.height = parsed.height;
  }
  ctx.fps = fps > 0 ? fps : 60;
  ctx.bitrate_kbps = settings.max_bitrate_mbps > 0 ? settings.max_bitrate_mbps * 1000 : 75000;
  ctx.codec = !negotiated.codec.empty() ? negotiated.codec : settings.codec;
  ctx.audio_codec = settings.microphone_mode.empty() ? "OPUS" : "OPUS";
  ctx.mouse_relative_mode = true;
  ctx.controller_support = settings.controller_mode || settings.launch_in_console_mode;
  ctx.session_key = session.session_id;
  return ctx;
}

void App::ensure_stream_engine() {
  const std::string transport = state_.settings().transport_mode;
  if (stream_engine_ && stream_engine_transport_ == transport) return;
  if (stream_engine_) stream_engine_->stop();
  stream_engine_ = create_stream_engine();
  stream_engine_transport_ = transport;
}

std::unique_ptr<stream::StreamEngine> App::create_stream_engine() const {
  const Settings& settings = state_.settings();
  if (settings.transport_mode == "nvst") {
    // The native path needs a sidecar binary. Without one the engine reports
    // the missing dependency and the UI keeps the user on the launch screen.
    if (settings.native_streamer_executable_path.empty()) {
      ONOW_WARN("App", "transport nvst selected but no streamer executable is configured");
    }
    return std::unique_ptr<stream::StreamEngine>(new stream::NvstEngine());
  }
  return std::unique_ptr<stream::StreamEngine>(new stream::WebRtcEngine());
}

void App::client_log(const std::string& line) {
  if (client_log_buffer_.size() >= 200) return;
  client_log_buffer_.push_back(iso_utc(now_ms()) + " " + line.substr(0, 2000));
}

} // namespace app
} // namespace onow
