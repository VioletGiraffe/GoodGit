#include "hgcommandserver.h"

#include <QTimer>
#include <QtEndian>

#include <algorithm>
#include <assert.h>
#include <utility>

namespace {

constexpr int CancelGraceMs = 2000;
constexpr int KillWaitMs = 2000; // a killed server should be gone at once; this is only so the wait is bounded

void appendBigEndianU32(QByteArray& buffer, quint32 value)
{
	const quint32 bigEndian = qToBigEndian(value);
	buffer.append(reinterpret_cast<const char*>(&bigEndian), 4);
}

} // namespace

namespace Hg {

ServerJob::ServerJob(Vcs::Tool tool, QString workDir, QStringList args, const QObject* context, Vcs::Callback callback) :
	Vcs::Job{ std::move(tool), std::move(workDir), std::move(args), {}, context, std::move(callback) }
{
}

void ServerJob::completed(int exitCode)
{
	_runningOn = nullptr;
	ProcessResult result;
	result.out = std::move(_out);
	result.err = std::move(_err);
	result.outcome = ProcessOutcome::Exited;
	result.exitCode = exitCode;
	result.ok = exitCode == 0;
	finish(std::move(result));
}

void ServerJob::failed(ProcessOutcome outcome, const QByteArray& serverStderr)
{
	_runningOn = nullptr;
	ProcessResult result;
	result.out = std::move(_out);
	result.err = _err + serverStderr;
	result.outcome = outcome;
	finish(std::move(result));
}

void ServerJob::cancel()
{
	_cancelled = true;
	if (!_runningOn)
	{
		HgServerPool::instance().removeQueued(this);
		deleteLater();
		return;
	}

	// Delivery is already suppressed; the command itself gets a grace period, since killing costs the
	// pool a warm server and most commands finish in tens of milliseconds anyway
	QTimer::singleShot(CancelGraceMs, _runningOn, [server = _runningOn, self = QPointer<ServerJob>{ this }] {
		if (self && server->currentJob() == self)
			server->killServer();
	});
}

} // namespace Hg

HgCommandServer::HgCommandServer(const Vcs::Tool& tool, QString bindRoot, HgServerPool& pool) :
	_pool{ pool },
	_bindRoot{ std::move(bindRoot) }
{
	_process = new QProcess(this);
	_process->setWorkingDirectory(_bindRoot);
	_process->setProcessEnvironment(tool.environment);

	connect(_process, &QProcess::readyReadStandardOutput, this, [this] {
		_buffer += _process->readAllStandardOutput();
		consumeChunks();
	});
	connect(_process, &QProcess::readyReadStandardError, this, [this] {
		_ownStderr += _process->readAllStandardError();
	});
	connect(_process, &QProcess::finished, this, [this] { died(); });
	// Queued: start() emits this synchronously when the OS refuses the launch, and failing the pool's
	// queue means firing job callbacks, which must come from the event loop
	connect(_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart)
			died(); // crashes arrive via finished()
	}, Qt::QueuedConnection);

	_process->start(tool.executable, { QStringLiteral("serve"), QStringLiteral("--cmdserver"), QStringLiteral("pipe"),
		QStringLiteral("-R"), _bindRoot });
}

HgCommandServer::~HgCommandServer()
{
	_dead = true; // killing emits finished from waitForFinished; died() must not run mid-destruction
	if (_process->state() != QProcess::NotRunning)
	{
		_process->kill();
		_process->waitForFinished(KillWaitMs);
	}
}

void HgCommandServer::execute(Hg::ServerJob* job)
{
	assert(idle());
	_currentJob = job;
	job->_runningOn = this;

	QStringList args = job->_args;
	// Without -R the dispatch stays on the bound repository whatever the cwd, and relative path
	// arguments resolve against the server's cwd - a foreign repository needs both named
	if (job->_workDir != _bindRoot)
		args = QStringList{ QStringLiteral("-R"), job->_workDir, QStringLiteral("--cwd"), job->_workDir } + args;

	// Local 8-bit, matching what hg reads off a real command line
	QByteArray block;
	for (const QString& arg : args)
	{
		if (!block.isEmpty())
			block += '\0';
		block += arg.toLocal8Bit();
	}

	QByteArray message = QByteArrayLiteral("runcommand\n");
	appendBigEndianU32(message, quint32(block.size()));
	message += block;
	_process->write(message);
}

void HgCommandServer::killServer()
{
	_buffer.clear(); // whatever else it holds is not worth parsing, and consumeChunks may still be in its loop
	_process->kill(); // died() runs from the finished signal
}

void HgCommandServer::consumeChunks()
{
	while (!_dead)
	{
		if (_buffer.size() < 5)
			return;
		const char channel = _buffer.at(0);
		const quint32 length = qFromBigEndian<quint32>(_buffer.constData() + 1);

		// An input request's length is how much input is wanted - no payload follows. Always answered
		// empty: ui.interactive=False means nothing should ask, and an unanswered request would
		// deadlock the pipe.
		if (channel == 'I' || channel == 'L')
		{
			_buffer.remove(0, 5);
			QByteArray empty;
			appendBigEndianU32(empty, 0);
			_process->write(empty);
			continue;
		}

		if (_buffer.size() - 5 < qsizetype(length))
			return;
		const QByteArray payload = _buffer.mid(5, qsizetype(length));
		_buffer.remove(0, 5 + qsizetype(length));
		handleChunk(channel, payload);
	}
}

void HgCommandServer::handleChunk(char channel, const QByteArray& payload)
{
	switch (channel)
	{
	case 'o':
		if (!_helloSeen)
			readHello(payload);
		else if (_currentJob)
			_currentJob->deliverOutput(payload);
		return;
	case 'e':
		if (_currentJob)
			_currentJob->deliverError(payload);
		return;
	case 'r':
		if (!_currentJob || payload.size() != 4)
		{
			killServer(); // a result outside a command, or malformed; died() fails the command if one runs
			return;
		}
		std::exchange(_currentJob, nullptr)->completed(qFromBigEndian<qint32>(payload.constData()));
		_pool.serverFreed(this);
		return;
	default:
		// A lowercase channel is optional and skippable; an uppercase one is required, so a protocol
		// this end does not know is a server this end cannot use
		if (channel >= 'A' && channel <= 'Z')
			killServer();
		return;
	}
}

void HgCommandServer::readHello(const QByteArray& payload)
{
	// "capabilities: ... runcommand ...\nencoding: ..." - runcommand is the one capability used
	if (!payload.contains("runcommand"))
	{
		killServer();
		return;
	}
	_helloSeen = true;
	_pool.serverReady(this);
}

void HgCommandServer::died()
{
	if (_dead)
		return;
	_dead = true;

	const ProcessOutcome outcome = _process->error() == QProcess::FailedToStart
		? ProcessOutcome::LaunchFailed : ProcessOutcome::Crashed;
	if (Hg::ServerJob* job = std::exchange(_currentJob, nullptr))
		job->failed(outcome, _ownStderr);
	_pool.serverDied(this, outcome);
}

HgServerPool& HgServerPool::instance()
{
	static HgServerPool pool;
	return pool;
}

Vcs::Job* HgServerPool::run(const Vcs::Tool& tool, const QString& workDir, QStringList args, const QObject* context,
	Vcs::Callback callback, QByteArray stdinData)
{
	if (_unavailable || !stdinData.isEmpty())
		return Vcs::run(tool, workDir, std::move(args), context, std::move(callback), std::move(stdinData));

	auto* job = new Hg::ServerJob{ tool, workDir, std::move(args), context, std::move(callback) };
	_queue.push_back(job);
	dispatch();
	return job;
}

void HgServerPool::dispatch()
{
	while (!_queue.empty())
	{
		Hg::ServerJob* job = _queue.front();
		if (job->abandonedWhileQueued())
		{
			_queue.pop_front();
			job->deleteLater(); // nothing left to deliver the result to, as the process queue discards too
			continue;
		}

		HgCommandServer* chosen = nullptr;
		for (const auto& server : _servers)
		{
			if (!server->idle())
				continue;
			if (server->bindRoot() == job->_workDir)
			{
				chosen = server.get();
				break;
			}
			if (!chosen)
				chosen = server.get();
		}

		if (!chosen)
		{
			// Nothing idle: grow toward the demand - a server per waiting job, up to the cap. All bind to
			// the front job's repository; on a fresh pool that is the repository whose refresh caused the
			// burst, so the warm caches land where most commands go.
			const size_t wanted = std::min<size_t>(MaxServers, _servers.size() + _queue.size());
			while (_servers.size() < wanted)
				_servers.push_back(std::make_unique<HgCommandServer>(job->_tool, job->_workDir, *this));
			return;
		}

		_queue.pop_front();
		chosen->execute(job);
	}
}

void HgServerPool::removeQueued(Hg::ServerJob* job)
{
	std::erase(_queue, job);
}

void HgServerPool::serverReady(HgCommandServer* /*server*/)
{
	_everReady = true;
	dispatch();
}

void HgServerPool::serverFreed(HgCommandServer* /*server*/)
{
	dispatch();
}

void HgServerPool::serverDied(HgCommandServer* server, ProcessOutcome outcome)
{
	const QByteArray serverStderr = server->ownStderr();

	// Called from inside the server's own signal handler: out of the pool now, deleted from the event loop
	const auto owned = std::ranges::find_if(_servers, [server](const std::unique_ptr<HgCommandServer>& s) { return s.get() == server; });
	if (owned != _servers.end())
	{
		owned->release()->deleteLater();
		_servers.erase(owned);
	}

	if (_everReady)
	{
		dispatch(); // respawns on demand if the queue calls for it
		return;
	}

	// No server has ever come up, so this hg cannot serve: fail what waited, run everything as processes from here on
	_unavailable = true;
	const std::deque<Hg::ServerJob*> stranded = std::move(_queue);
	_queue.clear();
	for (Hg::ServerJob* job : stranded)
		job->failed(outcome, serverStderr);
}
