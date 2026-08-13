# GoodGit architecture

A git commit GUI: check files, write a message, commit, push. Qt Widgets over the `git` CLI - every
operation is a `git` subprocess; no libgit2, no reimplemented git logic. The window layout is drawn in
`doc/UI/mockup.html`.

## The model: no index, no staging

The user-facing model has no concept of staging. The file list is the **HEAD-to-working-tree delta**
(`git diff --name-status HEAD` plus `git ls-files --others`), and committing is **path-limited**
(`git commit -- <checked paths>`): git builds the commit from HEAD's tree with the checked paths replaced by
their working-tree content, leaving the index untouched. The list and the commit are definitionally the same
delta, and anything staged by other tools is never clobbered.

Consequences that follow from this choice, all deliberate:

- Untracked files are `git add`ed as part of the commit (behind a confirmation dialog), and the add is rolled
  back with `git reset` if the commit then fails - otherwise the visible Untracked/Added states would lie.
- "Untrack" (`git rm --cached`) cannot exist: a path-limited commit derives content from the working tree,
  so "tracked in HEAD, absent from index, present on disk" is inexpressible. Do not re-attempt; un-adding an
  `A` file is supported and is a different operation.
- Hunk/line staging is out, permanently - the commit primitive was chosen with that explicitly waived.
- A merge/cherry-pick/revert/rebase in progress is a separate mode (git forbids path-limited commits then):
  tracked rows are forced on, untracked keep their checkboxes, and the commit stages everything and runs
  with no pathspec.

## Components (app/src/)

| | |
|---|---|
| `gitprocess` | `Git::run()` - async `QProcess` jobs, queued at most 4 concurrent, cancellable, callback skipped if the context object dies. `Git::runSync()` - blocking variant for before the event loop exists (startup repo-root resolution only). Both apply the invariants below |
| `gitparsers` | Free-function parsers for the porcelain v2 branch header, `--name-status -z`, NUL lists, `submodule status`, porcelain v1 dirtiness. UI- and process-free deliberately: exercisable directly |
| `repository` | `Repository` - one repo: `RepoState`, `FileEntry` list, refresh, and every git action (commit, push, add/un-add, checkout, diff providers). The unit of the application |
| `changedfilesmodel` | Checkable table over the entries. Row styling (state colors, fonts, submodule folder icon, blocked-row tint) is item data roles from the theme |
| `filelistdelegate` | Paints the two file-list details roles cannot express: the deleted strikethrough recolored (a QFont strike always draws in the text color) and the selected-row accent stripe |
| `theme` | The whole visual style in one place, mirroring the mockup's CSS variables: light + dark palettes, QPalette setup, and the generated app-wide stylesheet. Applied once at startup; the light/dark choice follows the system theme |
| `commitwindow` | One window = one repository. Owns a `Repository` and all user flows. Submodule rows open another `CommitWindow` on the submodule, same process; the child's `committed()` signal refreshes the parent |
| `diffhighlighter`, `messageedit` | Prefix-driven unified-diff highlighting; message editor with the 50-column subject guide and word completion (changed file names + identifier-shaped words from one `diff -U0 HEAD` per refresh; Tab accepts, Enter always stays a newline, Ctrl+Space forces the popup) |
| `settings` | Key vocabulary over qtutils `CSettings`. Window geometry is per-repo via qtutils `CPersistenceEnabler` |

## Git invocation invariants

Applied by `gitprocess` to every call: `-c core.quotepath=false`, `GIT_TERMINAL_PROMPT=0` (a credential miss
fails fast instead of hanging on an invisible prompt; Git Credential Manager's own GUI is unaffected),
`--no-optional-locks` on read-only queries. Paths travel NUL-separated (`-z`, `--pathspec-from-file=-
--pathspec-file-nul` via stdin - never argv, which Windows caps near 32 KB). The commit message goes through
a temp file, never `-m` and never stdin: `-F -` and a stdin pathspec cannot share the pipe.

Output is harvested once, on process exit - no streaming. QProcess drains the OS pipes as data arrives, so a
chatty child cannot deadlock; live progress display would need a `readyRead` path added to `gitprocess`.

## Refresh

Triggered by startup, F5, and the app's own state-changing actions - **never** by window activation, and
there is no file watcher. The list may therefore be stale; that is by design. **The ticked rows are the
commit pathspec, verbatim** - the window commits what it shows, and a stale list produces ordinary git
errors through the normal failure path, not silent re-scans.

`Repository::refresh()` runs two phases of parallel queries: base queries (branch header, name-status diff,
untracked list, submodule enumeration), then the queries the results call for (per-submodule dirtiness,
unborn-HEAD fallback, detached-HEAD branch tips, unpushed-commit subjects). Re-entry is coalesced.

Check state survives refresh, re-derived by path: persisting rows keep their state; new rows default the
same way on every refresh - checked unless untracked.

## Submodules

A submodule is just another `Repository` shown in another window. In the parent's list, submodule rows exist
in three conditions: pointer moved + clean inside (committable), pointer moved + modified tracked files
inside (blocked - warning tint, no checkbox), pointer unmoved + modified tracked files inside (not
committable; the row exists to be seen and double-clicked). Untracked content inside a submodule never
blocks and never creates a row by itself. The parent's diff query uses `--ignore-submodules=dirty` so a
merely-dirty submodule does not masquerade as a committable pointer change.

Push is `git push --recurse-submodules=on-demand`, passed explicitly (machine config varies): git pushes
referenced submodule commits first, which is a correctness requirement, not a preference - a superproject
commit referencing an unpushed submodule commit is unfetchable.

## Detached HEAD

Reattachment happens at commit time and is allowed only when the working tree would not move: exactly one
local branch tip == HEAD (checkout, silently), several (ask), none but a remote-tracking tip == HEAD and the
local name is free (create tracking branch, silently). Everything else refuses with an explanation. The
header strip announces the applicable case before the user commits. Same code path for the main repo and
submodules.

## Failure reporting

Any failed git command surfaces its own stderr verbatim (qtutils `MessageBox::notice`, scrollable details) -
hook output is the only thing that makes a rejected commit diagnosable. Push failures special-case "no
upstream" (offers `--set-upstream origin HEAD`); non-fast-forward is reported plainly, with no offer to
pull or force.

Push additionally logs its whole report, success or failure, to a pane under the diff view, hidden until
the first push of the session. Entries accumulate; each is written when the process exits, not as it runs
(see the streaming note above).

Delete goes to the OS trash (`QFile::moveToTrash`), never falls back to permanent deletion, and un-adds `A`
rows first so the index never points at a vanished file. The external difftool is launched via
`QProcess::startDetached`, bypassing the job queue: difftool blocks git until the tool closes, and a queue
slot held for minutes would starve refreshes (cost: no error dialog if no difftool is configured).

## Build

qmake subdirs: `app` plus the first-party submodules `cpputils`, `cpp-template-utils`, `qtutils` (static
libs). Compiler configuration lives in `global.pri`, included by both the app and qtutils.

**Submodule policy:** all submodules are first-party. When a helper almost fits, extend it in the library,
in library shape - ask before changing existing behavior, other projects share them. Each such change is a
commit in the submodule plus a pointer bump here.
