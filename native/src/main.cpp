// Entry point. Argument handling is deliberately small: the native client is a
// GUI app, and the only flags it needs are the ones that make it usable from a
// terminal, a test, or a CI runner.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "onow/Config.h"
#include "onow/Log.h"
#include "onow/app/App.h"
#include "onow/app/SelfTest.h"

namespace {

void print_usage() {
  std::printf(
      "OpenNOW %s — native GeForce NOW client\n"
      "\n"
      "Usage: opennow [options]\n"
      "\n"
      "  --backend-url <url>   OpenNOW backend to talk to\n"
      "                        (default http://127.0.0.1:3000)\n"
      "  --locale <code>       UI locale (en, de, es, fr, ja, ko, nl, pl, ro, ru, tr, zh)\n"
      "  --headless            no window; drive the app with a synthetic clock\n"
      "  --max-frames <n>      with --headless, quit after n frames\n"
      "  --self-test           run the built-in checks and exit\n"
      "  --version             print the build description and exit\n"
      "  --help                this text\n",
      ONOW_VERSION_STRING);
}

} // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  if (!args.empty()) {
    const std::string& first = args.front();
    if (first == "--help" || first == "-h") {
      print_usage();
      return 0;
    }
    if (first == "--version" || first == "-v") {
      std::printf("OpenNOW %s (%s)\n", ONOW_VERSION_STRING, onow::build_description().c_str());
      return 0;
    }
  }

  // The self-test needs no window and no backend, so it runs before the app
  // is constructed at all — that is what makes it usable in CI. It wins over
  // any other flag so `--self-test --headless --max-frames 3` still works.
  for (const std::string& arg : args) {
    if (arg == "--self-test") return onow::app::run_self_test();
  }

  onow::app::App app;
  std::string error;
  if (!app.init(argc, argv, error)) {
    std::fprintf(stderr, "OpenNOW failed to start: %s\n", error.c_str());
    return 1;
  }
  return app.run();
}
