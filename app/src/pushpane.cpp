#include "pushpane.h"
#include "commitwindow.h"
#include "consolelogview.h"
#include "repositoryfactory.h"
#include "repositorywindows.h"

#include "assert/advanced_assert.h"
#include "dialogs/messagedialog.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <utility>

PushPane::PushPane(std::shared_ptr<Repository> repo, QWidget* parent) :
	QWidget{ parent },
	_repo{ std::move(repo) }
{
	auto* pushLogLayout = new QVBoxLayout(this);
	pushLogLayout->setContentsMargins(0, 0, 0, 0);
	pushLogLayout->setSpacing(0);
	auto* pushLogHeader = new QFrame;
	pushLogHeader->setObjectName(QStringLiteral("pushLogHeader"));
	auto* pushLogHeaderLayout = new QHBoxLayout(pushLogHeader);
	pushLogHeaderLayout->setContentsMargins(8, 6, 8, 6);
	pushLogHeaderLayout->addWidget(new QLabel(tr("Push output")));
	pushLogHeaderLayout->addStretch();
	auto* hidePushLogButton = new QPushButton(tr("Hide"));
	pushLogHeaderLayout->addWidget(hidePushLogButton);
	pushLogLayout->addWidget(pushLogHeader);
	_pushLogView = new ConsoleLogView;
	_pushLogView->setMinimumHeight(70);
	_pushLogView->setMaximumHeight(170);
	pushLogLayout->addWidget(_pushLogView);

	connect(hidePushLogButton, &QPushButton::clicked, this, &QWidget::hide);
	hide();
}

bool PushPane::pushRunning() const
{
	return _repo->pushHeldBy(this);
}

void PushPane::startPush(PushMode mode)
{
	_repo->beginPush(this);
	_pushLogView->clearLog();

	_repo->planPush(mode, [this, mode](std::expected<std::vector<PushStep>, QString> steps) {
		if (!steps)
		{
			endPush();
			showError(tr("Cannot push"), steps.error());
			return;
		}

		_pushSteps = std::move(*steps);
		if (!lockPushedSubmodules() || (mode == PushMode::OneByOne && !confirmOneByOnePush()))
		{
			endPush();
			return;
		}
		runPushStep(0, /*setUpstream=*/false);
	});
}

bool PushPane::lockPushedSubmodules()
{
	std::vector<std::shared_ptr<Repository>> submodules;
	for (const RepositoryLocation& location : _repo->submodulesPushedBy(_pushSteps))
	{
		std::shared_ptr<Repository> submodule = sharedRepository(location);
		if (submodule->writeInFlight())
		{
			MessageDialog::notice(this, tr("Cannot push"), tr("Submodule '%1' has a write of its own in flight. Wait for it to finish, then push again.")
				.arg(QDir{ _repo->path() }.relativeFilePath(location.root)), {});
			return false;
		}
		submodules.push_back(std::move(submodule));
	}

	for (const std::shared_ptr<Repository>& submodule : submodules)
		submodule->beginPush(this);
	_pushedSubmodules = std::move(submodules);
	return true;
}

void PushPane::endPush()
{
	const std::vector<std::shared_ptr<Repository>> submodules = std::exchange(_pushedSubmodules, {});
	for (const std::shared_ptr<Repository>& submodule : submodules)
		submodule->endPush();
	_repo->endPush();

	// Any of them may have been pushed. One without a window stays unrefreshed: nothing shows its state.
	for (const std::shared_ptr<Repository>& submodule : submodules)
	{
		submodule->announcePush();
		if (CommitWindow* window = repositoryWindow(submodule->path()))
			window->refreshRepository();
	}
}

void PushPane::refreshAfterPush()
{
	_repo->refresh();
	_repo->announcePush();
}

bool PushPane::confirmOneByOnePush()
{
	const RepoState& state = _repo->state();
	const auto commitCount = int(std::ranges::count_if(_pushSteps, [](const PushStep& step) { return !step.commit.isEmpty(); }));
	QString text = tr("Push %1 commit(s) to %2 one at a time? Each push can start a CI run.").arg(commitCount).arg(state.upstream);
	if (const int publishedWithMerges = state.ahead - commitCount; publishedWithMerges > 0)
		text += QLatin1Char('\n') + tr("%1 more commit(s) arrive with the merges that bring them in.").arg(publishedWithMerges);
	if (std::ranges::any_of(_pushSteps, [](const PushStep& step) { return !step.subject.isEmpty(); }))
		text += QLatin1Char('\n') + tr("Submodules with unpushed commits are pushed first, with one push each.");

	return MessageDialog::question(this, tr("Push one by one?"), text, { tr("Push one by one") }) == 0;
}

void PushPane::runPushStep(size_t index, bool setUpstream)
{
	assert_r(index < _pushSteps.size());
	const PushStep& step = _pushSteps[index];

	show(); // before the entry: the log's scrolling needs a laid-out viewport
	_pushLogView->beginEntry(_repo->pushCommandLabel(step, setUpstream));

	const auto onDone = [this, index, setUpstream](const ProcessResult& result) {
		closePushLogEntry(result);

		if (result.ok)
		{
			if (index + 1 < _pushSteps.size())
			{
				runPushStep(index + 1, /*setUpstream=*/false);
				return;
			}
			endPush();
			refreshAfterPush();
			return;
		}

		std::optional<QString> missingUpstream;
		if (!setUpstream) // the retry already carried one
			missingUpstream = _repo->missingUpstreamName(_pushSteps[index], result);

		if (missingUpstream && offerUpstreamThenRetry(index, *missingUpstream))
			return;

		endPush();
		if (index > 0 && _pushSteps[index - 1].subject.isEmpty()) // a one-by-one push published some of this repository's commits
			refreshAfterPush();
		if (!missingUpstream) // a declined offer already named this failure
			showError(tr("Push failed"), result.errorText());
	};

	Vcs::Job* job = _repo->runPushStep(step, setUpstream, onDone);
	_pushLogView->setEncoding(converterEncoding(job->textEncoding()));
	job->streamTo([this](const QByteArray& chunk) { _pushLogView->appendOutput(chunk); });
}

bool PushPane::offerUpstreamThenRetry(size_t index, const QString& upstream)
{
	const PushStep& step = _pushSteps[index];
	const QString text = step.subject.isEmpty()
		? tr("The branch '%1' has no upstream configured. Push it to '%2' and set the upstream?").arg(step.branch, upstream)
		: tr("Submodule '%1' is on branch '%2', which has no upstream configured. Push it to '%3' and set the upstream?")
			.arg(step.subject, step.branch, upstream);

	if (MessageDialog::question(this, tr("No upstream branch"), text, { tr("Push and set upstream") }) != 0)
		return false;

	runPushStep(index, /*setUpstream=*/true);
	return true;
}

void PushPane::closePushLogEntry(const ProcessResult& result)
{
	// The output was streamed into the log as it ran; only the verdict is left to add
	if (result.ok)
		_pushLogView->appendNote(tr("Succeeded"), ConsoleLogView::NoteKind::Success);
	else if (result.outcome == ProcessOutcome::Exited)
		_pushLogView->appendNote(tr("Process failed with exit code %1").arg(result.exitCode), ConsoleLogView::NoteKind::Failure);
	else // no exit code: never started, or died without exiting
		_pushLogView->appendNote(result.errorText(), ConsoleLogView::NoteKind::Failure);
}

void PushPane::showError(const QString& title, const QString& details)
{
	// The command's output verbatim: hook output is what makes a rejected push diagnosable
	MessageDialog::notice(this, title, title + QLatin1Char('.'), details);
}
