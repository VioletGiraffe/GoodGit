#pragma once

#include "vcsprocess.h"

#include <stdint.h>

// Every hg invocation goes through here. The job contract is Vcs::Job's, whichever transport carries it.
// Invariants applied to every call:
//   HGPLAIN=1
//   --config ui.interactive=False: a prompt nobody would see fails instead of hanging
//   --config diff.nobinary=True: no diff carries a binary file's contents
// The user's extensions stay enabled, like their hooks. A broken extension prints to stderr when the
// interpreter loads, which the app shows verbatim when something else fails.
namespace Hg {

// Server is the default: a warm command server skips the interpreter startup every fresh process pays.
// Process is for a command that must own one: push streams its progress and must be kill-cancellable.
enum class Transport : uint8_t { Server, Process };

Vcs::Job* run(const QString& workDir, QStringList args, const QObject* context, Vcs::Callback callback,
	QByteArray stdinData = {}, Transport transport = Transport::Server);

// Blocking variant with the same invariants. Read-only queries only, and only where the answer is needed
// before the next line: the repository root at startup, before the event loop exists, and the plan a
// destructive action shows the user before it runs. Every call pays Python startup: the command server
// carries run() alone.
ProcessResult runSync(const QString& workDir, QStringList args, int timeoutMs = 10000);

// For the one command started detached rather than through run()
[[nodiscard]] QStringList invariantArgs();

} // namespace Hg
