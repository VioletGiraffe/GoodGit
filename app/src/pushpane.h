#pragma once

#include "repository.h"

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <vector>

class ConsoleLogView;

// The pane a window pushes through: runs a planned push and shows its output. Hidden until the first push.
// Holds the push lock itself, so the window hosting it must not close while pushRunning().
class PushPane final : public QWidget
{
public:
	explicit PushPane(std::shared_ptr<Repository> repo, QWidget* parent = nullptr);

	// Runs the steps the repository plans one after another, logged as one console session. The first
	// failure ends the push.
	// Requires no push in flight: every control that starts one is disabled during one.
	// `upToCommit`: see Repository::planPush.
	void startPush(PushMode mode, const QString& upToCommit = {});
	[[nodiscard]] bool pushRunning() const;

private:
	// Takes the push lock of every submodule the planned push writes, all or none. A busy one is reported, and
	// nothing is locked.
	[[nodiscard]] bool lockPushedSubmodules();
	// Ends the push flow, finished or failed: this repository's push lock and the submodules'. Each submodule's
	// history views re-read the unpushed marks, and its window refreshes.
	void endPush();
	// After this repository's own push: a refresh, and the announcement that stales the unpushed marks
	void refreshAfterPush();
	// Before a one-by-one push of the planned steps: each push of a commit can start a CI run
	[[nodiscard]] bool confirmOneByOnePush();
	// Before a push up to `commit`: a context menu item publishes from one click
	[[nodiscard]] bool confirmPushUpTo(const QString& commit);
	void runPushStep(size_t index, bool setUpstream);
	// Offers to set `upstream` for the step's branch and retries if accepted. Returns whether the push
	// goes on.
	bool offerUpstreamThenRetry(size_t index, const QString& upstream);
	void closePushLogEntry(const ProcessResult& result);
	void showError(const QString& title, const QString& details);

private:
	const std::shared_ptr<Repository> _repo;
	ConsoleLogView* _pushLogView = nullptr;
	std::vector<PushStep> _pushSteps; // the running push's plan
	std::vector<std::shared_ptr<Repository>> _pushedSubmodules; // their push locks are held until the push ends
};
