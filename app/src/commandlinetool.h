#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <expected>

class QWidget;

// Whether the gg link is absent or dangling: the two states installCommandLineTool() can fix. A link
// that resolves counts as present, wherever it points - another copy of GoodGit, or another program entirely.
[[nodiscard]] bool commandLineToolLinkMissingOrBroken();

// Points a gg link at this executable, creating its directory:
//   macOS - /usr/local/bin/gg: on the default PATH but not user-writable on every Mac; where it is not, macOS asks for
//           administrator credentials through its own dialog.
//   other - ~/.local/bin/gg: on PATH depending on the distribution and shell; the message says how to add it where it is not.
// Both the value and the error are messages for the user. The value also covers the outcomes where nothing
// was done: already installed, or cancelled.
[[nodiscard]] std::expected<QString, QString> installCommandLineTool();

// Installs and shows the message over `dialogParent`. Every outcome is a message, the ones that did nothing
// included.
void installCommandLineToolAndReport(QWidget* dialogParent);
