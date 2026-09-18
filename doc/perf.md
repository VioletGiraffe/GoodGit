# Measuring performance

`scripts/perf/` holds what the process-level performance decisions were made with. Windows only, PowerShell 7.
Each script's `-?` help states its parameters; none takes a repository of its own, so every run names the
repositories to measure.

| script | question it answers |
|---|---|
| `git_throughput.ps1` | how many git processes in flight are worth launching: wall time of a batch of read-only queries at a ladder of concurrency caps. `MaxConcurrentProcesses` in `vcsprocess` is chosen with it |
| `git_command_cost.ps1` | what a single git command costs, split into process startup and the work itself, over a ladder from `--version` to `status`. Comparing two builds shows a regression between git versions |
| `conhost_cost.ps1` | what a child process's console costs: `CREATE_NO_WINDOW`, which Qt asks for, against `DETACHED_PROCESS`. Why `vcsprocess` starts every child detached |
| `cpu_topology.ps1` | which logical processors are the slow cores, for reading the results on a hybrid CPU and for building the affinity masks the others take |

The harnesses launch git the way the app does - `CreateProcess`, output redirected, the read-only invariants
on the command line - so process startup counts as it does in the app.

## Reading the results

- **Process launching is serial and is often the limit.** One thread starts every process, at a few ms each,
  in the harness as in the app. A batch's time therefore flattens near the logical processor count however
  many processes are allowed at once.
- **A hybrid CPU needs the affinity masks.** Pinning to the slow cores stands in for a weaker machine than the
  one measuring; pinning to the fast ones tells whether the scheduler moved the work.
- **An affinity mask sticks for the rest of the PowerShell process.** A run that passes none after one that did
  is not unpinned, so a comparison passes the mask explicitly every time.
- **Keep the measuring app's window focused.** Windows biases a background process tree towards the slow cores.
- **Medians over repeats, one warm-up discarded, cap order shuffled per repeat**, so drift during a run does
  not line up with the ladder.
- **Single-process runs are pessimistic**: a batch at cap 2 comes out more than twice as fast as at cap 1,
  which is frequency ramping between short processes, not scaling.

## What has been measured

On a 16-thread desktop CPU with repositories on NVMe SSDs, against git 2.55 and 2.37 for Windows:

- **Throughput flattens at the logical processor count.** A batch of 173 `status` queries, one per submodule,
  takes the same time at caps 16, 24, 32 and 48. Four slow cores flatten at 4, and oversubscribing them costs
  no throughput.
- **The Git for Windows `cmd\git.exe` wrapper costs ~8.5 ms per query in latency, ~2 ms in a batch.** It is a
  stub that sets `MSYSTEM`, `HOME` and `PATH` before running `mingw64\bin\git.exe`. Going around it means
  taking over that environment: without `usr\bin` on `PATH`, LFS filters, `ssh` and hooks fail, and a
  read-only query can spawn a filter too. Rejected for that reason, not for the measurement.
- **A child's console costs a `conhost.exe` process**, and Qt's `CREATE_NO_WINDOW` hides that console instead
  of doing without one. Starting the child detached saves ~30% of a query's latency where the child is git
  itself, and nothing where it is the Git for Windows wrapper: the wrapper's own child, a console program
  started by a process that has no console, then allocates one instead. One process per query is saved either
  way.
- **Per-command cost dominates small repositories.** `git --version`, which touches nothing, takes ~20 ms of
  the ~30 ms a `status` of a small repository costs.
- **git 2.55 costs ~6 ms more per command than 2.37**, independently of the working directory.
