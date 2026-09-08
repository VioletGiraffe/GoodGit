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
same delta and `hg commit -A <checked paths>` commits it, with the null revision as an unborn repository's
parent.

Deliberate consequences:

- Untracked files are added as part of the commit, and the add is rolled back if the commit fails, so the
  Untracked/Added states shown never lie. Mercurial needs no rollback: `commit -A` adds inside the commit's
  own transaction.
- What another tool staged outside the checked paths does not survive a commit. Only a file mode is
  restored, since nothing else is lost: every other entry's content is still on disk. What is lost is the
  staging itself, including any hunk-level selection made with `git add -p`.
- Discarding is the same delta run backwards (`git restore --source=HEAD --staged --worktree`). Untracked
  files are outside it (git aborts the whole command on an unknown path), and an added file is only
  un-added, since restore would delete it.

## Undoing the last commit

`RepoState::lastCommitUndoRefusal()` names why the last commit cannot be undone, and every refusal lives
there rather than in a backend: a commit the upstream already has, a merge, a root commit, an operation in
progress, or a detached HEAD (where pushed cannot be told from unpushed). The menu item stays enabled for
all of them and reports which one applies. Both backends leave the changes in the working tree, where the
list shows them as uncommitted again.

Git uses `reset --soft`, not `--mixed`: the commit was assembled in the index, so leaving the index alone
restores exactly the state the commit was made from. Mercurial uses `uncommit`, not `rollback`, which
undoes the last *transaction*, whatever it was (after a pull, the pull). `uncommit` is an extension that
ships with hg but is off by default, so the command enables it for itself.

## Detached HEAD

Reattachment happens at commit time and only when the working tree would not move: a single local branch
tip at HEAD is checked out silently, several are offered as a choice, and a remote-tracking tip at HEAD
whose local name is free becomes a tracking branch. Everything else refuses with an explanation, and the
header strip announces the applicable case before the user commits. The same code path serves the main
repository and submodules. Mercurial has no detached state, so the flow is never entered there.

The operations that detach HEAD themselves - bisect and rebase - are the exception: committing is refused
while one is in progress, so the flow is never reached and the strip promises nothing. Every other
operation leaves HEAD on its branch, and a commit made during one attaches exactly as any other does.
