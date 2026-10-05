// Child process with piped stdin/stdout/stderr.
//
// The NVST engine is a sidecar process the client supervises: it speaks
// JSON-lines over stdio (see docs/NATIVE_STREAMER.md). Both transports the web
// client used — the browser's own WebRTC stack and this sidecar — are
// out-of-process on purpose, so the port needs the same primitive.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace onow {
namespace stream {

class Process {
public:
  Process() = default;
  ~Process();

  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;

  // Launches `path` with the given arguments. `env` entries are `KEY=VALUE`.
  bool start(const std::string& path, const std::vector<std::string>& args,
             const std::vector<std::string>& env, std::string& error);

  // Reads one line (without the newline) from the given stream. Blocks until a
  // line arrives, the stream closes (returns false with `eof` true) or
  // `timeout_ms` elapses (returns false with `eof` false).
  bool read_line(int stream_index, std::string& line, int timeout_ms, bool& eof);

  bool write_line(int stream_index, const std::string& line);

  // True once the child has exited. Fills `exit_code` on the first call that
  // observes the exit.
  bool try_reap(int& exit_code);

  // Closes the pipe and asks the child to exit; force-kills after `grace_ms`.
  void terminate(int grace_ms = 3000);
  bool running() const { return running_; }

private:
  struct Impl;
  Impl* impl_ = nullptr;
  bool running_ = false;
};

} // namespace stream
} // namespace onow
