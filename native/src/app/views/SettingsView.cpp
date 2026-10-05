// Settings: the tabbed inspector.
//
// SettingsScreen.tsx (plus the region picker, which the web client rendered as
// its own full page but which lives here as the first block of the Stream
// section). Sections are Account / Stream / Native Streamer / Game / Audio /
// Input / Interface / About, exactly like the web client's sidebar.
#include "onow/app/App.h"
#include "onow/app/Views.h"

#include "onow/Config.h"
#include "onow/Fs.h"
#include "onow/Str.h"
#include "onow/Time.h"
#include "onow/i18n.h"

#include "imgui.h"

#include <algorithm>

namespace onow {
namespace app {

namespace {

struct SectionDef {
  SettingsSection section;
  const char* label_key;
};

const SectionDef kSections[] = {
    {SettingsSection::Account, "settings.sections.account"},
    {SettingsSection::Stream, "settings.sections.stream"},
    {SettingsSection::NativeStreamer, "settings.sections.nativeStreamer"},
    {SettingsSection::Game, "settings.sections.game"},
    {SettingsSection::Audio, "settings.sections.audio"},
    {SettingsSection::Input, "settings.sections.input"},
    {SettingsSection::Interface, "settings.sections.interface"},
    {SettingsSection::About, "settings.sections.about"},
};

void label(const std::string& text, const Rgb& color, ImFont* font) {
  ImGui::PushFont(font);
  ImGui::TextColored(vec4(color), "%s", text.c_str());
  ImGui::PopFont();
}

void hint(const std::string& text, const Rgb& color, float width) {
  wrapped_text(text, width, color);
  ImGui::Dummy(ImVec2(0.0f, 6.0f));
}

void sub_header(const std::string& key) {
  const Palette& p = active_palette();
  ImGui::Dummy(ImVec2(0.0f, 14.0f));
  label(tr(key), p.ink, type_scale().title);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  ImGui::GetWindowDrawList()->AddLine(pos, ImVec2(pos.x + std::min(width, 220.0f), pos.y),
                                      col(mix(p.accent, p.void_, 0.4f)), 2.0f);
  ImGui::Dummy(ImVec2(0.0f, 10.0f));
}

// A labelled combo over an (id, label) list. `value` is updated in place.
void combo_string(const std::string& label_text, std::string& value,
                  const std::vector<std::pair<std::string, std::string>>& options) {
  label(label_text, active_palette().ink_soft, type_scale().body);
  std::vector<std::string> labels;
  int index = 0;
  for (size_t i = 0; i < options.size(); ++i) {
    if (options[i].first == value) index = static_cast<int>(i);
    labels.push_back(options[i].second);
  }
  ImGui::PushID(label_text.c_str());
  if (combo("##combo", labels, &index)) value = options[index].first;
  ImGui::PopID();
  ImGui::Dummy(ImVec2(0.0f, 8.0f));
}

void text_row(const std::string& label_text, std::string& value) {
  input_text(label_text, &value);
  ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

} // namespace

void draw_settings_view(App& app, AppState& state, const ImVec2& min, const ImVec2& max) {
  const Palette& p = active_palette();
  const float scale = state.content_scale;

  // Left: section rail.
  const float rail_width = 180.0f * scale;
  ImGui::SetCursorScreenPos(min);
  ImGui::BeginChild("SettingsRail", ImVec2(rail_width, max.y - min.y), false,
                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);
  for (const SectionDef& section : kSections) {
    const bool selected = state.settings_section == section.section;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const bool hovered =
        ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + width, pos.y + 40.0f * scale));
    ImDrawList* list = ImGui::GetWindowDrawList();
    if (selected) {
      list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + 40.0f * scale), col(p.accent_surface),
                          metrics().radius_md * scale);
      list->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + 3.0f * scale, pos.y + 40.0f * scale),
                          col(p.accent));
    } else if (hovered) {
      list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + 40.0f * scale), col(p.card),
                          metrics().radius_md * scale);
    }
    text_at(list, ImVec2(pos.x + 14.0f * scale, pos.y + 12.0f * scale),
            selected ? p.ink : p.ink_soft, tr(section.label_key).c_str(), type_scale().body);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      state.settings_section = section.section;
    }
    ImGui::Dummy(ImVec2(width, 44.0f * scale));
  }
  ImGui::EndChild();

  // Right: the section body.
  const ImVec2 body_min(min.x + rail_width + 16.0f * scale, min.y);
  const ImVec2 body_max(max.x, max.y);
  ImGui::SetCursorScreenPos(body_min);
  ImGui::BeginChild("SettingsBody", ImVec2(body_max.x - body_min.x, body_max.y - body_min.y), false,
                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);

  Settings& settings = state.settings();

  if (state.settings_section == SettingsSection::Account) {
    sub_header("settings.sections.account");
    if (state.auth_session) {
      const model::AuthSession& session = *state.auth_session;
      label(session.user.display_name.empty() ? session.user.email : session.user.display_name,
            p.ink, type_scale().title);
      ImGui::Dummy(ImVec2(0.0f, 2.0f));
      label(session.user.email, p.ink_muted, type_scale().caption);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      label(session.provider.display_name, p.ink_soft, type_scale().caption);
      ImGui::Dummy(ImVec2(0.0f, 14.0f));
      if (state.subscription) {
        label(tr("navbar.time.tier") + ": " + state.subscription->membership_tier, p.ink_soft,
              type_scale().caption);
        for (const auto& entry : state.subscription->entitled_resolutions) {
          const std::string line = entry.first + " · " + to_string(entry.second) + " fps";
          label(line, p.ink_muted, type_scale().caption);
        }
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
      }
    }
    if (button_danger(tr("auth.accounts.signOutAll"), ImVec2(-1.0f, 0.0f))) {
      app.logout();
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    if (button_ghost(tr("auth.accounts.switchAccount"), ImVec2(-1.0f, 0.0f))) {
      app.switch_account(state.auth_session ? state.auth_session->user.user_id : std::string());
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    if (button_ghost(tr("settings.accountConnections.refresh"), ImVec2(-1.0f, 0.0f))) {
      app.refresh_subscription();
    }
  } else if (state.settings_section == SettingsSection::Stream) {
    sub_header("settings.video.title");
    combo_string(tr("settings.video.resolution"), settings.resolution,
                 {{"1280x720", "720p"},
                  {"1920x1080", "1080p"},
                  {"2560x1440", "1440p"},
                  {"3840x2160", "2160p"}});
    combo_string(tr("settings.video.aspectRatio"), settings.aspect_ratio,
                 {{"16:9", "16:9"}, {"16:10", "16:10"}, {"21:9", "21:9"}, {"4:3", "4:3"}});
    {
      // FPS is an int, so it maps through an index like the web client's select.
      static const std::vector<int> kFps = {30, 60, 120};
      static const std::vector<std::string> kFpsLabels = {"30", "60", "120"};
      int index = 1;
      for (size_t i = 0; i < kFps.size(); ++i) {
        if (kFps[i] == settings.fps) index = static_cast<int>(i);
      }
      label(tr("settings.video.fps"), p.ink_soft, type_scale().body);
      ImGui::PushID("fps");
      if (combo("##combo", kFpsLabels, &index)) settings.fps = kFps[index];
      ImGui::PopID();
      ImGui::Dummy(ImVec2(0.0f, 8.0f));
    }
    combo_string(tr("settings.video.codec"), settings.codec,
                 {{"H264", "H.264"}, {"H265", "H.265"}, {"AV1", "AV1"}});
    combo_string(tr("settings.video.decoder"), settings.decoder_preference,
                 {{"auto", tr("app.labels.auto")},
                  {"hardware", tr("app.labels.hardware")},
                  {"software", tr("app.labels.softwareCpu")}});
    combo_string(tr("settings.video.encoder"), settings.encoder_preference,
                 {{"auto", tr("app.labels.auto")},
                  {"hardware", tr("app.labels.hardware")},
                  {"software", tr("app.labels.softwareCpu")}});
    combo_string(tr("settings.video.colorDepth"), settings.color_quality,
                 {{"8bit_420", "8-bit 4:2:0"},
                  {"10bit_420", "10-bit 4:2:0"},
                  {"10bit_444", "10-bit 4:4:4"}});
    label(tr("settings.video.maxBitrate"), p.ink_soft, type_scale().body);
    ImGui::PushID("maxBitrate");
    slider_int(tr("settings.video.maxBitrate"), &settings.max_bitrate_mbps, 5, 150,
               tr("app.units.bitrateMbps").c_str());
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    label(tr("settings.video.recordingBitrate"), p.ink_soft, type_scale().body);
    hint(tr("settings.video.recordingBitrateHint"), p.ink_muted, 380.0f * scale);
    ImGui::PushID("recordingBitrate");
    slider_int(tr("settings.video.recordingBitrate"), &settings.recording_bitrate_mbps, 0, 150,
               tr("app.units.bitrateMbps").c_str());
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    sub_header("settings.videoFilters.title");
    hint(tr("settings.videoFilters.hint"), p.ink_muted, 420.0f * scale);
    ImGui::PushID("shaderEnabled");
    toggle(tr("settings.videoFilters.sharpen"), &settings.video_shader.enabled);
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (settings.video_shader.enabled) {
      ImGui::PushID("sharpen");
      slider_float(tr("settings.videoFilters.sharpen"), &settings.video_shader.sharpness, 0.0f, 1.0f);
      ImGui::PopID();
      ImGui::PushID("saturation");
      slider_float(tr("settings.videoFilters.saturation"), &settings.video_shader.saturation, 0.0f,
                   2.0f);
      ImGui::PopID();
      ImGui::PushID("contrast");
      slider_float(tr("settings.videoFilters.contrast"), &settings.video_shader.contrast, 0.0f, 2.0f);
      ImGui::PopID();
      ImGui::PushID("brightness");
      slider_float(tr("settings.videoFilters.brightness"), &settings.video_shader.brightness, 0.0f,
                   2.0f);
      ImGui::PopID();
      ImGui::PushID("vibrance");
      slider_float(tr("settings.videoFilters.vibrance"), &settings.video_shader.noise_reduction, 0.0f,
                   1.0f);
      ImGui::PopID();
      ImGui::Dummy(ImVec2(0.0f, 8.0f));
      if (button_ghost(tr("settings.videoFilters.reset"))) {
        settings.video_shader = VideoShaderSettings{};
      }
    }

    sub_header("settings.region.title");
    if (state.regions.empty()) {
      label(tr("settings.region.noRegionsAvailable"), p.ink_muted, type_scale().caption);
      ImGui::Dummy(ImVec2(0.0f, 6.0f));
      if (button_ghost(tr("settings.region.retry"))) app.refresh_regions();
    } else {
      std::vector<std::string> items = {tr("settings.region.autoBest")};
      for (const model::StreamRegion& region : state.regions) {
        items.push_back(region.name.empty() ? region.url : region.name);
      }
      int selected = 0;
      if (!settings.region.empty()) {
        for (size_t i = 0; i < state.regions.size(); ++i) {
          if (state.regions[i].url == settings.region) selected = static_cast<int>(i) + 1;
        }
      }
      ImGui::PushID("region");
      if (combo("##region", items, &selected)) {
        settings.region = selected == 0 ? std::string() : state.regions[selected - 1].url;
      }
      ImGui::PopID();
      ImGui::Dummy(ImVec2(0.0f, 8.0f));
      for (const model::StreamRegion& region : state.regions) {
        const std::string ping = region.ping_failed
                                     ? tr("gameCard.status.unavailable")
                                     : (region.ping_ms < 0 ? std::string("-")
                                                          : to_string(region.ping_ms) + " ms");
        label("  " + region.url + " · " + ping, p.ink_muted, type_scale().caption);
      }
      ImGui::Dummy(ImVec2(0.0f, 8.0f));
      if (button_ghost(tr("settings.region.refreshPing"))) app.refresh_regions();
    }
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::PushID("sessionProxy");
    toggle(tr("settings.video.sessionProxy"), &settings.session_proxy_enabled);
    ImGui::PopID();
    hint(tr("settings.video.sessionProxyHint"), p.ink_muted, 420.0f * scale);
    if (settings.session_proxy_enabled) {
      text_row(tr("settings.video.sessionProxy"), settings.session_proxy_url);
    }
  } else if (state.settings_section == SettingsSection::NativeStreamer) {
    sub_header("settings.nativeStreamer.title");
    {
      // `stream_client_mode` is the persisted switch; the web client kept a
      // boolean and derived this, so mirror the derived value here.
      bool native_streaming = settings.stream_client_mode == "native";
      ImGui::PushID("nativeStreaming");
      toggle(tr("settings.nativeStreamer.nativeStreaming"), &native_streaming);
      ImGui::PopID();
      settings.stream_client_mode = native_streaming ? "native" : "web";
      hint(tr("settings.nativeStreamer.nativeStreamingHint"), p.ink_muted, 420.0f * scale);
    }
    ImGui::PushID("showNativeStats");
    toggle(tr("settings.nativeStreamer.showNativeStreamerStats"), &settings.show_native_streamer_stats);
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::PushID("externalRenderer");
    toggle(tr("settings.nativeStreamer.renderModeExternal"), &settings.native_external_renderer);
    ImGui::PopID();
    hint(tr("settings.nativeStreamer.renderModeHint"), p.ink_muted, 420.0f * scale);
    combo_string(tr("settings.nativeStreamer.directxBackend"), settings.native_d3d_fullscreen_mode,
                 {{"auto", tr("app.labels.auto")}, {"d3d11", "DirectX 11"}, {"d3d12", "DirectX 12"}});
    combo_string(tr("settings.nativeStreamer.framePacing"), settings.native_cloud_gsync_mode,
                 {{"auto", tr("settings.nativeStreamer.lowestLatency")},
                  {"smooth", tr("settings.nativeStreamer.smoothGsync")}});
    combo_string(tr("settings.nativeStreamer.transportMode"), settings.transport_mode,
                 {{"webrtc", tr("settings.nativeStreamer.transportModeWebrtc")},
                  {"nvst", tr("settings.nativeStreamer.transportModeNvst")}});
    hint(tr("settings.nativeStreamer.transportModeHint"), p.ink_muted, 420.0f * scale);
    text_row(tr("settings.nativeStreamer.gstreamerRuntime"), settings.native_streamer_executable_path);
  } else if (state.settings_section == SettingsSection::Game) {
    sub_header("settings.game.title");
    combo_string(tr("settings.game.language"), settings.game_language,
                 {{"en_US", "English (US)"},
                  {"de_DE", "Deutsch"},
                  {"es_ES", "Español"},
                  {"fr_FR", "Français"},
                  {"ja_JP", "日本語"},
                  {"ko_KR", "한국어"},
                  {"nl_NL", "Nederlands"},
                  {"pl_PL", "Polski"},
                  {"pt_BR", "Português (Brasil)"},
                  {"ru_RU", "Русский"},
                  {"tr_TR", "Türkçe"},
                  {"zh_CN", "简体中文"}});
    hint(tr("settings.game.inGameLanguageHint"), p.ink_muted, 420.0f * scale);
    ImGui::PushID("persistGameSettings");
    toggle(tr("settings.game.persistInGameSettings"), &settings.enable_persisting_in_game_settings);
    ImGui::PopID();
    hint(tr("settings.game.persistInGameSettingsHint"), p.ink_muted, 420.0f * scale);
    combo_string(tr("settings.game.keyboardLayout"), settings.keyboard_layout,
                 {{"en_US", "US"}, {"en_GB", "UK"}, {"de_DE", "DE"}, {"fr_FR", "FR"}});
  } else if (state.settings_section == SettingsSection::Audio) {
    sub_header("settings.audio.title");
    {
      bool mic_enabled = settings.microphone_mode != "disabled";
      ImGui::PushID("micEnabled");
      toggle(tr("settings.audio.microphone"), &mic_enabled);
      ImGui::PopID();
      settings.microphone_mode = mic_enabled ? "voice-activity" : "disabled";
      hint(tr("settings.audio.microphoneHint"), p.ink_muted, 420.0f * scale);
    }
    combo_string(tr("settings.audio.microphoneMode"), settings.microphone_mode,
                 {{"disabled", tr("settings.audio.disabled")},
                  {"push-to-talk", tr("settings.audio.pushToTalk")},
                  {"voice-activity", tr("settings.audio.voiceActivity")}});
    text_row(tr("settings.audio.microphoneDevice"), settings.microphone_device_id);
  } else if (state.settings_section == SettingsSection::Input) {
    sub_header("settings.input.mouseAndKeyboard");
    ImGui::PushID("clipboard");
    toggle(tr("settings.input.clipboardPaste"), &settings.clipboard_paste);
    ImGui::PopID();
    ImGui::PushID("cursorOverlay");
    toggle(tr("settings.input.nativeCursorOverlay"), &settings.native_cursor_overlay);
    ImGui::PopID();
    hint(tr("settings.input.nativeCursorOverlayHint"), p.ink_muted, 420.0f * scale);
    ImGui::PushID("mouseSensitivity");
    slider_float(tr("settings.input.mouseSensitivity"), &settings.mouse_sensitivity, 0.25f, 3.0f,
                 "%.2fx");
    ImGui::PopID();
    ImGui::PushID("mouseAcceleration");
    slider_float(tr("settings.input.mouseAccelerator"), &settings.mouse_acceleration, 0.0f, 2.0f,
                 "%.2fx");
    ImGui::PopID();
    ImGui::PushID("gyro");
    toggle(tr("settings.input.gyroscopeControls"), &settings.enable_gyroscope_controls);
    ImGui::PopID();
    ImGui::PushID("steamController");
    toggle(tr("settings.input.steamControllerCompatibilityMode"),
           &settings.steam_controller_compatibility_mode);
    ImGui::PopID();

    sub_header("settings.input.shortcuts");
    // Every binding is a text row; the web client had an editor dialog, but a
    // text field is easier to drive with a keyboard-only shell.
    const std::vector<std::pair<std::string, std::string Settings::*>> shortcuts = {
        {tr("settings.input.toggleStats"), &Settings::shortcut_toggle_stats},
        {tr("settings.input.togglePointerLock"), &Settings::shortcut_toggle_pointer_lock},
        {tr("settings.input.toggleFullscreen"), &Settings::shortcut_toggle_fullscreen},
        {tr("settings.input.stopStream"), &Settings::shortcut_stop_stream},
        {tr("settings.input.toggleAntiAfk"), &Settings::shortcut_toggle_anti_afk},
        {tr("settings.input.toggleMicrophone"), &Settings::shortcut_toggle_microphone},
        {tr("settings.input.screenshot"), &Settings::shortcut_screenshot},
        {tr("settings.input.recording"), &Settings::shortcut_toggle_recording},
    };
    for (const auto& shortcut : shortcuts) {
      input_text(shortcut.first, &(settings.*shortcut.second));
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }
    if (button_ghost(tr("settings.input.resetToDefaults"))) {
      settings.shortcut_toggle_stats = "F3";
      settings.shortcut_toggle_pointer_lock = "F8";
      settings.shortcut_toggle_fullscreen = "F10";
      settings.shortcut_stop_stream = "Ctrl+Shift+Q";
      settings.shortcut_toggle_anti_afk = "Ctrl+Shift+K";
      settings.shortcut_toggle_microphone = "Ctrl+Shift+M";
      settings.shortcut_screenshot = "F11";
      settings.shortcut_toggle_recording = "F12";
    }
  } else if (state.settings_section == SettingsSection::Interface) {
    sub_header("settings.interface.appearance");
    // Language picker over the loaded locale bundles.
    const std::vector<std::string>& locales = I18n::instance().available_locales();
    std::vector<std::string> locale_labels;
    int locale_index = 0;
    for (size_t i = 0; i < locales.size(); ++i) {
      locale_labels.push_back(I18n::instance().display_name(locales[i]));
      if (locales[i] == I18n::instance().locale()) locale_index = static_cast<int>(i);
    }
    label(tr("settings.interface.appLanguage"), p.ink_soft, type_scale().body);
    ImGui::PushID("locale");
    if (combo("##locale", locale_labels, &locale_index)) {
      app.apply_locale(locales[locale_index]);
    }
    ImGui::PopID();
    hint(tr("settings.interface.appLanguageHint"), p.ink_muted, 420.0f * scale);

    combo_string(tr("settings.interface.accentColor"), settings.app_accent_color,
                 {{"green", tr("settings.interface.accentColorNeon")},
                  {"blue", tr("settings.interface.accentColorAzure")},
                  {"white", tr("settings.interface.accentColorWhite")},
                  {"steel", tr("settings.interface.accentColorSteel")}});
    hint(tr("settings.interface.accentColorHint"), p.ink_muted, 420.0f * scale);
    app.apply_accent();

    ImGui::PushID("translucent");
    toggle(tr("settings.interface.translucentUI"), &settings.translucent_ui);
    ImGui::PopID();
    ImGui::PushID("controllerMode");
    toggle(tr("settings.interface.controllerMode"), &settings.controller_mode);
    ImGui::PopID();
    ImGui::PushID("hideButtons");
    toggle(tr("settings.interface.hideStreamOverlayButtons"), &settings.hide_stream_buttons);
    ImGui::PopID();
    ImGui::PushID("showStats");
    toggle(tr("settings.interface.showStatsOnStreamLaunch"), &settings.show_stats_on_launch);
    ImGui::PopID();
    ImGui::PushID("hideServer");
    toggle(tr("settings.interface.hideServerSelector"), &settings.hide_server_selector);
    ImGui::PopID();
    ImGui::PushID("antiAfk");
    toggle(tr("settings.interface.showAntiAfkIndicator"), &settings.show_anti_afk_indicator);
    ImGui::PopID();
    ImGui::PushID("autoFullscreen");
    toggle(tr("settings.interface.autoFullScreen"), &settings.auto_full_screen);
    ImGui::PopID();
    ImGui::PushID("escapeFullscreen");
    toggle(tr("settings.interface.escapeExitsFullscreen"),
           &settings.allow_escape_to_exit_fullscreen);
    ImGui::PopID();
    ImGui::PushID("discord");
    toggle(tr("settings.interface.discordRichPresence"), &settings.discord_rich_presence);
    ImGui::PopID();
    ImGui::PushID("posterSize");
    slider_float(tr("settings.interface.posterSize"), &settings.poster_size_scale, 0.75f, 1.5f,
                 "%.2fx");
    ImGui::PopID();
    ImGui::PushID("sessionCounter");
    toggle(tr("settings.interface.sessionElapsedCounter"), &settings.session_counter_enabled);
    ImGui::PopID();
    ImGui::PushID("sessionTime");
    toggle(tr("settings.interface.showSessionTimeRemainingInStatsOverlay"),
           &settings.show_session_time_remaining_in_stats_overlay);
    ImGui::PopID();
    ImGui::PushID("timerReappear");
    slider_int(tr("settings.interface.sessionTimerReappear"),
               &settings.session_clock_show_every_minutes, 0, 180,
               tr("settings.interface.everyMinutes").c_str());
    ImGui::PopID();
    ImGui::PushID("timerVisible");
    slider_int(tr("settings.interface.sessionTimerVisibleTime"),
               &settings.session_clock_show_duration_seconds, 1, 120,
               tr("app.units.seconds").c_str());
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0.0f, 12.0f));
    if (button_danger(tr("app.actions.reset"))) {
      state.modal.open = true;
      state.modal.title = tr("settings.interface.appearance");
      state.modal.body = tr("app.actions.reset");
      state.modal.options.clear();
      state.modal.options.push_back(
          {"reset", tr("app.actions.confirm"), [] { SettingsStore::instance().reset_to_defaults(); }});
    }
  } else {
    sub_header("settings.about.applicationUpdates");
    label(tr("settings.about.version", {{"version", ONOW_VERSION_STRING}}), p.ink_soft,
          type_scale().caption);
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::PushID("autoUpdate");
    toggle(tr("settings.about.automaticallyCheckForUpdates"), &settings.auto_check_for_updates);
    ImGui::PopID();
    combo_string(tr("settings.about.updateChannel"), settings.update_channel,
                 {{"stable", tr("settings.about.updateChannelStable")},
                  {"nightly", tr("settings.about.updateChannelNightly")}});
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (button_ghost(tr("settings.about.exportLogs"))) {
      // The diagnostics log is appended continuously, so exporting is a copy.
      const std::string source = fs_join(fs_app_data_dir(), "diagnostics.log");
      const std::string dest = fs_join(fs_app_data_dir(), "diagnostics-export.log");
      std::string contents;
      if (fs_read_text(source, contents) && fs_write_text(dest, contents)) {
        state.push_toast(ToastKind::Success, tr("settings.about.exportLogs"), dest);
      } else {
        state.push_toast(ToastKind::Error, tr("app.status.error"),
                         tr("settings.about.exportLogsFailed"));
      }
    }
    ImGui::Dummy(ImVec2(0.0f, 14.0f));
    sub_header("settings.sections.thanks");
    wrapped_text("OpenNOW is a community client. Thanks to everyone testing the native port.",
                 420.0f * scale, p.ink_muted);
  }

  ImGui::EndChild();
}

} // namespace app
} // namespace onow
