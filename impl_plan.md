# GoodGit v1 — build order

Companion to `plan.md`, which holds the design and rationale. This file only orders the work into
self-contained, individually buildable and reviewable steps. Each step ends at a checkpoint: the user builds,
reviews, commits. Design references (§) point into `plan.md`.

Delete both files once the code and an architecture doc supersede them.

---

**All submodules are first-party** (cpputils, cpp-template-utils, qtutils). When a helper or widget almost
fits, extend or fix it in the library — in library shape, not GoodGit-flavored — rather than working around it
here. Ask before changing existing behavior; other projects share these. Each such change is a commit in the
submodule plus a pointer bump here.

## Step 0 — build configuration

- `app/app.pro`: `QT = core gui widgets`, drop `CONFIG -= qt` and `CONFIG += console`, `TARGET = GoodGit` (§8).
- The qtutils submodule is already added; wire it into the build: root `app.pro` `SUBDIRS` + `app.depends`,
  `-lqtutils` in `app/app.pro` (the `INCLUDEPATH` entry is already there).
- Trivial `QApplication` + empty `QMainWindow` in `main.cpp` so the result is runnable.

**Checkpoint:** a window opens; all three submodules build.

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

**Verified 2026-08-12 against git 2.37.1.windows.1:**

1. Accepted by `git commit`. Confirmed live: only the pathspec'd file was committed, other modified and
   untracked files and the index untouched.
2. Works; exits 1 when the file has content, so exit code 1 is success for this one command.
3. Works. The collision is real: `-F -` together with `--pathspec-from-file=-` reads the pathspec as the
   message ("Aborting commit due to empty commit message"), so the temp-file message is mandatory, not
   an option.
4. (extra) `diff --name-status -z` rename field order confirmed: `R<score>\0<old>\0<new>\0`.

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

- `CommitWindow`: layout 4 exactly as drawn in `doc/UI/mockup.html` — 430 px left column (repo header
  row, file list, message editor with 50-column marker, Commit / Commit & Push), diff placeholder right,
  splitter persisted (§7). Layout built in code, not a `.ui` file.
- `ChangedFilesModel` over the entries; check state re-derivation by path across refreshes (§5.1);
  tri-state check-all in the counter line; commit button enable state + count.
- Settings: qtutils `CSettings` (key vocabulary only) + `CPersistenceEnabler` for window geometry/state;
  splitter position alongside. Recent messages storage comes with the commit step.
- `main`: repo resolution via `rev-parse --show-toplevel`, error exit if not a repo.
- F5 refresh. Startup refresh.

**Checkpoint:** first real milestone — the window shows this repo's live state, ticks survive F5.

## Step 4 — row presentation

- Per-state colors, strikethrough deleted paths, folder icon, warning tint + no checkbox on blocked
  submodule rows, `new/path (was old/path)` rename rendering. All supplied as item data roles from the
  model; the planned `FileRowDelegate` turned out unnecessary.
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

- Normal-mode commit sequence (§4): untracked confirmation dialog (§5.3, qtutils `MessageBox::question`),
  `add`, `commit`, rollback on failure. Post-action refresh (§5.1). Ticked rows are the pathspec, verbatim.
- Message editor glue: recent-messages dropdown (per-repo, ~20) as a plain non-editable `QComboBox`
  (full message in item data, first line displayed; persisted via `CSettings`). `CHistoryComboBox` was
  rejected on reading it: an editable combobox whose own line edit is the input - the wrong base for a
  picker feeding a separate editor. Ctrl+Enter, disabled-state rules.
- Merge mode (§4): tracked changes forced on, untracked keep checkboxes, no-pathspec commit path.
- Failure dialog (§5.6): qtutils `MessageBox::notice` — fixed text above scrollable details — with stderr
  verbatim as the details. Used by everything after.

**Checkpoint:** real commits in a scratch repo: plain, with untracked, rejected by a hook (rollback observed),
merge commit.

## Step 7 — detached HEAD reattachment

- The §5.5 table: silent checkout cases, the ask-dialog for multiple candidates (`MessageBox::question`,
  one button per branch), refusals with explanation. Runs at commit time; the strip from step 4 already
  announces the state beforehand.

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
