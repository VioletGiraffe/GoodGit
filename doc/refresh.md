# Refresh

Driven by a change watch on each repository (`repositorywatcher`, over thin_io's `directory_watcher`), shared by
the windows on it. A reported change leaves a window's refresh pending (`RefreshScheduler`) until the window is active
and no write or push is in flight; it then runs once the changes settle: 300 ms after the last, 2 s after the
first at most. Also triggered by F5, the app's own state-changing actions, and Undo Last Commit before it
decides (`committing.md`). A window's first refresh waits until the watch is set up: a change made before that
could go unreported.

- **What is watched**: the working tree, every state directory in it or outside it (an absorbed submodule's, a
  linked worktree's, an hg share's source), and the directory of git's global excludes file. A window showing
  only history watches the state directories alone.
- **What a change means**: the backend classifies it as noise (`changefilter`: the object store, the reflogs,
  the caches, the lock files), a change to the file rows only, or one every view reads. The directories nothing
  reads go unwatched: those ignored as a whole, untracked nested repositories, the object stores.
  - hg cannot list the directories ignored as a whole (`status -i` walks into each and lists every file), and
    `.hgignore` is not parsed here: a build in an hg repository costs settled refreshes while its window is active.
- **Windows: the folders above an open repository cannot be renamed** while a window watches it. The watch's
  handle locks them; the repository's own folder can still be renamed or deleted.
- **A miss is the bug; a needless refresh is acceptable.** hg's own read-only commands rewrite `.hg/dirstate`
  (`hg.md`), so an hg window refreshes once or twice more after a stat-only change.
- **Only an active window refreshes.** A modal dialog is the active window, so no refresh lands inside a write
  flow's confirmation. One landing as the dialog closes fails the flow's `StateStamp` check (below).
- **Not while a write or push runs**: the pending refresh runs once neither is in flight. The writing window
  refreshes as its write ends, and every refresh first reads the changes already queued, so a write's own
  changes are not left pending.
- **An untrusted watch**: one that stopped (the root moved or deleted, the volume ejected), failed to be set up,
  or covers a volume whose notifications may miss changes. Its windows refresh on every activation, and the
  watch is set up again then: during an eject, a handle opened earlier would block it.

The list may be stale by design: **the checked rows are the commit pathspec, verbatim**, and a stale list
produces ordinary VCS errors through the normal failure path, not silent re-scans. A refresh showing the same
row again keeps its text up until replaced, at the same scroll position. Check state survives a refresh by
path; the state a newly listed row starts with is a setting. A
row that has just become Added is the exception: adding a file is a decision to commit it, so the row is
checked whatever it was before and whatever the setting says.

A confirmation dialog spins a nested event loop that a refresh can complete inside, and an operation or a
commit can land outside the app - from a second instance, or from a shell - with no refresh seeing it. Every
write decided before a dialog therefore captures a `StateStamp` (`Repository`) - the refresh generation
plus fresh probes of the on-disk operation markers and of the commit HEAD names - and re-checks it before
writing; a mismatch refuses the action and asks the user to retry against the new state.

`GitRepository` refreshes in two rounds of parallel queries, the second depending on the first's answers.
`HgRepository` uses a single round: one `hg status` answers
tracked changes, untracked files and rename sources together, alongside the unpushed set, each submodule's
pointer and dirtiness, and the conflicted paths where a mergestate exists.

Some queries the state cannot be established without (the status listing the branch header, the conflicts and
the untracked paths; the submodule list, the git directory, the tracked-changes diff). If one fails, the run is
discarded whole: the previous state and rows stay, a strip says why, and committing, discarding and deleting are
refused until a refresh succeeds. Reading rows is still allowed; a stale row is worth opening. **The distinction
is between a shorter answer and a wrong one**: a failure outside that set costs line counts or a tooltip, never
correctness, so those fail silently.
