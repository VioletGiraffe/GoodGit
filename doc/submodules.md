# Submodules and push

## Submodules

A submodule is another `Repository` shown in another window. In the parent's list a submodule row exists
when the pointer moved or tracked files inside are modified. Modified tracked content inside blocks
committing the pointer; untracked content neither blocks nor earns a row by itself. A status query that fails
inside a submodule counts as dirty, since it may be; a submodule never initialized is not that case, its
directory being empty. The parent's diff uses `--ignore-submodules=dirty` so a merely dirty submodule does
not masquerade as a committable pointer change. Discarding a submodule row checks the recorded commit out
inside it, leaving a detached HEAD; the same dirtiness that blocks committing the pointer blocks discarding
it, because that checkout would overwrite the changes inside.

Those changes are discarded by the same Discard action: on a lone submodule row it runs the discard inside
the submodule instead of checking its pointer out, so its branch is not moved. `submoduleDiscardPlan()`
reads what that would do and refuses what it must not touch: an operation in progress, unresolved conflicts,
a nested submodule that moved or holds uncommitted changes of its own. It blocks, the plan being what the
confirmation lists. The plan's paths are also the pathspec the discard runs with - a whole-tree pathspec
would check out over every nested submodule it covers, detaching one that has nothing to do with the
discard. Paths the submodule's last commit does not have (files added there, a rename's new name) come out
of version control and stay on disk, and untracked files are untouched, as in the repository's own discard.

Mercurial's subrepositories are the same rows, read from `.hgsub` and `.hgsubstate`. A submodule may be a
git repository inside an hg parent, which is why `submoduleLocation()` answers with a kind and why
per-submodule refresh queries go to whichever tool owns the directory. What is uncommitted inside a
submodule is asked of the submodule itself, never of the parent's recursing `hg status`: that compares
against the node `.hgsubstate` records rather than the submodule's own parent changeset, and so reports a
committed pointer move as modified files inside. One level down, the query does recurse (`-S`): a nested
submodule the enclosing one has not recorded yet is uncommitted work there.

History shows submodule rows too. Git's file listing reads them from the modes (`--raw` rather than
`--name-status`). Mercurial's names only `.hgsubstate`, so that row is replaced with one row per submodule
from that file's own diff, which also serves as such a row's diff. Such a row carries the commit its pointer
names, and activating it opens the submodule's history there: a selection within the ordinary walk, so the
surrounding history stays visible. The all-ref walk holds the recorded commit wherever the submodule's own
checkout stands; one reachable from no ref, or older than the limit, is named in the count label and the
newest row is selected.

## Push

A superproject commit referencing an unpublished submodule commit is unfetchable, so those submodules must
be pushed first. Git's `--recurse-submodules=on-demand` does that only where every submodule is on a branch
named as the superproject's: it resolves the superproject's refspec and demands the same ref inside each
submodule, failing outright on a name mismatch.

So the plan is the app's. `planPush` walks the gitlinks in HEAD, recursing into each whose recorded commit
no remote reaches (git's own on-demand test, `rev-list -n 1 <sha> --not --remotes`), and returns one step
per submodule needing a push, innermost first, with the repository's own step last. Each step is a plain
push in its own working directory, resolving its own upstream, so no name has to match. Steps run in order
and the first failure ends the push. The superproject's step keeps `--recurse-submodules=on-demand` as a
backstop; a submodule's step carries `--recurse-submodules=no`, since letting git recurse there would
reintroduce the mismatch. Both are explicit because `submodule.recurse` varies by machine. A submodule that
needs pushing but cannot be (not on a branch, or on a branch that does not contain its recorded commit) is
refused before any step runs.

`hg push -r .` recurses into submodules itself, so Mercurial's plan is one step.

