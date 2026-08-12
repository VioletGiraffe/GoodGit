# GoodGit v1 — build order

Companion to `plan.md`, which holds the design and rationale. This file only orders the work into
self-contained, individually buildable and reviewable steps. Each step ends at a checkpoint: the user builds,
reviews, commits. Design references (§) point into `plan.md`.

Delete both files once the code and an architecture doc supersede them.

---

## Step 0 — build configuration

- `app/app.pro`: `QT = core gui widgets`, drop `CONFIG -= qt` and `CONFIG += console`, `TARGET = GoodGit` (§8).
- Trivial `QApplication` + empty `QMainWindow` in `main.cpp` so the result is runnable.

**Decision needed here:** `INCLUDEPATH` already points at `../qtutils`, which is not a submodule. Add it as
one, or drop the entry. Nothing in v1 strictly requires qtutils, but if it is wanted at all, wiring it in now
is cheaper than mid-project.

**Checkpoint:** a window opens; both submodules still build.

## Step 1 — process layer

- `GitResult`, `GitProcess` (§6): async `QProcess` wrapper owning every invariant from §3 — quotepath, `-z`,
  optional-locks flag on read queries, `GIT_TERMINAL_PROMPT=0`, stdin payloads, completion callbacks,
  the ≤4 concurrent processes cap, kill support for speculative requests.
- Git executable discovery (PATH, plus a `Settings` override slot for later).

**Also in this step — verify the assumptions the design leans on**, with a throwaway harness or by hand,
before anything is built on top of them:

1. `git commit --pathspec-from-file` exists in the supported git versions (§3 fallback if not).
2. `git diff --no-index -- /dev/null <path>` works on Git for Windows, or the all-`+` synthesis is needed (§4).
3. `-F <tmpfile>` + stdin pathspec ordering behaves as expected in one live commit.

Record the results in this file; adjust §3/§4 if any fail.

**Checkpoint:** a scratch `main()` that runs two overlapping queries and prints raw results.

## Step 2 — repository model, no UI

- `FileEntry`, `RepoState`, `Repository` (§6): the refresh pipeline (§4 refresh table) — header status,
  name-status diff, untracked list, direct submodule enumeration and per-submodule dirtiness, gitdir
  resolution, in-progress-operation detection (the `.git`-is-a-file rule), unborn-HEAD fallback.
- Parsers for `-z` name-status (rename entries carry two paths), porcelain v2 branch headers,
  `submodule status` output.
- Refresh coalescing (§5.1) and the `refreshed()` signal.

This is the highest-risk parsing code in the project; keep the parsers as free functions so they can be
exercised directly.

**Checkpoint:** scratch `main()` dumps the model for this repo and for a submodule; output eyeballed against
`git status`.

## Step 3 — window skeleton with live file list

- `CommitWindow` `.ui`: layout 4 exactly as drawn in `doc/UI/mockup.html` — 430 px left column (repo header
  row, file list, message editor with 50-column marker, Commit / Commit & Push), diff placeholder right,
  splitter persisted (§7).
- `ChangedFilesModel` over the entries; check state re-derivation by path across refreshes (§5.1);
  tri-state check-all in the counter line; commit button enable state + count.
- `Settings` (§6): geometry, splitter. Recent messages storage comes with the commit step.
- `main`: repo resolution via `rev-parse --show-toplevel`, error exit if not a repo.
- F5 refresh. Startup refresh.

**Checkpoint:** first real milestone — the window shows this repo's live state, ticks survive F5.

## Step 4 — row presentation

- `FileRowDelegate` (§6): per-state colors, strikethrough deleted paths, folder icon, warning tint + disabled
  checkbox on blocked submodule rows, `new/path (was old/path)` rename rendering.
- Header strips for merge-in-progress and detached HEAD — display only at this step; the behavior behind them
  comes in steps 6–7.

**Checkpoint:** visual pass against the mockup; every state from §2's table represented (fabricate repo states
by hand to see them).

## Step 5 — diff pane

- `DiffView` + `DiffHighlighter` (§6), wired to selection changes; in-flight process killed on re-selection.
- Tracked / renamed / untracked / submodule variants (§4 diff table), size cap placeholder, binary
  pass-through.

**Checkpoint:** click through every row kind in a real repo.

## Step 6 — commit

- Normal-mode commit sequence (§4): untracked confirmation dialog (§5.3), `add`, `commit`, rollback on
  failure. Post-action refresh (§5.1). Ticked rows are the pathspec, verbatim.
- Message editor glue: recent-messages dropdown (per-repo, ~20), Ctrl+Enter, disabled-state rules.
- Merge mode (§4): tracked changes forced on, untracked keep checkboxes, no-pathspec commit path.
- Failure dialog showing stderr verbatim (§5.6) — built here, reused by everything after.

**Checkpoint:** real commits in a scratch repo: plain, with untracked, rejected by a hook (rollback observed),
merge commit.

## Step 7 — detached HEAD reattachment

- The §5.5 table: silent checkout cases, the ask-dialog for multiple candidates, refusals with explanation.
  Runs at commit time; the strip from step 4 already announces the state beforehand.

**Checkpoint:** scripted scenarios in a scratch repo for each of the five table rows.

## Step 8 — push

- `push --recurse-submodules=on-demand`; no-upstream detection → `--set-upstream` offer; non-fast-forward
  reported plainly (§5.6). Push and Commit & Push buttons live.

**Checkpoint:** push to a scratch remote; both failure cases exercised.

## Step 9 — file actions

- Context menu (§7): Add to index / Un-add with mixed-selection no-op semantics, Open, Show in Explorer,
  Copy path, Delete.
- Delete to trash (§5.4) with its per-state table, including the un-add-first rule for `A` rows and the
  no-fallback rule on `moveToTrash` failure.
- Space / Del / Enter (external difftool via `git difftool`, if configured) keyboard wiring.

**Checkpoint:** each action on each applicable row state.

## Step 10 — submodule windows

- Double-click on a submodule row opens a `CommitWindow` on it, same process (§5.2); child signals the parent
  to refresh after commit (§5.1).
- Blocked-condition enforcement at commit time (belt: the checkbox is already disabled by step 4).

**Checkpoint:** commit inside a submodule from a child window; parent row appears/updates without touching F5.

## Step 11 — close-out

- Sweep of §5.6 against the implementation; window titles; any UI polish queued during earlier checkpoints.
- Replace `plan.md` + this file with a current-state architecture doc; update `README.md` (§8).

---

Steps 4–5 can swap or interleave with 3 if convenient; 6 is the spine and everything after it is
order-flexible except 10, which needs 6.
