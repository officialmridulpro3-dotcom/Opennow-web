#include "onow/model/Types.h"

#include "onow/Str.h"

#include <algorithm>

namespace onow {
namespace model {

bool is_numeric_id(const std::string& text) { return onow::is_numeric_id(text); }

bool is_owned_library_status(const std::string& status) {
  return status == "MANUAL" || status == "PLATFORM_SYNC" || status == "IN_LIBRARY";
}

bool is_owned_variant(const GameVariant& variant) {
  return is_owned_library_status(variant.library_status);
}

bool GameInfo::is_in_library() const {
  for (const GameVariant& variant : variants) {
    if (is_owned_variant(variant)) return true;
  }
  return false;
}

bool is_game_in_library(const GameInfo& game) { return game.is_in_library(); }

bool is_session_ready_for_connect(int status) { return status == 2 || status == 3; }

bool is_session_in_queue(const SessionInfo& session) {
  if (session.seat_setup_step == 1) return true;
  return session.queue_position > 1;
}

bool is_session_ads_required(const SessionAdState& ad_state) {
  return ad_state.session_ads_required || ad_state.is_ads_required;
}

std::string normalize_game_store(const std::string& store) {
  static const std::map<std::string, std::string> kAliases = {
      {"BATTLE_NET", "BATTLE_NET"},   {"BATTLENET", "BATTLE_NET"},
      {"EA", "EA_APP"},               {"EGS", "EPIC_GAMES_STORE"},
      {"EPIC", "EPIC_GAMES_STORE"},   {"GAIJIN_NET", "GAIJIN"},
      {"GOG_COM", "GOG"},             {"MICROSOFT", "XBOX"},
      {"MICROSOFT_STORE", "XBOX"},    {"ORIGIN", "EA_APP"},
      {"UBISOFT", "UPLAY"},           {"UBISOFT_CONNECT", "UPLAY"},
      {"XBOX_GAME_PASS", "XBOX"},
  };
  std::string key;
  for (char c : store) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u)) key.push_back(static_cast<char>(std::toupper(u)));
    else if (!key.empty() && key.back() != '_') key.push_back('_');
  }
  while (!key.empty() && key.back() == '_') key.pop_back();
  while (!key.empty() && key.front() == '_') key.erase(key.begin());
  const auto it = kAliases.find(key);
  return it != kAliases.end() ? it->second : key;
}

bool is_epic_store(const std::string& store) {
  return normalize_game_store(store) == "EPIC_GAMES_STORE";
}

std::string get_session_ad_media_url(const SessionAdInfo& ad) {
  for (const auto& file : ad.ad_media_files) {
    if (!file.first.empty()) return file.first;
  }
  if (!ad.ad_url.empty()) return ad.ad_url;
  return ad.media_url;
}

int get_session_ad_duration_ms(const SessionAdInfo& ad) {
  if (ad.ad_length_in_seconds > 0) return ad.ad_length_in_seconds * 1000;
  return ad.duration_ms;
}

} // namespace model
} // namespace onow
