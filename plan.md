# GoodGit v1 — implementation plan

A commit GUI and nothing else: choose whole files, write a message, commit, push. Cross-platform Qt Widgets
over the `git` CLI; no libgit2, no reimplemented git logic.

This file is the build plan for v1. Once the code exists it is superseded by an architecture document; it is
not meant to be maintained alongside the implementation.

---

## 1. Settled decisions

The design questions that were resolved during discussion, kept here because the rationale is not recoverable
from the code.

| | Decision | Why |
|---|---|---|
| A1 | Commit with a **path-limited commit**, never by reconciling the index | `git commit -- <paths>` builds the commit from HEAD's tree plus the *working tree* content of the named paths, leaving the index untouched. Exactly matches "commit the current state of these files", and never clobbers someone's staged work |
| B1 | Merge/rebase in progress is a **separate mode** | git refuses a partial commit mid-merge, so the window switches to all-tracked-changes-forced-on |
| C | **No amend** in v1 | — |
| D | Layout 4 — list top-left, message and primary button beneath it, diff full height on the right | Diff height beats message width; see §7 |
| E | **One window, one repository** | Submodules are rows that open their own window. Kills the repo-tree model entirely: no grouping, no cross-repo orchestration, no recursion |
| F2 | Push is `git push --recurse-submodules=on-demand`, flag passed explicitly | Not every machine has `push.recurseSubmodules` configured. Git orders submodule pushes before the superproject itself, which is a correctness requirement — a pushed superproject commit referencing an unpushed submodule commit is unfetchable |
| G/J | Detached HEAD: **reattach only if the working tree does not move** | See §5.5 |
| H | **Untracked and Added are distinct visible states** | Only Untracked needs `git add` and the confirmation prompt |
| K | A submodule pointer commit is blocked by **modified tracked files** inside, not by untracked content | Untracked content in a submodule is usually build output; it cannot affect the gitlink and cannot be lost |
| N1 | **No "untrack"** (`git rm --cached`) | Structurally inexpressible under A1: a path-limited commit derives everything from the working tree, so "tracked in HEAD, absent from the index, present on disk" can never be committed. Un-adding an `A` row is a different operation and is supported |

**Never in this project:** hunk or line staging. A1 is chosen partly because that will never be needed.

---

## 2. The model has no index in it

The file list is the **HEAD-to-working-tree delta**, which is by definition the set of changes a path-limited
commit will produce. Two queries, no `git status` parsing, no collapsing of index/worktree state pairs:

1. `git diff --name-status -M --ignore-submodules=dirty -z HEAD` — tracked changes
2. `git ls-files --others --exclude-standard -z` — untracked files

Change types, from the `--name-status` letters:

| Letter | State shown | Notes |
|---|---|---|
| `M` | Modified | also mode-only changes |
| `A` | Added | in the index, not in HEAD — reachable from the CLI or from our own "Add to index" |
| `D` | Deleted | |
| `R` | Renamed | one row, two paths; the commit pathspec **must carry both** or the old name's deletion is not recorded |
| `T` | Type changed | file ↔ symlink ↔ submodule |
| `U` | Conflicted | merge mode only |
| — | Untracked | from query 2, not a diff letter |

Copy detection (`-C`, letter `C`) stays off: expensive and near-useless here.

Unborn HEAD (a repo with no commits) fails query 1; fall back to listing everything as new.

---

## 3. Git invocation invariants

Applied to every call, in `GitProcess`:

- `-c core.quotepath=false`, `-z` wherever paths appear, output treated as raw UTF-8 bytes. Path mangling is
  the classic failure mode of this kind of tool.
- `--no-optional-locks` on read-only queries, so a concurrent IDE or build does not fight us for `index.lock`.
- `GIT_TERMINAL_PROMPT=0`, so a credential miss fails fast with a readable error instead of hanging forever on
  a terminal prompt that has no terminal. Git Credential Manager shows its own window and is unaffected.
- Commit message via file, never `-m`: quoting and encoding on a Windows command line are not worth fighting.
- Pathspecs via `--pathspec-from-file=<file> --pathspec-file-nul`, never argv. Windows caps the command line
  near 32 KB; a thousand-file commit would otherwise simply fail.
- `QProcess` used **asynchronously**, everything on the GUI thread. No worker threads, no locking. `status` and
  `diff` are slow enough on a large repo to hang a synchronous call visibly, and `push` is unbounded.

**Stdin collision.** `git commit -F -` and `--pathspec-from-file=-` both want stdin. Resolve it by writing the
message to a `QTemporaryFile` (UTF-8, no BOM) and passing `-F <tmpfile>`, leaving stdin free for the
NUL-separated pathspec.

**Verify at first use:** that `git commit` accepts `--pathspec-from-file` (it exists on `git add`; commit
gained it around 2.25). If it does not, fall back to a pathspec temp file for both.

---

## 4. Command inventory

### Refresh (per repository)

| Purpose | Command |
|---|---|
| Header state | `status --porcelain=v2 --branch --untracked-files=no -z` → `branch.head`, `branch.upstream`, `branch.ab` |
| Tracked changes | `diff --name-status -M --ignore-submodules=dirty -z HEAD` |
| Untracked files | `ls-files --others --exclude-standard -z` |
| Direct submodules | `submodule status` (not `--recursive`; the child window handles its own) |
| Submodule dirtiness | per initialised submodule: `-C <sub> status --porcelain -z` — any entry not prefixed `??` blocks |
| Git dir | `rev-parse --git-dir`, once per repository |

`status` is used **only** for the header. The file list never touches it.

`--ignore-submodules=dirty` on the diff makes a submodule appear only when its recorded commit actually
differs. Without it, a submodule with uncommitted junk inside shows as a modified row that cannot be
committed — ticking it would produce a confusing no-op.

Cap concurrent git processes at 4. Irrelevant for three submodules; a repo with fifty would otherwise spawn a
process storm on every window activation.

### In-progress operation detection

`rev-parse --git-dir` first, then filesystem checks for `MERGE_HEAD`, `CHERRY_PICK_HEAD`, `REVERT_HEAD`,
`rebase-merge/`, `rebase-apply/`.

**In a submodule `.git` is a file, not a directory.** Testing `<repo>/.git/MERGE_HEAD` directly would silently
never fire, and submodule windows are a first-class case here.

### Diff

| Row kind | Command |
|---|---|
| Tracked | `diff HEAD -- <path>` — the `HEAD` matters: a bare `git diff` shows index→worktree, so a fully staged file would render as an empty diff while still being committed |
| Renamed | as above, both paths |
| Untracked | `diff --no-index -- /dev/null <path>`; verify on Git for Windows, else read the file and synthesise an all-`+` view |
| Submodule | `-C <sub> log --oneline --no-decorate <old>..<new>`, where `<old>` is `rev-parse HEAD:<path>` in the parent and `<new>` is the submodule's HEAD |

Guard on output size with a "diff too large" placeholder. Binary files pass git's own message through. Diff
requests are speculative: kill the in-flight process when the selection changes rather than tracking stale
results.

### Commit

Normal mode:

1. If any ticked row is Untracked → confirmation dialog listing them (§6). On cancel, stop.
2. `add --pathspec-from-file=<f> --pathspec-file-nul` for those paths only.
3. `commit -F <msgfile> --pathspec-from-file=- --pathspec-file-nul` with every ticked path on stdin.
4. On failure, `reset -q --pathspec-from-file=<f> --pathspec-file-nul` to undo step 2.

Merge mode (B1): all *tracked* changes are forced on; untracked files keep their checkboxes, so build output
is not swept into the merge commit. `add -u`, then `add` for ticked untracked paths, then `commit -F <msgfile>`
with no pathspec — the index already holds the merge result, and staging conflicted files is what marks them
resolved.

### Other actions

| Action | Command | Enabled on |
|---|---|---|
| Add to index | `add -- <paths>` | Untracked rows only. On a mixed selection it applies to those and no-ops on the rest |
| Un-add | `reset -q -- <paths>` | `A` rows only. Same command as the commit rollback |
| Delete | `QFile::moveToTrash` | see §5.4 |
| Push | `push --recurse-submodules=on-demand` | |
| Push, no upstream | offer `push --set-upstream origin HEAD` after the first failure | |

---

## 5. Behaviour

### 5.1 Refresh

On startup, on F5, and after any action that changes repository state: commit, push, add to index, un-add,
delete, and the branch checkout performed during reattachment.

**Not on window activation, and no file watcher.** A watcher is unreliable and expensive on large repos.
Activation would spawn 2 + N git processes every time focus returned from the editor, and would move rows
under the cursor at exactly the moment the user is about to click one. The list stays still unless the user or
the application changes something.

A submodule window's commit changes the parent's gitlink. Since both windows live in the same process, the
child signals the parent directly rather than the parent discovering it by polling.

**The ticked rows are the pathspec, verbatim.** The window commits what it is showing; it does not re-scan
first to catch changes made behind its back. Keeping the list current is the user's call, which is what F5 is
for. A list that has gone stale produces ordinary git errors — a ticked path with nothing left to commit, or a
vanished untracked file failing `add` — and those surface through the normal failure path.

What is *not* under the window's control: a path-limited commit always takes the working tree content at the
moment it runs, so a file edited after its diff was displayed is committed in its newer form. The diff pane
shows a file, never a snapshot of what will be committed.

Check state across a refresh is re-derived by path:

- rows that persist keep their check state;
- rows that vanished are dropped;
- newly appeared rows default to checked **only on the initial load**. On later refreshes they arrive
  unchecked, so a refresh triggered by an unrelated action cannot quietly add to a selection the user has
  already built.

Coalesce: if a refresh is in flight, mark one pending rather than queueing several.

### 5.2 Submodule rows

Three conditions, all carrying the folder icon:

| Pointer | Inside | Row |
|---|---|---|
| moved | clean | committable, checkbox live |
| moved | modified tracked files | **blocked** (E) — pointer change is real but you have unfinished work in there |
| unmoved | modified tracked files | **not committable** — the row exists only so the state is visible and double-clickable |

A clean submodule with an unmoved pointer is not shown. Blocked rows get the state-column wording *and* a
warning-tinted background (R); text alone is too easy to skim past.

Double-click opens another `CommitWindow` on the submodule **in the same process** — settings, recent-message
storage and refresh scheduling stay consistent, and it is no more code than spawning ourselves.

### 5.3 Untracked confirmation

Fires only when at least one ticked row is Untracked. Lists the paths, no "don't ask again" (that would defeat
its purpose). `A` rows never appear: their tracking was already explicit.

The rollback in step 4 of the commit keeps this honest — without it a rejected commit would leave rows visibly
flipped from Untracked to Added, having been told the commit did not happen.

### 5.4 Delete

`QFile::moveToTrash`, from the Del key and the context menu, on the whole selection.

| Row | Behaviour |
|---|---|
| Untracked | trashed with no prompt; row disappears |
| Tracked | prompt, then trashed; row becomes Deleted automatically and committing it records the removal |
| Added (`A`) | `reset -q -- <path>` **first**, then trash — otherwise the index holds an added file that no longer exists on disk |
| Deleted | disabled |
| Submodule | **not offered**; removing a submodule is a procedure, not a file deletion |

If `moveToTrash` fails — locked file, or a volume with no recycle bin such as a network share — report and
stop. Never fall back to a permanent delete.

### 5.5 Detached HEAD

Applies to any repository, not just submodules; the main repo is detached after checking out a tag. The rule
that makes automatic reattachment safe: **only if the working tree does not move.** Any candidate whose tip is
not exactly HEAD is disqualified.

| Situation | Action |
|---|---|
| Exactly one local branch tip == HEAD | check it out silently, commit proceeds |
| Several local branches at HEAD | **ask**; never guess |
| No local branch, HEAD == a remote-tracking tip, local name free | create the tracking branch, check out, proceed silently |
| Same, but the local name is taken by another commit | refuse — taking the name would move the working tree |
| No branch anywhere at HEAD | **refuse and explain**. This is the normal state of a pinned submodule, not an edge case |

Queries: `for-each-ref --points-at HEAD refs/heads` and `refs/remotes`, plus `show-ref --verify --quiet
refs/heads/<name>` for availability. Computed only when the header says `(detached)`.

The state is shown in the header **before** you press anything, not discovered at commit time.

### 5.6 Failure paths

| Failure | Response |
|---|---|
| Any git command fails | dialog with the command's own stderr, verbatim |
| `pre-commit` / `commit-msg` hook rejects | same, plus the rollback in §4. Hook output is the only thing that makes a rejected commit diagnosable |
| Push rejected non-fast-forward | report plainly. **No** offer to pull or force — that is a different application |
| Push with no upstream | offer the `--set-upstream` variant |
| Commit during merge with a pathspec | cannot happen; merge mode takes the other branch |

---

## 6. Class breakdown

| Type | Responsibility |
|---|---|
| `GitProcess` | Async `QProcess` wrapper. Args, cwd, optional stdin payload, completion callback with `GitResult`. Owns the invariants from §3 |
| `GitResult` | `{ int exitCode; QByteArray out, err; bool ok; }` |
| `Repository` | One repo: path, git dir, `RepoState` (branch, upstream, ahead/behind, in-progress op, detached info) and `std::vector<FileEntry>`. Issues the queries, performs the actions, emits `refreshed()` |
| `FileEntry` | `{ path, oldPath, ChangeType, bool untracked, SubmoduleInfo }` |
| `ChangedFilesModel` | `QAbstractTableModel` over the entries: check state, columns check / state / path |
| `FileRowDelegate` | `QStyledItemDelegate`: per-state colour, strikethrough on deleted paths, folder icon, warning tint on blocked submodule rows |
| `DiffView` | `QPlainTextEdit` + `DiffHighlighter : QSyntaxHighlighter`, prefix-driven |
| `CommitWindow` | `.ui` + class. Orchestration; owns a `Repository`; opens submodule windows |
| `Settings` | Key vocabulary over qtutils `CSettings`: splitter state, recent messages per repo, git executable path. Window geometry via qtutils `CPersistenceEnabler` |
| `main` | Arg parsing, repo resolution via `rev-parse --show-toplevel`, window creation |

From qtutils, besides the above: `MessageBox::notice` is the git-failure dialog (stderr verbatim as the
scrollable details), `MessageBox::question` the confirmation/ask dialogs, `CHistoryComboBox` the
recent-messages dropdown (extended to multi-line items showing their first line), `CLabelMidElision` for
path labels.

`FileRowDelegate` is needed in v1 regardless of any later styling pass — the state colours, the strikethrough,
the folder icon and the blocked-row tint all require it.

---

## 7. The window (layout 4)

Drawn in `doc/UI/mockup.html`.

Left column, fixed 430 px: repo header row, file list, then the message editor and the primary button beneath
it. Right side: diff, full height. Splitter position persisted.

- **50-column subject marker, not 72.** 430 px is what the marker sets: the editor holds just over 50
  monospace characters, so the subject convention fits and the body-wrap convention does not. The subject one
  is the one that matters.
- Secondary actions (Push, Refresh) go in the repo header row at the top of the left column, which puts Push
  next to the ahead-count that justifies it, rather than stacking three buttons under the primary.
- Loud strips, not quiet labels, for merge mode and detached HEAD — the window behaves differently in both.
- Window title `<folder> [<branch>] - GoodGit`, so several submodule windows are distinguishable in the taskbar.
- Renames render as `new/path (was old/path)` in the single path column (Q).
- Check-all is a tri-state checkbox in the "N of M checked" line above the list.
- Recent-messages dropdown, last ~20 per repo in `QSettings` (P).
- Commit disabled with zero checked or an empty message; the count lives on the button.
- Keyboard: Space toggles the selection, Del trashes, F5 refreshes, Ctrl+Enter commits, Enter and double-click
  open the external difftool.
- Context menu: Add to index / Un-add (each greyed rather than hidden, so the menu never changes shape), Open,
  Show in Explorer, Copy path, Delete.

---

## 8. Build configuration

`app/app.pro` is currently a non-Qt console template and needs:

- `QT = core gui widgets`, dropping `CONFIG -= qt` and `CONFIG += console`
- `TARGET = GoodGit`
- qtutils wired into the build: root `SUBDIRS` + `app.depends`, `-lqtutils`
- `README.md` still describes the project template

All submodules are first-party: when a helper almost fits, extend it in the library (ask before changing
existing behavior — other projects share them) rather than working around it here.

---

## 9. Out of scope for v1

Hunk and line staging (never), amend, untrack, log and history browsing, branch operations, pull and fetch,
conflict resolution UI, shell extension, side-by-side diff.

Side-by-side is worth one note because it constrains the layout choice: it needs roughly double the diff
width, and layout 4's diff is wide enough to absorb that later.
