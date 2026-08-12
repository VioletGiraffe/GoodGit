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

private:
	friend Job* run(const QString&, QStringList, const QObject*, Callback, QByteArray, bool);
	explicit Job(QObject* parent = nullptr) : QObject(parent) {}

	void start();
	void finish(GitResult result);

private:
	QString _workDir;
	QStringList _args;
	QByteArray _stdinData;
	Callback _callback;
	QPointer<const QObject> _context;
	bool _hasContext = false;
	QProcess* _process = nullptr;
	bool _cancelled = false;

	friend struct JobQueue;
};

// Runs `git <args>` in workDir with the invariants from plan.md §3 applied:
// -c core.quotepath=false, GIT_TERMINAL_PROMPT=0, --no-optional-locks for read-only queries,
// stdin payload support, at most 4 concurrent git processes (excess is queued).
// The callback fires on the GUI thread; it is skipped if the job is cancelled or `context` dies.
Job* run(const QString& workDir, QStringList args, const QObject* context, Callback callback,
	QByteArray stdinData = {}, bool readOnlyQuery = false);

// Blocking variant with the same invariants, for the one moment before the event loop exists
// (resolving the repo root at startup). Read-only queries only; everything else goes through run().
GitResult runSync(const QString& workDir, QStringList args, int timeoutMs = 10000);

} // namespace Git
