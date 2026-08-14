#include "gitprocess.h"
#include "settings.h"

#include <QPointer>
#include <QProcessEnvironment>

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

QString GitResult::errorText() const
{
	if (launchFailed)
		return QStringLiteral("Failed to launch git. Check that git is installed and on PATH.");

	QString text = collapseCarriageReturns(QString::fromUtf8(err)).trimmed();
	if (text.isEmpty())
		text = QStringLiteral("git exited with code %1").arg(exitCode);
	return text;
}

namespace Git {

static constexpr int MaxConcurrentProcesses = 8;

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

// The doc/ARCHITECTURE.md invocation invariants, applied to every invocation - async and sync alike
static void applyInvariants(QStringList& args, bool readOnlyQuery)
{
	if (readOnlyQuery)
		args.prepend(QStringLiteral("--no-optional-locks"));
	args.prepend(QStringLiteral("core.quotepath=false"));
	args.prepend(QStringLiteral("-c"));
}

static QProcessEnvironment gitEnvironment()
{
	auto env = QProcessEnvironment::systemEnvironment();
	env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
	return env;
}

Job* run(const QString& workDir, QStringList args, const QObject* context, Callback callback, QByteArray stdinData, bool readOnlyQuery)
{
	auto* job = new Job;
	job->_workDir = workDir;
	job->_args = std::move(args);
	applyInvariants(job->_args, readOnlyQuery);
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
	_process->setProcessEnvironment(gitEnvironment());

	QObject::connect(_process, &QProcess::readyReadStandardOutput, this, [this] { collect(_process->readAllStandardOutput(), _out); });
	QObject::connect(_process, &QProcess::readyReadStandardError, this, [this] { collect(_process->readAllStandardError(), _err); });

	QObject::connect(_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
		collect(_process->readAllStandardOutput(), _out); // whatever exit raced past the last readyRead
		collect(_process->readAllStandardError(), _err);

		GitResult result;
		result.exitCode = exitCode;
		result.out = std::move(_out);
		result.err = std::move(_err);
		result.launchFailed = status != QProcess::NormalExit;
		result.ok = !result.launchFailed && exitCode == 0;
		finish(std::move(result));
	});
	// Queued: start() emits this synchronously when the OS refuses the launch, and run()'s contract is a
	// callback from the event loop - callers store the returned Job and count their outstanding ones.
	QObject::connect(_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart)
			return; // crashes arrive via finished()
		GitResult result;
		result.launchFailed = true;
		finish(std::move(result));
	}, Qt::QueuedConnection);

	_process->start(Settings::gitExecutable(), _args);
	// Refused synchronously: the write channel is closed, and the queued errorOccurred handler owns the result
	if (_process->state() == QProcess::NotRunning)
		return;

	if (!_stdinData.isEmpty())
		_process->write(_stdinData);
	_process->closeWriteChannel();
}

GitResult runSync(const QString& workDir, QStringList args, int timeoutMs)
{
	applyInvariants(args, /*readOnlyQuery=*/true);

	const QString git = Settings::gitExecutable();
	QProcess process;
	process.setWorkingDirectory(workDir);
	process.setProcessEnvironment(gitEnvironment());
	process.start(git, args);
	process.closeWriteChannel();

	GitResult result;
	if (!process.waitForFinished(timeoutMs))
	{
		result.launchFailed = true;
		result.err = process.readAllStandardError();
		return result;
	}
	result.exitCode = process.exitCode();
	result.out = process.readAllStandardOutput();
	result.err = process.readAllStandardError();
	result.launchFailed = process.exitStatus() != QProcess::NormalExit;
	result.ok = !result.launchFailed && result.exitCode == 0;
	return result;
}

void Job::finish(GitResult result)
{
	--s_queue.running;
	s_queue.pump();

	if (!_cancelled && _callback && (!_hasContext || _context))
		_callback(result);

	deleteLater();
}

} // namespace Git
