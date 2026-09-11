# Invoking the tools

## The executables

`git` and `hg` resolve on PATH unless Preferences sets a path. On macOS, startup prepends the Homebrew and
MacPorts bin directories missing from PATH: an app started from Finder or the Dock inherits launchd's PATH,
without them. Child processes inherit the result, including the helpers git and hg start (git-lfs, gpg).

## Invocation invariants

`gitprocess` applies to every call: `-c core.quotepath=false`, `GIT_TERMINAL_PROMPT=0` (a credential miss
fails instead of hanging on an invisible prompt; Git Credential Manager's own GUI is unaffected), and
`--no-optional-locks` on read-only queries. Paths travel NUL-separated (`-z`, `--pathspec-from-file=-
--pathspec-file-nul` via stdin), never on argv, which Windows caps near 32 KB. The commit message goes
through a temp file, never `-m`: it is the one argument with no bound on its length.

`hgprocess` applies `HGPLAIN=1`, `--config ui.interactive=False` and `--config diff.nobinary=True`, and
deliberately does **not** disable the user's extensions: a repository may need one (largefiles, lfs) to be
readable at all. `nobinary` is what makes hg's `--git` diffs safe to ask for: without it a binary file
arrives as a base85 patch. Queries use `-T json`, never with `-q` or `-v`, which change the field set. The
message and any long path list travel in temp files, the latter passed as `listfile0:<file>`.

Output accumulates as it arrives, so a chatty child cannot fill a pipe and stall, and reaches the result
callback whole. A job may additionally stream chunks to a sink (`Job::streamTo`), attached before control
returns to the event loop or the first chunks are missed. Push is the one command that streams, and the one
that carries `--progress`: into a pipe git prints nothing until it finishes, and the meter arrives as carriage
returns rewriting one line, which `ConsoleLogView` renders as a terminal would and `errorText()` collapses for
dialogs.

Line-endings-only changes are hidden from the shown diffs and the line counts by default
(`--ignore-cr-at-eol`, `-Z` for hg); a setting shows them instead and flips both together so the counts
always match the shown diff. Either way the file still lists as modified and commits its content byte for
byte. The word-pool diff ignores such changes regardless, since a wholesale conversion would flood it.

An untracked file is not a modification of anything, so no backend is asked to diff one: the window reads
the file and shows it with highlighting off.
