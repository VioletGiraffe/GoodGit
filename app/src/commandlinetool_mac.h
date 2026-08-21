#pragma once

#include <QString>

#include <expected>

// Points /usr/local/bin/gg at this executable, so a terminal in any repository can launch the app by name.
// That location is on the default PATH, but is not user-writable on every Mac; where it is not, macOS asks
// for administrator credentials through its own dialog. Both the value and the error are messages to show
// the user - the value also covers the outcomes where nothing was done: already installed, or cancelled.
[[nodiscard]] std::expected<QString, QString> installCommandLineTool();
