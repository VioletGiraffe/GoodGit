#include "vcsprocess.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryFile>
RESTORE_COMPILER_WARNINGS

#include <deque>

namespace {

// Keeps only the final state of lines a progress meter rewrote with CR
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
	{
		const bool bareName = QFileInfo{ executable }.fileName() == executable; // looked up on PATH
		const QString what = bareName
			? QObject::tr("Failed to launch %1: %2").arg(toolName, launchError)
			: QObject::tr("Failed to launch %1 from \"%2\": %3").arg(toolName, executable, launchError);
		const QString where = QObject::tr("Working directory: %1").arg(QDir::toNativeSeparators(workDir));
		const QString advice = bareName
			? QObject::tr("Check that %1 is installed and on PATH, or set its full path in Preferences.").arg(toolName)
			: QObject::tr("Check the %1 path set in Preferences.").arg(toolName);
		return QStringList{ what, where, advice }.join(QLatin1Char('\n'));
	}

	const QString stderrText = collapseCarriageReturns(QString::fromUtf8(err)).trimmed();
	if (outcome == ProcessOutcome::Exited)
	{
		if (!stderrText.isEmpty())
			return stderrText;

		// Some refusals go to stdout with stderr left empty, e.g. git's "nothing to commit"
		const QString stdoutText = collapseCarriageReturns(QString::fromUtf8(out)).trimmed();
		if (!stdoutText.isEmpty())
			return stdoutText;

		return QStringLiteral("%1 exited with code %2").arg(toolName).arg(exitCode);
	}

	const QString note = outcome == ProcessOutcome::Crashed
		? QStringLiteral("%1 terminated abnormally.").arg(toolName)
		: QStringLiteral("%1 did not finish within the time allowed and was stopped.").arg(toolName);
	return stderrText.isEmpty() ? note : note + QStringLiteral("\n\n") + stderrText;
}

namespace Vcs {

static constexpr int MaxConcurrentProcesses = 24;
static constexpr int KillWaitMs = 2000; // only bounds the wait; a killed process should be gone at once

// The process transport: one QProcess per job, capped by JobQueue
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
				job->deleteLater(); // nobody left to deliver to
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
		collect(_process->readAllStandardOutput(), _out); // whatever arrived after the last readyRead
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
			result.outcome = ProcessOutcome::Crashed; // a killed process's exit code describes the kill, not the command
		finishProcess(std::move(result));
	});
	// Queued: start() emits this synchronously when the OS refuses the launch, and run() promises a callback
	// from the event loop - callers store the returned Job first
	QObject::connect(_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart)
			return; // crashes arrive via finished()
		ProcessResult result;
		result.outcome = ProcessOutcome::LaunchFailed;
		result.launchError = _process->errorString();
		finishProcess(std::move(result));
	}, Qt::QueuedConnection);

	_process->start(_tool.executable, _args);
	if (_process->state() == QProcess::NotRunning)
		return; // launch refused; the queued errorOccurred handler delivers the result

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
	result.executable = tool.executable;
	result.workDir = workDir;
	if (!process.waitForFinished(timeoutMs))
	{
		// The wait also fails when the process never started
		if (process.error() == QProcess::FailedToStart)
		{
			result.outcome = ProcessOutcome::LaunchFailed;
			result.launchError = process.errorString();
			return result;
		}
		// Otherwise ~QProcess kills it, with a warning
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
			// The default outcome, Exited, makes errorText() return this err
			onFailure(ProcessResult{ .err = QObject::tr("Failed to create the %1 temp file").arg(description).toUtf8() });
		}, Qt::QueuedConnection);
		return nullptr;
	}

	file->write(contents);
	file->close(); // releases the handle for the tool; the file lives as long as the QTemporaryFile does
	return file;
}

void Job::finish(ProcessResult result)
{
	result.toolName = _tool.displayName;
	result.executable = _tool.executable;
	result.workDir = _workDir;

	if (!_cancelled && _callback && (!_hasContext || _context))
		_callback(result);

	deleteLater();
}

void ProcessJob::finishProcess(ProcessResult result)
{
	// The slot is freed before the callback runs, so a job the callback enqueues starts at once
	--s_queue.running;
	s_queue.pump();
	finish(std::move(result));
}

} // namespace Vcs
