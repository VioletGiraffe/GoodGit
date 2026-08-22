#pragma once

#include <QString>

class CommitWindow;
class QWidget;
struct RepositoryLocation;

// Opening a repository in a window, from the command line, the current directory, the recent list, a chosen
// folder or a dropped one.
// One window per repository. The recent list is updated on every open.
// Each open returns the window now showing the repository, or nullptr with the failure already reported.

[[nodiscard]] CommitWindow* repositoryWindow(const QString& root);

// The window already open on it, raised, or a new one. Records the open.
CommitWindow* openRepositoryWindow(const RepositoryLocation& location, QWidget* dialogParent = nullptr);

// Opens the repository containing `path`, whichever kind claims it. Reports a path that none does.
CommitWindow* openRepositoryWindowAt(const QString& path, QWidget* dialogParent);

// The same for a root from the recent list; on failure offers to drop the entry
CommitWindow* openRecentRepository(const QString& root, QWidget* dialogParent);

// Asks for a directory and opens the repository containing it. Null when cancelled, as when nothing claimed it.
CommitWindow* browseForRepository(QWidget* dialogParent);

// Asks for a folder and adds the repositories one level inside it to the recent list, opening none, each
// placed by when it was last worked in. Nothing is run on them: see repositoriesInFolder(). Reports what
// the scan found either way.
void scanFolderForRepositories(QWidget* dialogParent);

// The welcome window, raised if one is already up. It deletes itself when closed, and any open through
// openRepositoryWindow() closes it.
void showWelcomeWindow();

// Opens the repository containing any folder dropped on `target`; its window is the dialogs' parent. Repeat
// for a child that takes drops of its own: a drag over it never reaches the window.
void acceptRepositoryFolderDrops(QWidget* target);
