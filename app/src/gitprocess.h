#pragma once

#include "vcsprocess.h"

// Every git invocation in the app goes through here: it names the executable and applies the invocation
// invariants from doc/ARCHITECTURE.md - `-c core.quotepath=false`, GIT_TERMINAL_PROMPT=0, and
// `--no-optional-locks` on read-only queries. The queue, the streaming and the outcomes are Vcs::run's.
namespace Git {

// See Vcs::run for what the callback guarantees and when it is skipped.
Vcs::Job* run(const QString& workDir, QStringList args, const QObject* context, Vcs::Callback callback,
	QByteArray stdinData = {}, bool readOnlyQuery = false);

// Blocking variant with the same invariants, for the one moment before the event loop exists
// (resolving the repo root at startup). Read-only queries only; everything else goes through run().
ProcessResult runSync(const QString& workDir, QStringList args, int timeoutMs = 10000);

} // namespace Git
