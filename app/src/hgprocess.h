#pragma once

#include "vcsprocess.h"

// Every hg invocation in the app goes through here: it names the executable and applies the invocation
// invariants - HGPLAIN=1 in the environment, `--config ui.interactive=False`, so a prompt nothing here
// would show fails instead of hanging, and `--config diff.nobinary=True`, so no diff can carry a binary
// file's contents. The job contract - streaming, cancellation, the outcomes - is Vcs::Job's, whichever
// transport carries the command.
//
// The user's extensions are deliberately left enabled: they are part of their hg, exactly as their hooks
// are. A broken extension prints to stderr when the interpreter loads - once per process, once per
// command-server start - which the app shows verbatim when something else fails.
namespace Hg {

// How a command travels. Server is the default: a warm command server skips the interpreter startup
// every fresh process pays. Process is for the command that must own one - push streams its progress
// and must be kill-cancellable mid-transfer.
enum class Transport : uint8_t { Server, Process };

// See Vcs::run for what the callback guarantees and when it is skipped.
Vcs::Job* run(const QString& workDir, QStringList args, const QObject* context, Vcs::Callback callback,
	QByteArray stdinData = {}, Transport transport = Transport::Server);

// Blocking variant with the same invariants, for the one moment before the event loop exists
// (resolving the repo root at startup).
ProcessResult runSync(const QString& workDir, QStringList args, int timeoutMs = 10000);

// The invariant arguments alone, for the one command the app starts detached rather than through run()
[[nodiscard]] QStringList invariantArgs();

} // namespace Hg
