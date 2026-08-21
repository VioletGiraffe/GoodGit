#pragma once

#include "vcsprocess.h"

// Every git invocation goes through here. The job contract is Vcs::run's.
// Invariants applied to every call (doc/ARCHITECTURE.md): `-c core.quotepath=false`, GIT_TERMINAL_PROMPT=0,
// and `--no-optional-locks` on read-only queries.
namespace Git {

Vcs::Job* run(const QString& workDir, QStringList args, const QObject* context, Vcs::Callback callback,
	QByteArray stdinData = {}, bool readOnlyQuery = false);

// Blocking variant with the same invariants, for before the event loop exists (resolving the repo root at
// startup). Read-only queries only.
ProcessResult runSync(const QString& workDir, QStringList args, int timeoutMs = 10000);

} // namespace Git
