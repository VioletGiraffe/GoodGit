#pragma once

#include <QString>

class CommitWindow;
class QWidget;
struct RepositoryLocation;

// Opening a repository in a window of its own, wherever the request comes from - the command line, the
// current directory, the recent list, a chosen folder. One window per repository, the recent list kept as
// they open, and one wording for a path no backend claims. Every function answers with the window that is
// now showing that repository, or nullptr where none is - the failure already reported.

// The window already open on that repository root, if there is one
[[nodiscard]] CommitWindow* repositoryWindow(const QString& root);

// That repository's window: the one already open on it, raised, or a new one. Records the open.
CommitWindow* openRepositoryWindow(const RepositoryLocation& location);

// The repository containing `path`, whichever kind claims it. Reports a path that none does.
CommitWindow* openRepositoryWindowAt(const QString& path, QWidget* dialogParent);

// The same for a root the recent list named, where the failure is the entry's rather than the user's:
// it offers to drop it from the list.
CommitWindow* openRecentRepository(const QString& root, QWidget* dialogParent);

// Asks for a directory and opens what contains it. Null when the user cancelled, as when nothing claimed it.
CommitWindow* browseForRepository(QWidget* dialogParent);
