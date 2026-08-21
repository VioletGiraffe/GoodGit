#pragma once

#include "repository.h"

#include <expected>
#include <memory>
#include <vector>

// The only place that knows every backend

// The repository containing `startPath`, of whichever kind claims it.
// On failure, each backend's result in the order tried; the caller tells "not a repository" from "no tool could run".
// Blocking: this runs before the event loop exists.
[[nodiscard]] std::expected<RepositoryLocation, std::vector<ProcessResult>> findRepository(const QString& startPath);

// No QObject parent: the caller owns it
[[nodiscard]] std::unique_ptr<Repository> openRepository(const RepositoryLocation& location);
