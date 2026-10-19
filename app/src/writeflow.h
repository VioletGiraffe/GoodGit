#pragma once

#include "repository.h"

class QWidget;

// What the write flows of every window share

// True when `repository` no longer matches `stamp` (Repository::stateMatches()).
// Tells the user in a dialog over `dialogParent` and queues a refresh before returning true.
[[nodiscard]] bool stateMovedSince(QWidget* dialogParent, Repository& repository, const Repository::StateStamp& stamp);
