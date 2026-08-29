#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QProcessEnvironment>
#include <QString>
#include <QStringConverter>
#include <QStringList>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <expected>
#include <functional>
#include <memory>
#include <stdint.h>

class QTemporaryFile;

// Only Exited leaves an exit code to read
enum class ProcessOutcome : uint8_t
{
	Exited,         // ran to completion, with any exit code; also used for results the app synthesises itself
	LaunchFailed,   // the OS refused to start it: nothing by that name on PATH, or a missing working directory
	Crashed,        // started, then died without exiting - killed from outside, or a genuine crash
	TimedOut,       // runSync gave up waiting and stopped it
	OutputTooLarge, // the output passed the caller's limit; the rest was discarded, and `out` is empty
};

// How a tool's byte output becomes text. Git writes UTF-8 on every platform; hg writes the local 8-bit
// encoding - the ANSI codepage on Windows - which is also what it reads back (see Hg::localBytes).
enum class TextEncoding : uint8_t { Utf8, Local };

[[nodiscard]] inline QString decodedText(const QByteArray& bytes, TextEncoding encoding)
{
	return encoding == TextEncoding::Local ? QString::fromLocal8Bit(bytes) : QString::fromUtf8(bytes);
}

// For output decoded in chunks, where the decoder must carry state across a split multi-byte sequence
[[nodiscard]] inline QStringConverter::Encoding converterEncoding(TextEncoding encoding)
{
	return encoding == TextEncoding::Local ? QStringConverter::System : QStringConverter::Utf8;
}

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
	TextEncoding textEncoding = TextEncoding::Utf8; // what `out` and `err` are; copied from the tool that ran
	QString executable; // what was launched: a bare name looked up on PATH, or a configured path
	QString workDir;    // named in a launch failure, which may be about the directory as much as the executable
	QString launchError; // the OS's reason, with LaunchFailed (where there is no stderr)
	qint64 outputLimit = 0; // what was asked for, with OutputTooLarge; errorText() names it

	// stderr, or stdout where the tool reported its refusal there and left stderr empty. A process that did
	// not exit normally says so first.
	[[nodiscard]] QString errorText() const;
};

namespace Vcs {

// Bounds the wait for a killed process to exit; it should be gone at once
inline constexpr int KillWaitMs = 2000;

// The program one backend drives. This layer knows no version control system: arguments arrive with the
// backend's invariants already applied.
struct Tool
{
	QString executable;  // a bare name is looked up on PATH
	QString displayName; // what a failure calls it, whatever `executable` is
	QProcessEnvironment environment;
	TextEncoding textEncoding = TextEncoding::Utf8; // what this program writes to stdout and stderr
};

using Callback = std::function<void(const ProcessResult&)>;

// One queued asynchronous invocation, whatever transports it: a dedicated process (Vcs::run) or a command
// on a running hg command server (hgcommandserver). cancel() guarantees the callback will not fire.
class Job : public QObject
{
public:
	virtual void cancel() = 0;

	// The encoding of this job's output bytes, for a caller that decodes a stream itself
	[[nodiscard]] TextEncoding textEncoding() const { return _tool.textEncoding; }

	// Delivers output to `sink` as it arrives, both channels in arrival order; the result still carries all of it.
	// Attach before returning to the event loop: output only arrives from there, so nothing is missed.
	// Like the callback, the sink stops being called once `context` dies.
	void streamTo(std::function<void(const QByteArray&)> sink);

	// Refuses the answer past `maxBytes` of output rather than buffering it: a caller that would not use an
	// oversize answer must not hold one first. The result is then OutputTooLarge with empty `out`.
	// Attach before returning to the event loop, as with streamTo(). stderr is never limited: a failure needs
	// its diagnosis. Applies to the process now answering, so a query that runs several sets it per process.
	void limitOutput(qint64 maxBytes);

protected:
	Job(Tool tool, QString workDir, QStringList args, QByteArray stdinData, const QObject* context, Callback callback);

	// Buffers a chunk of stdout, forwarding it to the sink while the job is still wanted. Past the limit the
	// buffer is dropped and the job fails: see limitOutput().
	void collectOutput(const QByteArray& chunk);
	// The same for stderr, which no limit applies to
	void collectError(const QByteArray& chunk);
	// Called once the output limit is passed, for a transport that can stop the work it no longer wants
	virtual void stopProducingOutput() {}
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
	qint64 _outputLimit = 0; // 0: unlimited
	bool _outputTooLarge = false;
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

// The parse for a query whose output text is the answer: the overloaded QString::fromUtf8 cannot be
// passed as a callable
[[nodiscard]] inline QString outputAsText(const QByteArray& output)
{
	return QString::fromUtf8(output);
}

// The same for a one-value answer, without the trailing newline a command may or may not print after it
[[nodiscard]] inline QString outputAsTrimmedText(const QByteArray& output)
{
	return QString::fromUtf8(output.trimmed());
}

// An answer decided without running a command, still delivered from the event loop: like a run()
// callback, it never arrives before the asking call has returned. Skipped if `context` dies first.
// The returned job makes it cancellable, for a caller handing out a Query; everyone else ignores it.
template <typename Callable, typename Value>
Job* answerLater(const QObject* context, Callable onDone, Value answer)
{
	class QueuedAnswer final : public Job
	{
	public:
		QueuedAnswer(const QObject* context, Callable onDone, Value answer) :
			Job{ {}, {}, {}, {}, context, {} },
			_onDone{ std::move(onDone) },
			_answer{ std::move(answer) }
		{
			// Explicit this->: in a lambda within a local class, MSVC resolves bare members against the
			// enclosing function scope and fails (C2327)
			QTimer::singleShot(0, this, [this] {
				if (!this->_cancelled && (!this->_hasContext || this->_context))
					this->_onDone(std::move(this->_answer));
				this->deleteLater();
			});
		}

		void cancel() override { _cancelled = true; } // the queued delivery still runs, to self-delete

	private:
		Callable _onDone;
		Value _answer;
	};

	return new QueuedAnswer{ context, std::move(onDone), std::move(answer) };
}

// A temp file holding `contents`, for a command that takes input by file name (a commit message, a long pathspec).
// Null if the file could not be created; `onFailure` is then already queued, and fires from the event loop like
// every run() callback. `description` names the file in that failure.
// The file is removed when the last owner drops the pointer: hold it until the command has run.
std::shared_ptr<QTemporaryFile> openTempFile(const QByteArray& contents, const QString& description,
	QObject* context, const Callback& onFailure);

// The commit message in a temp file: it is the one argument with no bound on its length. Same failure
// contract as openTempFile. The caller encodes the message: the backends' encodings differ.
std::shared_ptr<QTemporaryFile> openMessageFile(const QByteArray& message, QObject* context, const Callback& onFailure);

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
