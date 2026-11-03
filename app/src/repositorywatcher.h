#pragma once

#include "vcsprocess.h"
#include "vcstypes.h"

#include "directory_watcher.hpp" // thin_io

DISABLE_COMPILER_WARNINGS
#include <QObject>
#include <QPointer>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <optional>
#include <stdint.h>
#include <vector>

#ifdef Q_OS_WIN
class QWinEventNotifier;
#else
class QSocketNotifier;
#endif

class Repository;
class RepositoryWatcher;

// One window's interest in its repository's changes: see Repository::subscribeToChanges()
class ChangeSubscription final : public QObject
{
	Q_OBJECT

public:
	~ChangeSubscription() override;

	// False where a change may go unreported: a watch stopped or failed, or a remote volume may never notify.
	// Read at ready(); trustLost() and trustRestored() report the changes after it.
	[[nodiscard]] bool trusted() const;
	// Watches again what stopped or failed to be watched. Only called as the window activates: an eject that stopped the
	// watch has finished or failed by then, and an earlier handle would block it.
	void rearmIfStopped();

signals:
	// Emitted once, after subscribeToChanges() returns, when the watch is set up or has failed to be: a refresh that
	// starts after it misses nothing.
	void ready();
	// Something in the scopes subscribed to changed, or a watch set up again may have missed a change. Only after ready().
	void changed();
	void trustLost();
	void trustRestored();

private:
	friend class RepositoryWatcher;
	ChangeSubscription(RepositoryWatcher& watcher, bool workingTree, bool stateDirectory);

	QPointer<RepositoryWatcher> _watcher; // null once the repository is gone
	const bool _workingTree;
	const bool _stateDirectory;
	bool _readyQueued = false;
	bool _ready = false; // ready() was emitted
};

// One armed thin_io watch, and the notifier that waits on its handle in the GUI thread's event loop
class DirectoryWatch final : public QObject
{
	Q_OBJECT

public:
	// Only constructed on the GUI thread, which the notifier belongs to
	DirectoryWatch(thin_io::directory_watcher watcher, QStringList excludedDirectories);
	~DirectoryWatch() override;

	[[nodiscard]] bool partialCoverage() const;
	// Relative to the root
	void setExcludedDirectories(const QStringList& relativePaths);
	// Emits what is pending, if anything. Also the notifier's slot: the handle stays signalled until drained.
	void readEvents();

signals:
	// A read that fails arrives as a `stopped` event. A slot must not destroy this watch: the notifier is emitting.
	void eventsRead(const std::vector<thin_io::watch_event>& events);

private:
	thin_io::directory_watcher _watcher;
	QStringList _excludedDirectories;
#ifdef Q_OS_WIN
	using Notifier = QWinEventNotifier;
#else
	using Notifier = QSocketNotifier;
#endif
	std::unique_ptr<Notifier> _notifier; // declared after the watcher, so destroyed first: it waits on the watcher's handle
};

// The watches on one repository's roots, owned by its Repository, shared by every window on it.
// Watches the working tree only while a subscription wants it, and the state directories in it alone otherwise.
// A watch is set up on the thread pool: placing a watch per directory of a large tree (Linux) would stall the GUI.
class RepositoryWatcher final : public QObject
{
public:
	explicit RepositoryWatcher(Repository& repository);
	~RepositoryWatcher() override;

	[[nodiscard]] std::unique_ptr<ChangeSubscription> subscribe(bool workingTree, bool stateDirectory);
	// Reports the changes the operating system has queued without waiting for the notifiers: a refresh starting now
	// reads them, so a window's pending refresh is that one
	void readPendingChanges();

private:
	friend class ChangeSubscription;

	struct Watch
	{
		WatchedRoot root;
		std::shared_ptr<DirectoryWatch> watch;
	};

	void unsubscribe(const ChangeSubscription* subscription);
	void rearmIfStopped();

	void queryRoots();
	[[nodiscard]] std::vector<WatchedRoot> neededRoots() const;
	// Arms the needed roots not watched yet; finishArming() runs once every arm has answered
	void armNeededRoots();
	// False where the root was adopted at once: an app-wide shared watch already holds it
	bool armRoot(const WatchedRoot& root);
	void adoptWatch(const WatchedRoot& root, std::shared_ptr<DirectoryWatch> watch);
	void finishArming();
	void releaseWatchesNotNeeded();
	void releaseWatchAt(size_t index);

	void onEvents(const WatchedRoot& root, const std::vector<thin_io::watch_event>& events);
	void applyExclusions();
	[[nodiscard]] QStringList exclusionsUnder(const WatchedRoot& root) const;
	void updateTrust();
	// A slot connected to a subscription's signal may end that subscription, or another
	[[nodiscard]] std::vector<QPointer<ChangeSubscription>> guardedSubscriptions() const;
	[[nodiscard]] bool busy() const { return _rootsQueryRunning || _armsPending > 0; }

	Repository& _repository;
	std::vector<ChangeSubscription*> _subscriptions;

	std::optional<std::vector<WatchedRoot>> _roots; // the last watchedRoots() answer; empty after a failed one
	Vcs::Query _rootsQuery;
	bool _rootsQueryRunning = false;
	std::vector<Watch> _watches;
	int _armsPending = 0;
	uint64_t _generation = 0; // an arm answering for an older generation is dropped: every subscription left meanwhile

	bool _armAgain = false;  // the subscriptions changed while busy
	bool _rearming = false;  // the arming in progress follows a stop or a failure, which may have missed changes
	bool _rootsMissing = false; // a needed root stopped or failed to arm
	bool _trusted = false;
};
