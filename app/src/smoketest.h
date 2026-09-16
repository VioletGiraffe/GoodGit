#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

class QApplication;

// `gg --smoke-test <repository>`, for CI: runs the deployed binary on a fixture repository and exits.
// Exit code 0 once the repository's file list shows a row; 1 with the reason on stderr otherwise.
inline constexpr const char* SmokeTestOption = "--smoke-test";

// Takes the place of the normal startup after the QApplication exists: the theme, not the update check.
// Settings go to a throwaway directory, so a run never touches the user's.
// Returns false where the run already failed; otherwise the event loop finishes it.
[[nodiscard]] bool startSmokeTest(QApplication& app, const QString& repositoryPath);
