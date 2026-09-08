# History

## History

Read-only, bounded rather than paged: one `log --max-count=N` builds the list (N is a setting) and "Load
more" re-runs it with N doubled. A cold open runs the walk twice, a small batch for an instant list and
then the full limit in the background. Load more and that background phase both extend the shown rows in
place, keeping the selection and scroll position: the shorter walk's result is verified to be a prefix of
the longer one's, with a plain reset as the fallback when the repository changed in between. A
file history is the same window with a path and `--follow`, so it traces renames; search, marks and panes
are shared. **Such a walk has no resumable cursor**: continuing from the last sha's ancestors drops every
commit on a parallel branch, and `--skip` re-walks the skipped commits anyway, which is what the re-run
costs. The alternative would be streaming one `log` process, which `vcsprocess` has no incremental-read path
for.

The repo-wide walk covers **every ref** - git's `--branches --tags --remotes HEAD` (not `--all`, which adds
`refs/stash` and `refs/notes`), Mercurial's plain `log` - so commits above the checkout, on other branches,
and fetched but unmerged are listed. A **file history is the exception, rooted at the checkout**: the rename
tracing both systems spell `--follow`/`-f` rewrites the one path it tracks at each rename it crosses, for
the whole walk, so a second tip in it would carry that path onto a line of history that never held it. The
content search takes the scope of the listing it filters.

The lane diagram (`commitgraph`) is computed from the parent links alone, so one implementation serves both
backends. It needs a **topologically ordered** listing, every commit above all its parents: git's walk
carries `--topo-order`, and Mercurial's revision-descending order already is one. An unrelated head opens a
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
own, the window owning a `Repository` it never refreshes.

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

Selecting a commit shows its message and queries its files, their line counts and the commit's whole diff, a
job each, so the rows may appear before their counts; selecting a file cuts its section out of the whole
diff, held as a `ChangeSetDiff` so a block moved between two files shows as one, and queries the file's own
diff only where the whole one failed or passed its cap. All are cancelled when the selection moves on. A merge shows a note instead of files: `git show` prints no diff for one without `--cc`,
so merges are detected from the parent count rather than from an empty result.

