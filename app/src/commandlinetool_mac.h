#pragma once

#include <QString>

#include <expected>

class QWidget;

// Whether /usr/local/bin/gg is absent or dangling: the two states installCommandLineTool() can fix. A link
// that resolves counts as present, wherever it points - another copy of GoodGit, or another program entirely.
[[nodiscard]] bool commandLineToolLinkMissingOrBroken();

// Points /usr/local/bin/gg at this executable.
// /usr/local/bin is on the default PATH but not user-writable on every Mac; where it is not, macOS asks for
// administrator credentials through its own dialog.
// Both the value and the error are messages for the user. The value also covers the outcomes where nothing
// was done: already installed, or cancelled.
[[nodiscard]] std::expected<QString, QString> installCommandLineTool();

// Installs and shows the message over `dialogParent`. Every outcome is a message, the ones that did nothing
// included.
void installCommandLineToolAndReport(QWidget* dialogParent);
