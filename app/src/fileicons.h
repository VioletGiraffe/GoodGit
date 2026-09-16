#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
#include <QString>
RESTORE_COMPILER_WARNINGS

// The system's icon for a file's type, or the style's generic file icon where the system has none. Only the file
// name is read: the file need not exist, and no shell overlay badge of one file lands on the others of its type.
// Cached per type and rendered once, so a repaint costs a hash lookup. GUI thread only: the cache is unguarded.
[[nodiscard]] QIcon fileTypeIcon(const QString& path);
