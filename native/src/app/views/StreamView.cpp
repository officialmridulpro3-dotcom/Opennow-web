// Stream: the in-session view.
//
// StreamView.tsx plus StreamLoadingOverlay.tsx. The web client rendered the
// video element, a controls bar (microphone, fullscreen, stop), a stats
// overlay, a settings sidebar and — while the session was still queueing,
// setting up or connecting — a full-screen launch overlay with the live queue
// position and the ad state. The video surface itself is the platform
// backend's job; this file owns everything drawn on top of it.
#include "onow/app/App.h"
#include "onow/app/Views.h"

#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/Time.h"
#include "onow/i18n.h"
#include "onow/model/Types.h"

#include "imgui.h"

#include <algorithm>

namespace onow {
namespace app {

namespace {

// The four launch stages, matching the web client's overlay steps: the labels
// and the index of the step a given status sits on, in one place.
const char* const kStepKeys[] = {"streamLoading.steps.queue", "streamLoading.steps.setup",
                                 "streamLoading.steps.connect", "streamLoading.steps.ready"};

int stage_index(StreamStatus status) {
  switch (status) {
    case StreamStatus::Setup:
    case StreamStatus::Starting: return 1;
    case StreamStatus::Connecting: return 2;
    case StreamStatus::Streaming: return 3;
    case StreamStatus::Queue:
    case StreamStatus::Idle:
    default: return 0;
  }
}

const char* stage_status_key(StreamStatus status) {
  switch (status) {
    case StreamStatus::Queue: return "streamLoading.status.waitingInQueue";
    case StreamStatus::Setup: return "streamLoading.status.settingUpRig";
    case StreamStatus::Starting: return "streamLoading.status.startingStream";
    case StreamStatus::Connecting: return "streamLoading.status.connectingToServer";
    case StreamStatus::Streaming: return "streamLoading.status.loading";
    case StreamStatus::Idle:
    default: return "streamLoading.status.loading";
  }
}

void draw_launch_overlay(App& app, AppState& state, const ImVec2& min, const ImVec2& max) {
  const Palette& p = active_palette();
  const float scale = state.content_scale;
  ImDrawList* list = ImGui::GetWindowDrawList();

  draw_gradient(list, min, max, p.bg_a, p.bg_c);
  draw_glow(list, ImVec2((min.x + max.x) * 0.5f, min.y + 180.0f * scale), 380.0f * scale,
            p.accent_glow, 0.16f);

  const std::string title = state.launch_error.active ? tr("streamLoading.topbar.error")
                                                      : tr("streamLoading.topbar.settingUp");
  const ImVec2 title_size = measure_text(title.c_str(), type_scale().display);
  text_at(list, ImVec2((min.x + max.x) * 0.5f - title_size.x * 0.5f, min.y + 72.0f * scale), p.ink,
          title.c_str(), type_scale().display);

  const std::string status = tr(stage_status_key(state.stream_status));
  const ImVec2 status_size = measure_text(status.c_str(), type_scale().body);
  text_at(list, ImVec2((min.x + max.x) * 0.5f - status_size.x * 0.5f, min.y + 116.0f * scale),
          p.ink_soft, status.c_str(), type_scale().body);

  // Stage strip: Queue → Setup → Connect → Ready.
  const int active_step = stage_index(state.stream_status);
  const float step_width = 132.0f * scale;
  const float strip_x = (min.x + max.x) * 0.5f - step_width * 2.0f;
  for (int i = 0; i < 4; ++i) {
    const ImVec2 pos(strip_x + i * step_width, min.y + 168.0f * scale);
    const bool done = i < active_step;
    const bool current = i == active_step;
    const Rgb fill = done || current ? mix(p.accent, p.void_, current ? 0.0f : 0.55f) : p.card;
    list->AddRectFilled(pos, ImVec2(pos.x + step_width - 12.0f * scale, pos.y + 4.0f * scale),
                        col(fill));
    text_at(list, ImVec2(pos.x, pos.y + 14.0f * scale), current ? p.accent : p.ink_muted,
            tr(kStepKeys[i]).c_str(), type_scale().caption);
  }

  // Queue position, when the seat is still queued.
  if (state.stream_status == StreamStatus::Queue) {
    const std::string position = state.queue_position_known
                                     ? tr("streamLoading.status.positionInQueue",
                                          {{"position", std::to_string(state.queue_position)}})
                                     : tr("streamLoading.status.waitingInQueue");
    const ImVec2 pos_size = measure_text(position.c_str(), type_scale().title);
    text_at(list, ImVec2((min.x + max.x) * 0.5f - pos_size.x * 0.5f, min.y + 232.0f * scale), p.ink,
            position.c_str(), type_scale().title);
  }

  // Ad state, for the free tier.
  if (state.queue_ad.has_ad) {
    const std::string ad_title = tr("streamLoading.ads.advertisementInProgress");
    const ImVec2 ad_size = measure_text(ad_title.c_str(), type_scale().body);
    text_at(list, ImVec2((min.x + max.x) * 0.5f - ad_size.x * 0.5f, min.y + 276.0f * scale),
            p.ink_soft, ad_title.c_str(), type_scale().body);
  }

  // Elapsed, and the cancel affordance.
  const long long elapsed = state.session_elapsed_seconds();
  const std::string elapsed_label = tr("streamLoading.telemetry.elapsed") + " " + format_mmss(elapsed);
  const ImVec2 elapsed_size = measure_text(elapsed_label.c_str(), type_scale().caption);
  text_at(list, ImVec2((min.x + max.x) * 0.5f - elapsed_size.x * 0.5f, min.y + 320.0f * scale),
          p.ink_muted, elapsed_label.c_str(), type_scale().caption);

  ImGui::SetCursorScreenPos(ImVec2((min.x + max.x) * 0.5f - 110.0f * scale, min.y + 360.0f * scale));
  if (button_danger(tr("streamLoading.actions.cancelLoading"), ImVec2(220.0f * scale, 40.0f * scale))) {
    app.stop_stream();
  }

  if (!state.launch_poll_diagnostic.empty()) {
    const ImVec2 diag_size =
        measure_text(state.launch_poll_diagnostic.c_str(), type_scale().caption);
    text_at(list, ImVec2((min.x + max.x) * 0.5f - diag_size.x * 0.5f, max.y - 56.0f * scale),
            p.ink_muted,
            ellipsize(state.launch_poll_diagnostic, 420.0f * scale, type_scale().caption).c_str(),
            type_scale().caption);
  }
}

} // namespace

void draw_stream_view(App& app, AppState& state, const ImVec2& min, const ImVec2& max) {
  const Palette& p = active_palette();
  const float scale = state.content_scale;
  const Settings& settings = state.settings();

  // Anything short of Streaming is still the launch overlay.
  if (!state.is_streaming()) {
    draw_launch_overlay(app, state, min, max);
    return;
  }

  ImDrawList* list = ImGui::GetWindowDrawList();

  // The controls bar sits at the bottom of the viewport.
  const float bar_height = 56.0f * scale;
  const ImVec2 bar_min(min.x, max.y - bar_height);
  const ImVec2 bar_max(max.x, max.y);
  if (!settings.hide_stream_buttons) {
    draw_panel(list, bar_min, bar_max, mix(p.void_, p.bg_b, 0.5f), p.panel_border, 0.0f, 0.0f, 1.0f);
    const float cx = (bar_min.x + bar_max.x) * 0.5f;
    float x = cx - 96.0f * scale;
    const float by = bar_min.y + 10.0f * scale;
    const bool muted = settings.microphone_mode == "disabled";
    if (icon_button(muted ? "\xE2\x9C\x87" : "\xE2\x99\x9A", ImVec2(36.0f * scale, 36.0f * scale),
                    tr("stream.controls.muteMicrophone").c_str())) {
      app.update_setting_string("microphoneMode", muted ? "voice-activity" : "disabled");
    }
    x += 44.0f * scale;
    if (icon_button("\xE2\x9A\xA0", ImVec2(36.0f * scale, 36.0f * scale),
                    tr("stream.controls.enterFullscreen").c_str())) {
      state.session_fullscreen = !state.session_fullscreen;
    }
    x += 44.0f * scale;
    if (icon_button("\xE2\x96\xA0", ImVec2(36.0f * scale, 36.0f * scale),
                    tr("stream.sidebar.stopStream").c_str())) {
      app.stop_stream();
    }
    if (settings.show_anti_afk_indicator && state.anti_afk_enabled) {
      const std::string badge_text = tr("stream.controls.antiAfkEnabled");
      const ImVec2 badge_size = measure_text(badge_text.c_str(), type_scale().caption);
      const ImVec2 badge_pos(bar_max.x - 24.0f * scale - badge_size.x - 20.0f * scale,
                             by + 10.0f * scale);
      list->AddRectFilled(ImVec2(badge_pos.x - 10.0f * scale, badge_pos.y - 6.0f * scale),
                          ImVec2(badge_pos.x + badge_size.x + 10.0f * scale,
                                 badge_pos.y + badge_size.y + 6.0f * scale),
                          col(p.accent_surface), 999.0f);
      text_at(list, badge_pos, p.accent, badge_text.c_str(), type_scale().caption);
    }
  }

  // Stats overlay, fed by the engine's telemetry.
  if (state.show_stats_overlay) {
    const float panel_width = 320.0f * scale;
    const ImVec2 panel_min(min.x + 16.0f * scale, min.y + 16.0f * scale);
    ImGui::SetCursorScreenPos(panel_min);
    ImGui::BeginChild("StatsOverlay", ImVec2(panel_width, 260.0f * scale), false,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);
    hud_panel(tr("stream.stats.overlayLabel"), [&state, &p] {
      const StreamDiagnostics& d = state.diagnostics;
      hud_line(tr("stream.stats.live"), format_mmss(state.session_elapsed_seconds()), p.ink);
      hud_line(tr("stream.stats.bitrateShort"),
               to_string(static_cast<long long>(d.bitrate_kbps / 1000.0)) + " Mbps", p.accent);
      hud_line(tr("stream.stats.rtt"), to_string(d.rtt_ms) + " ms", p.ink_soft);
      hud_line(tr("stream.stats.decode"), to_string(d.decoded_fps) + " fps", p.ink_soft);
      hud_line(tr("stream.stats.packetLoss"), to_string(d.packet_loss_percent, 2) + " %",
               d.packet_loss_percent > 1.0 ? p.error : p.ink_soft);
      hud_line(tr("streamLoading.telemetry.session"), d.transport.empty() ? "-" : d.transport,
               p.ink_muted);
    });
    ImGui::EndChild();
  }

  // Settings sidebar.
  if (state.show_stream_sidebar) {
    const float panel_width = 300.0f * scale;
    const ImVec2 panel_min(max.x - panel_width, min.y);
    ImGui::SetCursorScreenPos(panel_min);
    ImGui::BeginChild("StreamSidebar", ImVec2(panel_width, max.y - min.y), false,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);
    ImGui::Dummy(ImVec2(0.0f, 14.0f * scale));
    ImGui::PushFont(type_scale().title);
    ImGui::TextColored(vec4(p.ink), "%s", tr("stream.sidebar.title").c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));
    if (state.session) {
      ImGui::PushFont(type_scale().caption);
      ImGui::TextColored(vec4(p.ink_muted), "%s", state.session->app_id.c_str());
      if (!state.session->zone.empty()) {
        ImGui::TextColored(vec4(p.ink_muted), "%s", state.session->zone.c_str());
      }
      ImGui::PopFont();
      ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));
    }
    const int remaining = state.session_time_remaining_seconds();
    if (remaining >= 0) {
      const std::string label = tr("stream.sidebar.remainingPlaytime") + " " + format_mmss(remaining);
      ImGui::PushFont(type_scale().caption);
      ImGui::TextColored(vec4(p.ink_soft), "%s", label.c_str());
      ImGui::PopFont();
      ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));
    }
    if (button_ghost(tr("stream.sidebar.toggleStats"), ImVec2(-1.0f, 0.0f))) {
      state.show_stats_overlay = !state.show_stats_overlay;
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    if (button_ghost(tr("stream.controls.enterFullscreen"), ImVec2(-1.0f, 0.0f))) {
      state.session_fullscreen = !state.session_fullscreen;
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    if (button_danger(tr("stream.sidebar.stopStream"), ImVec2(-1.0f, 0.0f))) {
      app.stop_stream();
    }
    ImGui::EndChild();
  }
}

} // namespace app
} // namespace onow
