// Home: hero, shelves, filters, and the poster grid.
//
// HomePage.tsx plus GameCard.tsx. The web client rendered a hero, a "jump back
// in" shelf, a "most played" shelf, a favourites shelf and then the full grid;
// this does the same against the in-memory snapshot.
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

PosterCard make_card(const model::GameInfo& game, const AppState& state) {
  PosterCard card;
  card.title = game.title;
  card.subtitle = game.description;
  card.store = game.variants.empty() ? std::string() : game.variants.front().store;
  card.favorite = std::find(state.settings().favorite_game_ids.begin(),
                            state.settings().favorite_game_ids.end(),
                            game.id) != state.settings().favorite_game_ids.end();
  card.selected = state.selected_game_id == game.id;
  card.in_library = model::is_game_in_library(game);
  card.aspect = 0.72f;
  return card;
}

// The library the home page shelves show. When the catalog snapshot already
// knows which entries are owned, that is authoritative (it survives a failed
// library fetch); otherwise fall back to the library page's own list.
std::vector<model::GameInfo> home_library(const AppState& state) {
  std::vector<model::GameInfo> owned;
  for (const model::GameInfo& game : state.all_games) {
    if (model::is_game_in_library(game)) owned.push_back(game);
  }
  if (!owned.empty()) return owned;
  return state.library_games;
}

// The library page sorts by the catalog's sort id, except that "relevance"
// means "most recently played" once you are looking at your own library.
std::string current_sort_id(const AppState& state) {
  return state.catalog_sort_id == "relevance" ? "last_played" : state.catalog_sort_id;
}

long long playtime_ms_for(const AppState& state, const std::string& game_id) {
  for (const app::PlaytimeRecord& record : state.playtime.records) {
    if (record.game_id == game_id) return record.total_ms;
  }
  return 0;
}

} // namespace

void draw_home_view(App& app, AppState& state, const ImVec2& min, const ImVec2& max) {
  const Palette& p = active_palette();
  const float scale = state.content_scale;
  ImGui::SetCursorScreenPos(min);
  ImGui::BeginChild("HomeScroll", ImVec2(max.x - min.x, max.y - min.y), false,
                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);

  // Hero: the first featured game (or the first catalog entry).
  const model::GameInfo* hero_game = nullptr;
  if (!state.featured_games.empty()) hero_game = &state.featured_games.front();
  else if (!state.all_games.empty()) hero_game = &state.all_games.front();

  if (hero_game) {
    hero(hero_game->title,
         hero_game->description.empty() ? hero_game->publisher_name : hero_game->description,
         ImTextureID{}, tr("app.actions.play"), tr("app.actions.browse"),
         [&app, hero_game] { app.initiate_play(*hero_game); },
         [&state] { state.view = View::Library; });
    ImGui::Dummy(ImVec2(0.0f, 18.0f * scale));
  }

  std::vector<model::GameInfo> library = home_library(state);
  if (state.loading_catalog && library.empty()) {
    spinner(tr("home.empty.loadingGames").c_str(), 14.0f);
  } else if (library.empty() && state.all_games.empty()) {
    ImGui::Dummy(ImVec2(0.0f, 40.0f * scale));
    ImGui::PushFont(type_scale().title);
    ImGui::TextColored(vec4(p.ink), "%s", tr("home.empty.noGamesFound").c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    wrapped_text(tr("home.empty.tryAdjustingSearch"), 420.0f * scale, p.ink_muted);
  } else {
    // "Jump back in" — most recently played.
    std::vector<model::GameInfo> recent = library;
    catalog::sort_library(recent, "last_played", state.playtime.records);
    if (!recent.empty()) {
      section_header(tr("home.deck.jumpBackIn"));
      std::vector<PosterCard> cards;
      for (size_t i = 0; i < recent.size() && i < 12; ++i) cards.push_back(make_card(recent[i], state));
      size_t shelf_selected = 0;
      card_row("recent", cards, 200.0f * scale, nullptr,
               [&app, &recent](size_t index) { app.initiate_play(recent[index]); }, &shelf_selected);
      ImGui::Dummy(ImVec2(0.0f, 16.0f * scale));
    }

    // "Most played" — only games that actually have recorded time.
    std::vector<model::GameInfo> most = library;
    catalog::sort_library(most, "playtime", state.playtime.records);
    std::vector<model::GameInfo> played;
    for (const model::GameInfo& game : most) {
      if (playtime_ms_for(state, game.id) > 0) played.push_back(game);
    }
    if (!played.empty()) {
      section_header(tr("home.deck.mostPlayed"));
      std::vector<PosterCard> cards;
      for (size_t i = 0; i < played.size() && i < 12; ++i) cards.push_back(make_card(played[i], state));
      size_t shelf_selected = 0;
      card_row("most", cards, 200.0f * scale, nullptr,
               [&app, &played](size_t index) { app.initiate_play(played[index]); }, &shelf_selected);
      ImGui::Dummy(ImVec2(0.0f, 16.0f * scale));
    }

    // Favourites, in the order the user pinned them.
    std::vector<model::GameInfo> favorite_games;
    for (const std::string& id : state.settings().favorite_game_ids) {
      for (const model::GameInfo& game : library) {
        if (game.id == id) {
          favorite_games.push_back(game);
          break;
        }
      }
    }
    if (!favorite_games.empty()) {
      section_header(tr("home.deck.favourites"));
      std::vector<PosterCard> cards;
      for (const model::GameInfo& game : favorite_games) cards.push_back(make_card(game, state));
      size_t shelf_selected = 0;
      card_row("favorites", cards, 200.0f * scale, nullptr,
               [&app, &favorite_games](size_t index) { app.initiate_play(favorite_games[index]); },
               &shelf_selected);
      ImGui::Dummy(ImVec2(0.0f, 16.0f * scale));
    }

    // Filters.
    if (!state.catalog_filter_groups.empty()) {
      section_header(tr("home.filters"));
      for (const model::CatalogFilterGroup& group : state.catalog_filter_groups) {
        ImGui::PushID(group.id.c_str());
        ImGui::PushFont(type_scale().caption);
        ImGui::TextColored(vec4(p.ink_muted), "%s", group.label.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        for (const model::CatalogFilterOption& option : group.options) {
          const std::string id = group.id + ":" + option.id;
          const bool selected = std::find(state.catalog_selected_filter_ids.begin(),
                                          state.catalog_selected_filter_ids.end(),
                                          id) != state.catalog_selected_filter_ids.end();
          if (chip(option.label, selected)) {
            if (selected) {
              state.catalog_selected_filter_ids.erase(
                  std::remove(state.catalog_selected_filter_ids.begin(),
                              state.catalog_selected_filter_ids.end(), id),
                  state.catalog_selected_filter_ids.end());
            } else {
              state.catalog_selected_filter_ids.push_back(id);
            }
          }
          ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::PopID();
      }
      ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));
    }

    // The grid: whatever the search and filters leave.
    std::vector<model::GameInfo> searched;
    for (const model::GameInfo& game : state.all_games) {
      if (catalog::matches_search(game, state.search_query)) searched.push_back(game);
    }
    catalog::apply_filters(searched, state.catalog_selected_filter_ids);
    catalog::sort_catalog(searched, state.catalog_sort_id, state.playtime.records);

    const std::string grid_title = state.search_query.empty()
                                       ? tr("home.deck.allGames")
                                       : tr("home.search.resultsFor") + " \"" + state.search_query + "\"";
    section_header(grid_title, tr_count("library.gameCount", static_cast<long long>(searched.size())));

    const float available = ImGui::GetContentRegionAvail().x;
    const float card_width = 172.0f * scale;
    const int columns = std::max(1, static_cast<int>(available / (card_width + 14.0f * scale)));
    const float stride = available / static_cast<float>(columns);
    int column = 0;
    for (size_t i = 0; i < searched.size(); ++i) {
      if (column == 0) ImGui::BeginGroup();
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (stride - card_width) * 0.5f);
      PosterCard card = make_card(searched[i], state);
      card.width = card_width;
      if (poster_card(card)) app.initiate_play(searched[i]);
      if (ImGui::IsItemHovered()) state.selected_game_id = searched[i].id;
      ++column;
      if (column >= columns) {
        ImGui::EndGroup();
        column = 0;
      } else {
        ImGui::SameLine();
      }
    }
    if (column != 0) ImGui::EndGroup();
  }

  ImGui::EndChild();
}

void draw_library_view(App& app, AppState& state, const ImVec2& min, const ImVec2& max) {
  const Palette& p = active_palette();
  const float scale = state.content_scale;
  ImGui::SetCursorScreenPos(min);
  ImGui::BeginChild("LibraryScroll", ImVec2(max.x - min.x, max.y - min.y), false,
                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);

  // Sort picker. The library page drops "relevance" from the list — a library
  // has no search relevance to sort by.
  std::vector<std::string> sort_labels;
  for (const model::CatalogSortOption& option : state.catalog_sort_options) {
    if (option.id == "relevance") continue;
    sort_labels.push_back(option.label);
  }
  if (!sort_labels.empty()) {
    ImGui::PushFont(type_scale().caption);
    ImGui::TextColored(vec4(p.ink_muted), "%s", tr("app.actions.sort").c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    int index = 0;
    const std::string current = current_sort_id(state);
    for (const model::CatalogSortOption& option : state.catalog_sort_options) {
      if (option.id == "relevance") continue;
      if (option.id == current) break;
      ++index;
    }
    if (combo("##sort", sort_labels, &index)) {
      int seen = 0;
      for (const model::CatalogSortOption& option : state.catalog_sort_options) {
        if (option.id == "relevance") continue;
        if (seen == index) {
          state.catalog_sort_id = option.id;
          break;
        }
        ++seen;
      }
    }
    ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));
  }

  std::vector<model::GameInfo> games;
  for (const model::GameInfo& game : state.library_games) {
    if (catalog::matches_search(game, state.search_query)) games.push_back(game);
  }
  catalog::sort_library(games, current_sort_id(state), state.playtime.records);

  if (state.loading_library && games.empty()) {
    spinner(tr("common.loading").c_str(), 14.0f);
  } else if (games.empty()) {
    ImGui::Dummy(ImVec2(0.0f, 40.0f * scale));
    ImGui::PushFont(type_scale().title);
    ImGui::TextColored(vec4(p.ink), "%s", tr("home.empty.noGamesFound").c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    wrapped_text(tr("home.empty.checkBackLater"), 420.0f * scale, p.ink_muted);
  } else {
    section_header(tr("library.title"),
                   tr_count("library.gameCount", static_cast<long long>(games.size())));
    const float available = ImGui::GetContentRegionAvail().x;
    const float card_width = 172.0f * scale;
    const int columns = std::max(1, static_cast<int>(available / (card_width + 14.0f * scale)));
    const float stride = available / static_cast<float>(columns);
    int column = 0;
    for (size_t i = 0; i < games.size(); ++i) {
      if (column == 0) ImGui::BeginGroup();
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (stride - card_width) * 0.5f);
      PosterCard card = make_card(games[i], state);
      card.width = card_width;
      if (poster_card(card)) app.initiate_play(games[i]);
      if (ImGui::IsItemHovered()) state.selected_game_id = games[i].id;
      ++column;
      if (column >= columns) {
        ImGui::EndGroup();
        column = 0;
      } else {
        ImGui::SameLine();
      }
    }
    if (column != 0) ImGui::EndGroup();
  }

  ImGui::EndChild();
}

} // namespace app
} // namespace onow
