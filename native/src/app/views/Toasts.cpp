// Toasts and the modal dialogs.
//
// ToastStack.tsx plus the dialogs App.tsx opened (the free-tier server picker
// and the "stop session?" confirmation). The web client animated these in from
// the corner; a native shell has no CSS transitions, so they pop in and the
// host owns their lifetime.
#include "onow/app/App.h"
#include "onow/app/Views.h"

#include "onow/Str.h"
#include "onow/Time.h"
#include "onow/i18n.h"

#include "imgui.h"

#include <algorithm>

namespace onow {
namespace app {

namespace {

ImVec4 toast_color(ToastKind kind, const Palette& p) {
  switch (kind) {
    case ToastKind::Success: return vec4(p.accent);
    case ToastKind::Error: return vec4(p.error);
    case ToastKind::Warning: return vec4(p.warning);
    case ToastKind::Info:
    default: return vec4(p.info);
  }
}

// The modal scrim: void at 72%, the same overlay the web client's dialogs used.
ImU32 scrim_color(const Palette& p) {
  Rgb c = p.void_;
  c.a = 0.72f;
  return col(c);
}

} // namespace

void draw_toasts(App& app, AppState& state, const ImVec2& min, const ImVec2& max) {
  const Palette& p = active_palette();
  const float scale = state.content_scale;
  const int64_t now = now_ms();
  ImDrawList* list = ImGui::GetWindowDrawList();

  const float width = 320.0f * scale;
  float y = min.y + 16.0f * scale;
  for (const ToastMessage& item : state.toasts) {
    if (now - item.at_ms > item.ttl_ms) continue;
    const ImVec2 panel_min(max.x - width - 16.0f * scale, y);
    const ImVec2 panel_max(max.x - 16.0f * scale, y + 72.0f * scale);
    draw_shadow(list, panel_min, panel_max, metrics().radius_md * scale);
    draw_panel(list, panel_min, panel_max, mix(p.void_, p.bg_b, 0.7f), p.panel_border, 0.0f,
               metrics().radius_md * scale, 1.0f);
    // Accent spine so the kind is readable at a glance.
    list->AddRectFilled(panel_min, ImVec2(panel_min.x + 3.0f * scale, panel_max.y),
                        ImGui::ColorConvertFloat4ToU32(toast_color(item.kind, p)));
    const ImVec2 text_pos(panel_min.x + 16.0f * scale, panel_min.y + 12.0f * scale);
    text_at(list, text_pos, p.ink,
            ellipsize(item.title, width - 40.0f * scale, type_scale().body).c_str(),
            type_scale().body);
    if (!item.detail.empty()) {
      text_at(list, ImVec2(text_pos.x, text_pos.y + 20.0f * scale), p.ink_muted,
              ellipsize(item.detail, width - 40.0f * scale, type_scale().caption).c_str(),
              type_scale().caption);
    }
    y += 80.0f * scale;
  }
  (void)app;
}

void draw_modal(App& app, AppState& state, const ImVec2& min, const ImVec2& max) {
  if (!state.modal.open) return;
  const Palette& p = active_palette();
  const float scale = state.content_scale;
  ImDrawList* list = ImGui::GetWindowDrawList();

  // Scrim.
  list->AddRectFilled(min, max, scrim_color(p));

  const float width = std::min(440.0f * scale, (max.x - min.x) * 0.9f);
  const float height = 240.0f * scale;
  const ImVec2 panel_min((min.x + max.x) * 0.5f - width * 0.5f, (min.y + max.y) * 0.5f - height * 0.5f);
  const ImVec2 panel_max(panel_min.x + width, panel_min.y + height);
  draw_shadow(list, panel_min, panel_max, metrics().radius_lg * scale);
  draw_panel(list, panel_min, panel_max, mix(p.void_, p.bg_b, 0.85f), p.panel_border, 0.0f,
             metrics().radius_lg * scale, 1.0f);

  ImGui::SetCursorScreenPos(ImVec2(panel_min.x + 24.0f * scale, panel_min.y + 20.0f * scale));
  ImGui::BeginChild("ModalBody", ImVec2(width - 48.0f * scale, height - 40.0f * scale), false,
                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);

  ImGui::PushFont(type_scale().display);
  ImGui::TextColored(vec4(p.ink), "%s", state.modal.title.c_str());
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
  wrapped_text(state.modal.body, width - 48.0f * scale, p.ink_soft);
  ImGui::Dummy(ImVec2(0.0f, 14.0f * scale));

  for (const ModalOption& option : state.modal.options) {
    ImGui::PushID(option.id.c_str());
    if (button_accent(option.label, ImVec2(-1.0f, 38.0f * scale))) {
      if (option.action) option.action();
      state.modal.open = false;
      state.modal.options.clear();
    }
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
  }
  if (button_ghost(tr("app.actions.cancel"))) {
    state.modal.open = false;
    state.modal.options.clear();
  }
  ImGui::EndChild();
  (void)app;
}

void draw_queue_server_modal(App& app, AppState& state, const ImVec2& min, const ImVec2& max) {
  // The free-tier picker. NVIDIA routes free accounts through whichever
  // CloudMatch server has capacity; the web client let the user pick a zone
  // first (with live waits from the community queue API) and then launched
  // straight into that zone's streaming base URL.
  if (!state.queue_modal_open) return;
  const Palette& p = active_palette();
  const float scale = state.content_scale;
  ImDrawList* list = ImGui::GetWindowDrawList();

  list->AddRectFilled(min, max, scrim_color(p));

  const float width = std::min(460.0f * scale, (max.x - min.x) * 0.92f);
  const float height = std::min(340.0f * scale, (max.y - min.y) * 0.8f);
  const ImVec2 panel_min((min.x + max.x) * 0.5f - width * 0.5f, (min.y + max.y) * 0.5f - height * 0.5f);
  const ImVec2 panel_max(panel_min.x + width, panel_min.y + height);
  draw_shadow(list, panel_min, panel_max, metrics().radius_lg * scale);
  draw_panel(list, panel_min, panel_max, mix(p.void_, p.bg_b, 0.85f), p.panel_border, 0.0f,
             metrics().radius_lg * scale, 1.0f);

  ImGui::SetCursorScreenPos(ImVec2(panel_min.x + 24.0f * scale, panel_min.y + 20.0f * scale));
  ImGui::BeginChild("QueueModalBody", ImVec2(width - 48.0f * scale, height - 40.0f * scale), false,
                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);

  ImGui::PushFont(type_scale().display);
  ImGui::TextColored(vec4(p.ink), "%s", tr("queue.servers.allServers").c_str());
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));

  if (state.queue_servers.empty()) {
    spinner(tr("common.loading").c_str(), 10.0f);
  } else {
    for (const QueueServer& server : state.queue_servers) {
      ImGui::PushID(server.id.c_str());
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      const float row_width = ImGui::GetContentRegionAvail().x;
      const bool hovered =
          ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + row_width, pos.y + 52.0f * scale));
      draw_panel(list, pos, ImVec2(pos.x + row_width, pos.y + 52.0f * scale),
                 hovered ? p.card_hover : p.card, p.panel_border, 0.0f, metrics().radius_md * scale,
                 1.0f);
      text_at(list, ImVec2(pos.x + 16.0f * scale, pos.y + 16.0f * scale), p.ink,
              server.label.c_str(), type_scale().body);
      if (server.latency_ms >= 0) {
        const std::string wait = tr("queue.servers.wait", {{"duration", format_mmss(server.latency_ms / 1000)}});
        text_at(list, ImVec2(pos.x + 16.0f * scale, pos.y + 30.0f * scale), p.ink_muted,
                wait.c_str(), type_scale().caption);
      }
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        state.selected_queue_server_id = server.id;
        state.queue_modal_open = false;
        // Launching into the picked zone is the same call the default path
        // makes, with the zone's streaming base URL substituted.
        if (state.queue_modal_game) {
          const std::string base = "https://" + str_lower(server.id) +
                                   ".cloudmatchbeta.nvidiagrid.net/";
          app.play_game(*state.queue_modal_game, std::string(), base);
        }
      }
      ImGui::Dummy(ImVec2(row_width, 52.0f * scale));
      ImGui::PopID();
    }
  }

  ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
  if (button_ghost(tr("queue.servers.cancel"))) {
    state.queue_modal_open = false;
  }
  ImGui::EndChild();
}

} // namespace app
} // namespace onow
