#include "onow/services/GfnClient.h"
#include "onow/services/CatalogService.h"

#include "onow/Fs.h"
#include "onow/Json.h"
#include "onow/Log.h"
#include "onow/Time.h"

namespace onow {
namespace services {
namespace {

Json game_to_json(const model::GameInfo& game) {
  Json json = Json::object();
  json["id"] = game.id;
  json["uuid"] = game.uuid;
  json["launchAppId"] = game.launch_app_id;
  json["title"] = game.title;
  json["shortName"] = game.short_name;
  json["description"] = game.description;
  json["longDescription"] = game.long_description;
  json["developerName"] = game.developer_name;
  json["imageUrl"] = game.image_url;
  json["heroImageUrl"] = game.hero_image_url;
  json["screenshotUrl"] = game.screenshot_url;
  json["playType"] = game.play_type;
  json["membershipTierLabel"] = game.membership_tier_label;
  json["publisherName"] = game.publisher_name;
  json["selectedVariantIndex"] = game.selected_variant_index;
  json["lastPlayed"] = game.last_played;
  Json genres = Json::array();
  for (const std::string& genre : game.genres) genres.push_back(Json(genre));
  json["genres"] = genres;
  Json variants = Json::array();
  for (const model::GameVariant& variant : game.variants) {
    Json entry = Json::object();
    entry["id"] = variant.id;
    entry["store"] = variant.store;
    entry["storeUrl"] = variant.store_url;
    entry["libraryStatus"] = variant.library_status;
    entry["inLibrary"] = variant.in_library;
    entry["supportsInGameSettingsPersistence"] = variant.supports_in_game_settings_persistence;
    entry["lastPlayedDate"] = variant.last_played_date;
    Json controls = Json::array();
    for (const std::string& control : variant.supported_controls) controls.push_back(Json(control));
    entry["supportedControls"] = controls;
    variants.push_back(entry);
  }
  json["variants"] = variants;
  return json;
}

} // namespace

CatalogService::CatalogService(GfnClient& client) : client_(client) {}

CatalogService::~CatalogService() {
  stop_.store(true);
  if (worker_.joinable()) worker_.join();
}

void CatalogService::load(const std::string& user_id, const std::string& streaming_base_url,
                          const std::string& proxy_url, bool force) {
  if (loading_.exchange(true)) return;
  if (worker_.joinable()) worker_.join();
  worker_ = std::thread([this, user_id, streaming_base_url, proxy_url, force] {
    run_load(user_id, streaming_base_url, proxy_url, force);
  });
}

void CatalogService::run_load(const std::string& user_id, const std::string& streaming_base_url,
                              const std::string& proxy_url, bool force) {
  last_error_.clear();
  // Snapshot first so the UI can paint immediately.
  const std::string path = fs_join(fs_app_data_dir(), "catalog.json");
  std::string text;
  if (fs_read_text(path, text) && !text.empty()) {
    Json json = Json::parse_or(text);
    const std::string saved_user = json.find("userId") ? json.find("userId")->as_string() : "";
    const int64_t saved_at = json.find("savedAtMs") ? json.find("savedAtMs")->as_ll() : 0;
    if (saved_user == user_id || user_id.empty()) {
      snapshot_.saved_at_ms = saved_at;
      snapshot_.user_id = saved_user;
      snapshot_.games.clear();
      if (const Json* games = json.find("games")) {
        for (size_t i = 0; i < games->size(); ++i) {
          snapshot_.games.push_back(decode::game(games->at(i)));
        }
      }
      last_load_ms_ = saved_at;
    }
  }

  const bool stale = now_ms() - snapshot_.saved_at_ms > 15 * 60 * 1000;
  if (!force && !stale && !snapshot_.games.empty()) {
    loading_.store(false);
    return;
  }

  BrowseCatalogInput input;
  input.fetch_count = 200;
  input.sort_id = "relevance";
  input.streaming_base_url = streaming_base_url;
  input.proxy_url = proxy_url;
  Result<model::CatalogBrowseResult> result = client_.browse(input);
  if (!result.ok()) {
    last_error_ = result.error().message;
    ONOW_WARN("Catalog", "browse failed: %s", last_error_.c_str());
    loading_.store(false);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.games = result->games;
    snapshot_.saved_at_ms = now_ms();
    snapshot_.user_id = user_id;
    last_load_ms_ = snapshot_.saved_at_ms;
  }
  save_snapshot(user_id);
  loading_.store(false);
}

void CatalogService::load_library(const std::string& user_id,
                                  const std::string& streaming_base_url,
                                  const std::string& proxy_url) {
  if (library_loading_.exchange(true)) return;
  if (worker_.joinable()) worker_.join();
  worker_ = std::thread([this, user_id, streaming_base_url, proxy_url] {
    run_load_library(user_id, streaming_base_url, proxy_url);
  });
}

void CatalogService::run_load_library(const std::string& user_id,
                                      const std::string& streaming_base_url,
                                      const std::string& proxy_url) {
  (void)streaming_base_url;
  (void)proxy_url;
  (void)user_id;
  Result<std::vector<model::GameInfo>> library = client_.library();
  if (!library.ok()) {
    last_error_ = library.error().message;
    library_loading_.store(false);
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.library = library.value();
  library_loading_.store(false);
}

void CatalogService::refresh_if_stale(const std::string& user_id,
                                      const std::string& streaming_base_url,
                                      const std::string& proxy_url) {
  if (now_ms() - last_load_ms_ > 15 * 60 * 1000) {
    load(user_id, streaming_base_url, proxy_url, false);
  }
}

void CatalogService::search(const std::string& query, const std::string& sort_id,
                            const std::vector<std::string>& filter_ids) {
  (void)query;
  (void)sort_id;
  (void)filter_ids;
  // Filtering happens on the UI thread against the in-memory snapshot; the
  // backend's browse endpoint is only consulted when the local snapshot has no
  // match, which keeps typing instant.
}

Result<bool> CatalogService::mark_owned(const model::GameInfo& game,
                                        const std::string& variant_id,
                                        const std::string& streaming_base_url,
                                        const std::string& proxy_url) {
  MarkOwnedInput input;
  input.variant_id = variant_id;
  input.streaming_base_url = streaming_base_url;
  input.proxy_url = proxy_url;
  Result<bool> result = client_.mark_game_owned(input);
  if (!result.ok()) return result.error();
  // Reflect the change locally so the card updates without a full reload.
  std::lock_guard<std::mutex> lock(mutex_);
  for (model::GameInfo& candidate : snapshot_.games) {
    if (candidate.id != game.id) continue;
    for (model::GameVariant& variant : candidate.variants) {
      if (variant.id == variant_id) {
        variant.library_status = "MANUAL";
        variant.in_library = true;
      }
    }
  }
  for (model::GameInfo& candidate : snapshot_.library) {
    if (candidate.id != game.id) continue;
    for (model::GameVariant& variant : candidate.variants) {
      if (variant.id == variant_id) {
        variant.library_status = "MANUAL";
        variant.in_library = true;
      }
    }
  }
  save_snapshot(snapshot_.user_id);
  return true;
}

void CatalogService::save_snapshot(const std::string& user_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  Json json = Json::object();
  json["version"] = 1;
  json["savedAtMs"] = snapshot_.saved_at_ms;
  json["userId"] = user_id.empty() ? snapshot_.user_id : user_id;
  Json games = Json::array();
  for (const model::GameInfo& game : snapshot_.games) games.push_back(game_to_json(game));
  json["games"] = games;
  fs_write_text(fs_join(fs_app_data_dir(), "catalog.json"), json.dump());
}

void CatalogService::clear_snapshot() {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = CatalogSnapshot();
  last_load_ms_ = 0;
  fs_remove(fs_join(fs_app_data_dir(), "catalog.json"));
}

} // namespace services
} // namespace onow
