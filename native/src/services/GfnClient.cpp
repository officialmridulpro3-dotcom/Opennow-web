#include "onow/services/GfnClient.h"

#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/Time.h"
#include "onow/net/Http.h"
#include "onow/net/Url.h"

namespace onow {
namespace services {
namespace decode {

model::GameVariant variant(const Json& json) {
  model::GameVariant out;
  out.id = json.find("id") ? json.find("id")->as_string() : "";
  out.store = json.find("store") ? json.find("store")->as_string() : "";
  out.store_url = json.find("storeUrl") ? json.find("storeUrl")->as_string() : "";
  if (const Json* controls = json.find("supportedControls")) {
    for (size_t i = 0; i < controls->size(); ++i) {
      out.supported_controls.push_back(controls->at(i).as_string());
    }
  }
  out.supports_in_game_settings_persistence =
      json.find("supportsInGameSettingsPersistence")
          ? json.find("supportsInGameSettingsPersistence")->as_bool()
          : false;
  out.library_selected = json.find("librarySelected") ? json.find("librarySelected")->as_bool() : false;
  out.in_library = json.find("inLibrary") ? json.find("inLibrary")->as_bool() : false;
  out.library_status = json.find("libraryStatus") ? json.find("libraryStatus")->as_string() : "";
  out.last_played_date = json.find("lastPlayedDate") ? json.find("lastPlayedDate")->as_string() : "";
  out.gfn_status = json.find("gfnStatus") ? json.find("gfnStatus")->as_string() : "";
  return out;
}

model::GameInfo game(const Json& json) {
  model::GameInfo out;
  out.id = json.find("id") ? json.find("id")->as_string() : "";
  out.uuid = json.find("uuid") ? json.find("uuid")->as_string() : "";
  out.launch_app_id = json.find("launchAppId") ? json.find("launchAppId")->as_string() : "";
  out.title = json.find("title") ? json.find("title")->as_string() : "";
  out.short_name = json.find("shortName") ? json.find("shortName")->as_string() : "";
  out.description = json.find("description") ? json.find("description")->as_string() : "";
  out.long_description = json.find("longDescription") ? json.find("longDescription")->as_string() : "";
  out.developer_name = json.find("developerName") ? json.find("developerName")->as_string() : "";
  out.max_local_players = json.find("maxLocalPlayers") ? json.find("maxLocalPlayers")->as_int() : 0;
  out.max_online_players = json.find("maxOnlinePlayers") ? json.find("maxOnlinePlayers")->as_int() : 0;
  if (const Json* labels = json.find("featureLabels")) {
    for (size_t i = 0; i < labels->size(); ++i) out.feature_labels.push_back(labels->at(i).as_string());
  }
  if (const Json* genres = json.find("genres")) {
    for (size_t i = 0; i < genres->size(); ++i) out.genres.push_back(genres->at(i).as_string());
  }
  if (const Json* controls = json.find("supportedControls")) {
    for (size_t i = 0; i < controls->size(); ++i) {
      out.supported_controls.push_back(controls->at(i).as_string());
    }
  }
  if (const Json* tech = json.find("nvidiaTech")) {
    for (size_t i = 0; i < tech->size(); ++i) out.nvidia_tech.push_back(tech->at(i).as_string());
  }
  out.image_url = json.find("imageUrl") ? json.find("imageUrl")->as_string() : "";
  out.hero_image_url = json.find("heroImageUrl") ? json.find("heroImageUrl")->as_string() : "";
  out.screenshot_url = json.find("screenshotUrl") ? json.find("screenshotUrl")->as_string() : "";
  if (const Json* shots = json.find("screenshotUrls")) {
    for (size_t i = 0; i < shots->size(); ++i) {
      out.screenshot_urls.push_back(shots->at(i).as_string());
    }
  }
  if (const Json* by_type = json.find("imageUrlsByType")) {
    for (const std::string& key : by_type->keys()) {
      std::vector<std::string> urls;
      if (const Json* values = by_type->find(key)) {
        for (size_t i = 0; i < values->size(); ++i) urls.push_back(values->at(i).as_string());
      }
      if (key == "HERO_IMAGE") out.images_by_type.hero = urls;
      else if (key == "KEY_ART") out.images_by_type.key_art = urls;
      else if (key == "BOX_ART") out.images_by_type.box_art = urls;
      else if (key == "SCREENSHOTS") out.images_by_type.screenshots = urls;
    }
  }
  out.play_type = json.find("playType") ? json.find("playType")->as_string() : "";
  out.membership_tier_label =
      json.find("membershipTierLabel") ? json.find("membershipTierLabel")->as_string() : "";
  out.publisher_name = json.find("publisherName") ? json.find("publisherName")->as_string() : "";
  if (const Json* ratings = json.find("contentRatings")) {
    for (size_t i = 0; i < ratings->size(); ++i) {
      out.content_ratings.push_back(ratings->at(i).as_string());
    }
  }
  out.playability_state =
      json.find("playabilityState") ? json.find("playabilityState")->as_string() : "";
  if (const Json* stores = json.find("availableStores")) {
    for (size_t i = 0; i < stores->size(); ++i) {
      out.available_stores.push_back(stores->at(i).as_string());
    }
  }
  out.search_text = json.find("searchText") ? json.find("searchText")->as_string() : "";
  out.last_played = json.find("lastPlayed") ? json.find("lastPlayed")->as_string() : "";
  out.selected_variant_index =
      json.find("selectedVariantIndex") ? json.find("selectedVariantIndex")->as_int() : 0;
  if (const Json* variants = json.find("variants")) {
    for (size_t i = 0; i < variants->size(); ++i) out.variants.push_back(variant(variants->at(i)));
  }
  return out;
}

model::LoginProvider provider(const Json& json) {
  model::LoginProvider out;
  out.idp_id = json.find("idpId") ? json.find("idpId")->as_string() : "";
  out.code = json.find("code") ? json.find("code")->as_string() : "";
  out.display_name = json.find("displayName") ? json.find("displayName")->as_string() : "";
  out.streaming_service_url =
      json.find("streamingServiceUrl") ? json.find("streamingServiceUrl")->as_string() : "";
  out.priority = json.find("priority") ? json.find("priority")->as_int() : 0;
  return out;
}

model::AuthSession auth_session(const Json& json) {
  model::AuthSession out;
  if (const Json* provider_json = json.find("provider")) out.provider = provider(*provider_json);
  if (const Json* user = json.find("user")) {
    out.user.user_id = user->find("userId") ? user->find("userId")->as_string() : "";
    out.user.display_name = user->find("displayName") ? user->find("displayName")->as_string() : "";
    out.user.email = user->find("email") ? user->find("email")->as_string() : "";
    out.user.avatar_url = user->find("avatarUrl") ? user->find("avatarUrl")->as_string() : "";
    out.user.membership_tier =
        user->find("membershipTier") ? user->find("membershipTier")->as_string() : "";
  }
  if (const Json* tokens = json.find("tokens")) {
    out.tokens.access_token = tokens->find("accessToken") ? tokens->find("accessToken")->as_string() : "";
    out.tokens.refresh_token = tokens->find("refreshToken") ? tokens->find("refreshToken")->as_string() : "";
    out.tokens.id_token = tokens->find("idToken") ? tokens->find("idToken")->as_string() : "";
    out.tokens.expires_at = tokens->find("expiresAt") ? tokens->find("expiresAt")->as_ll() : 0;
  }
  return out;
}

model::StreamRegion region(const Json& json) {
  model::StreamRegion out;
  out.name = json.find("name") ? json.find("name")->as_string() : "";
  out.url = json.find("url") ? json.find("url")->as_string() : "";
  if (const Json* ping = json.find("pingMs")) {
    if (ping->is_number()) {
      out.ping_ms = ping->as_int();
    }
  }
  return out;
}

model::SubscriptionInfo subscription(const Json& json) {
  model::SubscriptionInfo out;
  out.membership_tier = json.find("membershipTier") ? json.find("membershipTier")->as_string() : "";
  if (const Json* resolutions = json.find("entitledResolutions")) {
    for (size_t i = 0; i < resolutions->size(); ++i) {
      const Json& entry = resolutions->at(i);
      std::string resolution;
      int fps = 60;
      if (entry.is_object()) {
        resolution = entry.find("resolution") ? entry.find("resolution")->as_string() : "";
        fps = entry.find("fps") ? entry.find("fps")->as_int() : 60;
      } else if (entry.is_string()) {
        resolution = entry.as_string();
      }
      if (!resolution.empty()) out.entitled_resolutions.emplace_back(resolution, fps);
    }
  }
  if (const Json* storage = json.find("storageAddon")) {
    out.storage_addon_region_name =
        storage->find("regionName") ? storage->find("regionName")->as_string() : "";
  }
  out.fetched_at_ms = now_ms();
  return out;
}

model::IceServer ice_server(const Json& json) {
  model::IceServer out;
  if (const Json* urls = json.find("urls")) {
    if (urls->is_array()) {
      for (size_t i = 0; i < urls->size(); ++i) out.urls.push_back(urls->at(i).as_string());
    } else if (urls->is_string()) {
      out.urls.push_back(urls->as_string());
    }
  }
  out.username = json.find("username") ? json.find("username")->as_string() : "";
  out.credential = json.find("credential") ? json.find("credential")->as_string() : "";
  return out;
}

model::SessionAdState ad_state(const Json& json) {
  model::SessionAdState out;
  if (json.is_null()) return out;
  out.is_ads_required = json.find("isAdsRequired") ? json.find("isAdsRequired")->as_bool() : false;
  out.session_ads_required =
      json.find("sessionAdsRequired") ? json.find("sessionAdsRequired")->as_bool() : false;
  out.is_queue_paused = json.find("isQueuePaused") ? json.find("isQueuePaused")->as_bool() : false;
  out.grace_period_seconds =
      json.find("gracePeriodSeconds") ? json.find("gracePeriodSeconds")->as_int() : 0;
  out.message = json.find("message") ? json.find("message")->as_string() : "";
  out.server_sent_empty_ads =
      json.find("sessionAds") && json.find("sessionAds")->is_null() ? true : false;
  out.enable_l4s = json.find("enableL4S") ? json.find("enableL4S")->as_bool() : false;

  auto read_ads = [](const Json& list, std::vector<model::SessionAdInfo>& out_ads) {
    if (!list.is_array()) return;
    for (size_t i = 0; i < list.size(); ++i) {
      const Json& entry = list.at(i);
      model::SessionAdInfo ad;
      ad.ad_id = entry.find("adId") ? entry.find("adId")->as_string() : "";
      ad.state = entry.find("state") ? entry.find("state")->as_int() : 0;
      ad.ad_state = entry.find("adState") ? entry.find("adState")->as_int() : 0;
      ad.ad_url = entry.find("adUrl") ? entry.find("adUrl")->as_string() : "";
      ad.media_url = entry.find("mediaUrl") ? entry.find("mediaUrl")->as_string() : "";
      if (const Json* files = entry.find("adMediaFiles")) {
        for (size_t f = 0; f < files->size(); ++f) {
          const Json& file = files->at(f);
          const std::string url = file.find("mediaFileUrl") ? file.find("mediaFileUrl")->as_string() : "";
          const std::string profile =
              file.find("encodingProfile") ? file.find("encodingProfile")->as_string() : "";
          ad.ad_media_files.emplace_back(url, profile);
        }
      }
      ad.click_through_url =
          entry.find("clickThroughUrl") ? entry.find("clickThroughUrl")->as_string() : "";
      ad.ad_length_in_seconds =
          entry.find("adLengthInSeconds") ? entry.find("adLengthInSeconds")->as_int() : 0;
      ad.duration_ms = entry.find("durationMs") ? entry.find("durationMs")->as_int() : 0;
      ad.title = entry.find("title") ? entry.find("title")->as_string() : "";
      ad.description = entry.find("description") ? entry.find("description")->as_string() : "";
      out_ads.push_back(ad);
    }
  };
  if (const Json* session_ads = json.find("sessionAds")) read_ads(*session_ads, out.session_ads);
  if (const Json* ads = json.find("ads")) read_ads(*ads, out.ads);
  if (const Json* opportunity = json.find("opportunity")) {
    out.opportunity.state = opportunity->find("state") ? opportunity->find("state")->as_string() : "";
    out.opportunity.queue_paused =
        opportunity->find("queuePaused") ? opportunity->find("queuePaused")->as_bool() : false;
    out.opportunity.grace_period_seconds =
        opportunity->find("gracePeriodSeconds") ? opportunity->find("gracePeriodSeconds")->as_int() : 0;
    out.opportunity.message = opportunity->find("message") ? opportunity->find("message")->as_string() : "";
    out.opportunity.title = opportunity->find("title") ? opportunity->find("title")->as_string() : "";
    out.opportunity.description =
        opportunity->find("description") ? opportunity->find("description")->as_string() : "";
  }
  return out;
}

model::SessionInfo session_info(const Json& json) {
  model::SessionInfo out;
  out.session_id = json.find("sessionId") ? json.find("sessionId")->as_string() : "";
  out.app_id = json.find("appId") ? json.find("appId")->as_string() : "";
  out.status = json.find("status") ? json.find("status")->as_int() : 0;
  out.queue_position = json.find("queuePosition") ? json.find("queuePosition")->as_int() : 0;
  out.seat_setup_step = json.find("seatSetupStep") ? json.find("seatSetupStep")->as_int() : 0;
  if (const Json* ad = json.find("adState")) out.ad_state = ad_state(*ad);
  out.zone = json.find("zone") ? json.find("zone")->as_string() : "prod";
  out.streaming_base_url =
      json.find("streamingBaseUrl") ? json.find("streamingBaseUrl")->as_string() : "";
  out.server_ip = json.find("serverIp") ? json.find("serverIp")->as_string() : "";
  out.signaling_server = json.find("signalingServer") ? json.find("signalingServer")->as_string() : "";
  out.signaling_url = json.find("signalingUrl") ? json.find("signalingUrl")->as_string() : "";
  out.gpu_type = json.find("gpuType") ? json.find("gpuType")->as_string() : "";
  out.app_launch_mode = json.find("appLaunchMode") ? json.find("appLaunchMode")->as_int() : 0;
  out.enable_persisting_in_game_settings = json.find("enablePersistingInGameSettings")
                                               ? json.find("enablePersistingInGameSettings")->as_bool()
                                               : false;
  if (const Json* endpoints = json.find("rtspsEndpoints")) {
    for (size_t i = 0; i < endpoints->size(); ++i) {
      out.rtsps_endpoints.push_back(endpoints->at(i).as_string());
    }
  }
  if (const Json* servers = json.find("iceServers")) {
    for (size_t i = 0; i < servers->size(); ++i) out.ice_servers.push_back(ice_server(servers->at(i)));
  }
  if (const Json* media = json.find("mediaConnectionInfo")) {
    model::MediaConnectionInfo info;
    info.ip = media->find("ip") ? media->find("ip")->as_string() : "";
    info.port = media->find("port") ? media->find("port")->as_int() : 0;
    info.usage = media->find("usage") ? media->find("usage")->as_int() : 0;
    if (!info.ip.empty()) out.media_connection_info = info;
  }
  if (const Json* negotiated = json.find("negotiatedStreamProfile")) {
    if (const Json* resolution = negotiated->find("resolution")) {
      out.negotiated_stream_profile.resolution = resolution->as_string();
      out.negotiated_stream_profile.has_resolution = true;
    }
    if (const Json* fps = negotiated->find("fps")) {
      out.negotiated_stream_profile.fps = fps->as_int();
      out.negotiated_stream_profile.has_fps = true;
    }
    out.negotiated_stream_profile.codec =
        negotiated->find("codec") ? negotiated->find("codec")->as_string() : "";
    out.negotiated_stream_profile.color_quality =
        negotiated->find("colorQuality") ? negotiated->find("colorQuality")->as_string() : "";
    out.negotiated_stream_profile.enable_l4s =
        negotiated->find("enableL4S") ? negotiated->find("enableL4S")->as_bool() : false;
    out.negotiated_stream_profile.enable_cloud_gsync =
        negotiated->find("enableCloudGsync") ? negotiated->find("enableCloudGsync")->as_bool() : false;
    out.negotiated_stream_profile.enable_reflex =
        negotiated->find("enableReflex") ? negotiated->find("enableReflex")->as_bool() : false;
  }
  out.requested_streaming_features = json.find("requestedStreamingFeatures")
                                         ? json.find("requestedStreamingFeatures")->dump()
                                         : "";
  out.finalized_streaming_features = json.find("finalizedStreamingFeatures")
                                         ? json.find("finalizedStreamingFeatures")->dump()
                                         : "";
  out.client_id = json.find("clientId") ? json.find("clientId")->as_string() : "";
  out.device_id = json.find("deviceId") ? json.find("deviceId")->as_string() : "";
  return out;
}

model::ActiveSessionInfo active_session(const Json& json) {
  model::ActiveSessionInfo out;
  out.session_id = json.find("sessionId") ? json.find("sessionId")->as_string() : "";
  out.app_id = json.find("appId") ? json.find("appId")->as_int() : 0;
  out.app_launch_mode = json.find("appLaunchMode") ? json.find("appLaunchMode")->as_int() : 0;
  out.enable_persisting_in_game_settings = json.find("enablePersistingInGameSettings")
                                               ? json.find("enablePersistingInGameSettings")->as_bool()
                                               : false;
  out.gpu_type = json.find("gpuType") ? json.find("gpuType")->as_string() : "";
  out.status = json.find("status") ? json.find("status")->as_int() : 0;
  out.queue_position = json.find("queuePosition") ? json.find("queuePosition")->as_int() : 0;
  out.seat_setup_step = json.find("seatSetupStep") ? json.find("seatSetupStep")->as_int() : 0;
  out.streaming_base_url =
      json.find("streamingBaseUrl") ? json.find("streamingBaseUrl")->as_string() : "";
  out.server_ip = json.find("serverIp") ? json.find("serverIp")->as_string() : "";
  out.signaling_url = json.find("signalingUrl") ? json.find("signalingUrl")->as_string() : "";
  out.resolution = json.find("resolution") ? json.find("resolution")->as_string() : "";
  out.fps = json.find("fps") ? json.find("fps")->as_int() : 0;
  return out;
}

} // namespace decode

GfnClient::GfnClient(std::string base_url) : base_url_(std::move(base_url)) {}

Result<Json> GfnClient::get(const std::string& path,
                            const std::vector<std::pair<std::string, std::string>>& query) {
  std::string url = base_url_ + path;
  if (!query.empty()) {
    std::string separator = url.find('?') == std::string::npos ? "?" : "&";
    for (size_t i = 0; i < query.size(); ++i) {
      if (i) separator = "&";
      url += separator + url_encode(query[i].first) + "=" + url_encode(query[i].second);
    }
  }
  HttpRequest request;
  request.method = "GET";
  request.url = url;
  request.headers = {{"X-OpenNOW-Client", "native"}, {"Accept", "application/json"}};
  return http_json(request);
}

Result<Json> GfnClient::post(const std::string& path, const Json& body) {
  HttpRequest request;
  request.method = "POST";
  request.url = base_url_ + path;
  request.headers = {{"X-OpenNOW-Client", "native"}, {"Content-Type", "application/json"},
                     {"Accept", "application/json"}};
  request.body = body.dump();
  return http_json(request);
}

Result<Json> GfnClient::ping() { return get("/api/providers"); }

Result<std::vector<model::LoginProvider>> GfnClient::providers() {
  Result<Json> json = get("/api/providers");
  if (!json.ok()) return json.error();
  std::vector<model::LoginProvider> out;
  if (const Json* list = json->find("providers")) {
    for (size_t i = 0; i < list->size(); ++i) out.push_back(decode::provider(list->at(i)));
  } else if (json->is_array()) {
    for (size_t i = 0; i < json->size(); ++i) out.push_back(decode::provider(json->at(i)));
  }
  return out;
}

Result<std::optional<model::AuthSession>> GfnClient::session() {
  Result<Json> json = get("/api/session");
  if (!json.ok()) return json.error();
  const Json* session = json->find("session");
  if (!session || session->is_null()) return std::optional<model::AuthSession>();
  return std::optional<model::AuthSession>(decode::auth_session(*session));
}

Result<std::vector<model::StreamRegion>> GfnClient::regions() {
  Result<Json> json = get("/api/regions");
  if (!json.ok()) return json.error();
  std::vector<model::StreamRegion> out;
  const Json* list = json->find("regions");
  if (!list && json->is_array()) list = &json.value();
  if (list) {
    for (size_t i = 0; i < list->size(); ++i) out.push_back(decode::region(list->at(i)));
  }
  return out;
}

Result<std::optional<model::SubscriptionInfo>> GfnClient::subscription() {
  Result<Json> json = get("/api/subscription");
  if (!json.ok()) return json.error();
  if (json->is_null()) return std::optional<model::SubscriptionInfo>();
  return std::optional<model::SubscriptionInfo>(decode::subscription(*json));
}

Result<std::vector<model::GameInfo>> GfnClient::catalog() {
  Result<Json> json = get("/api/catalog");
  if (!json.ok()) return json.error();
  std::vector<model::GameInfo> out;
  const Json* list = json->find("games");
  if (!list && json->is_array()) list = &json.value();
  if (list) {
    for (size_t i = 0; i < list->size(); ++i) out.push_back(decode::game(list->at(i)));
  }
  return out;
}

Result<std::vector<model::GameInfo>> GfnClient::library() {
  Result<Json> json = get("/api/library");
  if (!json.ok()) return json.error();
  std::vector<model::GameInfo> out;
  const Json* list = json->find("games");
  if (!list && json->is_array()) list = &json.value();
  if (list) {
    for (size_t i = 0; i < list->size(); ++i) out.push_back(decode::game(list->at(i)));
  }
  return out;
}

Result<model::CatalogBrowseResult> GfnClient::browse(const BrowseCatalogInput& input) {
  std::vector<std::pair<std::string, std::string>> query;
  if (!input.search_query.empty()) query.emplace_back("q", input.search_query);
  if (input.fetch_count > 0) query.emplace_back("count", to_string(input.fetch_count));
  Result<Json> json = get("/api/catalog", query);
  if (!json.ok()) return json.error();
  model::CatalogBrowseResult out;
  const Json* games = json->find("games");
  if (games) {
    for (size_t i = 0; i < games->size(); ++i) out.games.push_back(decode::game(games->at(i)));
  }
  out.number_returned = json->find("numberReturned") ? json->find("numberReturned")->as_int() : 0;
  out.number_supported = json->find("numberSupported") ? json->find("numberSupported")->as_int() : 0;
  out.total_count = json->find("totalCount") ? json->find("totalCount")->as_int() : 0;
  out.has_next_page = json->find("hasNextPage") ? json->find("hasNextPage")->as_bool() : false;
  out.end_cursor = json->find("endCursor") ? json->find("endCursor")->as_string() : "";
  out.search_query = input.search_query;
  out.selected_sort_id = input.sort_id;
  out.selected_filter_ids = input.filter_ids;
  if (const Json* groups = json->find("filterGroups")) {
    for (size_t g = 0; g < groups->size(); ++g) {
      const Json& group_json = groups->at(g);
      model::CatalogFilterGroup group;
      group.id = group_json.find("id") ? group_json.find("id")->as_string() : "";
      group.label = group_json.find("label") ? group_json.find("label")->as_string() : "";
      if (const Json* options = group_json.find("options")) {
        for (size_t o = 0; o < options->size(); ++o) {
          const Json& option_json = options->at(o);
          model::CatalogFilterOption option;
          option.id = option_json.find("id") ? option_json.find("id")->as_string() : "";
          option.raw_id = option_json.find("rawId") ? option_json.find("rawId")->as_string() : "";
          option.label = option_json.find("label") ? option_json.find("label")->as_string() : "";
          option.group_id = group.id;
          option.group_label = group.label;
          group.options.push_back(option);
        }
      }
      out.filter_groups.push_back(group);
    }
  }
  if (const Json* sorts = json->find("sortOptions")) {
    for (size_t s = 0; s < sorts->size(); ++s) {
      const Json& sort_json = sorts->at(s);
      model::CatalogSortOption option;
      option.id = sort_json.find("id") ? sort_json.find("id")->as_string() : "";
      option.label = sort_json.find("label") ? sort_json.find("label")->as_string() : "";
      option.order_by = sort_json.find("orderBy") ? sort_json.find("orderBy")->as_string() : "";
      out.sort_options.push_back(option);
    }
  }
  return out;
}

Result<std::string> GfnClient::resolve_launch_app_id(const ResolveLaunchIdInput& input) {
  Json body = Json::object();
  body["appIdOrUuid"] = input.app_id_or_uuid;
  if (!input.proxy_url.empty()) body["proxyUrl"] = input.proxy_url;
  Result<Json> json = post("/api/resolve-launch-id", body);
  if (!json.ok()) return json.error();
  return json->find("appId") ? json->find("appId")->as_string() : std::string();
}

Result<std::string> GfnClient::resolve_store_url(const ResolveStoreUrlInput& input) {
  Json body = Json::object();
  body["appIdOrUuid"] = input.app_id_or_uuid;
  if (!input.variant_id.empty()) body["variantId"] = input.variant_id;
  if (!input.store.empty()) body["store"] = input.store;
  if (!input.proxy_url.empty()) body["proxyUrl"] = input.proxy_url;
  Result<Json> json = post("/api/resolve-store-url", body);
  if (!json.ok()) return json.error();
  return json->find("url") ? json->find("url")->as_string() : std::string();
}

Result<bool> GfnClient::mark_game_owned(const MarkOwnedInput& input) {
  Json body = Json::object();
  body["variantId"] = input.variant_id;
  if (!input.proxy_url.empty()) body["proxyUrl"] = input.proxy_url;
  Result<Json> json = post("/api/mark-owned", body);
  if (!json.ok()) return json.error();
  return true;
}

Result<model::SessionInfo> GfnClient::create_session(const CreateSessionInput& input) {
  Json body = Json::object();
  body["appId"] = input.app_id;
  body["internalTitle"] = input.internal_title;
  body["accountLinked"] = input.account_linked;
  body["enablePersistingInGameSettings"] = input.enable_persisting_in_game_settings;
  body["supportsInGameSettingsPersistence"] = input.supports_in_game_settings_persistence;
  if (!input.existing_session_strategy.empty()) {
    body["existingSessionStrategy"] = input.existing_session_strategy;
  }
  body["zone"] = input.zone;
  if (!input.streaming_base_url.empty()) body["streamingBaseUrl"] = input.streaming_base_url;
  if (!input.proxy_url.empty()) body["proxyUrl"] = input.proxy_url;
  if (!input.settings.is_null()) body["settings"] = input.settings;
  Result<Json> json = post("/api/stream/create", body);
  if (!json.ok()) return json.error();
  const Json* session = json->find("session");
  return decode::session_info(session ? *session : *json);
}

Result<model::SessionInfo> GfnClient::poll_session(const PollSessionInput& input) {
  Json body = Json::object();
  body["sessionId"] = input.session_id;
  body["serverIp"] = input.server_ip;
  body["zone"] = input.zone;
  if (!input.streaming_base_url.empty()) body["streamingBaseUrl"] = input.streaming_base_url;
  if (!input.client_id.empty()) body["clientId"] = input.client_id;
  if (!input.device_id.empty()) body["deviceId"] = input.device_id;
  if (!input.proxy_url.empty()) body["proxyUrl"] = input.proxy_url;
  Result<Json> json = post("/api/stream/poll", body);
  if (!json.ok()) return json.error();
  const Json* session = json->find("session");
  return decode::session_info(session ? *session : *json);
}

Result<bool> GfnClient::stop_session(const StopSessionInput& input) {
  Json body = Json::object();
  body["sessionId"] = input.session_id;
  body["serverIp"] = input.server_ip;
  body["zone"] = input.zone;
  if (!input.streaming_base_url.empty()) body["streamingBaseUrl"] = input.streaming_base_url;
  if (!input.client_id.empty()) body["clientId"] = input.client_id;
  if (!input.device_id.empty()) body["deviceId"] = input.device_id;
  Result<Json> json = post("/api/stream/stop", body);
  if (!json.ok()) return json.error();
  return true;
}

Result<model::SessionInfo> GfnClient::claim_session(const ClaimSessionInput& input) {
  Json body = Json::object();
  body["sessionId"] = input.session_id;
  body["serverIp"] = input.server_ip;
  if (!input.streaming_base_url.empty()) body["streamingBaseUrl"] = input.streaming_base_url;
  if (!input.client_id.empty()) body["clientId"] = input.client_id;
  if (!input.device_id.empty()) body["deviceId"] = input.device_id;
  if (!input.app_id.empty()) body["appId"] = input.app_id;
  body["appLaunchMode"] = input.app_launch_mode;
  body["enablePersistingInGameSettings"] = input.enable_persisting_in_game_settings;
  body["recoveryMode"] = input.recovery_mode;
  if (!input.settings.is_null()) body["settings"] = input.settings;
  Result<Json> json = post("/api/stream/claim", body);
  if (!json.ok()) return json.error();
  const Json* session = json->find("session");
  return decode::session_info(session ? *session : *json);
}

Result<std::vector<model::ActiveSessionInfo>> GfnClient::active_sessions() {
  Result<Json> json = get("/api/active-sessions");
  if (!json.ok()) return json.error();
  std::vector<model::ActiveSessionInfo> out;
  const Json* list = json->find("sessions");
  if (!list && json->is_array()) list = &json.value();
  if (list) {
    for (size_t i = 0; i < list->size(); ++i) out.push_back(decode::active_session(list->at(i)));
  }
  return out;
}

Result<PingResult> GfnClient::ping_region(const std::string& url) {
  PingResult result;
  result.url = url;
  Url parsed;
  if (!url_parse(url, parsed) || !parsed.is_tls()) {
    result.failed = true;
    result.error = "Region latency checks require HTTPS.";
    return result;
  }
  // Warm the connection, discard that sample, then time keep-alive probes —
  // the same three-sample approach the web client used, because a cold
  // connection pays DNS + TCP + TLS and reports several round trips.
  const int kWarmup = 1;
  const int kSamples = 3;
  int best = -1;
  std::string last_error;
  for (int probe = 0; probe < kWarmup + kSamples; ++probe) {
    const int64_t started = now_ms();
    HttpRequest request;
    request.method = "GET";
    request.url = url;
    request.timeout_ms = 6000;
    request.max_redirects = 0;
    Result<HttpResponse> response = http_request(request);
    if (!response.ok()) {
      last_error = response.error().message;
      continue;
    }
    if (probe < kWarmup) continue;
    const int elapsed = static_cast<int>(now_ms() - started);
    if (best < 0 || elapsed < best) best = elapsed;
  }
  if (best < 0) {
    result.failed = true;
    result.error = last_error.empty() ? "Region latency check failed." : last_error;
    return result;
  }
  result.ping_ms = best < 1 ? 1 : best;
  return result;
}

Result<Json> GfnClient::printed_waste_queue() {
  // The community queue API is a plain HTTPS GET of a JSON object keyed by
  // zone id. It is optional: the free-tier picker falls back to default routing
  // when it is unreachable, exactly like the web client did.
  HttpRequest request;
  request.method = "GET";
  request.url = "https://api.printedwaste.com/gfn/queue/";
  request.headers = {{"Accept", "application/json"}};
  request.timeout_ms = 6000;
  return http_json(request);
}

Result<bool> GfnClient::post_client_log(const std::vector<std::string>& lines) {
  if (lines.empty()) return true;
  Json body = Json::object();
  Json array = Json::array();
  for (const std::string& line : lines) array.push_back(Json(line));
  body["lines"] = array;
  Result<Json> json = post("/api/client-log", body);
  if (!json.ok()) return json.error();
  return true;
}

} // namespace services
} // namespace onow
