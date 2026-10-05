#!/usr/bin/env bash
# OpenNOW native client — test runner.
#
# Builds the headless target and runs the three checks that make up `make test`:
# the i18n key audit, the unit checks, and a headless smoke run of the real
# application. Works with no window, no GPU and no network.
#
#   tests/run.sh              build + everything
#   tests/run.sh --no-build   run against the existing build/
#   tests/run.sh --quick      unit checks only
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD=1
QUICK=0
for arg in "$@"; do
  case "$arg" in
    --no-build) BUILD=0 ;;
    --quick) QUICK=1 ;;
    -h|--help)
      sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

status=0

if [[ $BUILD -eq 1 ]]; then
  echo "== building (headless) =="
  if ! make PLATFORM=headless -j"$(nproc 2>/dev/null || echo 2)"; then
    echo "FAIL: build" >&2
    exit 1
  fi
fi

BIN=build/headless/opennow
if [[ ! -x $BIN ]]; then
  echo "FAIL: $BIN missing (run without --no-build)" >&2
  exit 1
fi

echo
echo "== i18n key audit =="
if python3 tools/check_i18n_keys.py; then :; else status=1; fi

echo
echo "== unit checks =="
if "$BIN" --self-test; then :; else status=1; fi

if [[ $QUICK -eq 1 ]]; then
  echo
  [[ $status -eq 0 ]] && echo "PASS (quick)" || echo "FAIL"
  exit $status
fi

echo
echo "== headless smoke run =="
if "$BIN" --headless --max-frames 60; then :; else status=1; fi

echo
if [[ $status -eq 0 ]]; then
  echo "PASS"
else
  echo "FAIL"
fi
exit $status
