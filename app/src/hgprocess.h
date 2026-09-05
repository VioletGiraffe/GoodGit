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

// Ends the command servers run() spawned; each finishes the command in flight first. Called once, after the
// event loop returns: nothing that could start a command is left by then.
void shutdown();

// The configured executable, or the default. For the one command started detached rather than through
// run(), which alone applies the environment.
[[nodiscard]] QString executablePath();

// For the one command started detached rather than through run()
[[nodiscard]] QStringList invariantArgs();

// The bytes hg reads for `text`: local 8-bit, with a lone surrogate U+DC80..DCFF becoming its original
// byte - the inverse of hg's utf8b encoding of undecodable path bytes (see jsonRecords in hgparsers.cpp).
// Every byte handed to hg goes through this: argv, listfile and logfile contents, ignore patterns.
[[nodiscard]] QByteArray localBytes(const QString& text);

} // namespace Hg
