// Built-in smoke checks.
//
// The native client has no GUI in CI (and no GPU), so the portable way to know
// the port still works is a self-test that exercises every pure-logic unit
// without touching the network or a window: JSON, string helpers, the locale
// tables, the settings store, the session/runtime types and the URL parser.
//
//   ./opennow --self-test
//
// Returns 0 when every check passes, 1 otherwise, and prints one line per
// check so a failure names itself.
#pragma once

namespace onow {
namespace app {

int run_self_test();

} // namespace app
} // namespace onow
