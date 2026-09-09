# Operations in progress

Follows from the no-staging model in `committing.md`:

- A merge, cherry-pick, revert or rebase in progress is a separate mode: the index already holds that
  operation's result, so the commit takes every tracked change rather than the checked rows, and discarding
  is refused because restoring a path to HEAD would drop the operation's result for it.
- A conflict is resolved in two steps, as both VCSs record it: edit the file, then mark it resolved (`git
  add`, `hg resolve -m`). Committing is refused while any row still reads Conflicted, since the commit
  would take that row's working tree copy, markers and all.
- The way out is Abort, which hands the operation back to the command that started it (`git <op> --abort`,
  `hg <op> --abort`) and loses every resolution with it. Two enders keep what they have already committed
  instead of rewinding - git's sequencer past its first commit, hg's transplant - and the confirmation says
  so rather than promising the repository back.
- A bisect in progress is an operation of its own kind: it owns the checkout, but does not constrain what
  a commit takes. Committing is refused during one: a new commit would fall outside the search range.
  Abort maps to the session's own ender (`git bisect reset`, `hg bisect --reset`); only git's returns to
  the pre-bisect branch, hg's leaves the working directory where the bisect left it.
- Committing is refused during any operation that is finished by continuing it rather than by committing: a
  rebase, git's `am`, and every hg operation but a merge. A commit made during one would strand it. Continue
  and Abort are what the window offers, and the op strip names the command Continue runs.
- Continue runs that command unchanged, so it may open the user's editor and wait for it to close: `git
  rebase --continue` does, on the message of the commit it completes. The editor is deliberately not
  overridden - the window stays in its mutation-in-flight state until the editor closes.
- Abort and Continue read their command off the on-disk markers at action time rather than from the cached
  state, so an operation finished or abandoned outside the app since the last refresh is reported instead of
  run. Continue has no confirmation dialog, and so no `StateStamp` to catch that first (`refresh.md`).
- An operation is classified by how it must be finished, not by what its system calls it, so `RepoOp` stays
  git's vocabulary and everything the app cannot carry through is one value. The strip shows the operation's
  own name instead, which the backend supplies alongside it: an hg graft is not a cherry-pick to whoever
  started it.
