#pragma once

#include "repository.h"

#include <expected>
#include <memory>
#include <vector>

// Where the backends are named, and the only place that knows every one of them.

// The repository containing `startPath`, of whichever kind claims it - or, where none does, what each
// backend was asked and what came of asking, in the order they were tried. Whether that amounts to "no
// repository here" or to "nothing could be asked" is read off those outcomes by whoever reports it.
// Blocking: this runs before the event loop exists, and everything after it is asynchronous.
[[nodiscard]] std::expected<RepositoryLocation, std::vector<ProcessResult>> findRepository(const QString& startPath);

// A Repository of that kind, rooted there. Given no QObject parent: whoever opens one owns it.
[[nodiscard]] std::unique_ptr<Repository> openRepository(const RepositoryLocation& location);
