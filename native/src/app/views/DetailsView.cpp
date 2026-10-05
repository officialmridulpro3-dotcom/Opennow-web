// Game details: the right-hand inspector panel.
//
// GameDetails.tsx plus the store picker dialog from GameCard.tsx. The web
// client showed the game's hero, publisher, genres, store badges and the
// actions (Play / Preload / Buy / Mark as owned) inside a slide-over.
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

// One store row: what the web client drew as a store icon plus a button.
void draw_variant_row(App& app, AppState& state, const model::GameVariant& variant,
                      const model::GameInfo& game, float scale) {
  const Palette& p = active_palette();
  ImDrawList* list = ImGui::GetWindowDrawList();
  ImGui::PushID(variant.id.c_str());
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  draw_panel(list, pos, ImVec2(pos.x + width, pos.y + 52.0f * scale), p.card, p.panel_border, 0.0f,
             metrics().radius_md * scale, 1.0f);
  const std::string store = variant.store.empty() ? tr("app.labels.other") : variant.store;
  text_at(list, ImVec2(pos.x + 16.0f * scale, pos.y + 16.0f * scale), p.ink, store.c_str(),
          type_scale().body);
  const std::string status = model::is_owned_variant(variant)
                                 ? tr("gameDetails.owned")
                                 : tr("gameDetails.notSubscribed");
  text_at(list, ImVec2(pos.x + 16.0f * scale, pos.y + 30.0f * scale), p.ink_muted, status.c_str(),
          type_scale().caption);

  const bool owned = model::is_owned_variant(variant);
  const bool launching = state.session && state.session->app_id == variant.id;
  ImGui::SetCursorScreenPos(ImVec2(pos.x + width - 128.0f * scale, pos.y + 8.0f * scale));
  if (owned) {
    ImGui::BeginDisabled(launching);
    if (button_accent(launching ? tr("app.status.connecting") : tr("app.actions.play"),
                      ImVec2(112.0f * scale, 36.0f * scale))) {
      app.play_game(game, variant.id);
    }
    ImGui::EndDisabled();
  } else {
    if (button_accent(tr("gameDetails.buy"), ImVec2(112.0f * scale, 36.0f * scale))) {
      app.buy_game(game, variant.id);
    }
  }
  ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + 52.0f * scale));
  ImGui::Dummy(ImVec2(width, 4.0f * scale));
  ImGui::PopID();
}

} // namespace

void draw_details_view(App& app, AppState& state, const ImVec2& min, const ImVec2& max) {
  const Palette& p = active_palette();
  const float scale = state.content_scale;
  ImDrawList* list = ImGui::GetWindowDrawList();

  // The selected game is tracked by id; look it up in whichever list has it.
  const model::GameInfo* game = nullptr;
  for (const model::GameInfo& candidate : state.all_games) {
    if (candidate.id == state.selected_game_id) {
      game = &candidate;
      break;
    }
  }
  if (!game) {
    for (const model::GameInfo& candidate : state.library_games) {
      if (candidate.id == state.selected_game_id) {
        game = &candidate;
        break;
      }
    }
  }

  if (!game) {
    ImGui::SetCursorScreenPos(min);
    ImGui::BeginChild("DetailsEmpty", ImVec2(max.x - min.x, max.y - min.y), false,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);
    ImGui::Dummy(ImVec2(0.0f, 60.0f * scale));
    ImGui::PushFont(type_scale().title);
    ImGui::TextColored(vec4(p.ink), "%s", tr("gameDetails.gameNotFound").c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    wrapped_text(tr("home.empty.tryAdjustingSearch"), 420.0f * scale, p.ink_muted);
    ImGui::EndChild();
    return;
  }

  // The panel takes the right half on wide screens, the whole area otherwise.
  const float panel_width = std::min(420.0f * scale, (max.x - min.x) * 0.5f);
  const ImVec2 panel_min(max.x - panel_width, min.y);
  const ImVec2 panel_max(max.x, max.y);
  ImGui::SetCursorScreenPos(panel_min);
  ImGui::BeginChild("DetailsPanel", ImVec2(panel_width, max.y - min.y), false,
                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);

  // Key art: the game's image when the platform has one, else a tinted block
  // with the title, which is what the web client's placeholder did.
  const float art_height = 170.0f * scale;
  const ImVec2 art_min = ImGui::GetCursorScreenPos();
  draw_panel(list, art_min, ImVec2(art_min.x + panel_width, art_min.y + art_height),
             mix(p.bg_b, p.void_, 0.25f), p.panel_border, 0.0f, metrics().radius_lg * scale, 1.0f);
  const ImVec2 art_size = measure_text(game->title.c_str(), type_scale().title);
  text_at(list, ImVec2(art_min.x + (panel_width - art_size.x) * 0.5f,
                       art_min.y + (art_height - art_size.y) * 0.5f),
          p.ink, ellipsize(game->title, panel_width - 24.0f * scale, type_scale().title).c_str(),
          type_scale().title);
  ImGui::Dummy(ImVec2(panel_width, art_height + 14.0f * scale));

  ImGui::PushFont(type_scale().display);
  ImGui::TextColored(vec4(p.ink), "%s", game->title.c_str());
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));
  ImGui::PushFont(type_scale().caption);
  ImGui::TextColored(vec4(p.ink_muted), "%s", game->publisher_name.c_str());
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0.0f, 14.0f * scale));

  if (!game->genres.empty()) {
    for (const std::string& genre : game->genres) chip(genre, false);
    ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));
  }

  if (!game->description.empty()) {
    wrapped_text(game->description, panel_width - 24.0f * scale, p.ink_soft);
    ImGui::Dummy(ImVec2(0.0f, 16.0f * scale));
  }

  if (!game->variants.empty()) {
    section_header(tr("gameDetails.chooseStore"), tr("gameDetails.chooseStoreHint"));
    for (const model::GameVariant& variant : game->variants) {
      draw_variant_row(app, state, variant, *game, scale);
    }
    ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));
  }

  const bool owned = model::is_game_in_library(*game);
  const bool launching = state.session && state.session->app_id == game->launch_app_id;
  ImGui::BeginDisabled(launching);
  if (owned) {
    if (button_accent(tr("app.actions.play"), ImVec2(-1.0f, 44.0f * scale))) {
      app.initiate_play(*game);
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    if (button_ghost(tr("gameDetails.markAsOwned"))) {
      app.mark_game_owned(*game, catalog::default_variant_id(*game));
    }
  } else {
    if (button_accent(tr("gameDetails.buy"), ImVec2(-1.0f, 44.0f * scale))) {
      app.buy_game(*game, catalog::default_variant_id(*game));
    }
  }
  ImGui::EndDisabled();

  ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
  const bool favorite = std::find(state.settings().favorite_game_ids.begin(),
                                  state.settings().favorite_game_ids.end(),
                                  game->id) != state.settings().favorite_game_ids.end();
  if (button_ghost(favorite ? tr("app.actions.remove") : tr("app.actions.add"))) {
    app.toggle_favorite(game->id);
  }

  ImGui::EndChild();
}

} // namespace app
} // namespace onow
