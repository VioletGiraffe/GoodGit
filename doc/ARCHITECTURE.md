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

- Untracked files are `git add`ed as part of the commit, and the add is rolled back with `git reset` if the
  commit then fails - otherwise the visible Untracked/Added states would lie.
- "Untrack" (`git rm --cached`) cannot exist: a path-limited commit derives content from the working tree,
  so "tracked in HEAD, absent from index, present on disk" is inexpressible. Do not re-attempt; un-adding an
  `A` file is supported and is a different operation.
- Hunk/line staging is out, permanently - the commit primitive was chosen with that explicitly waived.
- A merge/cherry-pick/revert/rebase in progress is a separate mode, since git forbids path-limited commits
  then: the commit stages everything and runs with no pathspec.

## Components (app/src/)

| | |
|---|---|
| `gitprocess` | `Git::run()` - async `QProcess` jobs, at most 4 concurrent, cancellable, and scoped to a context `QObject` - once it dies the callback is skipped and a still-queued job is discarded unstarted. `Git::runSync()` - blocking variant for the one startup moment before the event loop exists. Both apply the invariants below |
| `gitparsers` | Free-function parsers for the git outputs the app consumes. UI- and process-free deliberately: exercisable directly |
| `repository` | `Repository` - one repo: state, file entries, refresh, and every git action. The unit of the application. Its read-only queries take the object that will show the answer as the job's context, so a query dies with its view rather than with the repo |
| `changedfilesmodel` | Checkable table over the entries; row styling comes from the theme as item data roles |
| `filelistdelegate` | Paints the two file-list details item roles cannot express: the recolored deleted strikethrough and the selected-row accent stripe |
| `theme` | The whole visual style in one place, mirroring the mockup's CSS variables. Applied once at startup; light or dark follows the system theme |
| `commitwindow` | One window = one repository. Owns a `Repository` and all user flows. Submodule rows open another `CommitWindow` on the submodule, same process; the child's `committed()` signal refreshes the parent |
| `historywindow`, `historymodels` | The commit history, read-only: log above, the selected commit's file list beside that file's diff. The same window narrowed to one path is a file history. Owns its own `Repository`, so a submodule's history opens without a `CommitWindow` on that submodule |
| `diffhighlighter`, `messageedit` | Prefix-driven unified-diff highlighting; message editor with the 50-column subject guide and word completion fed by one `diff -U0 HEAD` per refresh |
| `settings` | Key vocabulary over qtutils `CSettings`. Window geometry is per-repo via qtutils `CPersistenceEnabler` |

## Git invocation invariants

Applied by `gitprocess` to every call: `-c core.quotepath=false`, `GIT_TERMINAL_PROMPT=0` (a credential miss
fails fast instead of hanging on an invisible prompt; Git Credential Manager's own GUI is unaffected),
`--no-optional-locks` on read-only queries. Paths travel NUL-separated (`-z`, `--pathspec-from-file=-
--pathspec-file-nul` via stdin - never argv, which Windows caps near 32 KB). The commit message goes through
a temp file, never `-m` and never stdin: `-F -` and a stdin pathspec cannot share the pipe.

The diff shown in either window carries `--ignore-cr-at-eol`, so a CRLF/LF-only change reads as no change.
That is a display choice and nothing more: the file still lists as modified and still commits its
working-tree content byte for byte.

Output is harvested once, on process exit - no streaming, though QProcess drains the pipes as data arrives,
so a chatty child cannot deadlock. Live progress display would need a `readyRead` path in `gitprocess`.

## Refresh

Triggered by startup, F5, and the app's own state-changing actions - **never** by window activation, and
there is no file watcher. The list may therefore be stale; that is by design. **The ticked rows are the
commit pathspec, verbatim** - the window commits what it shows, and a stale list produces ordinary git
errors through the normal failure path, not silent re-scans.

`Repository::refresh()` runs two phases of parallel queries - the base queries, then the ones their results
call for - and coalesces re-entry. Check state survives a refresh, re-derived by path: persisting rows keep
their state, new rows default to checked unless untracked.

## History

Read-only, and bounded rather than paged: one `log --max-count=N` builds the whole list, and "Load more"
re-runs it with N doubled. A file history is that same window with a path appended to the query and
`--follow` set, so it traces the file across renames; everything else - search, marks, panes - is shared. **A date-ordered walk has no resumable cursor** - continuing from the last
sha's ancestors drops every commit that sits on a parallel branch, since those are ancestors of HEAD but
not of that sha. `--skip` avoids that bug but re-walks the skipped commits anyway, which is what the
re-run costs. The only alternative is streaming one `log` process, which needs the `readyRead` path
`gitprocess` does not have.

Search runs entirely in memory, over the records already loaded - sha, author, refs, date and message
are all held there, so no git process is involved and non-matching rows are simply hidden. This is why
a miss is reported against the loaded count rather than as "not found": the commit may just be older
than the limit. Searching diff *content* is the one thing the records cannot answer; that needs the
pickaxe (`log -S`), a query rather than a filter.

Commits the upstream has not seen are marked in the accent color, from a `rev-list @{upstream}..HEAD`
run beside the log query. Reachability has to be asked of git: position in a date-ordered list does not
imply it, and on a diverged branch the upstream ref is not in the list at all. The query failing means
there is nothing to compare against - no upstream, or a detached HEAD - and marks nothing.

Selecting a commit shows its message and queries its files; selecting a file queries that file's diff -
one short-lived job each, cancelled when the selection moves on. The diff highlighting is switched off
for message text, where a leading `-` is a bullet rather than a deletion. A merge is shown as a note instead: `git show` prints no
diff for one without `--cc`, so the emptiness is answered from the parent count rather than guessed from
an empty result.

## Submodules

A submodule is just another `Repository` shown in another window. In the parent's list a submodule row
exists when the pointer moved or tracked files inside are modified; modified tracked content inside blocks
committing the pointer, untracked content inside neither blocks nor earns a row by itself. The parent's diff
query uses `--ignore-submodules=dirty` so a merely-dirty submodule does not masquerade as a committable
pointer change.

Push is `git push --recurse-submodules=on-demand`, passed explicitly (machine config varies): git pushes
referenced submodule commits first, which is a correctness requirement, not a preference - a superproject
commit referencing an unpushed submodule commit is unfetchable.

## Fetching

"Peek" is the only thing in the app that fetches, and so the only thing that moves a remote-tracking ref:
it runs `git fetch`, refreshes (the header's ahead/behind counts are derived from that ref), and pops up
`log HEAD..@{upstream}`. Without the fetch that list would be empty almost always, nothing else advancing
the ref. It is disabled when the branch has no upstream, there being no ref for the walk to name. Nothing
pulls or merges.

## Detached HEAD

Reattachment happens at commit time and is allowed only when the working tree would not move: a single local
branch tip at HEAD is checked out silently, several are offered as a choice, and a remote-tracking tip at
HEAD whose local name is free becomes a tracking branch. Everything else refuses with an explanation. The
header strip announces the applicable case before the user commits. Same code path for the main repo and
submodules.

## Failure reporting

Any failed git command surfaces its own stderr verbatim (qtutils `MessageBox::notice`, scrollable details) -
hook output is the only thing that makes a rejected commit diagnosable. Push failures special-case "no
upstream" (offers `--set-upstream origin HEAD`); non-fast-forward is reported plainly, with no offer to
pull or force.

Delete goes to the OS trash (`QFile::moveToTrash`) and never falls back to permanent deletion. The external
difftool is launched detached, bypassing the job queue: it blocks git until the tool closes, and a queue
slot held for minutes would starve refreshes.

## Build

qmake subdirs: `app` plus the first-party submodules `cpputils`, `cpp-template-utils`, `qtutils` (static
libs). Compiler configuration lives in `global.pri`, included by both the app and qtutils.

**Submodule policy:** all submodules are first-party. When a helper almost fits, extend it in the library,
in library shape - ask before changing existing behavior, other projects share them. Each such change is a
commit in the submodule plus a pointer bump here.
