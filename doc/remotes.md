# Remotes

## Fetching

"Check for Incoming Changes" is the only thing that fetches, and so the only thing that moves a
remote-tracking ref: it runs `git fetch`, refreshes (the header's ahead/behind counts derive from that ref),
and pops up `log HEAD..@{upstream}`. Without the fetch that list would almost always be empty. It is
disabled when the branch has no upstream. Nothing pulls or merges.

Mercurial has no remote-tracking refs, so there is nothing for a fetch to move: the check is a single
`hg incoming` round trip, and the header's "behind" is whatever the last check found, blank until one runs.
