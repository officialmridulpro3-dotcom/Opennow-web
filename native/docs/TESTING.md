# Testing

The native client has no GUI in CI and no GPU, so the test strategy is: **test
everything that is pure logic without a window, and drive the real UI headlessly
on top of that.**

```sh
make test
```

does three things:

```
== i18n key audit ==      python3 tools/check_i18n_keys.py
== unit checks ==         ./build/headless/opennow --self-test
== headless smoke run ==  ./build/headless/opennow --headless --max-frames 60
```

## 1. Unit checks — `--self-test`

`src/app/SelfTest.cpp` runs **84 checks** covering every unit that does not need
a window, a socket or a sidecar process:

| Group | What is checked |
| --- | --- |
| `json` | parse, object/number/bool/array access, missing keys, rejection of garbage |
| `str` | resolution parsing and labels, case, trim, prefix/suffix, replace, split, duration formatting, UTF-8 substring clamping |
| `url` | scheme, host, explicit and default ports, path, query, rejection of garbage |
| `i18n` | base lookup, unknown-key echo, `{{value}}` interpolation, all 12 locales resolve, locale switching changes text, English fallback, singular/plural selection |
| `crypto` | the SHA-256 test vector, stability, distinctness, `random_hex`, base64 round-trip |
| `fs` | `ensure_dir`, write/read round-trip, file size, basename/extension, listing, removal |
| `settings` | defaults, JSON round-trip of every scalar type, partial JSON keeping defaults |
| `model` | session-ready (status 2/3), queue detection by seat step and by position, Epic store detection, library membership |
| `stream` | `SessionContext` JSON emission and round-trip |
| `time` | clock sanity, `iso_utc` shape, `parse_iso_utc` round-trip, monotonic clock |

Exit code is 0 when everything passes, 1 otherwise, with one `ok`/`FAIL` line per
check so a failure names itself.

```sh
$ ./build/headless/opennow --self-test
OpenNOW 1.0.0-native self-test
  ok   json.parse
  ok   json.object field
  ...
  ok   time.monotonic advances
84 checks, 0 failures
```

## 2. Headless smoke run

`--headless --max-frames 60` runs the **real** application: the real platform
loop, the real ImGui frame, the real layout pass, the real state machine — with a
synthetic clock and a discarded framebuffer. It exercises navigation, the launch
state machine, service callbacks, settings persistence and the whole draw path
(which is where the styling lives). The headless platform also injects
synthetic input (`inject_key`, `inject_text`, `inject_mouse`, `inject_click`),
which is how keyboard paths get covered without a display.

The headless backend is also what makes the `PLATFORM=headless` build the CI
build: no GLFW, no X server, no GPU.

## 3. i18n key audit

`tools/check_i18n_keys.py` parses every `tr("...")` / `tr_count("...")` call in
`src/` and `include/` and checks each key against `../locales/en.json`. It
exists because `I18n::lookup` echoes an unknown key back to the caller, so a
typo ships as visible `errors.fooBar` text instead of a build failure.

```sh
$ make check-keys
210 keys referenced, 0 missing from en.json
note: 418 keys are not translated in every locale (they fall back to English)
```

The second line is informational: the non-English locales are partial by design.

## What is not covered

Honest about the gaps:

- **No window-system integration test.** Pointer lock, fullscreen transitions,
  clipboard and native file dialogs only run on a real desktop.
- **No network integration test.** Everything that talks to the backend is
  written against the same HTTP/JSON contract as the web client, but there is no
  mock server in CI.
- **No codec tests.** There is no decoder (see the README), so nothing to test.
- **`PLATFORM=win32` is not built here.** No Windows toolchain in this
  environment; `PLATFORM=glfw` is syntax-checked against a stub GLFW header
  because no GLFW package is installable, and the headless build is the one that
  is compiled and executed.

## Adding a check

Add a `test_*()` function to `src/app/SelfTest.cpp`, call it from
`run_self_test()`, and use the `check()` helper:

```cpp
check("model.session ready 3", model::is_session_ready_for_connect(3));
check("str.resolution_label", resolution_label("1920x1080") == "1080p",
      resolution_label("1920x1080"));   // optional detail, printed on failure
```

Keep it pure — no I/O beyond the app-data directory, no threads, no clocks that
are not the monotonic one.
