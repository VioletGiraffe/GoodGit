# Recent repositories

The recent list is stored, and so are the submodules under each entry, harvested from `RepoState::submodules` on refresh
(every submodule the repository declares, not only the changed ones in the file list). The panel therefore
expands an entry without running anything, shows nothing under one never opened, and is as stale as the rest
of the entry. Opening a submodule records its parent: the list names workspaces, and a submodule is part of
one rather than one of its own.

Every entry carries when it was last used: the moment it was opened, or, for one a folder scan found, the
timestamp of the file everyday work rewrites - git's index, Mercurial's dirstate, since a repository
folder's own timestamp barely moves. The list is ordered by it, so scanned and opened repositories
interleave on one timeline. An entry stored before the field existed carries 0 and sorts last; nothing
migrates them.

A folder scan is the one way an entry joins the list unopened. Which folders are repositories, of which
kind, and when each was last worked in all come off the filesystem - the marker in a subfolder's root, what
that marker must hold, the timestamp of the file everyday work rewrites - so only working trees are found
and nothing is launched to find them. Submodules are the exception: git keeps them in the index, so every git
repository found costs one `ls-files`, launched through the queue that caps every other query and joined by
a `QueryRound`; Mercurial declares its own in `.hgsub`, which the scan reads. A repository whose query fails
is listed without submodules, as one never opened is. The cap applies to the merged list once it is sorted, so
a scan can push out an entry that was already there.
