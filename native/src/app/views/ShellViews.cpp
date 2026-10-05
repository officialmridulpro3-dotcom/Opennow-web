// The shell chrome: side rail, top header, status bar.
//
// These are the direct equivalents of SideRail.tsx, TopHeader.tsx and
// StatusBar.tsx, drawn with the deck's own primitives rather than CSS.
#include "onow/app/Views.h"

#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/Time.h"
#include "onow/i18n.h"

#include "imgui.h"

namespace onow {
namespace app {

namespace {

struct RailItem {
  View view;
  const char* glyph;
  const char* label_key;
};

const RailItem kRailItems[] = {
    {View::Home, "H", "navigation.home"},
    {View::Library, "L", "navigation.library"},
    {View::Playtime, "P", "navigation.playtime"},
    {View::Settings, "S", "navigation.settings"},
};

} // namespace

void draw_rail(App& app, AppState& state, const ImVec2& min, const ImVec2& max) {
  (void)app;
  const Palette& p = active_palette();
  const float scale = state.content_scale;
  ImDrawList* list = ImGui::GetWindowDrawList();
  list->AddRectFilled(min, max, col(p.bg_b));
  list->AddLine(ImVec2(max.x - 1.0f, min.y), ImVec2(max.x - 1.0f, max.y), col(p.panel_border));

  float y = min.y + 18.0f * scale;
  // The logo mark.
  const ImVec2 logo_center(min.x + (max.x - min.x) * 0.5f, y + 16.0f * scale);
  draw_glow(list, logo_center, 20.0f * scale, p.accent_glow, 0.5f);
  list->AddCircleFilled(logo_center, 13.0f * scale, col(p.accent), 32);
  text_at(list, ImVec2(logo_center.x - 5.0f * scale, logo_center.y - 7.0f * scale), p.accent_on,
          "O", type_scale().title);
  y += 46.0f * scale;

  for (const RailItem& item : kRailItems) {
    const bool selected = state.view == item.view ||
                          (item.view == View::Settings && state.view == View::Settings);
    const ImVec2 item_min(min.x + 8.0f * scale, y);
    const ImVec2 item_max(max.x - 8.0f * scale, y + 46.0f * scale);
    const bool hovered = ImGui::IsMouseHoveringRect(item_min, item_max);
    if (selected) {
      list->AddRectFilled(item_min, item_max, col(p.accent_surface), metrics().radius_md * scale);
      list->AddRectFilled(ImVec2(min.x, item_min.y), ImVec2(min.x + 3.0f * scale, item_max.y),
                          col(p.accent));
    } else if (hovered) {
      list->AddRectFilled(item_min, item_max, col(p.card), metrics().radius_md * scale);
    }
    const Rgb glyph_color = selected ? p.accent : p.ink_soft;
    const ImVec2 glyph_size = measure_text(item.glyph, type_scale().title);
    text_at(list, ImVec2((item_min.x + item_max.x) * 0.5f - glyph_size.x * 0.5f,
                         item_min.y + 8.0f * scale),
            glyph_color, item.glyph, type_scale().title);
    const ImVec2 label_size = measure_text(tr(item.label_key).c_str(), type_scale().caption);
    text_at(list, ImVec2((item_min.x + item_max.x) * 0.5f - label_size.x * 0.5f,
                         item_min.y + 28.0f * scale),
            selected ? p.ink : p.ink_muted, tr(item.label_key).c_str(), type_scale().caption);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      if (item.view == View::Settings && state.view != View::Settings) {
        state.page_before_settings = state.view == View::Settings ? state.page_before_settings : state.view;
      }
      state.view = item.view;
    }
    y += 52.0f * scale;
  }
}

void draw_header(App& app, AppState& state, const ImVec2& min, const ImVec2& max) {
  const Palette& p = active_palette();
  const float scale = state.content_scale;
  ImDrawList* list = ImGui::GetWindowDrawList();
  list->AddRectFilled(min, max, col(p.bg_a));
  list->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), col(p.panel_border));

  const View page = state.view == View::Settings ? state.page_before_settings : state.view;
  const char* title_key = "navigation.home";
  if (page == View::Library) title_key = "library.title";
  else if (page == View::Playtime) title_key = "playtime.title";
  else if (page == View::Settings) title_key = "navigation.settings";
  else if (page == View::GameDetails) title_key = "home.deck.allGames";
  const std::string title = tr(title_key);

  text_at(list, ImVec2(min.x + 24.0f * scale, min.y + 20.0f * scale), p.ink, title.c_str(),
          type_scale().display);

  // Count label, matching the web client's "N games" line.
  int count = 0;
  if (page == View::Playtime) count = static_cast<int>(state.playtime.records.size());
  else if (page == View::Library) count = static_cast<int>(state.library_games.size());
  else count = state.catalog_total_count > 0 ? state.catalog_total_count
                                             : static_cast<int>(state.all_games.size());
  if (count > 0) {
    const std::string label = tr_count(page == View::Playtime ? "playtime.ranking.count"
                                                              : "library.gameCount",
                                       count);
    const ImVec2 size = measure_text(label.c_str(), type_scale().caption);
    text_at(list, ImVec2(max.x - 24.0f * scale - size.x, min.y + 28.0f * scale), p.ink_muted,
            label.c_str(), type_scale().caption);
  }

  // Search box.
  const char* search_key = page == View::Library ? "library.searchPlaceholder"
                                                 : "home.searchPlaceholder";
  const std::string placeholder = tr(search_key);
  const float field_width = 320.0f * scale;
  const float field_height = 36.0f * scale;
  const ImVec2 field_min(max.x - 24.0f * scale - field_width - (count > 0 ? 140.0f * scale : 0.0f),
                         min.y + 18.0f * scale);
  const ImVec2 field_max(field_min.x + field_width, field_min.y + field_height);
  const bool hovered = ImGui::IsMouseHoveringRect(field_min, field_max);
  list->AddRectFilled(field_min, field_max, col(hovered ? p.card_hover : p.card),
                      metrics().radius_md * scale);
  list->AddRect(field_min, field_max, col(hovered ? p.panel_border_solid : p.panel_border),
                metrics().radius_md * scale);
  const std::string& query = state.search_query;
  const std::string shown = query.empty() ? placeholder : query;
  text_at(list, ImVec2(field_min.x + 12.0f * scale, field_min.y + 10.0f * scale),
          query.empty() ? p.ink_muted : p.ink,
          ellipsize(shown, field_width - 24.0f * scale, type_scale().body).c_str(),
          type_scale().body);
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    // Focusing the search field routes keyboard input to it; the app's
    // shortcut handler ignores text entry while a field is focused.
    state.search_focused = true;
  }
  (void)app;
}

void draw_status_bar(App& app, AppState& state, const ImVec2& min, const ImVec2& max) {
  const Palette& p = active_palette();
  const float scale = state.content_scale;
  ImDrawList* list = ImGui::GetWindowDrawList();
  list->AddRectFilled(min, max, col(p.bg_b));
  list->AddLine(ImVec2(min.x, min.y), ImVec2(max.x, min.y), col(p.panel_border));

  // Region + latency.
  std::string region_label = tr("statusbar.regionAuto");
  if (!state.settings().region.empty()) {
    region_label = state.settings().region;
    for (const model::StreamRegion& region : state.regions) {
      if (region.url == state.settings().region && !region.name.empty()) region_label = region.name;
    }
  } else if (state.auth_session && !state.auth_session->provider.streaming_service_url.empty()) {
    region_label = state.auth_session->provider.display_name;
  }
  float x = min.x + 24.0f * scale;
  text_at(list, ImVec2(x, min.y + 12.0f * scale), p.ink_muted, region_label.c_str(),
          type_scale().caption);
  x += measure_text(region_label.c_str(), type_scale().caption).x + 20.0f * scale;

  // Stream meta: resolution · fps · codec.
  const Settings& settings = state.settings();
  const std::string meta = resolution_label(settings.resolution) + " · " + to_string(settings.fps) +
                           " fps · " + str_replace_all(settings.codec, "H", "H.");
  text_at(list, ImVec2(x, min.y + 12.0f * scale), p.ink_muted, meta.c_str(),
          type_scale().caption);
  x += measure_text(meta.c_str(), type_scale().caption).x + 20.0f * scale;

  // Session / diagnostics summary on the right.
  std::string right;
  if (state.diagnostics.active && state.diagnostics.bitrate_kbps > 0.0) {
    right = format_bitrate_kbps(state.diagnostics.bitrate_kbps) + " · " +
            to_string(state.diagnostics.decoded_fps) + " fps";
  } else if (state.regions_loading) {
    right = tr("app.status.checking");
  } else if (!state.settings().region.empty()) {
    int best = -1;
    for (const model::StreamRegion& region : state.regions) {
      if (region.url == state.settings().region) {
        best = region.ping_failed ? -2 : region.ping_ms;
        break;
      }
    }
    if (best == -2) right = tr("gameCard.status.unavailable");
    else if (best >= 0) right = to_string(best) + " ms";
  }
  if (!right.empty()) {
    const ImVec2 size = measure_text(right.c_str(), type_scale().caption);
    text_at(list, ImVec2(max.x - 24.0f * scale - size.x, min.y + 12.0f * scale), p.ink_soft,
            right.c_str(), type_scale().caption);
  }

  // Keyboard hint, far left after the meta.
  const std::string hint = tr("statusbar.hint.commands");
  const ImVec2 hint_size = measure_text(hint.c_str(), type_scale().caption);
  if (x + hint_size.x + 40.0f * scale < max.x - 24.0f * scale - hint_size.x - 12.0f * scale) {
    text_at(list, ImVec2(x + 12.0f * scale, min.y + 12.0f * scale), p.ink_muted, hint.c_str(),
            type_scale().caption);
  }
  (void)app;
}

} // namespace app
} // namespace onow
