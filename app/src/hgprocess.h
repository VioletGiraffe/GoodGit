#pragma once

#include "vcsprocess.h"

// Every hg invocation in the app goes through here: it names the executable and applies the invocation
// invariants - HGPLAIN=1 in the environment and `--config ui.interactive=False`, so a prompt nothing here
// would show fails instead of hanging. The queue, the streaming and the outcomes are Vcs::run's.
//
// The user's extensions are deliberately left enabled: they are part of their hg, exactly as their hooks
// are. One consequence is that a broken extension prints to stderr on every invocation, which the app
// shows verbatim when something else fails.
namespace Hg {

// See Vcs::run for what the callback guarantees and when it is skipped.
Vcs::Job* run(const QString& workDir, QStringList args, const QObject* context, Vcs::Callback callback,
	QByteArray stdinData = {});

// Blocking variant with the same invariants, for the one moment before the event loop exists
// (resolving the repo root at startup).
ProcessResult runSync(const QString& workDir, QStringList args, int timeoutMs = 10000);

// The invariant arguments alone, for the one command the app starts detached rather than through run()
[[nodiscard]] QStringList invariantArgs();

} // namespace Hg
