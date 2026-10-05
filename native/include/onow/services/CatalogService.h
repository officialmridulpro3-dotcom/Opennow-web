// Catalog: games, library, filters, sorts, and the on-disk snapshot.
//
// The web client cached a snapshot in localStorage keyed by user id so a cold
// start painted instantly. This does the same thing with a JSON file, and adds
// a debounce so typing in the search box does not fire a request per keystroke.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "onow/model/Types.h"
#include "onow/Result.h"

namespace onow {
namespace services {

class GfnClient;

struct CatalogSnapshot {
  int64_t saved_at_ms = 0;
  std::string user_id;
  std::vector<model::GameInfo> games;
  std::vector<model::GameInfo> library;
};

class CatalogService {
public:
  explicit CatalogService(GfnClient& client);
  ~CatalogService();

  // Loads the snapshot from disk (instant) then refreshes from the backend.
  void load(const std::string& user_id, const std::string& streaming_base_url,
            const std::string& proxy_url, bool force);
  void load_library(const std::string& user_id, const std::string& streaming_base_url,
                    const std::string& proxy_url);
  void refresh_if_stale(const std::string& user_id, const std::string& streaming_base_url,
                        const std::string& proxy_url);

  // Debounced search. `query` is what the user typed; results land in the
  // store's `filtered_games` through the App.
  void search(const std::string& query, const std::string& sort_id,
              const std::vector<std::string>& filter_ids);

  Result<bool> mark_owned(const model::GameInfo& game, const std::string& variant_id,
                          const std::string& streaming_base_url, const std::string& proxy_url);

  bool loading() const { return loading_.load(); }
  bool library_loading() const { return library_loading_.load(); }
  const std::string& last_error() const { return last_error_; }
  int64_t last_load_ms() const { return last_load_ms_; }

  void save_snapshot(const std::string& user_id);
  void clear_snapshot();

private:
  void run_load(const std::string& user_id, const std::string& streaming_base_url,
                const std::string& proxy_url, bool force);
  void run_load_library(const std::string& user_id, const std::string& streaming_base_url,
                        const std::string& proxy_url);

  GfnClient& client_;
  std::atomic<bool> loading_{false};
  std::atomic<bool> library_loading_{false};
  std::atomic<bool> stop_{false};
  std::string last_error_;
  int64_t last_load_ms_ = 0;
  std::thread worker_;
  std::mutex mutex_;
  CatalogSnapshot snapshot_;
};

} // namespace services
} // namespace onow
