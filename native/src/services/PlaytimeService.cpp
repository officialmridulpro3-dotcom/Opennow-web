#include "onow/services/GfnClient.h"
#include "onow/services/PlaytimeService.h"

#include "onow/Fs.h"
#include "onow/Json.h"
#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/Time.h"

#include <algorithm>

namespace onow {
namespace services {

void PlaytimeService::load() {
  std::string text;
  if (!fs_read_text(fs_join(fs_app_data_dir(), "playtime.json"), text) || text.empty()) return;
  Json json = Json::parse_or(text);
  const Json* records = json.find("records");
  if (!records || !records->is_array()) return;
  records_.clear();
  for (size_t i = 0; i < records->size(); ++i) {
    const Json& entry = records->at(i);
    app::PlaytimeRecord record;
    record.game_id = entry.find("gameId") ? entry.find("gameId")->as_string() : "";
    record.game_title = entry.find("gameTitle") ? entry.find("gameTitle")->as_string() : "";
    record.total_ms = entry.find("totalMs") ? entry.find("totalMs")->as_ll() : 0;
    record.sessions = entry.find("sessions") ? entry.find("sessions")->as_int() : 0;
    record.last_played_ms = entry.find("lastPlayedMs") ? entry.find("lastPlayedMs")->as_ll() : 0;
    record.longest_session_ms = entry.find("longestSessionMs") ? entry.find("longestSessionMs")->as_ll() : 0;
    record.queue_ms = entry.find("queueMs") ? entry.find("queueMs")->as_ll() : 0;
    if (const Json* events = entry.find("events")) {
      for (size_t e = 0; e < events->size(); ++e) {
        const Json& raw = events->at(e);
        app::PlaytimeEvent event;
        event.started_at_ms = raw.find("startedAtMs") ? raw.find("startedAtMs")->as_ll() : 0;
        event.duration_ms = raw.find("durationMs") ? raw.find("durationMs")->as_ll() : 0;
        event.region = raw.find("region") ? raw.find("region")->as_string() : "";
        event.client_mode = raw.find("clientMode") ? raw.find("clientMode")->as_string() : "";
        record.events.push_back(event);
      }
    }
    if (!record.game_id.empty()) records_.push_back(record);
  }
}

bool PlaytimeService::save() const {
  Json json = Json::object();
  json["version"] = 1;
  Json records = Json::array();
  for (const app::PlaytimeRecord& record : records_) {
    Json entry = Json::object();
    entry["gameId"] = record.game_id;
    entry["gameTitle"] = record.game_title;
    entry["totalMs"] = static_cast<double>(record.total_ms);
    entry["sessions"] = record.sessions;
    entry["lastPlayedMs"] = static_cast<double>(record.last_played_ms);
    entry["longestSessionMs"] = static_cast<double>(record.longest_session_ms);
    entry["queueMs"] = static_cast<double>(record.queue_ms);
    Json events = Json::array();
    for (const app::PlaytimeEvent& event : record.events) {
      Json raw = Json::object();
      raw["startedAtMs"] = static_cast<double>(event.started_at_ms);
      raw["durationMs"] = static_cast<double>(event.duration_ms);
      raw["region"] = event.region;
      raw["clientMode"] = event.client_mode;
      events.push_back(raw);
    }
    entry["events"] = events;
    records.push_back(entry);
  }
  json["records"] = records;
  return fs_write_text(fs_join(fs_app_data_dir(), "playtime.json"), json.dump(true));
}

app::PlaytimeRecord* PlaytimeService::find(const std::string& game_id) {
  for (app::PlaytimeRecord& record : records_) {
    if (record.game_id == game_id) return &record;
  }
  return nullptr;
}

app::PlaytimeRecord& PlaytimeService::ensure(const std::string& game_id,
                                        const std::string& game_title) {
  if (app::PlaytimeRecord* existing = find(game_id)) return *existing;
  app::PlaytimeRecord record;
  record.game_id = game_id;
  record.game_title = game_title;
  records_.push_back(record);
  return records_.back();
}

void PlaytimeService::mark_launch_start(const std::string& game_id, const std::string& game_title) {
  queue_started_at_ms_ = now_ms();
  recording_game_id_ = game_id;
  if (!game_title.empty()) {
    app::PlaytimeRecord& record = ensure(game_id, game_title);
    (void)record;
  }
}

void PlaytimeService::mark_queue_end() {
  if (queue_started_at_ms_ != 0 && !recording_game_id_.empty()) {
    const long long waited = now_ms() - queue_started_at_ms_;
    if (waited > 0) ensure(recording_game_id_, std::string()).queue_ms += waited;
  }
  queue_started_at_ms_ = 0;
}

void PlaytimeService::mark_stream_start(const std::string& game_id,
                                        const std::string& game_title,
                                        const std::string& region,
                                        const std::string& client_mode) {
  stream_started_at_ms_ = now_ms();
  recording_game_id_ = game_id;
  last_region_ = region;
  last_client_mode_ = client_mode;
  ensure(game_id, game_title);
}

void PlaytimeService::end_session(const std::string& game_id) {
  if (stream_started_at_ms_ == 0) {
    queue_started_at_ms_ = 0;
    return;
  }
  const int64_t elapsed = now_ms() - stream_started_at_ms_;
  stream_started_at_ms_ = 0;
  queue_started_at_ms_ = 0;
  if (elapsed <= 0) return;
  app::PlaytimeRecord& record = ensure(game_id, recording_game_id_);
  record.total_ms += elapsed;
  record.sessions += 1;
  record.last_played_ms = now_ms();
  if (elapsed > record.longest_session_ms) record.longest_session_ms = elapsed;
  if (record.events.size() >= 20) record.events.erase(record.events.begin());
  app::PlaytimeEvent event;
  event.started_at_ms = stream_started_at_ms_;
  event.duration_ms = elapsed;
  event.region = last_region_;
  event.client_mode = last_client_mode_;
  record.events.push_back(event);
  save();
  ONOW_INFO("Playtime", "recorded %lld ms for %s", static_cast<long long>(elapsed),
            game_id.c_str());
}

PlaytimeService::Summary PlaytimeService::summary() const {
  Summary summary;
  for (const app::PlaytimeRecord& record : records_) {
    summary.total_ms += record.total_ms;
    summary.total_sessions += record.sessions;
  }
  summary.ranked = records_;
  std::sort(summary.ranked.begin(), summary.ranked.end(),
            [](const app::PlaytimeRecord& a, const app::PlaytimeRecord& b) {
              return a.total_ms > b.total_ms;
            });
  summary.recent = records_;
  std::sort(summary.recent.begin(), summary.recent.end(),
            [](const app::PlaytimeRecord& a, const app::PlaytimeRecord& b) {
              return a.last_played_ms > b.last_played_ms;
            });
  return summary;
}

void PlaytimeService::reset() {
  records_.clear();
  stream_started_at_ms_ = 0;
  queue_started_at_ms_ = 0;
  save();
}

} // namespace services
} // namespace onow
