#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>

struct GitResult
{
	int exitCode = -1;
	QByteArray out;
	QByteArray err;
	bool launchFailed = false;
	bool ok = false; // launched, exited normally, exit code 0

	// stderr as text, or a human-readable launch failure message
	[[nodiscard]] QString errorText() const;
};

namespace Git {

using Callback = std::function<void(const GitResult&)>;

// A queued asynchronous git invocation. cancel() guarantees the callback will not fire.
class Job final : public QObject
{
public:
	void cancel();

	// Delivers output to `sink` as it arrives, both channels in arrival order, on top of the complete
	// output the result still carries. Attach it before returning to the event loop - QProcess reads
	// only from there, so nothing has been read yet and nothing to come is missed. Like the result
	// callback, the sink stops being called once `context` dies.
	void streamTo(std::function<void(const QByteArray&)> sink);

private:
	friend Job* run(const QString&, QStringList, const QObject*, Callback, QByteArray, bool);
	explicit Job(QObject* parent = nullptr) : QObject(parent) {}

	void start();
	void collect(const QByteArray& chunk, QByteArray& buffer);
	void finish(GitResult result);

private:
	QString _workDir;
	QStringList _args;
	QByteArray _stdinData;
	Callback _callback;
	std::function<void(const QByteArray&)> _sink;
	QByteArray _out, _err; // filled as the process runs, so a sink and the result see the same bytes
	QPointer<const QObject> _context;
	bool _hasContext = false;
	QProcess* _process = nullptr;
	bool _cancelled = false;

	friend struct JobQueue;
};

// Runs `git <args>` in workDir with the invariants from doc/ARCHITECTURE.md applied:
// -c core.quotepath=false, GIT_TERMINAL_PROMPT=0, --no-optional-locks for read-only queries,
// stdin payload support, and a cap on concurrent git processes - excess is queued.
// The callback fires on the GUI thread; it is skipped if the job is cancelled or `context` dies.
// A job whose `context` died while it was still queued is discarded without ever running.
Job* run(const QString& workDir, QStringList args, const QObject* context, Callback callback,
	QByteArray stdinData = {}, bool readOnlyQuery = false);

// Blocking variant with the same invariants, for the one moment before the event loop exists
// (resolving the repo root at startup). Read-only queries only; everything else goes through run().
GitResult runSync(const QString& workDir, QStringList args, int timeoutMs = 10000);

} // namespace Git
