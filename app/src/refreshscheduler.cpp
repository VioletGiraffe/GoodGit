#include "refreshscheduler.h"
#include "repository.h"
#include "repositorywatcher.h"

DISABLE_COMPILER_WARNINGS
#include <QEvent>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#include <chrono>
#include <utility>

namespace {

// After the last change of a burst: one refresh per save, checkout or build step
constexpr std::chrono::milliseconds ChangeSettleDelay{ 300 };
// After the first change of a burst that does not settle
constexpr std::chrono::milliseconds ChangeMaxDelay{ 2'000 };

} // namespace

RefreshScheduler::RefreshScheduler(QWidget& window, Repository& repository, std::initializer_list<ChangeScope> scopes, std::function<void()> refreshWindow) :
	_window{ window },
	_repository{ repository },
	_run{ std::move(refreshWindow) },
	_subscription{ repository.subscribeToChanges(scopes) }
{
	_settleTimer.setSingleShot(true);
	_settleTimer.setInterval(ChangeSettleDelay);
	_maxDelayTimer.setSingleShot(true);
	_maxDelayTimer.setInterval(ChangeMaxDelay);
	connect(&_settleTimer, &QTimer::timeout, this, &RefreshScheduler::runIfPending);
	connect(&_maxDelayTimer, &QTimer::timeout, this, &RefreshScheduler::runIfPending);

	// Active or not: the window shows nothing until its first refresh
	connect(_subscription.get(), &ChangeSubscription::ready, this, [this] {
		_ready = true;
		run();
	});
	connect(_subscription.get(), &ChangeSubscription::changed, this, &RefreshScheduler::markPending);
	// A change before the loss may have gone unreported
	connect(_subscription.get(), &ChangeSubscription::trustLost, this, &RefreshScheduler::markPending);
	// Delayed, not run: the write's own window refreshes as the write ends, which clears this
	connect(&_repository, &Repository::writeStateChanged, this, &RefreshScheduler::schedule);
	_window.installEventFilter(this);
}

RefreshScheduler::~RefreshScheduler() = default;

void RefreshScheduler::cancelPending()
{
	_pending = false;
	_settleTimer.stop();
	_maxDelayTimer.stop();
}

bool RefreshScheduler::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == &_window && _ready)
	{
		if (event->type() == QEvent::WindowActivate)
		{
			_subscription->rearmIfStopped();
			runIfPending();
		}
		else if (event->type() == QEvent::WindowDeactivate && !_subscription->trusted())
			_pending = true;
	}
	return QObject::eventFilter(watched, event);
}

void RefreshScheduler::markPending()
{
	_pending = true;
	schedule();
}

void RefreshScheduler::schedule()
{
	if (!_pending || !canRun())
		return;

	_settleTimer.start();
	if (!_maxDelayTimer.isActive())
		_maxDelayTimer.start();
}

void RefreshScheduler::runIfPending()
{
	if (_pending && canRun())
		run();
}

void RefreshScheduler::run()
{
	cancelPending();
	_run();
}

// A modal dialog is the active window, so no refresh lands inside a write flow's confirmation
bool RefreshScheduler::canRun() const
{
	return _window.isActiveWindow() && !_repository.writeInFlight();
}
