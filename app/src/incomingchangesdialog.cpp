#include "incomingchangesdialog.h"
#include "historymodels.h"
#include "repositoryfactory.h"
#include "theme.h"

DISABLE_COMPILER_WARNINGS
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

#include <utility>

namespace {

constexpr int DefaultWidth = 640;
constexpr int DefaultHeight = 440;
constexpr int TickMs = 1000;
constexpr int MaxWaitingNames = 5;

// An uninitialized submodule is an empty directory: nothing to check
bool isCloned(const RepositoryLocation& location)
{
	return QFileInfo::exists(location.root + (location.kind == VcsKind::Git ? QStringLiteral("/.git") : QStringLiteral("/.hg")));
}

// One repository's row: its name and answer, a child per listed commit, or a child per line of the failure
void fillRepositoryRow(QTreeWidgetItem& row, const QString& name, const QString& upstream,
	const std::expected<std::vector<CommitRecord>, QString>& commits, int total)
{
	QFont font = row.font(0);
	font.setBold(true);
	row.setFont(0, font);

	if (!commits)
	{
		row.setText(0, QObject::tr("%1 - could not check").arg(name));
		for (const QString& line : commits.error().split(QLatin1Char('\n'), Qt::SkipEmptyParts))
			new QTreeWidgetItem(&row, { line });
		row.setExpanded(true);
		return;
	}

	if (commits->empty())
	{
		row.setText(0, upstream.isEmpty() ? QObject::tr("%1 - nothing to pull").arg(name)
			: QObject::tr("%1 - nothing to pull from %2").arg(name, upstream));
		return;
	}

	row.setText(0, upstream.isEmpty() ? QObject::tr("%1 - %2 commit(s) to pull").arg(name, QString::number(total))
		: QObject::tr("%1 - %2 commit(s) to pull from %3").arg(name, QString::number(total), upstream));
	for (const CommitRecord& commit : *commits)
	{
		auto* child = new QTreeWidgetItem(&row, { shortSha(commit.sha) + QStringLiteral("  ") + commit.subject() });
		child->setFont(0, monospaceFont());
		child->setToolTip(0, commit.author + QLatin1Char('\n') + commit.date);
	}
	if (total > int(commits->size()))
	{
		new QTreeWidgetItem(&row, { QObject::tr("... and %1 more; only the newest %2 are listed")
			.arg(total - int(commits->size())).arg(MaxListedIncomingCommits) });
	}
	row.setExpanded(true);
}

} // namespace

IncomingChangesDialog::IncomingChangesDialog(Repository& repository, QWidget* parent) :
	QDialog(parent),
	_ownName(repository.name()),
	_ownUpstream(repository.state().upstream)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(tr("Incoming Changes - %1").arg(_ownName));
	resize(DefaultWidth, DefaultHeight);

	auto* layout = new QVBoxLayout(this);
	_statusLabel = new QLabel;
	_progressBar = new QProgressBar;
	_progressBar->setTextVisible(false);
	_waitingLabel = new QLabel;
	_waitingLabel->setWordWrap(true);
	_tree = new QTreeWidget;
	_tree->setHeaderHidden(true);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(_statusLabel);
	layout->addWidget(_progressBar);
	layout->addWidget(_waitingLabel);
	layout->addWidget(_tree, 1);
	layout->addWidget(buttons);

	_ownRow = new QTreeWidgetItem(_tree, { tr("%1 - checking...").arg(_ownName) });

	for (const QString& subPath : repository.state().submodules)
	{
		RepositoryLocation location = repository.nestedRepositoryLocation(subPath);
		if (isCloned(location))
			_submodules.push_back({ .path = subPath, .location = std::move(location) });
	}

	_ticker = new QTimer(this);
	connect(_ticker, &QTimer::timeout, this, &IncomingChangesDialog::updateProgress);
	_ticker->start(TickMs);
	_elapsed.start();
	updateProgress();

	if (!_submodules.empty())
	{
		repository.submoduleUpdateBranches(this, [this](std::expected<std::map<QString, QString>, QString> updateBranches) {
			startSubmoduleChecks(updateBranches);
		});
	}
}

void IncomingChangesDialog::setOwnAnswer(const std::expected<std::vector<CommitRecord>, QString>& commits, int total)
{
	if (std::exchange(_ownAnswered, true))
		return;

	fillRepositoryRow(*_ownRow, _ownName, _ownUpstream, commits, total);
	++_answeredCount;
	updateProgress();
}

void IncomingChangesDialog::startSubmoduleChecks(const std::expected<std::map<QString, QString>, QString>& updateBranches)
{
	for (size_t index = 0; index < _submodules.size(); ++index)
	{
		// Without the branches .gitmodules names, a detached submodule's comparison is unknown
		if (!updateBranches)
		{
			recordSubmoduleAnswer(index, std::unexpected(updateBranches.error()));
			continue;
		}

		SubmoduleCheck& check = _submodules[index];
		const auto branch = updateBranches->find(check.path);
		check.repository = sharedRepository(check.location);
		check.repository->checkIncoming(/*interactive=*/true, branch == updateBranches->end() ? QString{} : branch->second,
			MaxListedIncomingCommits, this, [this, index](std::expected<std::vector<CommitRecord>, QString> commits) {
				recordSubmoduleAnswer(index, commits);
			});
	}
}

void IncomingChangesDialog::recordSubmoduleAnswer(size_t index, const std::expected<std::vector<CommitRecord>, QString>& commits)
{
	SubmoduleCheck& check = _submodules[index];
	check.answered = true;
	++_answeredCount;

	if (commits && commits->empty())
	{
		if (!_upToDateRow)
			_upToDateRow = new QTreeWidgetItem(_tree);
		new QTreeWidgetItem(_upToDateRow, { check.path });
		_upToDateRow->sortChildren(0, Qt::AscendingOrder);
		_upToDateRow->setText(0, tr("%1 submodule(s) up to date").arg(_upToDateRow->childCount()));
	}
	else
	{
		// Rows keep path order whatever order the answers arrive in: after the repository's own, before the summary
		int position = 1;
		for (size_t earlier = 0; earlier < index; ++earlier)
			position += _submodules[earlier].hasRow ? 1 : 0;
		check.hasRow = true;

		auto* row = new QTreeWidgetItem;
		_tree->insertTopLevelItem(position, row);
		fillRepositoryRow(*row, check.path, {}, commits, check.repository ? check.repository->state().behind : 0);
	}
	updateProgress();
}

void IncomingChangesDialog::updateProgress()
{
	const int repositoryCount = 1 + int(_submodules.size());
	const qint64 seconds = _elapsed.elapsed() / 1000;

	if (_answeredCount == repositoryCount)
	{
		_ticker->stop();
		_progressBar->hide();
		_waitingLabel->hide();
		_statusLabel->setText(repositoryCount == 1 ? tr("Checked in %1 s").arg(seconds)
			: tr("Checked %1 repositories in %2 s").arg(repositoryCount).arg(seconds));
		return;
	}

	// Busy (maximum 0) until the first answer: an empty determinate bar does not move.
	// Set only on a change: this runs every tick, and the busy animation must not restart.
	const int maximum = _answeredCount == 0 ? 0 : repositoryCount;
	if (_progressBar->maximum() != maximum)
		_progressBar->setRange(0, maximum);
	_progressBar->setValue(_answeredCount);

	if (repositoryCount == 1)
	{
		_statusLabel->setText(tr("Checking %1... %2 s").arg(_ownName, QString::number(seconds)));
		_waitingLabel->hide();
		return;
	}

	_statusLabel->setText(tr("Checked %1 of %2 repositories... %3 s").arg(_answeredCount).arg(repositoryCount).arg(seconds));
	QStringList waiting;
	if (!_ownAnswered)
		waiting.push_back(_ownName);
	for (const SubmoduleCheck& check : _submodules)
	{
		if (!check.answered)
			waiting.push_back(check.path);
	}
	const qsizetype unnamed = waiting.size() - MaxWaitingNames;
	waiting = waiting.mid(0, MaxWaitingNames);
	_waitingLabel->setText(unnamed > 0 ? tr("Waiting for %1 and %2 more").arg(waiting.join(QStringLiteral(", ")), QString::number(unnamed))
		: tr("Waiting for %1").arg(waiting.join(QStringLiteral(", "))));
}
