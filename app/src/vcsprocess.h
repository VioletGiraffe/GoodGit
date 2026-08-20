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

// How the process ended. Only the first of these leaves an exit code behind to be read.
enum class ProcessOutcome : uint8_t
{
	Exited,       // ran to completion, whatever it then reported - also a result the app synthesises itself
	LaunchFailed, // the OS refused to start it: nothing by that name on PATH, or a working directory that is gone
	Crashed,      // started, then died without exiting - killed from outside, or a genuine crash
	TimedOut,     // runSync gave up waiting and stopped it
};

struct ProcessResult
{
	int exitCode = -1; // meaningful only when `outcome` is Exited
	QByteArray out;
	QByteArray err;
	ProcessOutcome outcome = ProcessOutcome::Exited;
	// The command succeeded. Set here from exit code 0, but whoever ran a command that reports success
	// otherwise (exit 1 for "nothing to report", say) corrects it before the result travels on, so that
	// every reader can treat this as the answer.
	bool ok = false;
	// Names the program in the sentences errorText() composes itself. Every result a process produced
	// carries it; a synthesised one carries stderr of its own instead, which is what errorText then returns.
	QString toolName;
	// What was launched under that name: a bare name looked up on PATH, or the path configured for it
	QString executable;
	// The directory it was to run in. Named in a launch failure, which can be about this as readily as
	// about the executable, and the OS's reason names neither.
	QString workDir;
	// The OS's account of refusing to start it. Carried with LaunchFailed, where there is no stderr to have.
	QString launchError;

	// The failure as text: stderr, or stdout where the tool reported its refusal there and left stderr empty.
	// A process that did not exit normally says so first, since its stderr cannot account for the output that
	// never came.
	[[nodiscard]] QString errorText() const;
};

namespace Vcs {

// The program one backend drives. Arguments arrive here already carrying whatever invariants that
// backend applies - this layer adds none of its own and knows no version control system.
struct Tool
{
	QString executable;  // a bare name is looked up on PATH
	QString displayName; // what a failure calls it, whatever path `executable` took
	QProcessEnvironment environment;
};

using Callback = std::function<void(const ProcessResult&)>;

// One queued asynchronous invocation, whatever transports it: a process of its own (Vcs::run), or a
// command on a running hg command server (hgcommandserver). cancel() guarantees the callback will not fire.
class Job : public QObject
{
public:
	virtual void cancel() = 0;

	// Delivers output to `sink` as it arrives, both channels in arrival order, on top of the complete
	// output the result still carries. Attach it before returning to the event loop - output arrives
	// only from there, so nothing has been read yet and nothing to come is missed. Like the result
	// callback, the sink stops being called once `context` dies.
	void streamTo(std::function<void(const QByteArray&)> sink);

protected:
	Job(Tool tool, QString workDir, QStringList args, QByteArray stdinData, const QObject* context, Callback callback);

	// Buffers a chunk of one output channel, forwarding it to the sink while the job is still wanted
	void collect(const QByteArray& chunk, QByteArray& buffer);
	// Delivers the result per the contract - skipped after cancel() or the context's death - and
	// self-deletes. The transport is done with the job by the time it calls this.
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

// How a query answers: what it was asked for, or why it cannot be had. Taken by value - the answer
// belongs to whoever asked, to move into their model.
template <typename T>
using Answer = std::function<void(std::expected<T, QString>)>;

// A cancellable read-only query. A backend may answer one with several processes in turn, so this
// names whichever is current rather than one of them: cancelling stops the query wherever it has got
// to. Copies name the same query; cancelling one that has already answered does nothing.
class Query
{
public:
	Query() = default;
	// The query as the job first answering it names it
	explicit Query(Job* job) { attach(job); }

	void cancel()
	{
		if (*_current)
			(*_current)->cancel();
	}

	// Backend side: the process now answering. A second one replaces the first, which has answered by then.
	void attach(Job* job) { *_current = job; }

private:
	// Shared, so the handle its asker holds follows the query into its next process
	std::shared_ptr<QPointer<Job>> _current = std::make_shared<QPointer<Job>>();
};

// One process's result as the answer a query promised: `parse` of its output, or the error text instead
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

// The same for a command run for its effect alone: that it worked, or why it did not
[[nodiscard]] inline Callback reporting(Answer<void> onDone)
{
	return [onDone = std::move(onDone)](const ProcessResult& result) {
		if (result.ok)
			onDone({});
		else
			onDone(std::unexpected(result.errorText()));
	};
}

// A path list as a tool takes one where a path may hold anything a text separator could - on stdin, or in
// a file named as an argument
[[nodiscard]] QByteArray nulJoined(const QStringList& paths);

// A temp file holding `contents`, for a command that takes its input by file name rather than on stdin -
// a message to commit, a pathspec too long for a command line. Null if the file could not be created, the
// failure already on its way to `onFailure`: like every run() callback, it has to reach the caller from the
// event loop rather than from inside this call. `description` names the file in that failure. The file is
// removed when the last owner drops it, so hold the pointer until the command has run.
std::shared_ptr<QTemporaryFile> openTempFile(const QByteArray& contents, const QString& description,
	QObject* context, const Callback& onFailure);

// Runs `tool` in workDir with the given arguments, capping how many processes may run at once - excess
// is queued. Output is accumulated as it arrives, so a chatty child cannot fill a pipe and stall.
// The callback fires on the GUI thread, from the event loop and so never before this returns; it is
// skipped if the job is cancelled or `context` dies.
// A job whose `context` died while it was still queued is discarded without ever running.
Job* run(const Tool& tool, const QString& workDir, QStringList args, const QObject* context, Callback callback,
	QByteArray stdinData = {});

// Blocking variant, for the one moment before the event loop exists (resolving the repo root at
// startup). Read-only queries only; everything else goes through run().
ProcessResult runSync(const Tool& tool, const QString& workDir, QStringList args, int timeoutMs = 10000);

} // namespace Vcs
