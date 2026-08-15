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

static constexpr int MaxConcurrentProcesses = 8;
static constexpr int KillWaitMs = 2000; // a killed process should be gone at once; this is only so the wait is bounded

struct JobQueue
{
	std::deque<Job*> pending;
	int running = 0;

	void pump()
	{
		while (running < MaxConcurrentProcesses && !pending.empty())
		{
			Job* job = pending.front();
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

	void remove(Job* job)
	{
		std::erase(pending, job);
	}
};

static JobQueue s_queue;

Job* run(const Tool& tool, const QString& workDir, QStringList args, const QObject* context, Callback callback, QByteArray stdinData)
{
	auto* job = new Job;
	job->_tool = tool;
	job->_workDir = workDir;
	job->_args = std::move(args);
	job->_stdinData = std::move(stdinData);
	job->_callback = std::move(callback);
	job->_context = context;
	job->_hasContext = context != nullptr;

	s_queue.pending.push_back(job);
	s_queue.pump();
	return job;
}

void Job::cancel()
{
	_cancelled = true;
	if (_process)
		_process->kill(); // finish() runs from the finished/error signal and self-deletes
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

void Job::start()
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
		finish(std::move(result));
	});
	// Queued: start() emits this synchronously when the OS refuses the launch, and run()'s contract is a
	// callback from the event loop - callers store the returned Job and count their outstanding ones.
	QObject::connect(_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart)
			return; // crashes arrive via finished()
		ProcessResult result;
		result.outcome = ProcessOutcome::LaunchFailed;
		finish(std::move(result));
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

	--s_queue.running;
	s_queue.pump();

	if (!_cancelled && _callback && (!_hasContext || _context))
		_callback(result);

	deleteLater();
}

} // namespace Vcs
