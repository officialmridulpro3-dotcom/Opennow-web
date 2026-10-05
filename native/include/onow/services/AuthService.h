// Authentication: NVIDIA device authorization, token refresh, and the local
// account vault.
//
// The web client kept tokens in AES-256-GCM encrypted HTTP-only cookies and
// polled `https://login.nvidia.com/token` from the server. The native client
// keeps the same shape but owns both sides: the vault lives in the app-data
// directory (encrypted when the platform provides a cipher) and the device
// login poll runs here instead of behind an HTTP endpoint, because the C++
// client talks to NVIDIA's endpoints through the backend for anything
// token-shaped.
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

class AuthService {
public:
  explicit AuthService(GfnClient& client);
  ~AuthService();

  // Discovers providers and restores a saved session. Blocking; run once at
  // startup on a worker thread.
  void bootstrap();

  // Device login. `start()` kicks off the challenge; the UI polls
  // `poll_result()` or waits for `on_complete`.
  void start_device_login(const std::string& provider_idp_id);
  void cancel_device_login();
  model::DeviceLoginStatus poll_device_login(model::DeviceLoginPoll& out);

  void logout();
  void switch_account(const std::string& user_id);

  bool busy() const { return busy_.load(); }
  const std::string& last_error() const { return last_error_; }

  std::vector<model::SavedAccount> saved_accounts() const;

  // Called from the UI thread to adopt a completed session.
  void adopt_session(const model::AuthSession& session);

private:
  void run_bootstrap();
  void save_vault();
  void load_vault();
  void device_login_loop(std::string attempt_id, int interval_seconds);

  GfnClient& client_;
  std::atomic<bool> busy_{false};
  std::string last_error_;

  mutable std::mutex mutex_;
  std::vector<model::LoginProvider> providers_;
  std::vector<model::SavedAccount> saved_;
  std::optional<model::AuthSession> session_;
  std::optional<model::DeviceLoginChallenge> challenge_;
  std::string active_attempt_id_;
  bool device_login_active_ = false;
  std::thread device_login_thread_;
  std::atomic<bool> device_login_stop_{false};
};

} // namespace services
} // namespace onow
