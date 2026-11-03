#include "repositorywatcher.h"
#include "repository.h"

#include "assert/advanced_assert.h"
#include "qt_paths.hpp" // thin_io

DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QFile>
#include <QThreadPool>
#ifdef Q_OS_WIN
#include <QWinEventNotifier>
#else
#include <QSocketNotifier>
#endif
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <map>
#include <utility>

namespace {

[[nodiscard]] thin_io::native_string nativeString(const QString& path)
{
#ifdef Q_OS_WIN
	return path.toStdWString();
#else
	return QFile::encodeName(path).toStdString();
#endif
}

[[nodiscard]] std::vector<thin_io::native_string> nativeStrings(const QStringList& paths)
{
	std::vector<thin_io::native_string> strings;
	strings.reserve(static_cast<size_t>(paths.size()));
	for (const QString& path : paths)
		strings.push_back(nativeString(path));
	return strings;
}

// Relative to the watched root, '/'-separated
[[nodiscard]] QString changedPath(const thin_io::native_string& nativePath)
{
	QString path = thin_io::to_qstring(nativePath);
#ifdef Q_OS_WIN
	path.replace(QLatin1Char('\\'), QLatin1Char('/'));
#endif
	return path;
}

// The non-recursive watches by path, shared by every repository listing one: the global excludes file's directory is
// the same for all of them, and Linux allots each user a limited number of inotify instances.
[[nodiscard]] std::map<QString, std::weak_ptr<DirectoryWatch>>& sharedDirectoryWatches()
{
	static std::map<QString, std::weak_ptr<DirectoryWatch>> watches;
	return watches;
}

// Null where no repository holds one
[[nodiscard]] std::shared_ptr<DirectoryWatch> sharedDirectoryWatch(const QString& path)
{
	const auto it = sharedDirectoryWatches().find(path);
	return it != sharedDirectoryWatches().end() ? it->second.lock() : nullptr;
}

// Destroyed from the event loop: a release can come from a slot connected to the watch's own notifier
void releaseLater(std::shared_ptr<DirectoryWatch> watch)
{
	QMetaObject::invokeMethod(QCoreApplication::instance(), [watch = std::move(watch)] {}, Qt::QueuedConnection);
}

} // namespace

ChangeSubscription::ChangeSubscription(RepositoryWatcher& watcher, bool workingTree, bool stateDirectory) :
	_watcher{ &watcher },
	_workingTree{ workingTree },
	_stateDirectory{ stateDirectory }
{
}

ChangeSubscription::~ChangeSubscription()
{
	if (_watcher)
		_watcher->unsubscribe(this);
}

bool ChangeSubscription::trusted() const
{
	return _watcher && _watcher->_trusted;
}

void ChangeSubscription::rearmIfStopped()
{
	if (_watcher)
		_watcher->rearmIfStopped();
}

DirectoryWatch::DirectoryWatch(thin_io::directory_watcher watcher, QStringList excludedDirectories) :
	_watcher{ std::move(watcher) },
	_excludedDirectories{ std::move(excludedDirectories) }
{
#ifdef Q_OS_WIN
	_notifier = std::make_unique<QWinEventNotifier>(_watcher.wait_handle());
	connect(_notifier.get(), &QWinEventNotifier::activated, this, &DirectoryWatch::readEvents);
#else
	_notifier = std::make_unique<QSocketNotifier>(_watcher.wait_handle(), QSocketNotifier::Read);
	connect(_notifier.get(), &QSocketNotifier::activated, this, &DirectoryWatch::readEvents);
#endif
}

DirectoryWatch::~DirectoryWatch() = default;

bool DirectoryWatch::partialCoverage() const
{
	return _watcher.coverage() == thin_io::watch_coverage::partial;
}

void DirectoryWatch::setExcludedDirectories(const QStringList& relativePaths)
{
	if (relativePaths == _excludedDirectories)
		return;

	_excludedDirectories = relativePaths;
	_watcher.set_excluded_directories(nativeStrings(relativePaths));
}

void DirectoryWatch::readEvents()
{
	auto events = _watcher.read_events();
	if (!events)
	{
		// Nothing drains the handle any more
		_notifier->setEnabled(false);
		emit eventsRead({ thin_io::watch_event{ .kind = thin_io::watch_event_kind::stopped, .reason = thin_io::stop_reason::failed,
			.error = events.error() } });
		return;
	}
	if (!events->empty())
		emit eventsRead(*events);
}

RepositoryWatcher::RepositoryWatcher(Repository& repository) :
	_repository{ repository }
{
	connect(&_repository, &Repository::refreshed, this, &RepositoryWatcher::applyExclusions);
}

RepositoryWatcher::~RepositoryWatcher()
{
	for (Watch& watch : _watches)
		releaseLater(std::move(watch.watch));
}

std::unique_ptr<ChangeSubscription> RepositoryWatcher::subscribe(bool workingTree, bool stateDirectory)
{
	assert_r(workingTree || stateDirectory);

	std::unique_ptr<ChangeSubscription> subscription{ new ChangeSubscription{ *this, workingTree, stateDirectory } };
	_subscriptions.push_back(subscription.get());
	if (_subscriptions.size() == 1)
		queryRoots();
	else if (busy())
		_armAgain = true;
	else
		armNeededRoots(); // the tree, for the first subscription wanting it; ready() for this one in any case
	return subscription;
}

void RepositoryWatcher::readPendingChanges()
{
	// A copy: a stop releases its watch
	std::vector<std::shared_ptr<DirectoryWatch>> watches;
	for (const Watch& watch : _watches)
		watches.push_back(watch.watch);
	for (const std::shared_ptr<DirectoryWatch>& watch : watches)
		watch->readEvents();
}

void RepositoryWatcher::unsubscribe(const ChangeSubscription* subscription)
{
	std::erase(_subscriptions, subscription);
	if (!_subscriptions.empty())
	{
		// The state directories in the tree are armed before the tree watch goes
		if (busy())
			_armAgain = true;
		else
			armNeededRoots();
		return;
	}

	// The roots are queried again by the next first subscription: they may have moved
	++_generation;
	_rootsQuery.cancel();
	_rootsQueryRunning = false;
	_roots.reset();
	_armsPending = 0;
	_armAgain = _rearming = _rootsMissing = _trusted = false;
	while (!_watches.empty())
		releaseWatchAt(_watches.size() - 1);
}

void RepositoryWatcher::rearmIfStopped()
{
	if (!_rootsMissing || busy())
		return;

	_rearming = true;
	queryRoots();
}

void RepositoryWatcher::queryRoots()
{
	_rootsQueryRunning = true;
	// Checked: a backend may answer without a process to cancel
	_rootsQuery = _repository.watchedRoots(this, [this, generation = _generation](std::expected<std::vector<WatchedRoot>, QString> roots) {
		if (generation != _generation)
			return;

		_rootsQueryRunning = false;
		if (roots)
			_roots = std::move(*roots);
		else
			_roots.reset(); // untrusted: rearmIfStopped() queries again
		armNeededRoots();
	});
}

std::vector<WatchedRoot> RepositoryWatcher::neededRoots() const
{
	if (!_roots)
		return {};

	const bool workingTree = std::ranges::any_of(_subscriptions, &ChangeSubscription::_workingTree);
	const auto treeWatchCovers = [this](const WatchedRoot& stateRoot) {
		return std::ranges::any_of(*_roots, [&stateRoot](const WatchedRoot& root) {
			return root.scope == ChangeScope::WorkingTree && root.recursive && relativePathWithin(root.path, stateRoot.path).has_value();
		});
	};

	std::vector<WatchedRoot> needed;
	for (const WatchedRoot& root : *_roots)
	{
		const bool isNeeded = root.scope == ChangeScope::WorkingTree ? workingTree : !(workingTree && treeWatchCovers(root));
		if (isNeeded)
			needed.push_back(root);
	}
	return needed;
}

void RepositoryWatcher::armNeededRoots()
{
	assert_r(!busy());

	for (const WatchedRoot& root : neededRoots())
	{
		if (std::ranges::none_of(_watches, [&root](const Watch& watch) { return watch.root == root; }) && armRoot(root))
			++_armsPending;
	}
	if (_armsPending == 0)
		finishArming();
}

bool RepositoryWatcher::armRoot(const WatchedRoot& root)
{
	if (!root.recursive)
	{
		if (std::shared_ptr<DirectoryWatch> shared = sharedDirectoryWatch(root.path))
		{
			adoptWatch(root, std::move(shared));
			return false;
		}
	}

	QStringList exclusions = exclusionsUnder(root);
	thin_io::watch_options options{ .recursive = root.recursive, .excluded_directories = nativeStrings(exclusions) };
	// Answered through the application object, which outlives this watcher, and only applied while this one lives
	QThreadPool::globalInstance()->start([guard = QPointer<RepositoryWatcher>{ this }, generation = _generation, root,
		exclusions = std::move(exclusions), nativeRoot = nativeString(root.path), options = std::move(options)]() mutable {
		// Shared: a functor posted to an event loop must be copyable
		auto armed = std::make_shared<thin_io::filesystem_result<thin_io::directory_watcher>>(
			thin_io::directory_watcher::watch(nativeRoot.c_str(), std::move(options)));
		QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, generation, root, exclusions, armed] {
			if (!guard || generation != guard->_generation)
				return;

			--guard->_armsPending;
			if (*armed)
			{
				std::shared_ptr<DirectoryWatch> watch;
				if (!root.recursive)
					watch = sharedDirectoryWatch(root.path); // armed meanwhile by another repository
				if (!watch)
				{
					watch = std::make_shared<DirectoryWatch>(std::move(**armed), exclusions);
					if (!root.recursive)
						sharedDirectoryWatches()[root.path] = watch;
				}
				guard->adoptWatch(root, std::move(watch));
			}
			if (guard->_armsPending == 0)
				guard->finishArming();
		}, Qt::QueuedConnection);
	});
	return true;
}

void RepositoryWatcher::adoptWatch(const WatchedRoot& root, std::shared_ptr<DirectoryWatch> watch)
{
	connect(watch.get(), &DirectoryWatch::eventsRead, this, [this, root](const std::vector<thin_io::watch_event>& events) { onEvents(root, events); });
	_watches.push_back({ root, std::move(watch) });
}

void RepositoryWatcher::finishArming()
{
	if (std::exchange(_armAgain, false))
	{
		armNeededRoots();
		return;
	}

	// Only now: what replaces a released watch is armed, so no change falls between them
	releaseWatchesNotNeeded();
	const std::vector<WatchedRoot> needed = neededRoots();
	_rootsMissing = !_roots || std::ranges::any_of(needed, [this](const WatchedRoot& root) {
		return std::ranges::none_of(_watches, [&root](const Watch& watch) { return watch.root == root; });
	});
	updateTrust();

	const bool rearmed = std::exchange(_rearming, false);
	for (const QPointer<ChangeSubscription>& subscription : guardedSubscriptions())
	{
		if (!subscription)
			continue;
		if (!subscription->_readyQueued)
		{
			subscription->_readyQueued = true;
			// Never before subscribeToChanges() returns: the caller connects to it afterwards
			QMetaObject::invokeMethod(subscription, [subscription = subscription.data()] {
				subscription->_ready = true;
				emit subscription->ready();
			}, Qt::QueuedConnection);
		}
		else if (rearmed && subscription->_ready)
			emit subscription->changed();
	}
}

std::vector<QPointer<ChangeSubscription>> RepositoryWatcher::guardedSubscriptions() const
{
	std::vector<QPointer<ChangeSubscription>> subscriptions;
	for (ChangeSubscription* subscription : _subscriptions)
		subscriptions.emplace_back(subscription);
	return subscriptions;
}

void RepositoryWatcher::releaseWatchesNotNeeded()
{
	const std::vector<WatchedRoot> needed = neededRoots();
	for (size_t i = _watches.size(); i-- > 0;)
	{
		if (std::ranges::find(needed, _watches[i].root) == needed.end())
			releaseWatchAt(i);
	}
}

void RepositoryWatcher::releaseWatchAt(size_t index)
{
	assert_and_return_r(index < _watches.size(), );

	std::shared_ptr<DirectoryWatch> watch = std::move(_watches[index].watch);
	_watches.erase(_watches.begin() + static_cast<ptrdiff_t>(index));
	disconnect(watch.get(), nullptr, this, nullptr);
	releaseLater(std::move(watch));
}

void RepositoryWatcher::onEvents(const WatchedRoot& root, const std::vector<thin_io::watch_event>& events)
{
	bool workingTree = false;
	bool stateDirectory = false;
	bool stopped = false;
	const auto classify = [&](const thin_io::native_string& nativePath) {
		if (const std::optional<ChangeScope> scope = _repository.changeScope(root, changedPath(nativePath)))
			(*scope == ChangeScope::WorkingTree ? workingTree : stateDirectory) = true;
	};
	for (const thin_io::watch_event& event : events)
	{
		switch (event.kind)
		{
		case thin_io::watch_event_kind::overflow:
			workingTree = stateDirectory = true;
			break;
		case thin_io::watch_event_kind::stopped:
			stopped = true;
			break;
		case thin_io::watch_event_kind::renamed:
			classify(event.old_path);
			classify(event.path);
			break;
		default:
			classify(event.path);
			break;
		}
	}

	if (stopped)
	{
		const auto it = std::ranges::find(_watches, root, &Watch::root);
		if (it != _watches.end())
			releaseWatchAt(static_cast<size_t>(it - _watches.begin()));
		_rootsMissing = true;
		updateTrust();
	}

	if (!workingTree && !stateDirectory)
		return;
	for (const QPointer<ChangeSubscription>& subscription : guardedSubscriptions())
	{
		if (subscription && subscription->_ready
			&& ((workingTree && subscription->_workingTree) || (stateDirectory && subscription->_stateDirectory)))
			emit subscription->changed();
	}
}

void RepositoryWatcher::applyExclusions()
{
	for (const Watch& watch : _watches)
	{
		if (watch.root.recursive)
			watch.watch->setExcludedDirectories(exclusionsUnder(watch.root));
	}
}

QStringList RepositoryWatcher::exclusionsUnder(const WatchedRoot& root) const
{
	QStringList exclusions;
	if (!root.recursive)
		return exclusions;

	for (const QString& directory : _repository.unwatchedDirectories())
	{
		if (const std::optional<QString> relative = relativePathWithin(root.path, directory); relative && !relative->isEmpty())
			exclusions.push_back(*relative);
	}
	return exclusions;
}

void RepositoryWatcher::updateTrust()
{
	const bool trusted = !_rootsMissing && std::ranges::none_of(_watches, [](const Watch& watch) { return watch.watch->partialCoverage(); });
	if (trusted == std::exchange(_trusted, trusted))
		return;

	for (const QPointer<ChangeSubscription>& subscription : guardedSubscriptions())
	{
		if (!subscription || !subscription->_ready)
			continue; // one not ready reads trusted() at ready()
		if (trusted)
			emit subscription->trustRestored();
		else
			emit subscription->trustLost();
	}
}
