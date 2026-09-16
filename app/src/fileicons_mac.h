#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QPixmap>
#include <QString>
RESTORE_COMPILER_WARNINGS

// The icon of the type an extension names, square, `pixelSize` at a device pixel ratio of 1.
// Not QFileIconProvider: on macOS it resolves the icon from the file on disk, so a missing file gets the generic one.
[[nodiscard]] QPixmap macFileTypePixmap(const QString& extension, int pixelSize);
