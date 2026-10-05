// The local playtime ledger.
//
// The web client kept this server-side (`/api/playtime`) with a browser cache
// for instant painting. The native client keeps the same ledger shape locally
// and mirrors it to the backend when one is reachable, so a session recorded
// offline is not lost.
#pragma once

#include <string>
#include <vector>

#include "onow/app/AppState.h"
#include "onow/Result.h"

namespace onow {
namespace services {

class PlaytimeService {
public:
  void load();
  bool save() const;

  // Marks the start of a queue wait; the queue segment is recorded separately
  // from stream time because the web client's Playtime page counts only
  // gameplay.
  void mark_launch_start(const std::string& game_id, const std::string& game_title);
  void mark_queue_end();
  void mark_stream_start(const std::string& game_id, const std::string& game_title,
                         const std::string& region, const std::string& client_mode);
  void end_session(const std::string& game_id);

  std::vector<app::PlaytimeRecord>& records() { return records_; }
  const std::vector<app::PlaytimeRecord>& records() const { return records_; }

  // Aggregated view used by the Playtime page.
  struct Summary {
    long long total_ms = 0;
    int total_sessions = 0;
    std::vector<app::PlaytimeRecord> ranked;
    std::vector<app::PlaytimeRecord> recent;
  };
  Summary summary() const;
  void reset();

private:
  app::PlaytimeRecord* find(const std::string& game_id);
  app::PlaytimeRecord& ensure(const std::string& game_id, const std::string& game_title);

  std::vector<app::PlaytimeRecord> records_;
  int64_t stream_started_at_ms_ = 0;
  int64_t queue_started_at_ms_ = 0;
  std::string recording_game_id_;
  std::string last_region_;
  std::string last_client_mode_;
};

} // namespace services
} // namespace onow
