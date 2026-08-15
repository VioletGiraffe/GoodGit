#pragma once

#include "repository.h"

#include <expected>
#include <memory>

// Where the backends are named, and the only place that knows every one of them.

// The repository containing `startPath`, of whichever kind claims it, or why nothing does. Blocking:
// this runs before the event loop exists, and everything after it is asynchronous.
[[nodiscard]] std::expected<RepositoryLocation, QString> findRepository(const QString& startPath);

// A Repository of that kind, rooted there. Given no QObject parent: whoever opens one owns it.
[[nodiscard]] std::unique_ptr<Repository> openRepository(const RepositoryLocation& location);
