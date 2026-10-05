// Process implementation: Win32 CreateProcess on Windows, fork/exec elsewhere.
#include "onow/stream/Process.h"

#include "onow/Log.h"

#include <chrono>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace onow {
namespace stream {

namespace {

constexpr int kStdin = 0;
constexpr int kStdout = 1;
constexpr int kStderr = 2;

} // namespace

#if defined(_WIN32)

struct Process::Impl {
  HANDLE child_stdin_read = nullptr;
  HANDLE child_stdin_write = nullptr;
  HANDLE child_stdout_read = nullptr;
  HANDLE child_stdout_write = nullptr;
  HANDLE child_stderr_read = nullptr;
  HANDLE child_stderr_write = nullptr;
  PROCESS_INFORMATION pi{};
  bool spawned = false;
};

Process::~Process() {
  if (impl_ && impl_->spawned) terminate();
  delete impl_;
}

bool Process::start(const std::string& path, const std::vector<std::string>& args,
                    const std::vector<std::string>& env, std::string& error) {
  impl_ = new Impl();
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  auto make_pipe = [&](HANDLE& read_end, HANDLE& write_end) {
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) return false;
    // The parent must not hold the child's ends, or EOF never arrives.
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(write_end, HANDLE_FLAG_INHERIT, 0);
    return true;
  };

  if (!make_pipe(impl_->child_stdin_read, impl_->child_stdin_write) ||
      !make_pipe(impl_->child_stdout_read, impl_->child_stdout_write) ||
      !make_pipe(impl_->child_stderr_read, impl_->child_stderr_write)) {
    error = "CreatePipe failed";
    return false;
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = impl_->child_stdin_read;
  si.hStdOutput = impl_->child_stdout_write;
  si.hStdError = impl_->child_stderr_write;

  std::wstring command;
  command += L"\"" + std::wstring(path.begin(), path.end()) + L"\"";
  for (const std::string& arg : args) {
    command += L" \"" + std::wstring(arg.begin(), arg.end()) + L"\"";
  }

  // The environment block has to be a flat NUL-terminated, double-NUL-ended
  // buffer, and CreateProcess writes into the command line, so both need to be
  // mutable copies.
  std::wstring env_block;
  for (const std::string& entry : env) {
    env_block += std::wstring(entry.begin(), entry.end());
    env_block.push_back(L'\0');
  }
  env_block.push_back(L'\0');
  std::vector<wchar_t> command_buffer(command.begin(), command.end());
  command_buffer.push_back(L'\0');

  std::vector<wchar_t> env_buffer;
  if (!env_block.empty()) {
    env_buffer.assign(env_block.begin(), env_block.end());
  }

  const BOOL ok = CreateProcessW(nullptr, command_buffer.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                 env_buffer.empty() ? nullptr : env_buffer.data(), nullptr, &si,
                                 &impl_->pi);
  if (!ok) {
    error = "CreateProcess failed (" + std::to_string(static_cast<int>(GetLastError())) + ")";
    return false;
  }
  impl_->spawned = true;
  running_ = true;
  // The child's ends now belong to the child alone.
  CloseHandle(impl_->child_stdin_read);
  CloseHandle(impl_->child_stdout_write);
  CloseHandle(impl_->child_stderr_write);
  impl_->child_stdin_read = nullptr;
  impl_->child_stdout_write = nullptr;
  impl_->child_stderr_write = nullptr;
  return true;
}

bool Process::read_line(int stream_index, std::string& line, int timeout_ms, bool& eof) {
  line.clear();
  eof = false;
  HANDLE pipe = stream_index == kStderr ? impl_->child_stderr_read : impl_->child_stdout_read;
  if (!pipe) return false;

  std::string pending;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    // Look for a newline in what we already have.
    const size_t newline = pending.find('\n');
    if (newline != std::string::npos) {
      line = pending.substr(0, newline);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      return true;
    }
    const auto remaining = deadline - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::milliseconds(0)) return false;
    const DWORD wait_ms = static_cast<DWORD>(
        std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());
    DWORD available = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
      eof = true; // pipe closed
      return false;
    }
    if (available == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      (void)wait_ms;
      continue;
    }
    char buffer[4096];
    DWORD got = 0;
    if (!ReadFile(pipe, buffer, sizeof(buffer), &got, nullptr) || got == 0) {
      eof = true;
      return false;
    }
    pending.append(buffer, got);
  }
}

bool Process::write_line(int stream_index, const std::string& line) {
  if (stream_index != kStdin || !impl_->child_stdin_write) return false;
  std::string payload = line;
  payload.push_back('\n');
  DWORD written = 0;
  return WriteFile(impl_->child_stdin_write, payload.data(), static_cast<DWORD>(payload.size()),
                   &written, nullptr) == TRUE;
}

bool Process::try_reap(int& exit_code) {
  if (!impl_ || !impl_->spawned) return false;
  DWORD code = 0;
  if (!GetExitCodeProcess(impl_->pi.hProcess, &code)) return false;
  if (code == STILL_ACTIVE) return false;
  exit_code = static_cast<int>(code);
  running_ = false;
  CloseHandle(impl_->pi.hThread);
  CloseHandle(impl_->pi.hProcess);
  impl_->pi.hProcess = nullptr;
  impl_->pi.hThread = nullptr;
  impl_->spawned = false;
  return true;
}

void Process::terminate(int grace_ms) {
  if (!impl_ || !impl_->spawned) return;
  // Close stdin first: a well-behaved sidecar treats EOF as "stop".
  if (impl_->child_stdin_write) {
    CloseHandle(impl_->child_stdin_write);
    impl_->child_stdin_write = nullptr;
  }
  const DWORD waited = WaitForSingleObject(impl_->pi.hProcess, static_cast<DWORD>(grace_ms));
  if (waited == WAIT_TIMEOUT) {
    TerminateProcess(impl_->pi.hProcess, 1);
    WaitForSingleObject(impl_->pi.hProcess, 2000);
  }
  int code = 0;
  try_reap(code);
}

#else // POSIX

struct Process::Impl {
  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  pid_t pid = -1;
  bool spawned = false;
};

Process::~Process() {
  if (impl_ && impl_->spawned) terminate();
  delete impl_;
}

bool Process::start(const std::string& path, const std::vector<std::string>& args,
                    const std::vector<std::string>& env, std::string& error) {
  impl_ = new Impl();
  if (pipe(impl_->stdin_pipe) != 0 || pipe(impl_->stdout_pipe) != 0 || pipe(impl_->stderr_pipe) != 0) {
    error = std::string("pipe: ") + std::strerror(errno);
    return false;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    error = std::string("fork: ") + std::strerror(errno);
    return false;
  }
  if (pid == 0) {
    // Child: stdin from the read end of our stdin pipe, stdout/stderr to the
    // write ends of theirs.
    dup2(impl_->stdin_pipe[0], STDIN_FILENO);
    dup2(impl_->stdout_pipe[1], STDOUT_FILENO);
    dup2(impl_->stderr_pipe[1], STDERR_FILENO);
    close(impl_->stdin_pipe[0]);
    close(impl_->stdin_pipe[1]);
    close(impl_->stdout_pipe[0]);
    close(impl_->stdout_pipe[1]);
    close(impl_->stderr_pipe[0]);
    close(impl_->stderr_pipe[1]);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(path.c_str()));
    for (const std::string& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    if (!env.empty()) {
      std::vector<std::string> owned;
      owned.reserve(env.size());
      for (const std::string& entry : env) owned.push_back(entry);
      for (std::string& entry : owned) {
        const size_t eq = entry.find('=');
        if (eq != std::string::npos) setenv(entry.substr(0, eq).c_str(), entry.substr(eq + 1).c_str(), 1);
      }
    }
    execv(path.c_str(), argv.data());
    _exit(127);
  }

  impl_->pid = pid;
  impl_->spawned = true;
  running_ = true;
  // Parent keeps only its own ends.
  close(impl_->stdin_pipe[0]);
  close(impl_->stdout_pipe[1]);
  close(impl_->stderr_pipe[1]);
  impl_->stdin_pipe[0] = -1;
  impl_->stdout_pipe[1] = -1;
  impl_->stderr_pipe[1] = -1;
  return true;
}

bool Process::read_line(int stream_index, std::string& line, int timeout_ms, bool& eof) {
  line.clear();
  eof = false;
  const int fd = stream_index == kStderr ? impl_->stderr_pipe[0] : impl_->stdout_pipe[0];
  if (fd < 0) return false;

  std::string pending;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    const size_t newline = pending.find('\n');
    if (newline != std::string::npos) {
      line = pending.substr(0, newline);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      return true;
    }
    const auto remaining = deadline - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::milliseconds(0)) return false;
    const int wait_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());

    struct pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    const int ready = poll(&pfd, 1, wait_ms);
    if (ready < 0) {
      if (errno == EINTR) continue;
      eof = true;
      return false;
    }
    if (ready == 0) return false;
    char buffer[4096];
    const ssize_t got = read(fd, buffer, sizeof(buffer));
    if (got < 0) {
      if (errno == EINTR) continue;
      eof = true;
      return false;
    }
    if (got == 0) {
      eof = true;
      return false;
    }
    pending.append(buffer, static_cast<size_t>(got));
  }
}

bool Process::write_line(int stream_index, const std::string& line) {
  if (stream_index != kStdin || impl_->stdin_pipe[1] < 0) return false;
  std::string payload = line;
  payload.push_back('\n');
  size_t offset = 0;
  while (offset < payload.size()) {
    const ssize_t wrote = write(impl_->stdin_pipe[1], payload.data() + offset, payload.size() - offset);
    if (wrote < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    offset += static_cast<size_t>(wrote);
  }
  return true;
}

bool Process::try_reap(int& exit_code) {
  if (!impl_ || !impl_->spawned) return false;
  int status = 0;
  const pid_t result = waitpid(impl_->pid, &status, WNOHANG);
  if (result <= 0) return false;
  exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  running_ = false;
  impl_->spawned = false;
  return true;
}

void Process::terminate(int grace_ms) {
  if (!impl_ || !impl_->spawned) return;
  if (impl_->stdin_pipe[1] >= 0) {
    close(impl_->stdin_pipe[1]);
    impl_->stdin_pipe[1] = -1;
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(grace_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    int code = 0;
    if (try_reap(code)) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  kill(impl_->pid, SIGKILL);
  int code = 0;
  try_reap(code);
}

#endif

} // namespace stream
} // namespace onow
