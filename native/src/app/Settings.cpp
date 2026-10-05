#include "onow/app/Settings.h"

#include "onow/Fs.h"
#include "onow/Log.h"
#include "onow/Str.h"

namespace onow {
namespace app {

namespace {

// Every key the web client persisted, with the same defaults. Keeping the list
// explicit (rather than reflecting over the struct) means a typo in the UI
// layer is a compile error rather than a silently ignored setting.
const char* const kStringKeys[] = {
    "resolution",  "aspectRatio",   "streamClientMode",  "nativeStreamerBackend",
    "nativeVideoBackend",           "nativeStreamerExecutablePath",
    "nativeCloudGsyncMode",         "nativeD3dFullscreenMode", "transportMode",
    "codec",       "decoderPreference", "encoderPreference", "colorQuality",
    "region",      "sessionProxyUrl",  "keyboardLayout",
    "shortcutToggleStats",           "shortcutTogglePointerLock",
    "shortcutToggleFullscreen",      "shortcutStopStream",
    "shortcutToggleAntiAfk",         "shortcutToggleMicrophone",
    "shortcutScreenshot",            "shortcutToggleRecording",
    "microphoneMode",                "microphoneDeviceId",
    "appAccentColor",                "appTheme",
    "gameLanguage",                  "updateChannel",
    "lastSeenReleaseHighlightsVersion",
};

const char* const kFloatKeys[] = {
    "posterSizeScale", "mouseSensitivity", "mouseAcceleration",
};

const char* const kIntKeys[] = {
    "fps",     "maxBitrateMbps", "recordingBitrateMbps",
    "sessionClockShowEveryMinutes", "sessionClockShowDurationSeconds",
    "windowWidth", "windowHeight",
};

const char* const kBoolKeys[] = {
    "nativeExternalRenderer",   "showNativeStreamerStats",
    "sessionProxyEnabled",      "clipboardPaste",
    "enableGyroscopeControls",  "steamControllerCompatibilityMode",
    "nativeCursorOverlay",      "allowEscapeToExitFullscreen",
    "hideStreamButtons",        "showAntiAfkIndicator",
    "showStatsOnLaunch",        "statsHudProvisioned",
    "streamModeChosen",         "hideServerSelector",
    "translucentUI",            "controllerMode",
    "launchInConsoleMode",      "autoFullScreen",
    "sessionCounterEnabled",    "showSessionTimeRemainingInStatsOverlay",
    "enablePersistingInGameSettings", "enableL4S",
    "enableCloudGsync",         "discordRichPresence",
    "autoCheckForUpdates",
};

} // namespace

Json Settings::to_json() const {
  Json json = Json::object();
  for (const char* key : kStringKeys) {
    if (key == std::string("resolution")) json[key] = resolution;
    else if (key == std::string("aspectRatio")) json[key] = aspect_ratio;
    else if (key == std::string("streamClientMode")) json[key] = stream_client_mode;
    else if (key == std::string("nativeStreamerBackend")) json[key] = native_streamer_backend;
    else if (key == std::string("nativeVideoBackend")) json[key] = native_video_backend;
    else if (key == std::string("nativeStreamerExecutablePath")) json[key] = native_streamer_executable_path;
    else if (key == std::string("nativeCloudGsyncMode")) json[key] = native_cloud_gsync_mode;
    else if (key == std::string("nativeD3dFullscreenMode")) json[key] = native_d3d_fullscreen_mode;
    else if (key == std::string("transportMode")) json[key] = transport_mode;
    else if (key == std::string("codec")) json[key] = codec;
    else if (key == std::string("decoderPreference")) json[key] = decoder_preference;
    else if (key == std::string("encoderPreference")) json[key] = encoder_preference;
    else if (key == std::string("colorQuality")) json[key] = color_quality;
    else if (key == std::string("region")) json[key] = region;
    else if (key == std::string("sessionProxyUrl")) json[key] = session_proxy_url;
    else if (key == std::string("keyboardLayout")) json[key] = keyboard_layout;
    else if (key == std::string("shortcutToggleStats")) json[key] = shortcut_toggle_stats;
    else if (key == std::string("shortcutTogglePointerLock")) json[key] = shortcut_toggle_pointer_lock;
    else if (key == std::string("shortcutToggleFullscreen")) json[key] = shortcut_toggle_fullscreen;
    else if (key == std::string("shortcutStopStream")) json[key] = shortcut_stop_stream;
    else if (key == std::string("shortcutToggleAntiAfk")) json[key] = shortcut_toggle_anti_afk;
    else if (key == std::string("shortcutToggleMicrophone")) json[key] = shortcut_toggle_microphone;
    else if (key == std::string("shortcutScreenshot")) json[key] = shortcut_screenshot;
    else if (key == std::string("shortcutToggleRecording")) json[key] = shortcut_toggle_recording;
    else if (key == std::string("microphoneMode")) json[key] = microphone_mode;
    else if (key == std::string("microphoneDeviceId")) json[key] = microphone_device_id;
    else if (key == std::string("appAccentColor")) json[key] = app_accent_color;
    else if (key == std::string("appTheme")) json[key] = app_theme;
    else if (key == std::string("gameLanguage")) json[key] = game_language;
    else if (key == std::string("updateChannel")) json[key] = update_channel;
    else if (key == std::string("lastSeenReleaseHighlightsVersion")) json[key] = last_seen_release_highlights_version;
  }
  for (const char* key : kFloatKeys) {
    if (key == std::string("posterSizeScale")) json[key] = poster_size_scale;
    else if (key == std::string("mouseSensitivity")) json[key] = mouse_sensitivity;
    else if (key == std::string("mouseAcceleration")) json[key] = mouse_acceleration;
  }
  for (const char* key : kIntKeys) {
    if (key == std::string("fps")) json[key] = fps;
    else if (key == std::string("maxBitrateMbps")) json[key] = max_bitrate_mbps;
    else if (key == std::string("recordingBitrateMbps")) json[key] = recording_bitrate_mbps;
    else if (key == std::string("sessionClockShowEveryMinutes")) json[key] = session_clock_show_every_minutes;
    else if (key == std::string("sessionClockShowDurationSeconds")) json[key] = session_clock_show_duration_seconds;
    else if (key == std::string("windowWidth")) json[key] = window_width;
    else if (key == std::string("windowHeight")) json[key] = window_height;
  }
  for (const char* key : kBoolKeys) {
    if (key == std::string("nativeExternalRenderer")) json[key] = native_external_renderer;
    else if (key == std::string("showNativeStreamerStats")) json[key] = show_native_streamer_stats;
    else if (key == std::string("sessionProxyEnabled")) json[key] = session_proxy_enabled;
    else if (key == std::string("clipboardPaste")) json[key] = clipboard_paste;
    else if (key == std::string("enableGyroscopeControls")) json[key] = enable_gyroscope_controls;
    else if (key == std::string("steamControllerCompatibilityMode")) json[key] = steam_controller_compatibility_mode;
    else if (key == std::string("nativeCursorOverlay")) json[key] = native_cursor_overlay;
    else if (key == std::string("allowEscapeToExitFullscreen")) json[key] = allow_escape_to_exit_fullscreen;
    else if (key == std::string("hideStreamButtons")) json[key] = hide_stream_buttons;
    else if (key == std::string("showAntiAfkIndicator")) json[key] = show_anti_afk_indicator;
    else if (key == std::string("showStatsOnLaunch")) json[key] = show_stats_on_launch;
    else if (key == std::string("statsHudProvisioned")) json[key] = stats_hud_provisioned;
    else if (key == std::string("streamModeChosen")) json[key] = stream_mode_chosen;
    else if (key == std::string("hideServerSelector")) json[key] = hide_server_selector;
    else if (key == std::string("translucentUI")) json[key] = translucent_ui;
    else if (key == std::string("controllerMode")) json[key] = controller_mode;
    else if (key == std::string("launchInConsoleMode")) json[key] = launch_in_console_mode;
    else if (key == std::string("autoFullScreen")) json[key] = auto_full_screen;
    else if (key == std::string("sessionCounterEnabled")) json[key] = session_counter_enabled;
    else if (key == std::string("showSessionTimeRemainingInStatsOverlay")) json[key] = show_session_time_remaining_in_stats_overlay;
    else if (key == std::string("enablePersistingInGameSettings")) json[key] = enable_persisting_in_game_settings;
    else if (key == std::string("enableL4S")) json[key] = enable_l4s;
    else if (key == std::string("enableCloudGsync")) json[key] = enable_cloud_gsync;
    else if (key == std::string("discordRichPresence")) json[key] = discord_rich_presence;
    else if (key == std::string("autoCheckForUpdates")) json[key] = auto_check_for_updates;
  }
  Json favorites = Json::array();
  for (const std::string& id : favorite_game_ids) favorites.push_back(Json(id));
  json["favoriteGameIds"] = favorites;
  return json;
}

Settings Settings::from_json(const Json& json) {
  Settings settings;
  for (const char* key : kStringKeys) {
    const Json* value = json.find(key);
    if (!value || !value->is_string()) continue;
    const std::string text = value->as_string();
    if (key == std::string("resolution")) settings.resolution = text;
    else if (key == std::string("aspectRatio")) settings.aspect_ratio = text;
    else if (key == std::string("streamClientMode")) settings.stream_client_mode = text;
    else if (key == std::string("nativeStreamerBackend")) settings.native_streamer_backend = text;
    else if (key == std::string("nativeVideoBackend")) settings.native_video_backend = text;
    else if (key == std::string("nativeStreamerExecutablePath")) settings.native_streamer_executable_path = text;
    else if (key == std::string("nativeCloudGsyncMode")) settings.native_cloud_gsync_mode = text;
    else if (key == std::string("nativeD3dFullscreenMode")) settings.native_d3d_fullscreen_mode = text;
    else if (key == std::string("transportMode")) settings.transport_mode = text;
    else if (key == std::string("codec")) settings.codec = text;
    else if (key == std::string("decoderPreference")) settings.decoder_preference = text;
    else if (key == std::string("encoderPreference")) settings.encoder_preference = text;
    else if (key == std::string("colorQuality")) settings.color_quality = text;
    else if (key == std::string("region")) settings.region = text;
    else if (key == std::string("sessionProxyUrl")) settings.session_proxy_url = text;
    else if (key == std::string("keyboardLayout")) settings.keyboard_layout = text;
    else if (key == std::string("shortcutToggleStats")) settings.shortcut_toggle_stats = text;
    else if (key == std::string("shortcutTogglePointerLock")) settings.shortcut_toggle_pointer_lock = text;
    else if (key == std::string("shortcutToggleFullscreen")) settings.shortcut_toggle_fullscreen = text;
    else if (key == std::string("shortcutStopStream")) settings.shortcut_stop_stream = text;
    else if (key == std::string("shortcutToggleAntiAfk")) settings.shortcut_toggle_anti_afk = text;
    else if (key == std::string("shortcutToggleMicrophone")) settings.shortcut_toggle_microphone = text;
    else if (key == std::string("shortcutScreenshot")) settings.shortcut_screenshot = text;
    else if (key == std::string("shortcutToggleRecording")) settings.shortcut_toggle_recording = text;
    else if (key == std::string("microphoneMode")) settings.microphone_mode = text;
    else if (key == std::string("microphoneDeviceId")) settings.microphone_device_id = text;
    else if (key == std::string("appAccentColor")) settings.app_accent_color = text;
    else if (key == std::string("appTheme")) settings.app_theme = text;
    else if (key == std::string("gameLanguage")) settings.game_language = text;
    else if (key == std::string("updateChannel")) settings.update_channel = text;
    else if (key == std::string("lastSeenReleaseHighlightsVersion")) settings.last_seen_release_highlights_version = text;
  }
  for (const char* key : kFloatKeys) {
    const Json* value = json.find(key);
    if (!value || !value->is_number()) continue;
    if (key == std::string("posterSizeScale")) settings.poster_size_scale = static_cast<float>(value->as_number());
    else if (key == std::string("mouseSensitivity")) settings.mouse_sensitivity = static_cast<float>(value->as_number());
    else if (key == std::string("mouseAcceleration")) settings.mouse_acceleration = static_cast<float>(value->as_number());
  }
  for (const char* key : kIntKeys) {
    const Json* value = json.find(key);
    if (!value || !value->is_number()) continue;
    if (key == std::string("fps")) settings.fps = value->as_int();
    else if (key == std::string("maxBitrateMbps")) settings.max_bitrate_mbps = value->as_int();
    else if (key == std::string("recordingBitrateMbps")) settings.recording_bitrate_mbps = value->as_int();
    else if (key == std::string("sessionClockShowEveryMinutes")) settings.session_clock_show_every_minutes = value->as_int();
    else if (key == std::string("sessionClockShowDurationSeconds")) settings.session_clock_show_duration_seconds = value->as_int();
    else if (key == std::string("windowWidth")) settings.window_width = value->as_int();
    else if (key == std::string("windowHeight")) settings.window_height = value->as_int();
  }
  for (const char* key : kBoolKeys) {
    const Json* value = json.find(key);
    if (!value) continue;
    const bool state = value->as_bool();
    if (key == std::string("nativeExternalRenderer")) settings.native_external_renderer = state;
    else if (key == std::string("showNativeStreamerStats")) settings.show_native_streamer_stats = state;
    else if (key == std::string("sessionProxyEnabled")) settings.session_proxy_enabled = state;
    else if (key == std::string("clipboardPaste")) settings.clipboard_paste = state;
    else if (key == std::string("enableGyroscopeControls")) settings.enable_gyroscope_controls = state;
    else if (key == std::string("steamControllerCompatibilityMode")) settings.steam_controller_compatibility_mode = state;
    else if (key == std::string("nativeCursorOverlay")) settings.native_cursor_overlay = state;
    else if (key == std::string("allowEscapeToExitFullscreen")) settings.allow_escape_to_exit_fullscreen = state;
    else if (key == std::string("hideStreamButtons")) settings.hide_stream_buttons = state;
    else if (key == std::string("showAntiAfkIndicator")) settings.show_anti_afk_indicator = state;
    else if (key == std::string("showStatsOnLaunch")) settings.show_stats_on_launch = state;
    else if (key == std::string("statsHudProvisioned")) settings.stats_hud_provisioned = state;
    else if (key == std::string("streamModeChosen")) settings.stream_mode_chosen = state;
    else if (key == std::string("hideServerSelector")) settings.hide_server_selector = state;
    else if (key == std::string("translucentUI")) settings.translucent_ui = state;
    else if (key == std::string("controllerMode")) settings.controller_mode = state;
    else if (key == std::string("launchInConsoleMode")) settings.launch_in_console_mode = state;
    else if (key == std::string("autoFullScreen")) settings.auto_full_screen = state;
    else if (key == std::string("sessionCounterEnabled")) settings.session_counter_enabled = state;
    else if (key == std::string("showSessionTimeRemainingInStatsOverlay")) settings.show_session_time_remaining_in_stats_overlay = state;
    else if (key == std::string("enablePersistingInGameSettings")) settings.enable_persisting_in_game_settings = state;
    else if (key == std::string("enableL4S")) settings.enable_l4s = state;
    else if (key == std::string("enableCloudGsync")) settings.enable_cloud_gsync = state;
    else if (key == std::string("discordRichPresence")) settings.discord_rich_presence = state;
    else if (key == std::string("autoCheckForUpdates")) settings.auto_check_for_updates = state;
  }
  if (const Json* favorites = json.find("favoriteGameIds")) {
    settings.favorite_game_ids.clear();
    for (size_t i = 0; i < favorites->size(); ++i) {
      settings.favorite_game_ids.push_back(favorites->at(i).as_string());
    }
  }
  return settings;
}

SettingsStore& SettingsStore::instance() {
  static SettingsStore store;
  return store;
}

void SettingsStore::load() {
  path_ = fs_join(fs_app_data_dir(), "settings.json");
  std::string text;
  if (fs_read_text(path_, text) && !text.empty()) {
    Json json = Json::parse_or(text);
    settings_ = Settings::from_json(json);
    ONOW_INFO("Settings", "loaded %s", path_.c_str());
  } else {
    ONOW_INFO("Settings", "no stored settings; using defaults");
  }

  // --- Migrations, mirroring the web client's one-time provisions. ---

  // The live stats HUD shipped default-on. Sessions that predate the change
  // get it provisioned once; later explicit toggles are respected.
  if (!settings_.stats_hud_provisioned) {
    settings_.show_stats_on_launch = true;
    settings_.stats_hud_provisioned = true;
    dirty_ = true;
  }

  // Builds before the stream-mode setting persisted a mode the user never
  // chose, so until a mode is picked explicitly the shipped default owns it.
  if (!settings_.stream_mode_chosen) {
    settings_.stream_client_mode = "native";
    settings_.transport_mode = "nvst";
    dirty_ = true;
  }

  if (dirty_) save();
}

bool SettingsStore::save() const {
  const std::string text = settings_.to_json().dump(true);
  if (!fs_write_text(path_, text)) {
    ONOW_ERROR("Settings", "failed to write %s", path_.c_str());
    return false;
  }
  return true;
}

void SettingsStore::set_json(const std::string& key, const Json& value) {
  Settings next = settings_;
  const Json merged = next.to_json();
  const_cast<Json&>(merged)[key] = value;
  settings_ = Settings::from_json(merged);
  dirty_ = true;
  save();
}

void SettingsStore::reset_to_defaults() {
  settings_ = Settings();
  dirty_ = true;
  save();
}

} // namespace app
} // namespace onow
