#pragma once

#include "repository.h"

#include <expected>
#include <functional>
#include <memory>
#include <vector>

#include <stdint.h>

// The only place that knows every backend

// The repository containing `startPath`, of whichever kind claims it.
// On failure, each backend's result in the order tried; the caller tells "not a repository" from "no tool could run".
// Blocking: this runs before the event loop exists.
[[nodiscard]] std::expected<RepositoryLocation, std::vector<ProcessResult>> findRepository(const QString& startPath);

// A repository a scan found, the subrepos it declares, and when it was last worked in, in the milliseconds
// since the epoch that RecentRepository stores
struct FoundRepository
{
	RepositoryLocation location;
	std::vector<Subrepo> subrepos;
	int64_t lastUsedMSecs;
};

// The repositories directly inside `folder`, one level down, most recently worked in first.
// Which folders are repositories, of which kind, and when each was last worked in are read off the
// filesystem: the marker a working tree's root carries, what that marker must hold, and the timestamp of
// the file everyday work rewrites. Subrepos cost one `ls-files` per git repository, the same index the
// refresh reads; Mercurial declares its own in a file. The queries run through the same capped queue as
// everything else, so a large folder fans out and no more.
// A repository whose query fails is answered without subrepos, as one never opened is listed without them.
// `onDone` runs from the event loop once the last query has answered - or before this returns, when there
// was nothing to ask - and also when `context` dies mid-scan, with whatever had arrived.
void findRepositoriesInFolder(const QString& folder, const QObject* context,
	std::function<void(std::vector<FoundRepository>)> onDone);

// No QObject parent: the caller owns it
[[nodiscard]] std::unique_ptr<Repository> openRepository(const RepositoryLocation& location);
