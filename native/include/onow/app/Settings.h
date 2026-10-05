// Settings. One struct mirroring the web client's `Settings`, one JSON file in
// the app-data directory, and the same migration markers the web client used
// (`statsHudProvisioned`, `streamModeChosen`) so an existing user's choices are
// respected rather than silently overwritten by a new default.
#pragma once

#include <string>
#include <vector>

#include "onow/Json.h"

namespace onow {
namespace app {

struct VideoShaderSettings {
  bool enabled = false;
  float sharpness = 0.0f;
  float saturation = 1.0f;
  float contrast = 1.0f;
  float brightness = 1.0f;
  float noise_reduction = 0.0f;
  bool chroma_upsampling = false;
};

struct Settings {
  // Stream quality.
  std::string resolution = "1920x1080";
  std::string aspect_ratio = "16:9";
  float poster_size_scale = 1.05f;
  int fps = 60;
  int max_bitrate_mbps = 75;
  int recording_bitrate_mbps = 0; // 0 = automatic
  std::string stream_client_mode = "native"; // native | web
  std::string native_streamer_backend = "gstreamer";
  std::string native_video_backend = "auto";
  std::string native_streamer_executable_path;
  std::string native_cloud_gsync_mode = "auto";
  std::string native_d3d_fullscreen_mode = "auto";
  bool native_external_renderer = false;
  std::string transport_mode = "nvst"; // nvst | webrtc
  bool show_native_streamer_stats = false;
  std::string codec = "H264";
  std::string decoder_preference = "auto";
  std::string encoder_preference = "auto";
  std::string color_quality = "8bit_420";
  std::string region;

  // Network.
  bool session_proxy_enabled = false;
  std::string session_proxy_url;
  bool enable_l4s = false;
  bool enable_cloud_gsync = false;

  // Input.
  bool clipboard_paste = true;
  bool enable_gyroscope_controls = false;
  bool steam_controller_compatibility_mode = false;
  bool native_cursor_overlay = true;
  float mouse_sensitivity = 1.0f;
  float mouse_acceleration = 1.0f;
  std::string keyboard_layout = "en_US";
  bool allow_escape_to_exit_fullscreen = false;

  // Shortcuts.
  std::string shortcut_toggle_stats = "F3";
  std::string shortcut_toggle_pointer_lock = "F8";
  std::string shortcut_toggle_fullscreen = "F10";
  std::string shortcut_stop_stream = "Ctrl+Shift+Q";
  std::string shortcut_toggle_anti_afk = "Ctrl+Shift+K";
  std::string shortcut_toggle_microphone = "Ctrl+Shift+M";
  std::string shortcut_screenshot = "F11";
  std::string shortcut_toggle_recording = "F12";

  // Audio.
  std::string microphone_mode = "disabled"; // disabled | push-to-talk | voice-activity
  std::string microphone_device_id;

  // Stream chrome.
  bool hide_stream_buttons = false;
  bool show_anti_afk_indicator = true;
  bool show_stats_on_launch = true;
  bool stats_hud_provisioned = false;
  bool stream_mode_chosen = false;
  bool hide_server_selector = false;
  VideoShaderSettings video_shader;

  // Interface.
  std::string app_accent_color = "green"; // green | blue | violet | amber | rose
  std::string app_theme = "auto";
  bool translucent_ui = false;
  bool controller_mode = false;
  bool launch_in_console_mode = false;
  bool auto_full_screen = false;
  std::vector<std::string> favorite_game_ids;
  bool session_counter_enabled = false;
  bool show_session_time_remaining_in_stats_overlay = false;
  int session_clock_show_every_minutes = 60;
  int session_clock_show_duration_seconds = 30;
  int window_width = 1400;
  int window_height = 900;

  // Game session.
  std::string game_language = "en_US";
  bool enable_persisting_in_game_settings = true;

  // Misc.
  bool discord_rich_presence = false;
  bool auto_check_for_updates = false;
  std::string update_channel = "stable";
  std::string last_seen_release_highlights_version = "0.5.1-web";

  Json to_json() const;
  static Settings from_json(const Json& json);
};

class SettingsStore {
public:
  static SettingsStore& instance();

  // Loads from disk (creating defaults when absent) and applies migrations.
  void load();
  // Writes synchronously; cheap enough to call on every change.
  bool save() const;

  Settings& get() { return settings_; }
  const Settings& get() const { return settings_; }

  // Mutates a field and persists immediately. `key` is the JSON key.
  void set_json(const std::string& key, const Json& value);

  // Typed convenience wrappers.
  void set_string(const std::string& key, const std::string& value) { set_json(key, Json(value)); }
  void set_bool(const std::string& key, bool value) { set_json(key, Json(value)); }
  void set_int(const std::string& key, int value) { set_json(key, Json(value)); }
  void set_float(const std::string& key, float value) { set_json(key, Json(static_cast<double>(value))); }
  void reset_to_defaults();

  bool dirty() const { return dirty_; }
  const std::string& path() const { return path_; }

private:
  SettingsStore() = default;
  Settings settings_;
  std::string path_;
  bool dirty_ = false;
};

} // namespace app
} // namespace onow
