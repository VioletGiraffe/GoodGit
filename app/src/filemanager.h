#pragma once

#include <QString>

// The platform's file manager: Explorer, Finder, or whatever handles a directory elsewhere.
// The action texts name it and the operation as each platform's users do.

[[nodiscard]] QString openInFileManagerActionText();
[[nodiscard]] QString showInFileManagerActionText();

// Opens a file manager window on `directory`
void openInFileManager(const QString& directory);

// Opens the directory holding `path` with `path` selected in it. Selecting is Windows and macOS only:
// elsewhere the directory alone is opened.
void showInFileManager(const QString& path);
