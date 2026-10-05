# Tests

```sh
tests/run.sh              # build (headless) + i18n audit + unit checks + smoke run
tests/run.sh --no-build   # skip the build
tests/run.sh --quick      # i18n audit + unit checks only
make test                 # the same thing, through the Makefile
```

Everything here runs with **no window, no GPU and no network**, which is the
point: the headless platform backend renders the real UI into a discarded
framebuffer, so the layout and state-machine paths are genuinely exercised.

See [../docs/TESTING.md](../docs/TESTING.md) for what the 84 unit checks cover,
what the smoke run covers, and what is deliberately not covered.

## Layout

| Path | What |
| --- | --- |
| `run.sh` | the runner described above |
| `../src/app/SelfTest.cpp` | the unit checks themselves — add new ones here |

There is no separate test binary and no test framework: the checks are compiled
into the client and reached with `--self-test`, so there is exactly one build
artifact to keep in sync.
