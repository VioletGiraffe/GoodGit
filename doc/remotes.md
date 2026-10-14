# Remotes

## The incoming check

Every refresh of a commit window starts an incoming check, unless one is in flight, a push is running (its end
refreshes), or the branch has no upstream or a gone one. Nothing pulls or merges.

- git: `git fetch`, then `rev-list --count` and `log` over `HEAD..@{upstream}`. The check is the only thing that
  fetches, and so the only thing besides a push that moves a remote-tracking ref.
- hg: one `hg incoming`. Mercurial has no remote-tracking refs, so the behind count is carried from the last check.

The check sets the behind count without a refresh: a refresh would invalidate the `StateStamp` of a write flow
whose dialog is open (`refresh.md`).

The header shows no count before a check in that window has succeeded: "checking" until the first answer,
"could not check" after a failure, with the reason in the tooltip. A re-check in flight keeps the last answer up.

A background check never shows Git Credential Manager's sign-in window; a missing credential fails it.
Check for Incoming Changes (Ctrl+I) starts an interactive check, or joins the one in flight, and lists the
commits in a popup.
