# Robustness review plan

Reusable fan-out plan for reviewing the app against plausible user input. Eight independent finder angles,
each scoped to one layer or concern; run them in parallel, then dedup and verify every candidate
(CONFIRMED / PLAUSIBLE / REFUTED, keep the first two). Each finder returns every candidate with a
nameable failure scenario, ranked by severity — no count cap; the verify pass is the filter. Each candidate:
file, line, one-line summary, concrete failure scenario (input/state -> wrong behavior), verbatim quote.
Finders read doc/ARCHITECTURE.md first for orientation.

## A. Output-parser robustness

Files: gitparsers, hgparsers, textdiff, unifieddiff; log parsing in commitgraph.

- Filenames: spaces, tabs, quotes, unicode / git C-quoted paths, rename arrows, names containing " -> "
- Status: unmerged states (UU, AA, DU...), copies, type changes, submodule entries
- Diff: binary files, mode-change-only, empty, "\ No newline at end of file", CRLF, huge hunks, "diff --git" with spaces in paths
- Log/graph: messages containing the format separators, empty messages, multi-parent merges, root commits
- Index arithmetic: off-by-one, out-of-range on split() results, assumed field counts

## B. Process-layer failure handling

Files: vcsprocess, gitprocess, hgprocess, hgcommandserver, queryround.

- git/hg missing from PATH; process fails to start, crashes mid-output, exits nonzero, hangs (timeouts?)
- Large output: buffering, blocking reads, memory; stderr/stdout interleaving
- Encoding: non-UTF8 output, invalid UTF-8 sequences
- Exit codes: which failures are swallowed vs. surfaced
- hg command server: malformed/truncated channel data, unexpected channels, server death mid-command
- Concurrency: overlapping operations on one repo/process object; callbacks after owner destruction; QProcess signals after deleteLater; lambdas capturing raw `this`

## C. Repository-state edge cases

Files: gitrepository, hgrepository, repository, repositoryfactory, recentrepositories.

- Zero-commit repo (unborn HEAD); detached HEAD; branch names with slashes/unicode or named "HEAD"
- Mid-merge / mid-rebase / mid-cherry-pick; conflicts in status
- No remote; no upstream; multiple remotes; push rejected (non-fast-forward, auth, network)
- Repo path deleted/renamed while open; disconnected network drive; unicode/space paths; non-repo dir; bare repo; worktree; submodule opened directly
- Recent-repositories persistence: stale entries, malformed settings; same repo opened twice

## D. UI and model robustness under interaction

Files: changedfilesmodel, historymodels, commitwindow, historywindow, filelistview, fileviewerwindow, diffpane, difftextview.

- Clicks during model refresh (stale QModelIndex, row count changed under selection)
- Out-of-range indexing into model backing storage; data() with invalid index
- Acting on a file deleted/renamed on disk between refresh and click
- Commit with empty message / empty selection / nothing staged; all files unchecked
- Rapid repeated clicks on push/commit/refresh (double-fire, overlapping async ops)
- Window closed with a query in flight (callback touching a dead widget)
- Huge files / extremely long lines in diff and file viewers
- Selection restoration after reset pointing at a missing row; Enter/Delete/context menu on empty list

## E. Cross-file contract tracing

Contracts defined in repository.h, vcstypes.h, queryround.h; callers in commitwindow, historywindow,
repositorywindows, welcomewindow, fileviewerwindow.

- Each async callback vs. the error/empty result the callee can deliver; indexing assuming non-empty
- Window lifetime vs. in-flight operations; ownership in repositorywindows; close/quit mid-operation
- externalapps + commandlinetool_mac: user-controlled paths (spaces, unicode), tool not installed
- main/welcomewindow startup: nonexistent path argument, file instead of dir

## F. Reuse + simplification

- Re-implementations of helpers that exist in cpputils/qtutils/cpp-template-utils or elsewhere in app/src
- Near-duplicate blocks between git and hg implementations where a shared helper is straightforward
- Redundant or derivable state, copy-paste with slight variation, dead code, unreachable branches
- Only cases with a clean fix; name the existing helper or simpler form

## G. Efficiency at plausible scale

Reference sizes: 100k commits, 100MB file, 50k-line diff, 10k changed files.

- O(n^2) over user-sized data: linear scans inside loops, per-row full-model searches, string concat in loops
- UI-thread blocking: synchronous waits, large parsing on the GUI thread, unbounded file reads
- Redundant work: repeated identical queries, re-parsing unchanged data, full rebuilds per keystroke
- Unbounded memory; eager per-instance OS resources (threads, watchers, handles)
- Quantify from the actual code, name the cheaper alternative

## H. Altitude + conventions

- Altitude: special cases layered on shared infrastructure; per-call-site workarounds for a shared defect; UI-layer handling of what the repository/process layer should guarantee
- Conventions: clear violations of the governing CLAUDE.md rules only — quote the exact rule and the exact offending line; includes the banned-wording grep from the comments register
