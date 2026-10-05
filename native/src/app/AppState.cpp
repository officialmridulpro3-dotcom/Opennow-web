#include "onow/app/AppState.h"

#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/Time.h"

namespace onow {
namespace app {

model::StreamProfile AppState::entitled_profile() const {
  model::StreamProfile profile;
  profile.resolution = settings().resolution;
  profile.fps = settings().fps;
  if (!subscription) return profile;
  const std::string wanted = settings().resolution;
  for (const auto& entry : subscription->entitled_resolutions) {
    if (entry.first == wanted) {
      profile.resolution = entry.first;
      profile.fps = entry.second;
      return profile;
    }
  }
  // Not entitled at the requested resolution: fall back to the highest the
  // subscription actually offers, which is what the web client's
  // `resolveEntitledStreamProfile` does.
  int best_fps = 0;
  std::string best_resolution;
  for (const auto& entry : subscription->entitled_resolutions) {
    if (entry.second > best_fps || (entry.second == best_fps && entry.first > best_resolution)) {
      best_fps = entry.second;
      best_resolution = entry.first;
    }
  }
  if (!best_resolution.empty()) {
    profile.resolution = best_resolution;
    profile.fps = best_fps;
  } else {
    // Safe fallback profile when the subscription advertises nothing usable.
    profile.resolution = "1280x720";
    profile.fps = 60;
  }
  return profile;
}

long long AppState::session_elapsed_seconds() const {
  if (!session_started_at_ms) return 0;
  const int64_t delta = now_ms() - session_started_at_ms;
  return delta > 0 ? delta / 1000 : 0;
}

int AppState::session_limit_seconds() const {
  std::string tier = subscription ? subscription->membership_tier : std::string();
  if (tier.empty() && auth_session) tier = auth_session->user.membership_tier;
  if (tier == "FREE") return 60 * 60; // GFN free tier is one hour
  return -1; // unlimited
}

int AppState::session_time_remaining_seconds() const {
  const int limit = session_limit_seconds();
  if (limit < 0) return -1;
  const long long elapsed = session_elapsed_seconds();
  const long long remaining = static_cast<long long>(limit) - elapsed;
  return remaining > 0 ? static_cast<int>(remaining) : 0;
}

bool AppState::free_tier_warnings_active() const {
  std::string tier = subscription ? subscription->membership_tier : std::string();
  if (tier.empty() && auth_session) tier = auth_session->user.membership_tier;
  return tier == "FREE" && is_streaming() && session_started_at_ms != 0;
}

void AppState::push_toast(ToastKind kind, const std::string& title, const std::string& detail) {
  ToastMessage message;
  message.kind = kind;
  message.title = title;
  message.detail = detail;
  message.at_ms = now_ms();
  toasts.push_back(message);
  while (toasts.size() > 6) toasts.erase(toasts.begin());
}

void AppState::tick_toasts(int64_t now) {
  for (auto it = toasts.begin(); it != toasts.end();) {
    if (now - it->at_ms > it->ttl_ms) it = toasts.erase(it);
    else ++it;
  }
}

// ------------------------------------------------------------------ catalog --

namespace catalog {

bool is_numeric_id(const std::string& text) { return onow::is_numeric_id(text); }

int parse_numeric_id(const std::string& text, int fallback) {
  const std::string trimmed = str_trim(text);
  if (!onow::is_numeric_id(trimmed)) return fallback;
  return str_to_int(trimmed, fallback);
}

std::string default_variant_id(const model::GameInfo& game) {
  if (game.selected_variant_index >= 0 &&
      game.selected_variant_index < static_cast<int>(game.variants.size())) {
    return game.variants[static_cast<size_t>(game.selected_variant_index)].id;
  }
  if (!game.variants.empty()) return game.variants.front().id;
  if (!game.launch_app_id.empty()) return game.launch_app_id;
  return game.id;
}

const model::GameVariant* selected_variant(const model::GameInfo& game,
                                           const std::string& variant_id) {
  if (!variant_id.empty()) {
    for (const model::GameVariant& variant : game.variants) {
      if (variant.id == variant_id) return &variant;
    }
  }
  if (game.selected_variant_index >= 0 &&
      game.selected_variant_index < static_cast<int>(game.variants.size())) {
    return &game.variants[static_cast<size_t>(game.selected_variant_index)];
  }
  return game.variants.empty() ? nullptr : &game.variants.front();
}

bool matches_search(const model::GameInfo& game, const std::string& query) {
  const std::string needle = str_lower(str_trim(query));
  if (needle.empty()) return true;
  if (str_icontains(game.title, needle)) return true;
  if (str_icontains(game.short_name, needle)) return true;
  if (str_icontains(game.description, needle)) return true;
  for (const std::string& genre : game.genres) {
    if (str_icontains(genre, needle)) return true;
  }
  for (const std::string& label : game.feature_labels) {
    if (str_icontains(label, needle)) return true;
  }
  return false;
}

namespace {

int playtime_ms_for(const std::vector<PlaytimeRecord>& ledger, const std::string& game_id) {
  for (const PlaytimeRecord& record : ledger) {
    if (record.game_id == game_id) return static_cast<int>(record.total_ms);
  }
  return 0;
}

int64_t last_played_for(const std::vector<PlaytimeRecord>& ledger, const std::string& game_id) {
  for (const PlaytimeRecord& record : ledger) {
    if (record.game_id == game_id) return record.last_played_ms;
  }
  return 0;
}

} // namespace

void sort_library(std::vector<model::GameInfo>& games, const std::string& sort_id,
                  const std::vector<PlaytimeRecord>& playtime) {
  if (sort_id == "title") {
    std::sort(games.begin(), games.end(), [](const model::GameInfo& a, const model::GameInfo& b) {
      return str_lower(a.title) < str_lower(b.title);
    });
  } else if (sort_id == "last_played") {
    std::sort(games.begin(), games.end(), [&](const model::GameInfo& a, const model::GameInfo& b) {
      const int64_t la = last_played_for(playtime, a.id);
      const int64_t lb = last_played_for(playtime, b.id);
      if (la != lb) return la > lb;
      return str_lower(a.title) < str_lower(b.title);
    });
  } else if (sort_id == "playtime") {
    std::sort(games.begin(), games.end(), [&](const model::GameInfo& a, const model::GameInfo& b) {
      const int pa = playtime_ms_for(playtime, a.id);
      const int pb = playtime_ms_for(playtime, b.id);
      if (pa != pb) return pa > pb;
      return str_lower(a.title) < str_lower(b.title);
    });
  } else if (sort_id == "recently_added") {
    std::sort(games.begin(), games.end(), [](const model::GameInfo& a, const model::GameInfo& b) {
      return str_lower(a.title) < str_lower(b.title);
    });
  } else {
    std::sort(games.begin(), games.end(), [](const model::GameInfo& a, const model::GameInfo& b) {
      return str_lower(a.title) < str_lower(b.title);
    });
  }
}

void sort_catalog(std::vector<model::GameInfo>& games, const std::string& sort_id,
                  const std::vector<PlaytimeRecord>& playtime) {
  if (sort_id == "relevance") {
    sort_library(games, "last_played", playtime);
    return;
  }
  sort_library(games, sort_id, playtime);
}

void apply_filters(std::vector<model::GameInfo>& games,
                   const std::vector<std::string>& filter_ids) {
  if (filter_ids.empty()) return;
  // Filters arrive as "<groupId>:<optionId>" pairs; a game matches when it
  // satisfies every selected group.
  std::map<std::string, std::vector<std::string>> by_group;
  for (const std::string& id : filter_ids) {
    const size_t colon = id.find(':');
    if (colon == std::string::npos) continue;
    by_group[id.substr(0, colon)].push_back(id.substr(colon + 1));
  }
  std::vector<model::GameInfo> kept;
  kept.reserve(games.size());
  for (const model::GameInfo& game : games) {
    bool keep = true;
    for (const auto& group : by_group) {
      bool matched = false;
      if (group.first == "genre") {
        for (const std::string& genre : game.genres) {
          if (std::find(group.second.begin(), group.second.end(), genre) != group.second.end()) {
            matched = true;
            break;
          }
        }
      } else if (group.first == "store") {
        for (const model::GameVariant& variant : game.variants) {
          const std::string normalized = model::normalize_game_store(variant.store);
          if (std::find(group.second.begin(), group.second.end(), normalized) !=
              group.second.end()) {
            matched = true;
            break;
          }
        }
      } else if (group.first == "controls") {
        for (const std::string& control : game.supported_controls) {
          if (std::find(group.second.begin(), group.second.end(), control) != group.second.end()) {
            matched = true;
            break;
          }
        }
      } else {
        matched = true; // unknown groups never filter anything out
      }
      if (!matched) {
        keep = false;
        break;
      }
    }
    if (keep) kept.push_back(game);
  }
  games.swap(kept);
}

const model::GameInfo* find_by_app_id(const std::vector<model::GameInfo>& games, int app_id,
                                      const std::map<std::string, std::string>& variants) {
  if (app_id <= 0) return nullptr;
  for (const model::GameInfo& game : games) {
    std::vector<int> ids;
    const int launch_id = parse_numeric_id(game.launch_app_id, 0);
    if (launch_id > 0) ids.push_back(launch_id);
    for (const model::GameVariant& variant : game.variants) {
      const int variant_id = parse_numeric_id(variant.id, 0);
      if (variant_id > 0) ids.push_back(variant_id);
    }
    if (std::find(ids.begin(), ids.end(), app_id) != ids.end()) return &game;
  }
  (void)variants;
  return nullptr;
}

} // namespace catalog
} // namespace app
} // namespace onow
