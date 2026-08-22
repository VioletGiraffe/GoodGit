#pragma once

#include "repository.h"

#include <expected>
#include <memory>
#include <vector>

#include <stdint.h>

// The only place that knows every backend

// The repository containing `startPath`, of whichever kind claims it.
// On failure, each backend's result in the order tried; the caller tells "not a repository" from "no tool could run".
// Blocking: this runs before the event loop exists.
[[nodiscard]] std::expected<RepositoryLocation, std::vector<ProcessResult>> findRepository(const QString& startPath);

// A repository a scan found, and when it was last worked in, in the milliseconds since the epoch that
// RecentRepository stores
struct FoundRepository
{
	RepositoryLocation location;
	int64_t lastUsedMSecs;
};

// The repositories directly inside `folder`, one level down. Found by the marker a working tree's root
// carries - .git, .hg - and by what that marker must hold, rather than by asking a tool: a folder of
// repositories would cost a process each. Most recently worked in first, by the file everyday work rewrites.
[[nodiscard]] std::vector<FoundRepository> repositoriesInFolder(const QString& folder);

// No QObject parent: the caller owns it
[[nodiscard]] std::unique_ptr<Repository> openRepository(const RepositoryLocation& location);
