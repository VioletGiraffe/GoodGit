#pragma once

#include "vcsprocess.h"

#include <deque>
#include <memory>
#include <vector>

class HgCommandServer;
class HgServerPool;

namespace Hg {

// One command on a running hg command server: the same contract as a process job - result callback,
// streaming, cancel - without the interpreter startup a fresh process pays. The protocol has no
// per-command cancel, so cancelling a running command suppresses its delivery immediately and lets it
// finish; only if it overstays a grace period is its server killed and respawned on demand.
class ServerJob final : public Vcs::Job
{
public:
	ServerJob(Vcs::Tool tool, QString workDir, QStringList args, const QObject* context, Vcs::Callback callback);

	void cancel() override;

private:
	friend class ::HgCommandServer;
	friend class ::HgServerPool;

	// The protocol events, translated to the Job contract by whichever server runs this
	void deliverOutput(const QByteArray& chunk) { collect(chunk, _out); }
	void deliverError(const QByteArray& chunk) { collect(chunk, _err); }
	void completed(int exitCode);
	// The server died under the command, or never came up at all; its own stderr is the only diagnosis there is
	void failed(ProcessOutcome outcome, const QByteArray& serverStderr);

	[[nodiscard]] bool abandonedWhileQueued() const { return _hasContext && !_context; }

private:
	QPointer<HgCommandServer> _runningOn; // set while in flight; a queued or finished job has none
};

} // namespace Hg

// One connection to `hg serve --cmdserver pipe`, bound to the repository it was started in: the
// interpreter, the config and that repository load once, then each request runs one command over the
// pipe. Framing is 1 channel byte + u32 BE length; 'o'/'e' carry the command's output, 'r' its exit
// code, and 'I'/'L' request input, always answered empty here - ui.interactive=False means nothing
// should ask, and an unanswered request would deadlock the pipe.
class HgCommandServer final : public QObject
{
public:
	HgCommandServer(const Vcs::Tool& tool, QString bindRoot, HgServerPool& pool);
	~HgCommandServer() override;

	[[nodiscard]] const QString& bindRoot() const { return _bindRoot; }
	[[nodiscard]] bool idle() const { return _helloSeen && !_dead && !_currentJob; }
	[[nodiscard]] Hg::ServerJob* currentJob() const { return _currentJob; }
	[[nodiscard]] const QByteArray& ownStderr() const { return _ownStderr; }

	// Sends the job's command; the server must be idle. A command for a repository other than the bound
	// one carries -R and --cwd, paying a fresh repo open there but no interpreter start.
	void execute(Hg::ServerJob* job);

	// The escape hatch for a command that outstayed its cancellation: fails it, and the pool respawns on demand
	void killServer();

private:
	void consumeChunks();
	void handleChunk(char channel, const QByteArray& payload);
	void readHello(const QByteArray& payload);
	void died(); // the process is gone, however that happened: fail the current command, tell the pool

private:
	HgServerPool& _pool;
	const QString _bindRoot;
	QProcess* _process = nullptr;
	QByteArray _buffer;    // stdout bytes not yet consumed as chunks
	QByteArray _ownStderr; // the server process's own stderr - extension warnings and crash text, no command's output
	Hg::ServerJob* _currentJob = nullptr;
	bool _helloSeen = false;
	bool _dead = false;
};

// Up to MaxServers connections, spawned lazily against demand and each bound to the repository of the
// command that was waiting when it spawned. Jobs queue FIFO; a free server prefers a job for its own
// repository's root. If no server ever becomes ready - hg missing or too old - the pool marks itself
// unavailable and routes everything through ordinary processes instead.
class HgServerPool final
{
public:
	static HgServerPool& instance();

	// The same contract as Vcs::run. Commands with stdin data go to the process transport - no hg call
	// site sends any, and the input channels are answered empty.
	Vcs::Job* run(const Vcs::Tool& tool, const QString& workDir, QStringList args, const QObject* context,
		Vcs::Callback callback, QByteArray stdinData);

private:
	friend class HgCommandServer;
	friend class Hg::ServerJob;

	void dispatch();
	void removeQueued(Hg::ServerJob* job);
	void serverReady(HgCommandServer* server);
	void serverFreed(HgCommandServer* server);
	void serverDied(HgCommandServer* server, ProcessOutcome outcome);

private:
	static constexpr int MaxServers = 4;

	std::vector<std::unique_ptr<HgCommandServer>> _servers;
	std::deque<Hg::ServerJob*> _queue;
	bool _everReady = false;   // one server coming up proves the executable can serve
	bool _unavailable = false; // none ever did: this hg cannot, so stop trying
};
