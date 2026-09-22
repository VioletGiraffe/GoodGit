#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <expected>

enum class Elevated { Ran, Cancelled };

// Creates `linkPath` and its directory as root, pointing at `target` and replacing a link already there.
// Blocks until the macOS credentials dialog is dismissed. The error is a message for the user.
[[nodiscard]] std::expected<Elevated, QString> createLinkAsAdministrator(const QString& target, const QString& linkPath);
