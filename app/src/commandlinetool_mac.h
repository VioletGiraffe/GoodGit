#pragma once

#include <QString>

#include <expected>

// Points /usr/local/bin/gg at this executable.
// /usr/local/bin is on the default PATH but not user-writable on every Mac; where it is not, macOS asks for
// administrator credentials through its own dialog.
// Both the value and the error are messages for the user. The value also covers the outcomes where nothing
// was done: already installed, or cancelled.
[[nodiscard]] std::expected<QString, QString> installCommandLineTool();
