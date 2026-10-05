#include "onow/services/GfnClient.h"
#include "onow/services/AuthService.h"

#include "onow/Crypto.h"
#include "onow/Fs.h"
#include "onow/Json.h"
#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/Time.h"
#include "onow/net/Http.h"

#include <algorithm>

namespace onow {
namespace services {

AuthService::AuthService(GfnClient& client) : client_(client) {}

AuthService::~AuthService() {
  device_login_stop_.store(true);
  if (device_login_thread_.joinable()) device_login_thread_.join();
}

void AuthService::run_bootstrap() {
  Result<std::vector<model::LoginProvider>> providers = client_.providers();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (providers.ok()) providers_ = providers.value();
    else last_error_ = providers.error().message;
  }
  load_vault();
  busy_.store(false);
}

void AuthService::bootstrap() {
  if (busy_.exchange(true)) return;
  std::thread([this] { run_bootstrap(); }).detach();
}

void AuthService::load_vault() {
  const std::string path = fs_join(fs_app_data_dir(), "session.json");
  std::string text;
  if (!fs_read_text(path, text) || text.empty()) return;
  Json json = Json::parse_or(text);
  if (const Json* accounts = json.find("accounts")) {
    std::lock_guard<std::mutex> lock(mutex_);
    saved_.clear();
    for (size_t i = 0; i < accounts->size(); ++i) {
      const Json& entry = accounts->at(i);
      model::SavedAccount account;
      account.user_id = entry.find("userId") ? entry.find("userId")->as_string() : "";
      account.display_name = entry.find("displayName") ? entry.find("displayName")->as_string() : "";
      account.email = entry.find("email") ? entry.find("email")->as_string() : "";
      account.avatar_url = entry.find("avatarUrl") ? entry.find("avatarUrl")->as_string() : "";
      account.membership_tier =
          entry.find("membershipTier") ? entry.find("membershipTier")->as_string() : "";
      account.provider_code =
          entry.find("providerCode") ? entry.find("providerCode")->as_string() : "";
      if (!account.user_id.empty()) saved_.push_back(account);
    }
  }
}

void AuthService::save_vault() {
  std::lock_guard<std::mutex> lock(mutex_);
  Json json = Json::object();
  Json accounts = Json::array();
  for (const model::SavedAccount& account : saved_) {
    Json entry = Json::object();
    entry["userId"] = account.user_id;
    entry["displayName"] = account.display_name;
    entry["email"] = account.email;
    entry["avatarUrl"] = account.avatar_url;
    entry["membershipTier"] = account.membership_tier;
    entry["providerCode"] = account.provider_code;
    accounts.push_back(entry);
  }
  json["accounts"] = accounts;
  json["version"] = 1;
  if (session_) {
    Json active = Json::object();
    active["providerCode"] = session_->provider.code;
    active["userId"] = session_->user.user_id;
    // Tokens stay in memory for the process lifetime; the backend re-issues
    // them on the next session handshake. Persisting them would only widen the
    // blast radius of a stolen file.
    json["activeUser"] = active;
  }
  fs_write_text(fs_join(fs_app_data_dir(), "session.json"), json.dump(true));
}

std::vector<model::SavedAccount> AuthService::saved_accounts() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return saved_;
}

void AuthService::adopt_session(const model::AuthSession& session) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    session_ = session;
    bool found = false;
    for (const model::SavedAccount& account : saved_) {
      if (account.user_id == session.user.user_id) {
        found = true;
        break;
      }
    }
    if (!found) {
      model::SavedAccount account;
      account.user_id = session.user.user_id;
      account.display_name = session.user.display_name;
      account.email = session.user.email;
      account.avatar_url = session.user.avatar_url;
      account.membership_tier = session.user.membership_tier;
      account.provider_code = session.provider.code;
      saved_.push_back(account);
    }
  }
  save_vault();
  ONOW_INFO("Auth", "session adopted for %s", session.user.display_name.c_str());
}

void AuthService::start_device_login(const std::string& provider_idp_id) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_.clear();
    challenge_.reset();
  }
  device_login_stop_.store(false);

  // The backend owns the NVIDIA token exchange (it holds the client secret);
  // the native client only drives the same start/poll pair the web UI does.
  Json body = Json::object();
  if (!provider_idp_id.empty()) body["providerIdpId"] = provider_idp_id;
  HttpRequest request;
  request.method = "POST";
  request.url = client_.base_url() + "/api/auth/device/start";
  request.headers = {{"X-OpenNOW-Client", "native"}, {"Content-Type", "application/json"}};
  request.body = body.dump();
  Result<Json> json = http_json(request);
  if (!json.ok()) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_ = json.error().message;
    ONOW_ERROR("Auth", "device login start failed: %s", last_error_.c_str());
    return;
  }
  model::DeviceLoginChallenge challenge;
  challenge.attempt_id = json->find("attemptId") ? json->find("attemptId")->as_string() : "";
  challenge.device_code = challenge.attempt_id;
  challenge.user_code = json->find("userCode") ? json->find("userCode")->as_string() : "";
  challenge.verification_uri =
      json->find("verificationUri") ? json->find("verificationUri")->as_string() : "";
  challenge.verification_uri_complete = json->find("verificationUriComplete")
                                            ? json->find("verificationUriComplete")->as_string()
                                            : challenge.verification_uri;
  challenge.expires_at = json->find("expiresAt") ? json->find("expiresAt")->as_ll() : 0;
  challenge.interval_seconds =
      json->find("intervalSeconds") ? json->find("intervalSeconds")->as_int() : 5;
  if (challenge.attempt_id.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_ = "The backend did not return a device login challenge.";
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    challenge_ = challenge;
    active_attempt_id_ = challenge.attempt_id;
    device_login_active_ = true;
  }
  if (device_login_thread_.joinable()) device_login_thread_.join();
  device_login_stop_.store(false);
  device_login_thread_ = std::thread([this, id = challenge.attempt_id, interval = challenge.interval_seconds] {
    device_login_loop(id, interval);
  });
  ONOW_INFO("Auth", "device login started; code %s", challenge.user_code.c_str());
}

void AuthService::device_login_loop(std::string attempt_id, int interval_seconds) {
  while (!device_login_stop_.load()) {
    onow::sleep_ms(interval_seconds * 1000);
    if (device_login_stop_.load()) break;

    Json body = Json::object();
    body["attemptId"] = attempt_id;
    HttpRequest request;
    request.method = "POST";
    request.url = client_.base_url() + "/api/auth/device/poll";
    request.headers = {{"X-OpenNOW-Client", "native"}, {"Content-Type", "application/json"}};
    request.body = body.dump();
    Result<Json> json = http_json(request);
    if (!json.ok()) {
      std::lock_guard<std::mutex> lock(mutex_);
      last_error_ = json.error().message;
      return;
    }
    const std::string status = json->find("status") ? json->find("status")->as_string() : "pending";
    if (status == "authorized") {
      if (const Json* session = json->find("session")) {
        adopt_session(decode::auth_session(*session));
      }
      std::lock_guard<std::mutex> lock(mutex_);
      challenge_.reset();
      device_login_active_ = false;
      return;
    }
    if (status == "expired" || status == "access_denied" || status == "error") {
      std::lock_guard<std::mutex> lock(mutex_);
      last_error_ = json->find("error") ? json->find("error")->as_string()
                                        : "Device authorization " + status + ".";
      challenge_.reset();
      device_login_active_ = false;
      return;
    }
    if (status == "slow_down") {
      interval_seconds = std::min(interval_seconds + 5, 30);
    }
  }
}

model::DeviceLoginStatus AuthService::poll_device_login(model::DeviceLoginPoll& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  out.session = session_;
  if (device_login_active_ && challenge_) {
    out.status = model::DeviceLoginStatus::Pending;
    out.interval_seconds = challenge_->interval_seconds;
    return out.status;
  }
  if (!last_error_.empty()) {
    out.status = model::DeviceLoginStatus::Error;
    out.error = last_error_;
    last_error_.clear();
    return out.status;
  }
  out.status = model::DeviceLoginStatus::Pending;
  return out.status;
}

void AuthService::cancel_device_login() {
  device_login_stop_.store(true);
  std::string attempt_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    attempt_id = active_attempt_id_;
    challenge_.reset();
    device_login_active_ = false;
  }
  if (device_login_thread_.joinable()) device_login_thread_.join();
  if (!attempt_id.empty()) {
    Json body = Json::object();
    body["attemptId"] = attempt_id;
    HttpRequest request;
    request.method = "POST";
    request.url = client_.base_url() + "/api/auth/device/cancel";
    request.headers = {{"X-OpenNOW-Client", "native"}, {"Content-Type", "application/json"}};
    request.body = body.dump();
    http_json(request);
  }
  device_login_stop_.store(false);
}

void AuthService::logout() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    session_.reset();
  }
  HttpRequest request;
  request.method = "POST";
  request.url = client_.base_url() + "/api/logout";
  request.headers = {{"X-OpenNOW-Client", "native"}};
  http_json(request);
  save_vault();
  ONOW_INFO("Auth", "logged out");
}

void AuthService::switch_account(const std::string& user_id) {
  (void)user_id;
  // The backend holds one session per browser profile; switching means signing
  // out and starting a fresh device login. The UI drives that sequence.
  logout();
}

} // namespace services
} // namespace onow
