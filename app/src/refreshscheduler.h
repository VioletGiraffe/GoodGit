#pragma once

#include "vcstypes.h"

DISABLE_COMPILER_WARNINGS
#include <QObject>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <initializer_list>
#include <memory>

class ChangeSubscription;
class QWidget;
class Repository;

// When a window's refresh runs, driven by its repository's change watch (doc/refresh.md):
// - a reported change leaves the refresh pending until the window is active with no write in flight; it then runs once the changes settle;
// - the first refresh runs once the watch is set up;
// - while the watch cannot be trusted, every activation refreshes.
class RefreshScheduler final : public QObject
{
public:
	// `run` refreshes the window. The window must outlive this object.
	RefreshScheduler(QWidget& window, Repository& repository, std::initializer_list<ChangeScope> scopes, std::function<void()> run);
	~RefreshScheduler() override;

	// A refresh the window started itself reads every change reported so far
	void cancelPending();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void markPending();
	// Starts the delays where the refresh is pending and may run
	void schedule();
	void runIfPending();
	void run();
	[[nodiscard]] bool canRun() const;

	QWidget& _window;
	Repository& _repository;
	const std::function<void()> _run;
	std::unique_ptr<ChangeSubscription> _subscription;
	QTimer _settleTimer;   // restarted by every change
	QTimer _maxDelayTimer; // started by the first change: a build that never settles still refreshes
	bool _ready = false;   // the subscription's ready() came
	bool _pending = false;
};
