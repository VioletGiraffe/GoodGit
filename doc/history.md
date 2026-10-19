# History

## History

Bounded rather than paged: one `log --max-count=N` builds the list (N is a setting) and "Load
more" re-runs it with N doubled. A cold open runs the walk twice, a small batch for an instant list and
then the full limit in the background. Load more and that background phase both extend the shown rows in
place, keeping the selection and scroll position: the shorter walk's result is verified to be a prefix of
the longer one's, with a plain reset as the fallback when the repository changed in between. A
file history is the same window with a path and `--follow`, so it traces renames; search, marks and panes
are shared. **Such a walk has no resumable cursor**: continuing from the last sha's ancestors drops every
commit on a parallel branch, and `--skip` re-walks the skipped commits anyway, which is what the re-run
costs. The alternative would be streaming one `log` process, which `vcsprocess` has no incremental-read path
for.

F5 reloads unconditionally. A return from another application (`refresh.md`) re-reads the unpushed marks and
reloads only if `Repository::historyFingerprint()` - the listed refs and the checkout - moved since the listing
was read: a reload re-walks the whole limit and re-centres the view.

The **branch selector** in the log bar picks whose history the walk covers (`LogQuery::Scope`):

- **Checked out**, the default: the working tree's commit and its upstream, with their ancestors, so the user's own
  line is at the top, incoming commits above it, and other branches' commits out of the way. The upstream is git's
  `@{upstream}`; Mercurial has none and takes the public heads of the checkout's named branch, which a pull moves
  without updating to them.
- **All branches**: every ref - git's `--branches --tags --remotes HEAD` (not `--all`, which adds `refs/stash` and
  `refs/notes`), Mercurial's plain `log` - so commits above the checkout, on other branches, and fetched but
  unmerged are listed.
- One branch: a local or remote-tracking branch, an hg bookmark or named branch, from `branchTips()`.

A **file history has no All branches**, and its Checked out leaves the upstream out: the rename tracing both systems
spell `--follow`/`-f` rewrites the one path it tracks at each rename it crosses, for the whole walk, so a second tip
in it would carry that path onto a line of history that never held it. The content search takes the scope of the
listing it filters. A reveal the scoped listing does not hold widens the scope to All branches once: a submodule's
recorded commit need not be on its checkout's line. A selected branch that is gone falls back to Checked out. The selection is per window, not stored.

The lane diagram (`commitgraph`) is computed from the parent links alone, so one implementation serves both
backends. It needs a **topologically ordered** listing, every commit above all its parents: git's walk
carries `--date-order`, and Mercurial's revision-descending order already is one. Both interleave parallel
branches by date, so they run side by side in the lanes; `--topo-order` would stack each branch as a block, the
checkout's possibly thousands of rows below another branch's. An unrelated head opens a
lane and a color of its own, having no child in the listing to inherit either from. The diagram is not
drawn for a file history or a content search, since neither listing holds the commits between the ones it
would join. A text search over the loaded records is the middle case: a chain is still one line of history,
so each node keeps its lane and consecutive shown rows on the same chain are joined, dashed where hidden
commits lie between them.

The **checked-out commit** carries a second ring outside its node, and its subject is bold and half a point
larger. Every line **above** it is drawn faded and those commits' subjects italic: history the checkout does
not hold yet. Which commits those are is decided by walking child links down to it, never by list position,
so an unrelated head above it stays solid. They keep their lane and color rather than moving into one of
their own: the checkout and the branch tip above it are one first-parent chain, and a lane of their own
would draw a branch point at a commit that has one child. The type carries both marks where the diagram is
hidden, as it is for a file history and a content search. The current commit's sha comes from a query of its
own: the shared `Repository` is refreshed only while a commit window is open on it.

Text search runs in memory over the loaded records (sha, revision, author, refs, date, message) and hides
non-matching rows, which is why a miss is reported against the loaded count rather than as "not found": the
commit may be older than the limit.

Searching diff *content* is the one thing the records cannot answer, so "Find in contents" re-runs the log.
It is one search reported at two strengths: `-G` lists every commit that changed a line containing the
text, and `-S`, run beside it, yields the shas where the number of occurrences changed (the text arriving
or leaving rather than being edited around), which are marked. `-S` results are a subset of `-G`'s except
inside binary files, which `-G` cannot see; those are counted and disclosed. **`-G` is always a regular
expression** (`--fixed-strings` does not apply to it), so the literal term is escaped at the boundary;
`hg grep --diff` takes the same flavour of pattern. hg answers both strengths from one `grep --diff` run,
which reports a record per matching line, and the log is then re-run over exactly those changesets.

Unpushed commits are marked in the accent color and drawn as a ring rather than a disc, from a `rev-list
--branches HEAD --not --remotes` run beside the log query - the listing's own refs, minus what a remote
already has. Reachability has to be asked: list position does not imply it, and on a diverged branch the
upstream ref is not in the list at all. With no remote-tracking ref every commit is unpushed and the whole
list would ring, so that case marks nothing instead. Mercurial answers from `draft()`, locally and for free,
and suppresses the same case on the configured push paths.

Selecting a commit shows its message body, where it has one, in a strip under the log, and queries its files,
their line counts and the commit's whole diff, a job each, so the rows may appear before their counts. Once the
files arrive, the file last shown for that commit is selected again, as after a refresh, or else the topmost
one. Selecting a file cuts its section out of the whole diff, held as a `ChangeSetDiff` so a block moved
between two files shows as one, and queries the file's own diff only where the whole one failed or passed its
cap. All are cancelled when the selection moves on. A merge shows a note instead of files: `git show` prints
no diff for one without `--cc`, so merges are detected from the parent count rather than from an empty result.

## Checkout

The window's one write. A commit row's **Check out** lists each way HEAD can be put on that commit:

- each local branch there, or hg bookmark, but the current one. One checked out in another worktree is shown disabled.
- git: each local branch whose upstream is there, behind it and not ahead, fast-forwarded to it, the current one
  included. The move is re-checked to be a fast-forward before it runs: the branch may have moved since the menu
  was built.
- a new local branch tracking each remote-tracking branch there, where that local name is free.
- the commit itself, except the current one: git detaches HEAD, hg updates with no bookmark active.

The branches come from `branchTips()`, run beside the walk. The submenu is disabled while any window holds the
write lock or an operation is in progress; the operation is read off the on-disk markers, so the window needs no
refreshed state. The checkout runs unconfirmed, under the window's own hold of the lock, and success announces a
history change.

Uncommitted changes the checkout leaves alone go along; one it would overwrite fails it before anything moves,
under hg too (`commands.update.check=noconflict`), and the refusal is the only dialog. Submodules move to the
commits the target records.

