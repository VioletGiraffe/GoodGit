#include "vcsprocess.h"

#include <QPointer>
#include <QTemporaryFile>

#include <deque>

namespace {

// Only the last state of a line a progress meter kept rewriting is text; a CRLF ends its line as an LF does
QString collapseCarriageReturns(QString text)
{
	QStringList lines = text.split(QLatin1Char('\n'));
	for (QString& line : lines)
	{
		if (line.endsWith(QLatin1Char('\r')))
			line.chop(1);
		line = line.mid(line.lastIndexOf(QLatin1Char('\r')) + 1);
	}
	return lines.join(QLatin1Char('\n'));
}

} // namespace

QString ProcessResult::errorText() const
{
	if (outcome == ProcessOutcome::LaunchFailed)
		return QStringLiteral("Failed to launch %1. Check that %1 is installed and on PATH.").arg(toolName); // nothing ran, so there is no stderr

	const QString stderrText = collapseCarriageReturns(QString::fromUtf8(err)).trimmed();
	if (outcome == ProcessOutcome::Exited)
		return stderrText.isEmpty() ? QStringLiteral("%1 exited with code %2").arg(toolName).arg(exitCode) : stderrText;

	// Died mid-run: whatever it managed to say still stands, but on its own it would read as the whole story
	const QString note = outcome == ProcessOutcome::Crashed
		? QStringLiteral("%1 terminated abnormally.").arg(toolName)
		: QStringLiteral("%1 did not finish within the time allowed and was stopped.").arg(toolName);
	return stderrText.isEmpty() ? note : note + QStringLiteral("\n\n") + stderrText;
}

namespace Vcs {

static constexpr int MaxConcurrentProcesses = 24;
static constexpr int KillWaitMs = 2000; // a killed process should be gone at once; this is only so the wait is bounded

// The process transport: one QProcess per job, capped by the queue below
class ProcessJob final : public Job
{
public:
	ProcessJob(Tool tool, QString workDir, QStringList args, QByteArray stdinData, const QObject* context, Callback callback) :
		Job{ std::move(tool), std::move(workDir), std::move(args), std::move(stdinData), context, std::move(callback) }
	{}

	void cancel() override;

private:
	void start();
	void finishProcess(ProcessResult result); // releases the queue slot, then delivers

private:
	QProcess* _process = nullptr;

	friend struct JobQueue;
};

struct JobQueue
{
	std::deque<ProcessJob*> pending;
	int running = 0;

	void pump()
	{
		while (running < MaxConcurrentProcesses && !pending.empty())
		{
			ProcessJob* job = pending.front();
			pending.pop_front();

			if (job->_hasContext && !job->_context)
			{
				job->deleteLater(); // nothing left to deliver the result to; starting the process would only hold a slot
				continue;
			}

			++running;
			job->start();
		}
	}

	void remove(ProcessJob* job)
	{
		std::erase(pending, job);
	}
};

static JobQueue s_queue;

Job::Job(Tool tool, QString workDir, QStringList args, QByteArray stdinData, const QObject* context, Callback callback) :
	_tool{ std::move(tool) },
	_workDir{ std::move(workDir) },
	_args{ std::move(args) },
	_stdinData{ std::move(stdinData) },
	_callback{ std::move(callback) },
	_context{ context },
	_hasContext{ context != nullptr }
{
}

Job* run(const Tool& tool, const QString& workDir, QStringList args, const QObject* context, Callback callback, QByteArray stdinData)
{
	auto* job = new ProcessJob{ tool, workDir, std::move(args), std::move(stdinData), context, std::move(callback) };
	s_queue.pending.push_back(job);
	s_queue.pump();
	return job;
}

void ProcessJob::cancel()
{
	_cancelled = true;
	if (_process)
		_process->kill(); // finishProcess() runs from the finished/error signal and self-deletes
	else
	{
		s_queue.remove(this);
		deleteLater();
	}
}

void Job::streamTo(std::function<void(const QByteArray&)> sink)
{
	_sink = std::move(sink);
}

void Job::collect(const QByteArray& chunk, QByteArray& buffer)
{
	if (chunk.isEmpty())
		return;

	buffer += chunk;
	if (_sink && !_cancelled && (!_hasContext || _context))
		_sink(chunk);
}

void ProcessJob::start()
{
	_process = new QProcess(this);
	_process->setWorkingDirectory(_workDir);
	_process->setProcessEnvironment(_tool.environment);

	QObject::connect(_process, &QProcess::readyReadStandardOutput, this, [this] { collect(_process->readAllStandardOutput(), _out); });
	QObject::connect(_process, &QProcess::readyReadStandardError, this, [this] { collect(_process->readAllStandardError(), _err); });

	QObject::connect(_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
		collect(_process->readAllStandardOutput(), _out); // whatever exit raced past the last readyRead
		collect(_process->readAllStandardError(), _err);

		ProcessResult result;
		result.out = std::move(_out);
		result.err = std::move(_err);
		if (status == QProcess::NormalExit)
		{
			result.outcome = ProcessOutcome::Exited;
			result.exitCode = exitCode;
			result.ok = exitCode == 0;
		}
		else
			result.outcome = ProcessOutcome::Crashed; // the exit code a killed process carries describes the kill, not the command
		finishProcess(std::move(result));
	});
	// Queued: start() emits this synchronously when the OS refuses the launch, and run()'s contract is a
	// callback from the event loop - callers store the returned Job and count their outstanding ones.
	QObject::connect(_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart)
			return; // crashes arrive via finished()
		ProcessResult result;
		result.outcome = ProcessOutcome::LaunchFailed;
		finishProcess(std::move(result));
	}, Qt::QueuedConnection);

	_process->start(_tool.executable, _args);
	// Refused synchronously: the write channel is closed, and the queued errorOccurred handler owns the result
	if (_process->state() == QProcess::NotRunning)
		return;

	if (!_stdinData.isEmpty())
		_process->write(_stdinData);
	_process->closeWriteChannel();
}

ProcessResult runSync(const Tool& tool, const QString& workDir, QStringList args, int timeoutMs)
{
	QProcess process;
	process.setWorkingDirectory(workDir);
	process.setProcessEnvironment(tool.environment);
	process.start(tool.executable, args);
	process.closeWriteChannel();

	ProcessResult result;
	result.toolName = tool.displayName;
	if (!process.waitForFinished(timeoutMs))
	{
		// The wait fails just the same when there was never a process to wait for
		if (process.error() == QProcess::FailedToStart)
		{
			result.outcome = ProcessOutcome::LaunchFailed;
			return result;
		}
		// Or ~QProcess does it instead, from a destructor and after printing a warning of its own
		process.kill();
		process.waitForFinished(KillWaitMs);
		result.outcome = ProcessOutcome::TimedOut;
	}
	else if (process.exitStatus() == QProcess::NormalExit)
	{
		result.outcome = ProcessOutcome::Exited;
		result.exitCode = process.exitCode();
		result.ok = result.exitCode == 0;
	}
	else
		result.outcome = ProcessOutcome::Crashed;

	// Whatever reached the pipes before it stopped, however it stopped
	result.out = process.readAllStandardOutput();
	result.err = process.readAllStandardError();
	return result;
}

QByteArray nulJoined(const QStringList& paths)
{
	QByteArray data;
	for (const QString& path : paths)
	{
		data += path.toUtf8();
		data += '\0';
	}
	return data;
}

std::shared_ptr<QTemporaryFile> openTempFile(const QByteArray& contents, const QString& description,
	QObject* context, const Callback& onFailure)
{
	auto file = std::make_shared<QTemporaryFile>();
	if (!file->open())
	{
		QMetaObject::invokeMethod(context, [onFailure, description] {
			// The default outcome is Exited, which is what makes errorText() report this err rather than
			// a process failure the app never had
			onFailure(ProcessResult{ .err = QStringLiteral("Failed to create the %1 temp file").arg(description).toUtf8() });
		}, Qt::QueuedConnection);
		return nullptr;
	}

	file->write(contents);
	file->close(); // release the handle for the tool; the file lives as long as this pointer does
	return file;
}

void Job::finish(ProcessResult result)
{
	result.toolName = _tool.displayName;

	if (!_cancelled && _callback && (!_hasContext || _context))
		_callback(result);

	deleteLater();
}

void ProcessJob::finishProcess(ProcessResult result)
{
	// The slot frees before the callback runs, so a job it enqueues starts without waiting on this one
	--s_queue.running;
	s_queue.pump();
	finish(std::move(result));
}

} // namespace Vcs
