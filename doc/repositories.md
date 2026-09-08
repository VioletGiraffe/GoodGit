# Opening a repository

## Opening a repository

Every way in resolves a path the same way, through `findRepository()` (two blocking local probes, git
first), so a mistyped argument, a chosen folder that is not a repository and a stored entry that has moved
all fail with one wording. They differ in which path is asked for. A command line argument is taken
literally: if it is not a repository, the program fails with an exit code, like any command line tool. With
no argument, the current directory is the first guess (started from a shortcut it is wherever that pointed),
falling through to the repository opened last and then to the welcome window, which states what the
application needs, asks for a folder, and lists the recent repositories in the panel the dock uses. A folder
dropped on a window opens like a chosen one. **No commit window exists without a repository**, so
`CommitWindow` never handles that case; the welcome window is the one that does. `View > Show Welcome
Screen` opens it at any time, and any open through `openRepositoryWindow()` closes it again. Closing the
last window quits.

One repository has one window: a second request raises the existing one. The open windows are the
registry; nothing else tracks them. Requests are matched by filesystem identity (`sameDirectoryOnDisk`, on
thin_io), so one repository reached both directly and through a subst drive or a junction finds its window
either way. Only the decisions an alias would break pay for that: the recent list's scans match on spelling,
where a wrong answer costs a duplicate row and not a second window.

Instances are independent: nothing stops a second process, and it may open a repository the first already
shows. They share the settings store, so the recent list is written as one value, and the repository, so a
write is re-checked against the on-disk state before it runs (`refresh.md`).
