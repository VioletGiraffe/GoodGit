#pragma once

#include "vcsprocess.h"

DISABLE_COMPILER_WARNINGS
#include <QDeadlineTimer>
#include <QSet>
RESTORE_COMPILER_WARNINGS

#include <deque>
#include <memory>
#include <vector>

class QProcess;
class HgCommandServer;
class HgServerPool;

namespace Hg {

// One command on a running hg command server, with the same contract as a process job.
// The protocol has no per-command cancel: cancelling suppresses delivery and lets the command finish.
// A cancelled command that overstays a grace period gets its server killed; the pool respawns on demand.
class ServerJob final : public Vcs::Job
{
public:
	ServerJob(Vcs::Tool tool, QString workDir, QStringList args, const QObject* context, Vcs::Callback callback);

	void cancel() override;

private:
	friend class ::HgCommandServer;
	friend class ::HgServerPool;

	// The protocol events, called by the server running this
	void deliverOutput(const QByteArray& chunk) { collectOutput(chunk); }
	void deliverError(const QByteArray& chunk) { collectError(chunk); }
	void completed(int exitCode);
	// The server died under the command, or never came up; its own stderr or the OS's launch error is the
	// only diagnosis there is
	void failed(ProcessOutcome outcome, const QByteArray& serverStderr, const QString& launchError);

	[[nodiscard]] bool abandonedWhileQueued() const { return _hasContext && !_context; }

private:
	QPointer<HgCommandServer> _runningOn; // set while in flight; a queued or finished job has none
};

} // namespace Hg

// One `hg serve --cmdserver pipe` process: the interpreter, the config and the bound repository load once,
// then each request runs one command over the pipe.
// Framing: 1 channel byte + u32 BE length. 'o'/'e' carry output, 'r' the exit code, 'I'/'L' request input.
// A frame that cannot be one is treated as a desynced stream: the server is killed and the command fails,
// since a length that is really file content would otherwise be waited on forever.
// Input requests are always answered empty: ui.interactive=False means nothing should ask, and an
// unanswered request would deadlock the pipe.
class HgCommandServer final : public QObject
{
public:
	HgCommandServer(const Vcs::Tool& tool, QString bindRoot, HgServerPool& pool);
	~HgCommandServer() override;

	[[nodiscard]] const QString& bindRoot() const { return _bindRoot; }
	[[nodiscard]] bool helloSeen() const { return _helloSeen; }
	[[nodiscard]] bool idle() const { return _helloSeen && !_dead && !_dying && !_currentJob; }
	[[nodiscard]] Hg::ServerJob* currentJob() const { return _currentJob; }
	[[nodiscard]] const QByteArray& ownStderr() const { return _ownStderr; }

	// The server must be idle. A command for another repository carries -R and --cwd, paying a repo open
	// there but no interpreter start.
	void execute(Hg::ServerJob* job);

	// Shutdown, in this order: stop taking part in the pool and let the command in flight run to its end,
	// then wait for the exit until `deadline`. Anything still running when the server is destroyed is killed.
	void requestExit();
	void waitForExit(QDeadlineTimer deadline);

	// Fails the current command; the pool respawns on demand
	void killServer();

private:
	void consumeChunks();
	void handleChunk(char channel, const QByteArray& payload);
	void readHello(const QByteArray& payload);
	void died(); // fails the current command, tells the pool

private:
	HgServerPool& _pool;
	const QString _bindRoot;
	QProcess* _process = nullptr;
	QByteArray _buffer;    // stdout bytes not yet consumed as chunks
	QByteArray _ownStderr; // the server's own stderr (extension warnings, crash text), never a command's output
	Hg::ServerJob* _currentJob = nullptr;
	bool _helloSeen = false;
	bool _dying = false; // killServer() ran; the finished signal has not arrived yet
	bool _dead = false;
};

// Up to MaxServers servers, spawned on demand, each bound to the repository of the job waiting at the time.
// Jobs queue FIFO; a free server prefers a job for its own repository.
// A server dying before its hello latches its cause: a launch failure (hg missing) routes everything
// through plain processes, a pre-hello crash (broken repo config) routes only that repository's jobs there.
// A settings change resets both latches: the executable path is a setting.
class HgServerPool final
{
public:
	static HgServerPool& instance();

	// The same contract as Vcs::run. Commands with stdin data go to the process transport, since the server's
	// input channels are answered empty (no hg call site sends any).
	Vcs::Job* run(const Vcs::Tool& tool, const QString& workDir, QStringList args, const QObject* context,
		Vcs::Callback callback, QByteArray stdinData);

	// Ends every server, letting the command in flight on each finish; what overstays the shared budget is
	// killed. Queued commands are dropped undelivered. Only called once the event loop has returned.
	void shutdown();

private:
	friend class HgCommandServer;
	friend class Hg::ServerJob;

	HgServerPool(); // installs the settings hook that resets the failure latches

	void dispatch();
	void removeQueued(Hg::ServerJob* job);
	void serverReady(HgCommandServer* server);
	void serverFreed(HgCommandServer* server);
	void serverDied(HgCommandServer* server, ProcessOutcome outcome, const QString& launchError);

private:
	static constexpr int MaxServers = 4;

	std::vector<std::unique_ptr<HgCommandServer>> _servers;
	std::deque<Hg::ServerJob*> _queue;
	// hg itself cannot launch: everything goes through plain processes until a settings change resets this
	bool _unavailable = false;
	// Repositories whose server died before its hello (broken repo hgrc or extension): their jobs bypass the
	// pool, so each command fails with hg's own error for that repository instead of poisoning the shared
	// queue. A repository fixed outside the app stays here until a settings change or restart.
	QSet<QString> _failedRoots;
	bool _shutDown = false; // for the assert in run(): a command started after shutdown() would spawn a server again
};
