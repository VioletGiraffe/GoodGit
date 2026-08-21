#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <expected>
#include <functional>
#include <memory>
#include <stdint.h>

class QTemporaryFile;

// Only Exited leaves an exit code to read
enum class ProcessOutcome : uint8_t
{
	Exited,       // ran to completion, with any exit code; also used for results the app synthesises itself
	LaunchFailed, // the OS refused to start it: nothing by that name on PATH, or a missing working directory
	Crashed,      // started, then died without exiting - killed from outside, or a genuine crash
	TimedOut,     // runSync gave up waiting and stopped it
};

struct ProcessResult
{
	int exitCode = -1; // meaningful only when `outcome` is Exited
	QByteArray out;
	QByteArray err;
	ProcessOutcome outcome = ProcessOutcome::Exited;
	// Set from exit code 0. A caller whose command reports success otherwise (e.g. exit 1 for "nothing to
	// report") corrects it before the result travels on, so every reader can rely on it.
	bool ok = false;
	// Names the program in errorText(). Every result a process produced carries it; a synthesised one
	// carries its own stderr instead, which errorText() then returns.
	QString toolName;
	QString executable; // what was launched: a bare name looked up on PATH, or a configured path
	QString workDir;    // named in a launch failure, which may be about the directory as much as the executable
	QString launchError; // the OS's reason, with LaunchFailed (where there is no stderr)

	// stderr, or stdout where the tool reported its refusal there and left stderr empty. A process that did
	// not exit normally says so first.
	[[nodiscard]] QString errorText() const;
};

namespace Vcs {

// The program one backend drives. This layer knows no version control system: arguments arrive with the
// backend's invariants already applied.
struct Tool
{
	QString executable;  // a bare name is looked up on PATH
	QString displayName; // what a failure calls it, whatever `executable` is
	QProcessEnvironment environment;
};

using Callback = std::function<void(const ProcessResult&)>;

// One queued asynchronous invocation, whatever transports it: a process of its own (Vcs::run) or a command
// on a running hg command server (hgcommandserver). cancel() guarantees the callback will not fire.
class Job : public QObject
{
public:
	virtual void cancel() = 0;

	// Delivers output to `sink` as it arrives, both channels in arrival order; the result still carries all of it.
	// Attach before returning to the event loop: output only arrives from there, so nothing is missed.
	// Like the callback, the sink stops being called once `context` dies.
	void streamTo(std::function<void(const QByteArray&)> sink);

protected:
	Job(Tool tool, QString workDir, QStringList args, QByteArray stdinData, const QObject* context, Callback callback);

	// Buffers a chunk of one output channel, forwarding it to the sink while the job is still wanted
	void collect(const QByteArray& chunk, QByteArray& buffer);
	// Delivers the result (skipped after cancel() or the context's death) and self-deletes. The transport
	// must be done with the job by then.
	void finish(ProcessResult result);

protected:
	Tool _tool;
	QString _workDir;
	QStringList _args;
	QByteArray _stdinData;
	Callback _callback;
	std::function<void(const QByteArray&)> _sink;
	QByteArray _out, _err; // filled as the command runs, so a sink and the result see the same bytes
	QPointer<const QObject> _context;
	bool _hasContext = false;
	bool _cancelled = false;
};

// The result, or why it cannot be had. Taken by value: the answer belongs to whoever asked, to move into
// their model.
template <typename T>
using Answer = std::function<void(std::expected<T, QString>)>;

// A cancellable read-only query.
// A backend may answer one with several processes in turn; this names whichever is current, so cancelling
// stops the query wherever it has got to.
// Copies name the same query. Cancelling one that has already answered does nothing.
class Query
{
public:
	Query() = default;
	explicit Query(Job* job) { attach(job); }

	void cancel()
	{
		if (*_current)
			(*_current)->cancel();
	}

	// Backend side: the process now answering. Replaces the previous one, which has answered by then.
	void attach(Job* job) { *_current = job; }

private:
	// Shared, so the asker's copy follows the query into its next process
	std::shared_ptr<QPointer<Job>> _current = std::make_shared<QPointer<Job>>();
};

// Adapts a process result to an Answer: `parse` of its output, or the error text
template <typename T, typename Parse>
[[nodiscard]] Callback answering(Answer<T> onDone, Parse parse)
{
	return [onDone = std::move(onDone), parse = std::move(parse)](const ProcessResult& result) {
		if (result.ok)
			onDone(parse(result.out));
		else
			onDone(std::unexpected(result.errorText()));
	};
}

// The same for a command run for its effect alone
[[nodiscard]] inline Callback reporting(Answer<void> onDone)
{
	return [onDone = std::move(onDone)](const ProcessResult& result) {
		if (result.ok)
			onDone({});
		else
			onDone(std::unexpected(result.errorText()));
	};
}

// NUL-separated path list, for passing paths that may contain any text separator - on stdin, or in a file
// named as an argument
[[nodiscard]] QByteArray nulJoined(const QStringList& paths);

// A temp file holding `contents`, for a command that takes input by file name (a commit message, a long pathspec).
// Null if the file could not be created; `onFailure` is then already queued, and fires from the event loop like
// every run() callback. `description` names the file in that failure.
// The file is removed when the last owner drops the pointer: hold it until the command has run.
std::shared_ptr<QTemporaryFile> openTempFile(const QByteArray& contents, const QString& description,
	QObject* context, const Callback& onFailure);

// Runs `tool` in workDir.
// Concurrent processes are capped; excess is queued. A queued job whose `context` dies is discarded unstarted.
// Output is read as it arrives: a chatty child cannot fill a pipe and stall.
// The callback fires from the event loop, never before this returns; skipped if the job is cancelled or `context` dies.
Job* run(const Tool& tool, const QString& workDir, QStringList args, const QObject* context, Callback callback,
	QByteArray stdinData = {});

// Blocking variant, for before the event loop exists (resolving the repo root at startup). Read-only
// queries only; everything else goes through run().
ProcessResult runSync(const Tool& tool, const QString& workDir, QStringList args, int timeoutMs = 10000);

} // namespace Vcs
