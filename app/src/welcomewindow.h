#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QWidget>
RESTORE_COMPILER_WARNINGS

// Shown when the application starts with nothing to open: what it needs from the user, a button that asks
// for a folder, and the recent list when there is one. A folder dropped on it opens like a chosen one.
// The one window that exists without a repository; openRepositoryWindow() closes it once one does.
class WelcomeWindow final : public QWidget
{
public:
	WelcomeWindow();
};
