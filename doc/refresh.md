# Refresh

Triggered by startup, F5, and the app's own state-changing actions; **never** by window activation, and
there is no file watcher. The list may be stale by design: **the checked rows are the commit pathspec,
verbatim**, and a stale list produces ordinary VCS errors through the normal failure path, not silent
re-scans. Check state survives a refresh by path; the state a newly listed row starts with is a setting.

A confirmation dialog spins a nested event loop that a refresh can complete inside, and an operation or a
commit can land outside the app - from a second instance, or from a shell - with no refresh seeing it. Every
write decided before a dialog therefore captures a `StateStamp` (`CommitWindow`) - the refresh generation
plus fresh probes of the on-disk operation markers and of the commit HEAD names - and re-checks it before
writing; a mismatch refuses the action and asks the user to retry against the new state.

`GitRepository` refreshes in two rounds of parallel queries, the second depending on the first's answers.
`HgRepository` uses a single round, since every hg invocation has a fixed cost: one `hg status` answers
tracked changes, untracked files and rename sources together, alongside the unpushed set, each submodule's
pointer and dirtiness, and the conflicted paths where a mergestate exists.

Some queries the state cannot be established without (the branch header, the submodule list, the git
directory, the tracked-changes diff). If one fails, the run is discarded whole: the previous state and rows
stay, a strip says why, and committing, discarding and deleting are refused until a refresh succeeds.
Reading rows is still allowed; a stale row is worth opening. **The distinction is between a shorter answer
and a wrong one**: a failure outside that set costs untracked rows, line counts or a tooltip, never
correctness, so those fail silently.
