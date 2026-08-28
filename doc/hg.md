# Mercurial behavior

Facts about Mercurial, not about this app. Each was established by a probe, a read of hg's own Python
sources, or a wrong assumption that cost work; the ones that produced no code have nowhere else to live.

What the app does about these is in `ARCHITECTURE.md` - Invocation invariants, Text encoding, Backends. This
file states what hg does; that one states the rules built on it.

Verified against Mercurial 7.2.2 on Windows. Version floors are named where one is known.

## Invocation

- `HGPLAIN=1` removes localisation, user aliases and defaults rewriting the command.
- The user's extensions must **not** be disabled: a repository may need one (largefiles, lfs) to be readable
  at all.
- Every invocation pays Python startup, so a refresh batches its queries rather than chaining them.
- `-T json` output changes its field set under `-q` or `-v`, so neither may be combined with it.
- `diff.nobinary=True` makes a binary file's diff a one-line note. Without it `--git` mode emits a base85
  patch.
- A long path list travels in a temp file passed as `listfile0:<file>`, and a commit message in a temp file.

**Exit code 1 means "nothing to report", not failure**: no incoming changesets, none outgoing, no search
hits, nothing to push.

## Encoding

- **hg writes and reads the local 8-bit encoding** - the ANSI codepage on Windows - unlike git, which is
  UTF-8 everywhere. This covers command output, argv, `listfile0` contents, and `.hgsub`/`.hgsubstate`,
  which hg reads in that encoding too.
- Encoding argv as local 8-bit matches what hg itself does with argv on Windows. A path the ANSI codepage
  cannot represent is a limitation hg shares, not one a caller introduces.
- **`-T json` is the exception**: hg escapes it to ASCII. A path byte the local encoding cannot decode
  arrives as a lone surrogate U+DC80..DCFF - hg's utf8b scheme - in raw WTF-8, which fails a strict JSON
  parse of the whole document. In WTF-8 those code points are `ED B2..B3 80..BF`, and an `ED B2`/`ED B3`
  pair is never valid UTF-8, so the sequences are findable. Mapping such a surrogate back to its byte
  round-trips the path through a pathspec unharmed.

## Output formats

- `incoming` and `outgoing` print `comparing with ...` before their JSON, and no JSON at all when they found
  nothing. The array begins on a fresh line, unlike a bracket inside a remote path.
- Dates are `[seconds since the epoch, seconds *west* of UTC]`. Most time APIs count east.
- `user` is `Name <email>`.
- `tip` is not a stable label: it names whichever changeset is newest.
- **hg has no `--numstat`.** `diff --stat` scales its bars to the widest file, so line counts have to come
  from the diff lines themselves.
- `--git` names renames and binary files instead of printing every line as added. hg **records** copies
  rather than inferring them, so `--git` prints the old path without being asked to detect anything.
- `-Z` is git's `--ignore-cr-at-eol`.
- `cat --decode` applies the `[decode]` filters, so bytes arrive as a working tree would hold them.
- A file missing from disk stays in the dirstate until its removal is recorded, so `hg diff` reports no
  change for it. Diffing the parent against null is the removal such a row would commit.

## Querying history

- `log -f` walks the ancestors of `.` (or of `-r`) only. A plain `hg log` lists every changeset, unrelated
  heads included.
- With a path, `-f` also follows that file across renames.
- **`hg grep` has no fixed-string mode**, so a search term must be escaped. `-f` confines it to the walked
  line of history; without it every head is searched.
- A revset union comes out oldest first, the opposite of a log view's order.
- `only(., X)` is what the working directory's parent has and X does not.
- Every changeset is draft where nothing is configured to push to, so a draft count is only meaningful with
  a path configured.

## Ignore syntax

Unlike git's:

- `#` starts a comment at line start. There is no `!` negation, so nothing needs escaping for it.
- Lines before the first `syntax:` declaration are **regular expressions**.
- In a `glob` section a pattern matches in any directory. Only `rootglob` anchors it to the repository root.
- A directory pattern takes no trailing slash: hg matches any leading part of a path.

## The .hg directory

- **dirstate-v1** opens with the two parent nodes, 20 bytes each. A null parent is twenty zero bytes.
- **dirstate-v2** puts the parents behind a docket file instead. A repository with
  `format.use-dirstate-v2=1` aborts every command where hg has no Rust extension available.
- Directory existence does not identify a repository: hg queried in a marker-less directory resolves upward
  and answers for the parent.

## Operations in progress

hg's own registry of these is `addunfinished()` in `mercurial/state.py` and in each extension. As of 7.2.2:

| operation | marker | hg allows commit | continue | end |
|---|---|---|---|---|
| merge | *none* - two working-dir parents | yes | `hg commit` | `merge --abort` |
| bisect | `.hg/bisect.state` | yes (report-only) | - | `bisect --reset` |
| graft | `.hg/graftstate` | no | `graft --continue` | `graft --abort`, or `--stop` to keep what it grafted |
| rebase | `.hg/rebasestate` | no | `rebase --continue` | `rebase --stop` |
| histedit | `.hg/histedit-state` | yes | `histedit --continue` | `histedit --abort` |
| unshelve | `.hg/shelvedstate` | no | `unshelve --continue` | `unshelve --abort` |
| transplant | `.hg/transplant/journal` | no | `transplant --continue` | `transplant --stop` (no `--abort`) |
| interrupted update | `.hg/updatestate` | no | `hg update` | - |

**`.hg/merge` is a conflict marker, not an operation marker.** Merge, graft, rebase, unshelve and update
over local changes all leave a mergestate when they conflict, and it outlives its command until the resolve
is committed or aborted. A merge that does not conflict leaves none, and a histedit stopped on an `edit`
action leaves none either, so its absence proves only that nothing is unresolved.

- `status` reports a conflicted file as modified; only the mergestate distinguishes them. Its states: `U` is
  an unresolved content conflict, `P` an unresolved path conflict whose incoming file lands under a
  `~<hash>` name. hg's own commit gate counts both.
- A modified-here/deleted-there conflict keeps the local file with an empty status, so the mergestate is its
  only evidence.
- Marking resolved moves the mergestate entry from `U` to `R` and does not touch the file.
- `bisect --reset` only clears the session state. Unlike git's, it does not move the working directory.
- A merge cannot be committed in parts.

## Continuing and aborting

Both are marked EXPERIMENTAL in 7.2.2, and neither is as generic as its name: each refuses per operation
with `<operation> in progress but does not support 'hg <verb>'`.

- **`hg continue` refuses a graft**, despite graft's registration declaring `continueflag=True`.
- **`hg abort` refuses a bisect**, pointing at `bisect --reset`. Bisect is registered report-only.
- `hg abort` ended a rebase, a graft and an uncommitted merge in testing. It answers "no operation in
  progress" for a histedit unless the histedit extension is loaded, so the operation's own extension should
  be enabled by `--config` regardless.

## uncommit and rollback

`uncommit` ships with hg but is off by default. It is narrower than `rollback`, which undoes the last
transaction of any kind.

- It refuses public and merge changesets.
- **It has no unfinished-operation guard.** `hgext/uncommit.py` never calls `checkunfinished`, and the only
  state check inside `rewriteutil.precheck` is `len(repo[None].parents()) > 1` - a two-parent working
  directory. A graft, rebase or histedit in progress does not stop it.
- Its one working-copy guard is `scmutil.bail_if_changed`, which `--allow-dirty-working-copy` disables. A
  bare `hg uncommit` mid-graft therefore aborts with "uncommitted changes" - because the conflicted file is
  modified, not because a graft is running - while the same command with that flag succeeds and strips the
  commit, leaving `graftstate` pointing at a node that no longer exists.

## Extensions

These ship with hg and are **off by default**: `uncommit`, `extdiff`, `rebase`, `histedit`, `shelve`,
`transplant`. A command can enable the one it needs for itself with `--config extensions.<name>=`, without
touching the user's configuration. Which tool `extdiff` starts is the user's `[extdiff]` configuration.

## Subrepos

- `.hgsub` declares them; `.hgsubstate` records their nodes and lags a newly added one by a commit.
- A path containing `=` is unrepresentable in `.hgsub`: hg's own config regex excludes `=` from the key and
  offers no escape.
- **The parent's recursing `status` compares a subrepo against the node `.hgsubstate` records**, not against
  the subrepo's own parent changeset. After a committed pointer move it reports the subrepo's files as
  modified - exactly when the pointer must be committable. Dirtiness has to be asked of the subrepo itself.
- A subrepo's own status must recurse: a nested subrepo the enclosing one has not recorded yet is
  uncommitted work.

## Working-directory and branch facts

- **hg has no detached state.** The working directory carries a named branch (`default` unless one was
  created), and it carries the branch a commit would land on before the parent changeset does - `hg branch`
  sets it ahead of the commit.
- The working directory has two parents while a merge is uncommitted.
- **hg has no per-branch upstream.** A push contacts `default-push` where configured, else `default`. No
  configured path is an answer - nowhere to push to - not a failure.
- hg keeps no remote-tracking state, so what a remote holds must be read from the remote.
- `commit -A` adds new and removes missing files **within the pathspec**; a missing file cannot otherwise be
  committed by name. Unscoped, `-A` sweeps in every unknown file.
- `revert` leaves a `.orig` copy of every reverted file unless `-C` is passed.
- Reverting a path the parent changeset does not have only takes it out of tracking.

## Command server protocol

- Channels are single letters. A lowercase channel may be skipped; an uppercase one may not, so a client
  that meets an uppercase channel it does not implement cannot use that server.
- The hello block is `capabilities: ... runcommand ...` then `encoding: ...`.
- An input request's length field is how much input is wanted; no payload follows it.
- Arguments are NUL-separated bytes, the same ones hg would read from a real command line.
- Without `-R` a command runs on the bound repository whatever the cwd; without `--cwd` relative paths
  resolve against the server's cwd.
- A desynced stream is only catchable at the frame header: past it, a garbage length is waited on forever by
  a server that stays alive and so never reports anything.

## Probing hg locally

A configured GUI merge tool that fails or is cancelled leaves the conflicted file unmodified, and every
dirty-working-copy behavior then reads backwards. Pass `--tool internal:merge` for real conflict markers.

## Corrections

Each of these was assumed to be otherwise:

- **`hg uncommit` is not blocked by an operation in progress.** See above.
- **`hg continue` does not work for every operation that declares it.** See above.
- **A clean uncommitted merge leaves no `.hg/merge`**, so a mergestate test does not detect one.
- **A histedit `edit` stop leaves no mergestate at all**, so nothing about the working directory reveals it
  except `.hg/histedit-state`.
