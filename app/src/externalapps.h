#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

class QWidget;

// Handing a path to a program outside the application: the platform's file manager, and the text editor the
// user configured.
// The file manager action texts name it and the operation as each platform's users do.

[[nodiscard]] QString openInFileManagerActionText();
[[nodiscard]] QString showInFileManagerActionText();

// Opens a file manager window on `directory`
void openInFileManager(const QString& directory);

// Opens the directory holding `path` with `path` selected in it. Selecting is Windows and macOS only:
// elsewhere the directory alone is opened.
void showInFileManager(const QString& path);

// Runs the configured text editor command on `path`, its %path% replaced by that file; a command without
// the placeholder gets the path as its last argument. An empty command (the default where no editor ships
// with every install) and one that will not start are both reported to the user.
void openInTextEditor(const QString& path, QWidget* dialogParent);
