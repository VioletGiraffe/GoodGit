#pragma once

#include "queryround.h"
#include "vcsprocess.h"

#include <optional>

// Every git invocation goes through here. The job contract is Vcs::run's.
// Invariants applied to every call (doc/ARCHITECTURE.md): `-c core.quotepath=false`, GIT_TERMINAL_PROMPT=0,
// and `--no-optional-locks` on read-only queries.
namespace Git {

// The configured executable, or the default. For the one command started detached rather than through
// run(), which alone applies the environment and invariants.
[[nodiscard]] QString executablePath();

Vcs::Job* run(const QString& workDir, QStringList args, const QObject* context, Vcs::Callback callback,
	QByteArray stdinData = {}, bool readOnlyQuery = false);

// Read-only queries that die with `context`; what a refresh, a push plan, a discard plan and the folder
// scan are made of
[[nodiscard]] QueryRound::Launcher readOnlyQueries(const QObject* context);

// Blocking variant with the same invariants. Read-only queries only, and only where the answer is needed
// before the next line: the repository root at startup, before the event loop exists, and the plan a
// destructive action shows the user before it runs.
ProcessResult runSync(const QString& workDir, QStringList args, int timeoutMs = 10000);

// The message for a git older than these invocations need, otherwise nullopt.
// Also nullopt when no version can be read: only a version known to be too old is reported.
// `workDir`: any existing directory - `--version` needs no repository.
[[nodiscard]] std::optional<QString> versionProblem(const QString& workDir);

} // namespace Git
