#pragma once

#include "vcsprocess.h"

#include <deque>
#include <memory>
#include <vector>

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
	void deliverOutput(const QByteArray& chunk) { collect(chunk, _out); }
	void deliverError(const QByteArray& chunk) { collect(chunk, _err); }
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
// Input requests are always answered empty: ui.interactive=False means nothing should ask, and an
// unanswered request would deadlock the pipe.
class HgCommandServer final : public QObject
{
public:
	HgCommandServer(const Vcs::Tool& tool, QString bindRoot, HgServerPool& pool);
	~HgCommandServer() override;

	[[nodiscard]] const QString& bindRoot() const { return _bindRoot; }
	[[nodiscard]] bool idle() const { return _helloSeen && !_dead && !_currentJob; }
	[[nodiscard]] Hg::ServerJob* currentJob() const { return _currentJob; }
	[[nodiscard]] const QByteArray& ownStderr() const { return _ownStderr; }

	// The server must be idle. A command for another repository carries -R and --cwd, paying a repo open
	// there but no interpreter start.
	void execute(Hg::ServerJob* job);

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
	bool _dead = false;
};

// Up to MaxServers servers, spawned on demand, each bound to the repository of the job waiting at the time.
// Jobs queue FIFO; a free server prefers a job for its own repository.
// If no server ever becomes ready (hg missing or too old), the pool routes everything through plain processes.
class HgServerPool final
{
public:
	static HgServerPool& instance();

	// The same contract as Vcs::run. Commands with stdin data go to the process transport, since the server's
	// input channels are answered empty (no hg call site sends any).
	Vcs::Job* run(const Vcs::Tool& tool, const QString& workDir, QStringList args, const QObject* context,
		Vcs::Callback callback, QByteArray stdinData);

private:
	friend class HgCommandServer;
	friend class Hg::ServerJob;

	void dispatch();
	void removeQueued(Hg::ServerJob* job);
	void serverReady(HgCommandServer* server);
	void serverFreed(HgCommandServer* server);
	void serverDied(HgCommandServer* server, ProcessOutcome outcome, const QString& launchError);

private:
	static constexpr int MaxServers = 4;

	std::vector<std::unique_ptr<HgCommandServer>> _servers;
	std::deque<Hg::ServerJob*> _queue;
	bool _everReady = false;   // one server coming up proves this hg can serve
	bool _unavailable = false; // none ever did
};
