# Committing

## The model: no staging

The user never sees an index. The file list is the delta from HEAD to the working tree (tracked changes
plus untracked files), and the checked rows are what the next commit contains. The row is the unit: a
checked row commits the whole file, there is no hunk or line staging.

For git, the index is only where a commit is assembled: the commit clears whatever is staged outside the
checked paths, adds the checked ones, commits, and restores what the clearing destroyed. It goes through
the index rather than committing from the working tree because only the index records a file mode when
`core.filemode` is off (git's default on Windows). An unborn repository is not a special mode: the empty
tree stands in for HEAD and everything else is unchanged. Mercurial has no index; `hg status` answers the
same delta, and an `addremove` of the checked files followed by `hg commit <checked paths>` commits it, with
the null revision as an unborn repository's parent.

Deliberate consequences:

- Untracked files are added as part of the commit, and the add is rolled back if the commit fails, so the
  Untracked/Added states shown never lie.
- Only the checked rows change state: a checked subrepo commits its pointer, never the unknown files inside it.
- What another tool staged outside the checked paths is cleared for the commit and written back after it,
  object and mode, so a newly added file stays Added and a hunk-level selection made with `git add -p`
  survives. An unmerged path is the one entry not written back, and committing is refused while one exists.
- Discarding is the same delta run backwards (`git restore --source=HEAD --staged --worktree`). Untracked
  files are outside it (git aborts the whole command on an unknown path), and an added file is only
  un-added, since restore would delete it.

## Undoing the last commit

`RepoState::lastCommitUndoRefusal()` names why the last commit cannot be undone, and every refusal lives
there rather than in a backend: a commit the upstream already has, a merge, a root commit, an operation in
progress, or a detached HEAD (where pushed cannot be told from unpushed). The menu item stays enabled for
all of them and reports which one applies. The action refreshes first and decides on that state: a commit
or push from outside the app since the last refresh changes the answer. Both backends
leave the changes in the working tree, where the list shows them as uncommitted again. The commit's message
returns to the message box, unless the box already holds text.

Git uses `reset --soft`, not `--mixed`: the commit was assembled in the index, so leaving the index alone
restores exactly the state the commit was made from. Mercurial uses `uncommit`, not `rollback`, which
undoes the last *transaction*, whatever it was (after a pull, the pull). `uncommit` is an extension that
ships with hg but is off by default, so the command enables it for itself.

## Detached HEAD

A detached HEAD is put back on a branch only where the working tree stays put and no commit is orphaned.
The candidates, computed on refresh:

| Kind | Condition | Action |
|---|---|---|
| At HEAD | a local branch's tip is HEAD | check it out |
| Move | a local branch with nothing unpushed, its upstream containing HEAD | move it to HEAD, check it out |
| Create | a remote-tracking branch containing HEAD, its local name free | create it at HEAD, tracking that branch |

A branch checked out in another worktree is never a candidate. A move loses nothing: every commit the
branch leaves behind is on its upstream. It covers a submodule update's detached HEAD, older or newer
than the local branch.

The header strip describes the candidates and offers them on a button; several are offered as a choice.
Committing takes only a candidate whose ref already points at HEAD, silently if there is one; moving a
branch needs the button. With no such candidate committing is blocked. With no candidate at all, the strip
names the branches that nearly qualify and what rules each out: unpushed commits, another worktree, or a
local branch of the needed name tracking something else. The same code path serves the main
repository and submodules. Mercurial has no detached state, so the flow is never entered there.

The operations that detach HEAD themselves - bisect and rebase - are the exception: committing is refused
while one is in progress, so the flow is never reached and the strip promises nothing. Every other
operation leaves HEAD on its branch, and a commit made during one attaches exactly as any other does.
