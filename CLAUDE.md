# GoodGit

Read `doc/ARCHITECTURE.md` before changing anything: it is the map of the code and the index into the
per-concern documents beside it. Read the one for the concern being changed, and keep them in sync with the
code: current structure and rationale only, no history, no implementation detail.

Read `doc/conventions.md` before writing code.

`scripts/build_tests.bat` builds the unit tests and `scripts/run_tests.bat` runs them; build and run them after changing any source `tests/tests.pro` builds.
`scripts/build_app.bat` builds the app; run it after changing any source under `app/src`: the tests compile only part of it.
Each has a `.sh` twin for macOS and Linux. Locally, pass `debug` to each (release is the default): release defines `NDEBUG`, which compiles out the asserts guarding invariants. CI tests both.
A run's output goes to a log file in the scratchpad, never into context; surface only the exit code and the informative lines:

```
(scripts/build_tests.bat debug && scripts/run_tests.bat debug) > "$LOG" 2>&1; echo "EXIT: $?"; grep -E "error [A-Z]+[0-9]+|All tests passed|FAILED" "$LOG" | head -30
scripts/build_app.bat debug > "$LOG" 2>&1; echo "EXIT: $?"; grep -E "error [A-Z]+[0-9]+" "$LOG" | head -30
```

The pattern matches MSVC errors and the Catch2 summary; adjust it for other toolchains, not by falling back to `tail`.
