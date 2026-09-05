#pragma once

#include "repositorywindows.h" // ScanReport

class QAction;
class QMenu;
class QMenuBar;
class QWidget;

// The menus that are the same in every window: their actions need no repository, only a parent for the
// dialogs they open. Each call adds one menu to the bar.

// Returns the Open Repository action, which the Repository menu lists as well
[[nodiscard]] QAction* addFileMenu(QMenuBar& menuBar, QWidget* dialogParent);
void addEditMenu(QMenuBar& menuBar, QWidget* dialogParent);
// Open, Scan and Clear only; a window with a repository appends its own items to the menu returned
QMenu* addRepositoryMenu(QMenuBar& menuBar, QWidget* dialogParent, QAction* openAction, ScanReport scanReport);
void addHelpMenu(QMenuBar& menuBar, QWidget* dialogParent);
