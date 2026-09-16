#include "hgcommandserver.h"
#include "hgprocess.h"

#include "assert/advanced_assert.h"
#include "appdialogs/csettingsnotifier.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QProcess>
#include <QTimer>
#include <QtEndian>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <utility>

namespace {

constexpr int CancelGraceMs = 2000;
constexpr int IdleRetireMs = 5 * 60 * 1000;
// Short: closing a window is often followed by deleting or moving the folder, but a reopen soon after still finds the server warm
constexpr int ClosedRetireMs = 5000;
// The whole pool's budget for exiting at shutdown, not each server's: long enough for a commit in flight to finish
constexpr int ShutdownWaitMs = 5000;
// No hg command sends a frame this large; a length read out of a desynced stream almost always exceeds it
constexpr quint32 MaxFrameBytes = 1u << 30;

// The protocol's channels are single letters; anything else means the stream is off its frame boundary
bool isProtocolChannel(char channel)
{
	return (channel >= 'a' && channel <= 'z') || (channel >= 'A' && channel <= 'Z');
}

void appendBigEndianU32(QByteArray& buffer, quint32 value)
{
	const quint32 bigEndian = qToBigEndian(value);
	buffer.append(reinterpret_cast<const char*>(&bigEndian), 4);
}

// Paths compare as spelled: a server's bound root is a job's working directory, built from its repository's root
bool isInsideRoot(const QString& path, const QString& root)
{
	return path.startsWith(root) && (path.size() == root.size() || path.at(root.size()) == QLatin1Char('/'));
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

void ServerJob::failed(ProcessOutcome outcome, const QByteArray& serverStderr, const QString& launchError)
{
	_runningOn = nullptr;
	ProcessResult result;
	result.out = std::move(_out);
	result.err = _err + serverStderr;
	result.outcome = outcome;
	result.launchError = launchError;
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

	// Delivery is already suppressed; the command gets a grace period, since killing costs a warm server and
	// most commands finish in tens of milliseconds anyway
	QTimer::singleShot(CancelGraceMs, _runningOn, [server = _runningOn, self = QPointer<ServerJob>{ this }] {
		if (self && server->currentJob() == self)
			server->killServer();
	});
}

} // namespace Hg

HgCommandServer::HgCommandServer(const Vcs::Tool& tool, QString bindRoot, HgServerPool& pool) :
	_pool{ pool },
	_bindRoot{ std::move(bindRoot) },
	_executable{ tool.executable }
{
	_process = new QProcess(this);
	_process->setWorkingDirectory(_bindRoot);
	_process->setProcessEnvironment(tool.environment);

	_retireTimer = new QTimer(this);
	_retireTimer->setSingleShot(true);
	connect(_retireTimer, &QTimer::timeout, this, &HgCommandServer::retire);

	connect(_process, &QProcess::readyReadStandardOutput, this, [this] {
		_buffer += _process->readAllStandardOutput();
		consumeChunks();
	});
	connect(_process, &QProcess::readyReadStandardError, this, [this] {
		_ownStderr += _process->readAllStandardError();
	});
	connect(_process, &QProcess::finished, this, [this] { died(); });
	// Queued: start() emits this synchronously when the OS refuses the launch, and failing the queue fires job
	// callbacks, which must come from the event loop
	connect(_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart)
			died(); // crashes arrive via finished()
	}, Qt::QueuedConnection);

	_process->start(tool.executable, { QStringLiteral("serve"), QStringLiteral("--cmdserver"), QStringLiteral("pipe"),
		QStringLiteral("-R"), _bindRoot });
}

HgCommandServer::~HgCommandServer()
{
	_dead = true; // waitForFinished emits finished; died() must not run mid-destruction
	if (_process->state() != QProcess::NotRunning)
	{
		_process->kill();
		_process->waitForFinished(Vcs::KillWaitMs);
	}
}

void HgCommandServer::requestExit()
{
	// died() erases this server from the list shutdown() is walking, and delivers a result nobody is left to take
	_dead = true;
	_process->closeWriteChannel(); // the command server exits at stdin EOF, once the command in flight is done
}

void HgCommandServer::waitForExit(QDeadlineTimer deadline)
{
	_process->waitForFinished(int(deadline.remainingTime()));
}

void HgCommandServer::retire()
{
	_retiring = true;
	// The command server exits at stdin EOF, after the command in flight: its output is still read and delivered
	_process->closeWriteChannel();
}

void HgCommandServer::execute(Hg::ServerJob* job)
{
	assert_r(idle());
	_currentJob = job;
	job->_runningOn = this;
	if (_pool.insideOpenRepository(_bindRoot))
		_retireTimer->stop(); // a closed root's deadline keeps running

	QStringList args = job->_args;
	// Without -R the command runs on the bound repository whatever the cwd; without --cwd relative paths
	// resolve against the server's cwd
	if (job->_workDir != _bindRoot)
		args = QStringList{ QStringLiteral("-R"), job->_workDir, QStringLiteral("--cwd"), job->_workDir } + args;

	// The same bytes hg would read from a real command line
	QByteArray block;
	for (const QString& arg : args)
	{
		if (!block.isEmpty())
			block += '\0';
		block += Hg::localBytes(arg);
	}

	QByteArray message = QByteArrayLiteral("runcommand\n");
	appendBigEndianU32(message, quint32(block.size()));
	message += block;
	_process->write(message);
}

void HgCommandServer::killServer()
{
	_dying = true; // idle() must refuse new jobs until died() runs
	_buffer.clear(); // consumeChunks may still be in its loop
	_process->kill(); // died() runs from the finished signal
}

void HgCommandServer::bindRootClosed()
{
	_retireTimer->start(ClosedRetireMs);
}

void HgCommandServer::bindRootOpened()
{
	if (_currentJob)
		_retireTimer->stop();
	else
		_retireTimer->start(IdleRetireMs);
}

void HgCommandServer::consumeChunks()
{
	while (!_dead)
	{
		if (_buffer.size() < 5)
			return;
		const char channel = _buffer.at(0);
		const quint32 length = qFromBigEndian<quint32>(_buffer.constData() + 1);

		// The last point a desync is catchable: past it a garbage length is waited on forever, on a server that
		// stays alive and so never reports anything. A first byte that is not a letter fails the channel test;
		// text whose first byte is a letter reads as a length above the cap.
		if (!isProtocolChannel(channel) || length > MaxFrameBytes)
		{
			// Carried into the command's failure, which has no other diagnosis
			_ownStderr += "The command server's output framing desynced, so the command was stopped. An extension "
				"or hook writing to stdout is the usual cause.\n";
			killServer();
			return;
		}

		// An input request's length is how much input is wanted; no payload follows. Always answered empty,
		// see the class comment.
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
		// Before the job's callback, which runs synchronously and may start the next command on this server
		if (_pool.insideOpenRepository(_bindRoot))
			_retireTimer->start(IdleRetireMs);
		std::exchange(_currentJob, nullptr)->completed(qFromBigEndian<qint32>(payload.constData()));
		_pool.serverFreed(this);
		return;
	default:
		// A lowercase channel is optional and skippable; an uppercase one is required, so an unknown one
		// means a server this end cannot use
		if (channel >= 'A' && channel <= 'Z')
			killServer();
		return;
	}
}

void HgCommandServer::readHello(const QByteArray& payload)
{
	// "capabilities: ... runcommand ...\nencoding: ..."; runcommand is the only capability used
	if (!payload.contains("runcommand"))
	{
		killServer();
		return;
	}
	_helloSeen = true;
	_retireTimer->start(_pool.insideOpenRepository(_bindRoot) ? IdleRetireMs : ClosedRetireMs);
	_pool.serverReady(this);
}

void HgCommandServer::died()
{
	if (_dead)
		return;
	_dead = true;

	const bool launchFailed = _process->error() == QProcess::FailedToStart;
	const ProcessOutcome outcome = launchFailed ? ProcessOutcome::LaunchFailed : ProcessOutcome::Crashed;
	const QString launchError = launchFailed ? _process->errorString() : QString{};
	if (Hg::ServerJob* job = std::exchange(_currentJob, nullptr))
		job->failed(outcome, _ownStderr, launchError);
	_pool.serverDied(this, outcome, launchError);
}

HgServerPool& HgServerPool::instance()
{
	static HgServerPool pool;
	return pool;
}

HgServerPool::HgServerPool()
{
	// The executable path is a setting, so a settings change may have fixed what the latches remember.
	// Servers keep the executable they started with; dispatch() spawns with a queued job's tool.
	QObject::connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, qApp, [this] {
		_unavailable = false;
		_failedRoots.clear();

		const QString executable = Hg::executablePath();
		for (Hg::ServerJob* job : _queue)
			job->_tool.executable = executable;
		for (const auto& server : _servers)
		{
			if (server->executable() != executable)
				server->retire();
		}
	});
}

void HgServerPool::shutdown()
{
	for (Hg::ServerJob* job : _queue)
		delete job; // never started, and there is no event loop left for deleteLater
	_queue.clear();

	for (const auto& server : _servers)
		server->requestExit();

	const QDeadlineTimer deadline{ ShutdownWaitMs };
	for (const auto& server : _servers)
		server->waitForExit(deadline);

	_servers.clear(); // the destructor kills whatever did not exit in time
	_shutDown = true;
}

void HgServerPool::repositoryOpened(const QString& root)
{
	if (_openRepositoryCounts[root]++ > 0)
		return;

	for (const auto& server : _servers)
	{
		if (isInsideRoot(server->bindRoot(), root))
			server->bindRootOpened();
	}
}

void HgServerPool::repositoryClosed(const QString& root)
{
	const auto open = _openRepositoryCounts.find(root);
	assert_and_return_r(open != _openRepositoryCounts.end(), );
	if (--open->second > 0)
		return;
	_openRepositoryCounts.erase(open);

	for (const auto& server : _servers)
	{
		if (isInsideRoot(server->bindRoot(), root) && !insideOpenRepository(server->bindRoot()))
			server->bindRootClosed();
	}
}

bool HgServerPool::insideOpenRepository(const QString& path) const
{
	return std::ranges::any_of(_openRepositoryCounts, [&path](const auto& open) { return isInsideRoot(path, open.first); });
}

Vcs::Job* HgServerPool::run(const Vcs::Tool& tool, const QString& workDir, QStringList args, const QObject* context,
	Vcs::Callback callback, QByteArray stdinData)
{
	assert_r(!_shutDown);
	if (_unavailable || _failedRoots.contains(workDir) || !stdinData.isEmpty())
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
			job->deleteLater(); // nobody left to deliver to
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
			// Nothing idle: spawn a server per waiting job, up to the cap.
			// All bind to the front job's repository: on a fresh pool that is the one whose refresh caused the
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
	dispatch();
}

void HgServerPool::serverFreed(HgCommandServer* /*server*/)
{
	dispatch();
}

void HgServerPool::serverDied(HgCommandServer* server, ProcessOutcome outcome, const QString& launchError)
{
	const QByteArray serverStderr = server->ownStderr();
	const QString deadRoot = server->bindRoot();
	const bool cameUp = server->helloSeen();
	const bool retired = server->retiring();

	// Called from the server's own signal handler, so it is deleted from the event loop
	const auto owned = std::ranges::find_if(_servers, [server](const std::unique_ptr<HgCommandServer>& s) { return s.get() == server; });
	if (owned != _servers.end())
	{
		owned->release()->deleteLater();
		_servers.erase(owned);
	}

	if (cameUp || retired)
	{
		// A crash under a command already failed that command with its own diagnosis.
		// A retired server's exit diagnoses neither the executable nor the repository.
		dispatch(); // respawns on demand if the queue calls for it
		return;
	}

	// The server never came up, and the death names its cause:
	//   launch failure - the executable's, so no root will fare better
	//   crash before the hello - the bound repository's (broken repo hgrc or extension)
	// Only the jobs the cause diagnoses are failed with this server's stderr; the rest stay queued, and
	// dispatch() respawns for them bound to their own front job's repository.
	if (outcome == ProcessOutcome::LaunchFailed)
		_unavailable = true;
	else
		_failedRoots.insert(deadRoot);

	std::deque<Hg::ServerJob*> stranded;
	for (auto it = _queue.begin(); it != _queue.end(); )
	{
		if (_unavailable || (*it)->_workDir == deadRoot)
		{
			stranded.push_back(*it);
			it = _queue.erase(it);
		}
		else
			++it;
	}
	for (Hg::ServerJob* job : stranded)
		job->failed(outcome, serverStderr, launchError);
	dispatch();
}
