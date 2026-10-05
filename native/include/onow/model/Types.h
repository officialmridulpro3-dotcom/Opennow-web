// The domain model. A direct port of `src/shared/gfn/*.ts` — same field names,
// same optionality, same helper predicates — so the C++ client speaks the exact
// wire contract the Node backend and NVIDIA's APIs already use.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace onow {
namespace model {

// ------------------------------------------------------------------ catalog --

struct GameVariant {
  std::string id;
  std::string store;
  std::string store_url;
  std::vector<std::string> supported_controls;
  bool supports_in_game_settings_persistence = false;
  bool library_selected = false;
  bool in_library = false;
  std::string library_status;
  std::string last_played_date;
  std::string gfn_status;
};

struct GameImageSet {
  std::vector<std::string> hero;
  std::vector<std::string> key_art;
  std::vector<std::string> box_art;
  std::vector<std::string> screenshots;
};

struct GameInfo {
  std::string id;
  std::string uuid;
  std::string launch_app_id;
  std::string title;
  std::string short_name;
  std::string description;
  std::string long_description;
  std::string developer_name;
  int max_local_players = 0;
  int max_online_players = 0;
  std::vector<std::string> feature_labels;
  std::vector<std::string> genres;
  std::vector<std::string> supported_controls;
  std::vector<std::string> nvidia_tech;
  std::string image_url;
  std::string hero_image_url;
  std::string screenshot_url;
  std::vector<std::string> screenshot_urls;
  GameImageSet images_by_type;
  std::string play_type;
  std::string membership_tier_label;
  std::string publisher_name;
  std::vector<std::string> content_ratings;
  std::string playability_state;
  std::vector<std::string> available_stores;
  std::string search_text;
  std::string last_played;
  int selected_variant_index = 0;
  std::vector<GameVariant> variants;

  bool is_in_library() const;
};

struct CatalogFilterOption {
  std::string id;
  std::string raw_id;
  std::string label;
  std::string group_id;
  std::string group_label;
};

struct CatalogFilterGroup {
  std::string id;
  std::string label;
  std::vector<CatalogFilterOption> options;
};

struct CatalogSortOption {
  std::string id;
  std::string label;
  std::string order_by;
};

struct CatalogBrowseResult {
  std::vector<GameInfo> games;
  int number_returned = 0;
  int number_supported = 0;
  int total_count = 0;
  bool has_next_page = false;
  std::string end_cursor;
  std::string search_query;
  std::string selected_sort_id;
  std::vector<std::string> selected_filter_ids;
  std::vector<CatalogFilterGroup> filter_groups;
  std::vector<CatalogSortOption> sort_options;
};

// ------------------------------------------------------------------- auth --

struct LoginProvider {
  std::string idp_id;
  std::string code;
  std::string display_name;
  std::string streaming_service_url;
  int priority = 0;
};

struct AuthTokens {
  std::string access_token;
  std::string refresh_token;
  std::string id_token;
  int64_t expires_at = 0;
};

struct AuthUser {
  std::string user_id;
  std::string display_name;
  std::string email;
  std::string avatar_url;
  std::string membership_tier;
};

struct AuthSession {
  LoginProvider provider;
  AuthTokens tokens;
  AuthUser user;
};

struct SavedAccount {
  std::string user_id;
  std::string display_name;
  std::string email;
  std::string avatar_url;
  std::string membership_tier;
  std::string provider_code;
};

struct DeviceLoginChallenge {
  std::string attempt_id;
  std::string device_code;
  std::string user_code;
  std::string verification_uri;
  std::string verification_uri_complete;
  int64_t expires_at = 0;
  int interval_seconds = 5;
};

enum class DeviceLoginStatus { Pending, SlowDown, Expired, AccessDenied, Authorized, Error };

struct DeviceLoginPoll {
  DeviceLoginStatus status = DeviceLoginStatus::Pending;
  std::optional<AuthSession> session;
  std::string error;
  int interval_seconds = 5;
};

// ---------------------------------------------------------------- session ---

struct StreamRegion {
  std::string name;
  std::string url;
  int ping_ms = -1; // -1 = not measured
  bool ping_failed = false;
};

struct IceServer {
  std::vector<std::string> urls;
  std::string username;
  std::string credential;
};

struct MediaConnectionInfo {
  std::string ip;
  int port = 0;
  int usage = 0;
};

struct NegotiatedStreamProfile {
  std::string resolution;
  int fps = 0;
  std::string codec;
  std::string color_quality;
  bool enable_l4s = false;
  bool enable_cloud_gsync = false;
  bool enable_reflex = false;
  bool has_resolution = false;
  bool has_fps = false;
};

struct SessionAdInfo {
  std::string ad_id;
  int state = 0;
  int ad_state = 0;
  std::string ad_url;
  std::string media_url;
  std::vector<std::pair<std::string, std::string>> ad_media_files;
  std::string click_through_url;
  int ad_length_in_seconds = 0;
  int duration_ms = 0;
  std::string title;
  std::string description;
};

struct SessionOpportunityInfo {
  std::string state;
  bool queue_paused = false;
  int grace_period_seconds = 0;
  std::string message;
  std::string title;
  std::string description;
};

struct SessionAdState {
  bool is_ads_required = false;
  bool session_ads_required = false;
  bool is_queue_paused = false;
  int grace_period_seconds = 0;
  std::string message;
  std::vector<SessionAdInfo> session_ads;
  std::vector<SessionAdInfo> ads;
  SessionOpportunityInfo opportunity;
  bool server_sent_empty_ads = false;
  bool enable_l4s = false;
};

struct SessionInfo {
  std::string session_id;
  std::string app_id;
  int status = 0;
  int queue_position = 0;
  int seat_setup_step = 0;
  SessionAdState ad_state;
  std::string zone;
  std::string streaming_base_url;
  std::string server_ip;
  std::string signaling_server;
  std::string signaling_url;
  std::string gpu_type;
  int app_launch_mode = 0;
  bool enable_persisting_in_game_settings = false;
  std::vector<std::string> rtsps_endpoints;
  std::vector<IceServer> ice_servers;
  std::optional<MediaConnectionInfo> media_connection_info;
  NegotiatedStreamProfile negotiated_stream_profile;
  std::string requested_streaming_features;
  std::string finalized_streaming_features;
  std::string client_id;
  std::string device_id;
};

struct ActiveSessionInfo {
  std::string session_id;
  int app_id = 0;
  int app_launch_mode = 0;
  bool enable_persisting_in_game_settings = false;
  std::string gpu_type;
  int status = 0;
  int queue_position = 0;
  int seat_setup_step = 0;
  std::string streaming_base_url;
  std::string server_ip;
  std::string signaling_url;
  std::string resolution;
  int fps = 0;
};

// ------------------------------------------------------------- entitlements --

struct SubscriptionInfo {
  std::string membership_tier;
  std::vector<std::pair<std::string, int>> entitled_resolutions; // "1920x1080" -> fps
  std::string storage_addon_region_name;
  int64_t fetched_at_ms = 0;
};

struct StreamProfile {
  std::string resolution;
  int fps = 0;
};

// ------------------------------------------------------------------ helpers --

bool is_session_ready_for_connect(int status);
bool is_session_in_queue(const SessionInfo& session);
bool is_session_ads_required(const SessionAdState& ad_state);
bool is_owned_library_status(const std::string& status);
bool is_owned_variant(const GameVariant& variant);
bool is_game_in_library(const GameInfo& game);
bool is_epic_store(const std::string& store);
std::string normalize_game_store(const std::string& store);
std::string get_session_ad_media_url(const SessionAdInfo& ad);
int get_session_ad_duration_ms(const SessionAdInfo& ad);
bool is_numeric_id(const std::string& text);

} // namespace model
} // namespace onow
