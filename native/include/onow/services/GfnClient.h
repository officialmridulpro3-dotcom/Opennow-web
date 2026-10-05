// The OpenNOW backend client.
//
// The web client talked to `src/server/api.ts` over `fetch()`; this is the same
// surface over the native HTTP stack. Every call is synchronous — the services
// own worker threads, so the UI never blocks. JSON is parsed into the model
// types here and nowhere else, which keeps the wire format in one file.
#pragma once

#include <string>
#include <vector>

#include "onow/Json.h"
#include "onow/model/Types.h"
#include "onow/Result.h"

namespace onow {
namespace services {

struct CreateSessionInput {
  std::string app_id;
  std::string internal_title;
  bool account_linked = false;
  bool enable_persisting_in_game_settings = false;
  bool supports_in_game_settings_persistence = false;
  std::string existing_session_strategy; // "" | auto-resume | force-new
  std::string zone = "prod";
  std::string streaming_base_url;
  std::string proxy_url;
  Json settings;
};

struct PollSessionInput {
  std::string session_id;
  std::string server_ip;
  std::string zone = "prod";
  std::string streaming_base_url;
  std::string client_id;
  std::string device_id;
  std::string proxy_url;
};

struct StopSessionInput {
  std::string session_id;
  std::string server_ip;
  std::string zone = "prod";
  std::string streaming_base_url;
  std::string client_id;
  std::string device_id;
};

struct ClaimSessionInput {
  std::string session_id;
  std::string server_ip;
  std::string streaming_base_url;
  std::string client_id;
  std::string device_id;
  std::string app_id;
  int app_launch_mode = 0;
  bool enable_persisting_in_game_settings = false;
  bool recovery_mode = false;
  Json settings;
};

struct MarkOwnedInput {
  std::string variant_id;
  std::string streaming_base_url;
  std::string proxy_url;
};

struct BrowseCatalogInput {
  std::string search_query;
  std::string sort_id;
  std::vector<std::string> filter_ids;
  int fetch_count = 60;
  std::string streaming_base_url;
  std::string proxy_url;
};

struct ResolveLaunchIdInput {
  std::string app_id_or_uuid;
  std::string streaming_base_url;
  std::string proxy_url;
};

struct ResolveStoreUrlInput {
  std::string app_id_or_uuid;
  std::string variant_id;
  std::string store;
  std::string streaming_base_url;
  std::string proxy_url;
};

struct PingResult {
  std::string url;
  int ping_ms = -1;
  bool failed = false;
  std::string error;
};

class GfnClient {
public:
  explicit GfnClient(std::string base_url);

  const std::string& base_url() const { return base_url_; }
  void set_base_url(const std::string& url) { base_url_ = url; }

  // Whether the backend answered /api/providers at all. Used to tell "backend
  // down" apart from "backend up, no session".
  Result<Json> ping();

  Result<std::vector<model::LoginProvider>> providers();
  Result<std::optional<model::AuthSession>> session();
  Result<std::vector<model::StreamRegion>> regions();
  Result<std::optional<model::SubscriptionInfo>> subscription();
  Result<std::vector<model::GameInfo>> catalog();
  Result<std::vector<model::GameInfo>> library();
  Result<model::CatalogBrowseResult> browse(const BrowseCatalogInput& input);
  Result<std::string> resolve_launch_app_id(const ResolveLaunchIdInput& input);
  Result<std::string> resolve_store_url(const ResolveStoreUrlInput& input);
  Result<bool> mark_game_owned(const MarkOwnedInput& input);

  Result<model::SessionInfo> create_session(const CreateSessionInput& input);
  Result<model::SessionInfo> poll_session(const PollSessionInput& input);
  Result<bool> stop_session(const StopSessionInput& input);
  Result<model::SessionInfo> claim_session(const ClaimSessionInput& input);
  Result<std::vector<model::ActiveSessionInfo>> active_sessions();

  Result<PingResult> ping_region(const std::string& url);

  // The community PrintedWaste queue API (https://api.printedwaste.com/gfn/queue/),
  // which the free-tier server picker uses to show live waits per zone.
  Result<Json> printed_waste_queue();

  // Best-effort diagnostics forwarding, batched by the caller.
  Result<bool> post_client_log(const std::vector<std::string>& lines);

private:
  Result<Json> get(const std::string& path, const std::vector<std::pair<std::string, std::string>>& query = {});
  Result<Json> post(const std::string& path, const Json& body);

  std::string base_url_;
};

// JSON -> model decoders, exported so tests can exercise them directly.
namespace decode {
model::GameInfo game(const Json& json);
model::GameVariant variant(const Json& json);
model::LoginProvider provider(const Json& json);
model::AuthSession auth_session(const Json& json);
model::StreamRegion region(const Json& json);
model::SubscriptionInfo subscription(const Json& json);
model::SessionInfo session_info(const Json& json);
model::ActiveSessionInfo active_session(const Json& json);
model::IceServer ice_server(const Json& json);
model::SessionAdState ad_state(const Json& json);
} // namespace decode

} // namespace services
} // namespace onow
