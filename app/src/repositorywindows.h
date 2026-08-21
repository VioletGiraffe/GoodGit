#pragma once

#include <QString>

class CommitWindow;
class QWidget;
struct RepositoryLocation;

// Opening a repository in a window, from the command line, the current directory, the recent list or a chosen folder.
// One window per repository. The recent list is updated on every open.
// Every function returns the window now showing the repository, or nullptr with the failure already reported.

[[nodiscard]] CommitWindow* repositoryWindow(const QString& root);

// The window already open on it, raised, or a new one. Records the open.
CommitWindow* openRepositoryWindow(const RepositoryLocation& location);

// Opens the repository containing `path`, whichever kind claims it. Reports a path that none does.
CommitWindow* openRepositoryWindowAt(const QString& path, QWidget* dialogParent);

// The same for a root from the recent list; on failure offers to drop the entry
CommitWindow* openRecentRepository(const QString& root, QWidget* dialogParent);

// Asks for a directory and opens the repository containing it. Null when cancelled, as when nothing claimed it.
CommitWindow* browseForRepository(QWidget* dialogParent);
