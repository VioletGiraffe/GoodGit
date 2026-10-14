# Remotes

## The incoming check

Every refresh of a commit window starts an incoming check, unless one is in flight, a push is running (its end
refreshes), or the branch has no upstream or a gone one. Nothing pulls or merges.

- git: `git fetch`, then `rev-list --count` and `log` from HEAD to what it compares against: its upstream, or for
  a detached HEAD what `submodule update --remote` follows (below). The check is the only thing that fetches, and
  so the only thing besides a push that moves a remote-tracking ref.
- hg: one `hg incoming`. Mercurial has no remote-tracking refs, so the behind count is carried from the last check.

The check sets the behind count without a refresh: a refresh would invalidate the `StateStamp` of a write flow
whose dialog is open (`refresh.md`).

The header shows no count before a check in that window has succeeded: "checking" until the first answer,
"could not check" after a failure, with the reason in the tooltip. A re-check in flight keeps the last answer up.

A background check never shows Git Credential Manager's sign-in window; a missing credential fails it.

## Check for Incoming Changes

Ctrl+I opens a window-modal dialog (`IncomingChangesDialog`) checking the repository and every cloned submodule
one level down; nested submodules are not visited. Every check is interactive, except the repository's own when
it joins a background check in flight. The checks run in parallel, and each repository's row appears as it
answers, in path order. The dialog shows how many have answered, which are still waited on, and the elapsed time.

A submodule is usually on a detached HEAD, with no upstream. It then compares against what
`git submodule update --remote` would follow: the branch `.gitmodules` names for it on origin (`.` meaning the
superproject's branch), else `origin/HEAD`. A clone without `origin/HEAD` fails its check with git's error.
Mercurial's `.hgsub` names no branch, and a Mercurial subrepository compares against its own default path.
