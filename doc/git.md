# Git behavior

Facts about git, not about this app. Each was established by a probe, a source read, or a wrong assumption
that cost work; the ones that produced no code have nowhere else to live.

What the app does about these is in `ARCHITECTURE.md` - Invocation invariants, Text encoding, Refresh. This
file states what git does; that one states the rules built on it.

Verified against git 2.37.1.windows.1. Version floors are named where one is known.

## Version floors

`--pathspec-from-file=-` and `--pathspec-file-nul` arrived in 2.25 (January 2020), and set the floor the
README states. A vendor appends its own fields to the reported version - `git version 2.37.1.windows.1` -
so only the leading numbers are the version.

## Paths

A path is bytes, not text in a known encoding. Git stores what the filesystem gave it.

- Git for Windows stores UTF-8 internally whatever the codepage. On Unix a path is whatever the filesystem
  holds, so the local 8-bit codec applies.
- Commit messages, author names and ref names are UTF-8 by git's own convention, whatever the paths are.
- Under `-z` git emits paths unquoted and gives a rename its own NUL-separated tokens, never the ` -> `
  syntax, so a filename containing ` -> ` stays unambiguous. `core.quotepath=false` covers the output that
  is not `-z`.
- argv on Windows caps near 32 KB, so a long path list travels through
  `--pathspec-from-file=- --pathspec-file-nul` on stdin. A commit message has no length bound at all and
  goes through a temp file.

## Output formats

| query | record layout |
|---|---|
| `status --porcelain=v2 -z`, rename | `2` record followed by its origin path as a bare NUL token |
| `status --porcelain=v2 -z`, unmerged | `u <XY> <sub> <m1> <m2> <m3> <mW> <h1> <h2> <h3> <path>`; path is everything past the tenth space |
| `diff --name-status -z` | `<letter>\0<path>\0`, or `<R\|C><score>\0<old>\0<new>\0` |
| `diff --raw -z` | `:<oldmode> <newmode> <oldsha> <newsha> <letter>\0<path>\0` |
| `diff-index --cached --raw -z` | `:<treemode> <indexmode> <treesha> <indexsha> <letter>\0<path>\0` |
| `diff --numstat -z` | `<added>\t<removed>\t<path>\0`, or `<added>\t<removed>\t\0<old>\0<new>\0` |
| `ls-files -s` | `<mode> <sha1> <stage>\t<path>` |
| `ls-tree` | `<mode> <type> <sha>\t<path>` |
| `packed-refs` | `<sha> <ref>` per line; a peeled tag adds a `^<sha>` line naming no ref |

Mode 160000 on either side of a raw record is a gitlink, so that row is a submodule.

Other format facts:

- `%B` carries a trailing newline, and a `log` format separator can occur inside a message, so the tail must
  be rejoined rather than treated as a field.
- `--no-ahead-behind` prints `+? -?` where the counts would be, so presence must come from parsing the
  numbers and not from the key being there.
- `diff --name-status HEAD` never reports `U` during a merge. A UU path shows as `M`; a UD path, whose
  worktree content equals HEAD, does not appear at all. The unmerged index entries are the only complete
  record of which paths are conflicted.
- `diff` cannot distinguish a conflict from an edit. Staging a path drops its unmerged entries, which is the
  whole of git's record that a resolution happened.
- Into a pipe git prints nothing until it finishes. `--progress` makes it emit as it goes, as carriage
  returns rewriting one line.
- `-S` counts occurrences and takes its term literally. `-G` matches patch lines with a regex that
  `--fixed-strings` does not apply to, so a term must be escaped - an unescaped `foo(` aborts the query.
- `log` default ordering only guarantees that a commit comes above the child it was first reached through.
  `--topo-order` guarantees it against every child and keeps each line of history contiguous.
- `--all` means every ref under `refs/`, `refs/stash` and `refs/notes` included. `--branches --tags
  --remotes HEAD` is the same set without those two, and names a detached checkout, which is on no branch.
- `--follow` tracks one path and rewrites it at each rename it crosses, for the whole walk and not just for
  the tip that crossed the rename. With several starting revisions it therefore carries a path onto a line of
  history that never held it: with `A` renaming `f`->`g` and `B` renaming `f`->`h`, `log --all --follow -- h`
  lists `A`'s rename of `f`, which never touched `h`.

## The git directory on disk

- `HEAD` holds either a sha (detached) or `ref: <name>`. That ref is a loose file under the git dir or a
  line of `packed-refs`.
- `packed-refs` lives in the **common** directory, not in a linked worktree's own git dir. A worktree whose
  ref is packed cannot have its head resolved from its own git dir alone.
- A submodule's `.git` is a file, not a directory.
- The empty tree is resolvable in every repository without being written: its sha depends only on the hash
  algorithm, and `hash-object -t tree --stdin` yields it.

## Operations in progress

Each leaves a marker in the git directory:

| operation | marker | ends with |
|---|---|---|
| merge | `MERGE_HEAD` | `merge --abort` |
| cherry-pick | `CHERRY_PICK_HEAD` | `cherry-pick --abort` |
| revert | `REVERT_HEAD` | `revert --abort` |
| rebase | `rebase-merge`, or `rebase-apply` without `applying` | `rebase --abort` |
| am | `rebase-apply/applying` | `am --abort` |
| bisect | `BISECT_LOG` | `bisect reset` |

An operation started mid-bisect owns the commit until it ends, so the conflict-resolution markers must be
tested before `BISECT_LOG`.

**`git am` and `git rebase` share `.git/rebase-apply`.** The file inside says which wrote it: `applying` for
am, `rebasing` for rebase. Testing only for the directory reports an interrupted am as a rebase, and
`git rebase --abort` then fails with `fatal: It looks like 'git am' is in progress. Cannot rebase.`

**A multi-commit `cherry-pick A..B` or `revert A..B` keeps state the markers above do not cover.**
`.git/sequencer/` holds `todo`, `head` and `abort-safety`. After a plain `git commit` resolves a stopped
pick, `CHERRY_PICK_HEAD` is gone but `.git/sequencer` remains with the rest of the picks; `git status` still
reports "Cherry-pick currently in progress", and `cherry-pick --continue` still works. The first word of
`todo` (`pick` or `revert`) says which command owns the sequencer.

## Continuing an operation

- **`git rebase --continue` requires an editor and has no `--no-edit`.** It resolves one in git's usual
  order - `GIT_EDITOR`, then `core.editor`, then `VISUAL` and `EDITOR` - so an unpinned call inherits
  whatever the machine configures. A GUI editor with a wait flag is the common developer setting, and that
  **blocks until a window is closed** rather than failing. An editor that exits non-zero fails instead, with
  "There was a problem with the editor". `GIT_EDITOR=true` completes the command, keeping the stopped
  commit's message unchanged.
- `git var GIT_EDITOR` reports which editor git would resolve, without running it.
- `git cherry-pick --continue --no-edit` needs no editor.
- `git merge --continue` rejects `--no-edit` outright ("--continue expects no arguments"). A merge is
  finished by committing.
- `GIT_TERMINAL_PROMPT=0` stops a credential miss from hanging on a prompt. It does not cover editors, and
  Git Credential Manager's own GUI is unaffected by it.

## Unborn HEAD

- `status --porcelain=v2 --branch` reports `branch.oid` as `(initial)`, and omits the `ab` field for want of
  a commit rather than for want of an upstream ref.
- Any query naming `HEAD` fails. The empty tree stands in for it: every change reads as an addition against
  it, which is also what the first commit's diff against its parent tree would show.
- `reset --pathspec-from-file=- --pathspec-file-nul` succeeds against an unborn HEAD, exit 0. It unstages
  exactly the named paths and leaves the files on disk. No special handling is needed for the no-HEAD case.

## Index

- `git commit` takes the whole index, so a path-limited commit has to make the index hold exactly that
  commit and restore it afterwards.
- `git add` fails on a path that is in neither the working tree nor the index.
- With `core.filemode` off, only the index records a file's mode, so re-adding a cleared entry from the
  working tree loses it. `update-index --index-info` restores it.
- `git restore` checks out over every nested submodule its pathspec covers, detaching one that has nothing
  to do with the paths intended. A restore must name paths, never a whole-tree pathspec.

## Submodules

- `git status` reports a submodule holding its own commits as modified, with no flag needed.
- A submodule's recorded commit is published if any remote reaches it; git's own on-demand test is whether
  no remote does. A commit this clone does not have came from a remote, so a remote has it.
- A push inside a submodule publishes its recorded commit only if that commit is on a branch and that branch
  contains it.
- Children must be pushed before their parent: the parent's tree references commits living in its children.

## Probing git locally

A shell profile or an agent harness may already export the variable under test. `GIT_EDITOR=true` makes
every editor-dependent command look as though it needs no editor, and `GIT_ALLOW_PROTOCOL` restricts which
transports a remote may use. Clear them per command - `env -u GIT_EDITOR git ...` - before concluding
anything about either.

## Corrections

Each of these was assumed to be otherwise, and acted on or nearly acted on:

- **`git rev-parse --show-toplevel` does not echo the traversed spelling.** Reached through a junction or a
  `subst` drive, it returns the real path, so a root git resolved is already canonical.
- **`git reset` against an unborn HEAD needs no special handling.** See above.
- **The marker files are not a complete test for cherry-pick and revert.** The sequencer outlives
  `CHERRY_PICK_HEAD`; see Operations in progress.
- `QFileInfo::canonicalFilePath()` resolves neither a junction, a `subst` drive, nor a second mount of one
  volume in Qt 6.9.3. A Qt fact, not a git one, and the first thing a path-identity question runs into.
