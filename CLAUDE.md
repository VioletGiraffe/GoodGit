# GoodGit

Read `doc/ARCHITECTURE.md` before changing anything: it is the map of the code and the index into the
per-concern documents beside it. Read the one for the concern being changed, and keep them in sync with the
code: current structure and rationale only, no history, no implementation detail.

Read `doc/conventions.md` before writing code.

`scripts/run_tests.bat` (`run_tests.sh` on macOS and Linux) builds and runs the unit tests; run it after changing any source `tests/tests.pro` compiles.
Run it locally with the `debug` argument (release is the default): release defines `NDEBUG`, which compiles out the asserts guarding invariants. CI runs both.
A run's output goes to a log file in the scratchpad, never into context; surface only the exit code and the informative lines:

```
scripts/run_tests.bat debug > "$LOG" 2>&1; echo "EXIT: $?"; grep -E "error [A-Z]+[0-9]+|All tests passed|FAILED" "$LOG" | head -30
```

The pattern matches MSVC errors and the Catch2 summary; adjust it for other toolchains, not by falling back to `tail`.
