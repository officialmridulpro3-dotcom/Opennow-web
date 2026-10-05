// Playtime report: totals, ranked list, recent sessions.
//
// PlaytimePage.tsx. The web client listed games with total hours, session
// counts and a per-game bar, plus a "recent sessions" timeline and a cadence
// breakdown. This draws the same three blocks from the local ledger.
#include "onow/app/App.h"
#include "onow/app/Views.h"

#include "onow/Str.h"
#include "onow/Time.h"
#include "onow/i18n.h"
#include "onow/services/PlaytimeService.h"

#include "imgui.h"

#include <algorithm>

namespace onow {
namespace app {

namespace {

// The web client's formatter: "3 h 42 m" above an hour, "42 m" below it.
std::string format_playtime(long long ms) {
  const long long total_minutes = ms / 60000;
  const long long hours = total_minutes / 60;
  const long long minutes = total_minutes % 60;
  if (hours > 0) {
    return to_string(hours) + " h " + to_string(minutes) + " m";
  }
  return to_string(total_minutes) + " m";
}

void stat_tile(const std::string& label, const std::string& value) {
  const Palette& p = active_palette();
  ImDrawList* list = ImGui::GetWindowDrawList();
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float width = 168.0f;
  const float height = 84.0f;
  draw_panel(list, pos, ImVec2(pos.x + width, pos.y + height), p.card, p.panel_border, 0.0f,
             metrics().radius_md, 1.0f);
  text_at(list, ImVec2(pos.x + 14.0f, pos.y + 12.0f), p.ink_muted,
          ellipsize(label, width - 28.0f, type_scale().caption).c_str(), type_scale().caption);
  text_at(list, ImVec2(pos.x + 14.0f, pos.y + 34.0f), p.ink, value.c_str(), type_scale().title);
  ImGui::Dummy(ImVec2(width, height + 12.0f));
}

} // namespace

void draw_playtime_view(App& app, AppState& state, const ImVec2& min, const ImVec2& max) {
  const Palette& p = active_palette();
  const float scale = state.content_scale;
  (void)app;

  ImGui::SetCursorScreenPos(min);
  ImGui::BeginChild("PlaytimeScroll", ImVec2(max.x - min.x, max.y - min.y), false,
                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);

  std::vector<app::PlaytimeRecord> ranked = state.playtime.records;
  std::sort(ranked.begin(), ranked.end(), [](const app::PlaytimeRecord& a, const app::PlaytimeRecord& b) {
    return a.total_ms > b.total_ms;
  });

  long long total = 0;
  int sessions = 0;
  long long longest = 0;
  long long queue = 0;
  for (const app::PlaytimeRecord& record : ranked) {
    total += record.total_ms;
    sessions += record.sessions;
    queue += record.queue_ms;
    longest = std::max(longest, record.longest_session_ms);
  }
  const long long average = sessions > 0 ? total / sessions : 0;

  // Stat tiles, in the web client's order: total, sessions, games, longest,
  // average, queue.
  stat_tile(tr("playtime.hero.label"), format_playtime(total));
  ImGui::SameLine();
  stat_tile(tr("playtime.stats.sessions"), to_string(sessions));
  ImGui::SameLine();
  stat_tile(tr("playtime.stats.games"), to_string(static_cast<long long>(ranked.size())));
  ImGui::SameLine();
  stat_tile(tr("playtime.stats.longest"), format_playtime(longest));
  ImGui::SameLine();
  stat_tile(tr("playtime.stats.average"), format_playtime(average));
  ImGui::SameLine();
  stat_tile(tr("playtime.stats.queue"), format_playtime(queue));
  ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));

  if (ranked.empty()) {
    ImGui::Dummy(ImVec2(0.0f, 40.0f * scale));
    ImGui::PushFont(type_scale().title);
    ImGui::TextColored(vec4(p.ink), "%s", tr("playtime.empty.title").c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    wrapped_text(tr("playtime.empty.body"), 420.0f * scale, p.ink_muted);
    ImGui::EndChild();
    return;
  }

  const long long top = ranked.front().total_ms;
  section_header(tr("playtime.ranking.title"),
                 tr_count("playtime.ranking.count", static_cast<long long>(ranked.size())));
  ImDrawList* list = ImGui::GetWindowDrawList();
  for (size_t i = 0; i < ranked.size(); ++i) {
    const app::PlaytimeRecord& record = ranked[i];
    const std::string title = record.game_title.empty() ? record.game_id : record.game_title;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    draw_panel(list, pos, ImVec2(pos.x + width, pos.y + 68.0f), p.card, p.panel_border, 0.0f,
               metrics().radius_md, 1.0f);
    const std::string rank = "#" + std::to_string(i + 1);
    text_at(list, ImVec2(pos.x + 14.0f, pos.y + 24.0f), p.ink_muted, rank.c_str(),
            type_scale().caption);
    text_at(list, ImVec2(pos.x + 52.0f, pos.y + 14.0f), p.ink,
            ellipsize(title, width - 220.0f, type_scale().body).c_str(), type_scale().body);
    const std::string meta = format_playtime(record.total_ms) + "  ·  " +
                             tr_count("playtime.ranking.sessions", record.sessions);
    text_at(list, ImVec2(pos.x + 52.0f, pos.y + 34.0f), p.ink_muted, meta.c_str(),
            type_scale().caption);
    // Share of total, the web client's "{{value}}% of total" line.
    const double share = total > 0 ? (static_cast<double>(record.total_ms) * 100.0 /
                                      static_cast<double>(total))
                                   : 0.0;
    (void)top;
    const std::string share_label = tr("playtime.ranking.share", {{"value", to_string(share, 0)}});
    text_at(list, ImVec2(pos.x + width - 190.0f, pos.y + 14.0f), p.ink_muted, share_label.c_str(),
            type_scale().caption);
    // Share-of-total bar: the web client's `{{value}}% of total` row.
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 52.0f, pos.y + 52.0f));
    ImGui::PushItemWidth(std::max(80.0f, width - 240.0f));
    progress(static_cast<float>(share / 100.0), share_label);
    ImGui::PopItemWidth();
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + 72.0f));
    ImGui::Dummy(ImVec2(width, 4.0f));
  }
  ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));

  // Recent sessions timeline.
  std::vector<app::PlaytimeEvent> recent;
  for (const app::PlaytimeRecord& record : ranked) {
    for (const app::PlaytimeEvent& event : record.events) recent.push_back(event);
  }
  std::sort(recent.begin(), recent.end(), [](const app::PlaytimeEvent& a, const app::PlaytimeEvent& b) {
    return a.started_at_ms > b.started_at_ms;
  });
  if (recent.size() > 10) recent.resize(10);

  if (!recent.empty()) {
    section_header(tr("playtime.recent.title"),
                   tr_count("playtime.ranking.sessions", static_cast<long long>(recent.size())));
    for (const app::PlaytimeEvent& event : recent) {
      const std::string engine = event.client_mode == "native" ? tr("playtime.recent.engineNative")
                                                               : tr("playtime.recent.engineWeb");
      const std::string line = format_playtime(event.duration_ms) + "  ·  " + engine + "  ·  " +
                               format_relative_time(event.started_at_ms, now_ms());
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      const float width = ImGui::GetContentRegionAvail().x;
      draw_panel(list, pos, ImVec2(pos.x + width, pos.y + 52.0f), p.card, p.panel_border, 0.0f,
                 metrics().radius_md, 1.0f);
      text_at(list, ImVec2(pos.x + 14.0f, pos.y + 18.0f), p.ink_soft,
              ellipsize(line, width - 28.0f, type_scale().caption).c_str(), type_scale().caption);
      ImGui::Dummy(ImVec2(width, 56.0f));
    }
  }

  ImGui::Dummy(ImVec2(0.0f, 16.0f * scale));
  if (button_ghost(tr("playtime.actions.reset"))) {
    state.modal.open = true;
    state.modal.title = tr("playtime.actions.resetConfirmTitle");
    state.modal.body = tr("playtime.actions.resetConfirmBody");
    state.modal.options.clear();
    state.modal.options.push_back(
        {"erase", tr("playtime.actions.resetConfirm"), [&app] { app.playtime().reset(); }});
  }

  ImGui::EndChild();
}

} // namespace app
} // namespace onow
