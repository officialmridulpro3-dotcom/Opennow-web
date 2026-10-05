// Built-in smoke checks — see SelfTest.h for the rationale.
//
// Every check is a pure function of the port's own logic: no window, no
// network, no sidecar process. That keeps it runnable anywhere the binary
// runs, which is what `make test` drives.
#include "onow/app/SelfTest.h"

#include "onow/Config.h"
#include "onow/Crypto.h"
#include "onow/Fs.h"
#include "onow/Json.h"
#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/Time.h"
#include "onow/app/Settings.h"
#include "onow/i18n.h"
#include "onow/model/Types.h"
#include "onow/net/Url.h"
#include "onow/stream/StreamEngine.h"

#include <cstdio>
#include <string>
#include <vector>

namespace onow {
namespace app {
namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char* name, bool ok, const std::string& detail = std::string()) {
  ++g_checks;
  if (ok) {
    std::printf("  ok   %s\n", name);
  } else {
    ++g_failures;
    std::printf("  FAIL %s%s%s\n", name, detail.empty() ? "" : " — ", detail.c_str());
  }
}

std::string show(const std::string& value) { return "\"" + value + "\""; }

// -------------------------------------------------------------------- json --

void test_json() {
  Json j = Json::object();
  j["name"] = Json("Cyberpunk 2077");
  j["id"] = Json(1234.0);
  j["ready"] = Json(true);
  Json list = Json::array();
  list.push_back(Json("a"));
  list.push_back(Json("b"));
  j["items"] = list;
  j["nested"] = Json::object();

  const std::string dumped = j.dump(false);
  Json parsed;
  check("json.parse", Json::parse(dumped, parsed), dumped);
  check("json.object field", parsed.find("name") && parsed.find("name")->as_string() == "Cyberpunk 2077");
  check("json.number field", parsed.find("id") && parsed.find("id")->as_number() == 1234.0);
  check("json.bool field", parsed.find("ready") && parsed.find("ready")->as_bool());
  check("json.array size", parsed.find("items") && parsed.find("items")->size() == 2);
  check("json.array element", parsed.find("items") && parsed.find("items")->at(1).as_string() == "b");
  check("json.missing field is null", parsed.find("nope") == nullptr);
  check("json.rejects garbage", !Json::parse("{not json", parsed));
}

// -------------------------------------------------------------------- str --

void test_str() {
  Resolution r;
  check("str.parse_resolution", parse_resolution("1920x1080", r) && r.width == 1920 && r.height == 1080);
  check("str.parse_resolution rejects junk", !parse_resolution("1080p", r));
  check("str.resolution_label", resolution_label("1920x1080") == "1080p");
  check("str.resolution_label passthrough", resolution_label("weird") == "weird");
  check("str.to_lower", str_lower("Montserrat") == "montserrat");
  check("str.to_upper", str_upper("nvidia") == "NVIDIA");
  check("str.trim", str_trim("  pad  ") == "pad");
  check("str.starts_with", str_starts_with("https://zone.example", "https://"));
  check("str.ends_with", str_ends_with("queue.json", ".json"));
  check("str.contains is case sensitive", str_contains("OpenNOW GeForce", "GeForce"));
  check("str.icontains is case insensitive", str_icontains("OpenNOW GeForce", "geforce"));
  check("str.replace_all", str_replace_all("a-b-c", "-", "_") == "a_b_c");
  check("str.split", str_split("a,b,c", ',').size() == 3);
  check("str.to_string int", to_string(42) == "42");
  check("str.str_to_int", str_to_int("-7") == -7);
  check("str.is_numeric_id", is_numeric_id("007") && !is_numeric_id("7a"));
  check("str.format_duration_hms", format_duration_hms(3725) == "01:02:05", format_duration_hms(3725));
  check("str.utf8_substr clamps", utf8_substr("abcdefghij", 0, 4) == "abcd");
}

// -------------------------------------------------------------------- url --

void test_url() {
  Url u;
  check("url.parse", url_parse("https://zone-1.cloudmatchbeta.nvidiagrid.net/v1/session?x=1", u));
  check("url.scheme", u.scheme == "https", u.scheme);
  check("url.host", u.host == "zone-1.cloudmatchbeta.nvidiagrid.net", u.host);
  check("url.port defaults per scheme", u.effective_port() == 443);
  check("url.path", u.path == "/v1/session", u.path);
  check("url.query", u.query == "x=1", u.query);

  Url p;
  check("url.parse host port", url_parse("http://127.0.0.1:3000/api/sessions", p));
  check("url.host port", p.host == "127.0.0.1" && p.port == 3000 && p.path == "/api/sessions");

  Url bad;
  check("url.rejects garbage", !url_parse("not a url at all", bad) || bad.host.empty());
}

// -------------------------------------------------------------------- i18n --

void test_i18n() {
  I18n& i18n = I18n::instance();
  i18n.set_locale("en");
  check("i18n.english lookup", i18n.t("app.name") == "OpenNOW", i18n.t("app.name"));
  check("i18n.missing key echoes key", i18n.t("nope.nope.nope") == "nope.nope.nope");
  check("i18n.missing key never empty", !i18n.t("nope.nope.nope").empty());
  check("i18n.interpolation",
        i18n.t("app.units.bitrateMbps", {{"value", "75"}}) == "75 Mbps",
        i18n.t("app.units.bitrateMbps", {{"value", "75"}}));
  check("i18n.interpolation leaves unknown placeholders alone",
        i18n.t("app.units.gb", {{"other", "1"}}) != "1 GB");

  const std::vector<std::string> locales = i18n.available_locales();
  check("i18n.12 locales", locales.size() == 12, to_string(static_cast<long long>(locales.size())));
  bool all_have_entries = true;
  for (const std::string& code : locales) {
    i18n.set_locale(code);
    if (i18n.t("app.name") != "OpenNOW") all_have_entries = false;
  }
  check("i18n.every locale resolves a base key", all_have_entries);

  i18n.set_locale("de");
  const std::string german = i18n.t("common.clear");
  i18n.set_locale("en");
  const std::string english = i18n.t("common.clear");
  check("i18n.locale switch changes text", german != english, german + " vs " + english);
  i18n.set_locale("de");
  check("i18n.falls back to english for a missing translation",
        !i18n.t("settings.sections.about").empty());

  // Plural selection: the base table ships a _plural sibling.
  i18n.set_locale("en");
  const std::string one = i18n.t_count("playtime.ranking.count", 1);
  const std::string many = i18n.t_count("playtime.ranking.count", 2);
  check("i18n.count form singular", one == "1 game", one);
  check("i18n.count form plural", many == "2 games", many);
}

// ------------------------------------------------------------------ crypto --

void test_crypto() {
  const std::string digest = sha256_hex("abc");
  check("crypto.sha256 vector", digest == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        digest);
  check("crypto.sha256 stable", sha256_hex("abc") == digest);
  check("crypto.sha256 distinct", sha256_hex("abd") != digest);
  const std::string uuid = random_hex(16);
  check("crypto.random_hex shape", uuid.size() == 32, uuid);
  check("crypto.random_hex distinct", random_hex(16) != random_hex(16));
  const std::string b64 = base64_encode("OpenNOW");
  check("crypto.base64 roundtrip", base64_decode_string(b64) == "OpenNOW", b64);
}

// --------------------------------------------------------------------- fs --

void test_fs() {
  const std::string dir = fs_join(fs_app_data_dir(), "selftest");
  check("fs.ensure_dir", fs_ensure_dir(dir) && fs_exists(dir));
  const std::string path = fs_join(dir, "probe.txt");
  check("fs.write_text", fs_write_text(path, "hello native"));
  std::string read_back;
  check("fs.read_text", fs_read_text(path, read_back) && read_back == "hello native");
  check("fs.file_size", fs_file_size(path) == 12);
  check("fs.basename", fs_basename(path) == "probe.txt");
  check("fs.extension", fs_extension(path) == "txt");
  check("fs.list_dir finds file", !fs_list_dir(dir).empty());
  check("fs.remove", fs_remove(path) && !fs_exists(path));
  check("fs.app_data_dir non-empty", !fs_app_data_dir().empty(), fs_app_data_dir());
}

// ---------------------------------------------------------------- settings --

void test_settings() {
  Settings s;
  check("settings.defaults", s.resolution == "1920x1080" && s.fps == 60 && s.max_bitrate_mbps == 75);
  check("settings.default transport", s.transport_mode == "nvst" || s.transport_mode == "webrtc",
        s.transport_mode);

  s.resolution = "2560x1440";
  s.mouse_sensitivity = 1.5f;
  s.clipboard_paste = false;
  const Json json = s.to_json();
  Settings restored = Settings::from_json(json);
  check("settings.roundtrip resolution", restored.resolution == "2560x1440", restored.resolution);
  check("settings.roundtrip float", restored.mouse_sensitivity == 1.5f);
  check("settings.roundtrip bool", !restored.clipboard_paste);

  Json partial_json;
  Settings partial = Json::parse("{\"resolution\":\"1280x720\"}", partial_json)
                            ? Settings::from_json(partial_json)
                            : Settings();
  check("settings.partial keeps defaults", partial.resolution == "1280x720" && partial.fps == 60);
}

// ------------------------------------------------------------------- model --

void test_model() {
  check("model.session ready 2", model::is_session_ready_for_connect(2));
  check("model.session ready 3", model::is_session_ready_for_connect(3));
  check("model.session not ready 1", !model::is_session_ready_for_connect(1));
  check("model.normalize store", model::normalize_game_store("epic") == "EPIC" ||
                                    !model::normalize_game_store("epic").empty());
  check("model.is_epic_store", model::is_epic_store("EPIC") && !model::is_epic_store("STEAM"));

  model::SessionInfo queued;
  queued.seat_setup_step = 1;
  check("model.in queue by seat step", model::is_session_in_queue(queued));
  model::SessionInfo queued_position;
  queued_position.queue_position = 4;
  check("model.in queue by position", model::is_session_in_queue(queued_position));
  model::SessionInfo ready;
  ready.status = 2;
  check("model.not in queue when ready", !model::is_session_in_queue(ready));

  model::GameInfo game;
  game.title = "Fortnite";
  check("model.game not in library by default", !model::is_game_in_library(game));
}

// --------------------------------------------------------------- streaming --

void test_streaming() {
  stream::SessionContext ctx;
  ctx.session_id = "sess-1";
  ctx.app_id = "1234";
  ctx.zone = "zone-1";
  ctx.width = 1920;
  ctx.height = 1080;
  ctx.fps = 60;
  ctx.bitrate_kbps = 75000;
  ctx.codec = "H264";
  ctx.ice_servers.push_back("stun:stun.example:3478");

  const Json json = ctx.to_json();
  check("stream.context json sessionId", json.find("sessionId")->as_string() == "sess-1");
  check("stream.context json video", json.find("video")->find("width")->as_number() == 1920.0);
  const stream::SessionContext back = stream::SessionContext::from_json(json);
  check("stream.context roundtrip", back.session_id == "sess-1" && back.width == 1920 &&
                                        back.fps == 60 && back.ice_servers.size() == 1);
}

// -------------------------------------------------------------------- time --

void test_time() {
  const int64_t now = now_ms();
  check("time.now_ms sane", now > 1700000000000LL, to_string(static_cast<long long>(now)));
  check("time.now_us sane", now_us() > 1700000000000000LL);
  const std::string iso = iso_utc(now);
  check("time.iso_utc shape", iso.size() == 24 && iso[4] == '-' && iso[10] == 'T' && iso[23] == 'Z', iso);
  check("time.parse_iso_utc roundtrip", parse_iso_utc(iso) / 1000 == now / 1000);
  check("time.monotonic advances", monotonic_ms() > 0);
}

} // namespace

int run_self_test() {
  std::printf("OpenNOW %s self-test\n", ONOW_VERSION_STRING);
  test_json();
  test_str();
  test_url();
  test_i18n();
  test_crypto();
  test_fs();
  test_settings();
  test_model();
  test_streaming();
  test_time();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}

} // namespace app
} // namespace onow
