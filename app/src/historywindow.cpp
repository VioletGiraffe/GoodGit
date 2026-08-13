#include "historywindow.h"
#include "diffhighlighter.h"
#include "filelistdelegate.h"
#include "settings.h"
#include "theme.h"

#include "widgets/clabelmidelision.h"
#include "widgets/cpersistentwindow.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

constexpr int InitialMaxCommits = 20000;
constexpr qsizetype MaxDiffBytes = 2 * 1024 * 1024;
constexpr int FileListWidth = 320;

} // namespace

HistoryWindow::HistoryWindow(const QString& repoPath, QWidget* parent) :
	QMainWindow(parent, Qt::Window),
	_repo{ repoPath },
	_maxCommits{ InitialMaxCommits }
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(tr("History - %1 - GoodGit").arg(_repo.name()));
	buildUi();

	const QString geometryKey = QStringLiteral("HistoryWindow_")
		+ QString::fromLatin1(QCryptographicHash::hash(QDir::cleanPath(_repo.path()).toUtf8(), QCryptographicHash::Md5).toHex());
	installEventFilter(new CPersistenceEnabler(geometryKey, this));

	reload();
}

void HistoryWindow::buildUi()
{
	auto* logPane = new QWidget;
	auto* logLayout = new QVBoxLayout(logPane);
	logLayout->setContentsMargins(0, 0, 0, 0);
	logLayout->setSpacing(0);

	auto* logBar = new QFrame;
	logBar->setObjectName(QStringLiteral("repoBar"));
	auto* logBarLayout = new QHBoxLayout(logBar);
	logBarLayout->setContentsMargins(8, 6, 8, 6);
	_countLabel = new QLabel;
	_loadMoreButton = new QPushButton(tr("Load more"));
	_loadMoreButton->setToolTip(tr("Re-read the log with twice the limit"));
	_loadMoreButton->setVisible(false);
	auto* refreshButton = new QPushButton(tr("Refresh"));
	refreshButton->setToolTip(QStringLiteral("F5"));
	logBarLayout->addWidget(_countLabel);
	logBarLayout->addStretch();
	logBarLayout->addWidget(_loadMoreButton);
	logBarLayout->addWidget(refreshButton);
	logLayout->addWidget(logBar);

	_logView = new QTreeView;
	_logView->setModel(&_logModel);
	_logView->setRootIsDecorated(false);
	_logView->setUniformRowHeights(true);
	_logView->setAllColumnsShowFocus(true);
	_logView->setSelectionBehavior(QAbstractItemView::SelectRows);
	_logView->header()->setStretchLastSection(false); // it would override Date's resize mode, and Subject is the one to grow
	_logView->header()->setSectionResizeMode(CommitLogModel::ShaColumn, QHeaderView::ResizeToContents);
	_logView->header()->setSectionResizeMode(CommitLogModel::SubjectColumn, QHeaderView::Stretch);
	_logView->header()->setSectionResizeMode(CommitLogModel::AuthorColumn, QHeaderView::ResizeToContents);
	_logView->header()->setSectionResizeMode(CommitLogModel::DateColumn, QHeaderView::ResizeToContents);
	logLayout->addWidget(_logView, 1);

	auto* filesPane = new QWidget;
	auto* filesLayout = new QVBoxLayout(filesPane);
	filesLayout->setContentsMargins(0, 0, 0, 0);
	filesLayout->setSpacing(0);

	auto* fileCountBar = new QFrame;
	fileCountBar->setObjectName(QStringLiteral("counterBar"));
	auto* fileCountLayout = new QHBoxLayout(fileCountBar);
	fileCountLayout->setContentsMargins(8, 4, 8, 4);
	_fileCountLabel = new QLabel;
	fileCountLayout->addWidget(_fileCountLabel);
	fileCountLayout->addStretch();
	filesLayout->addWidget(fileCountBar);

	_filesView = new QTreeView;
	_filesView->setModel(&_filesModel);
	_filesView->setItemDelegate(new FileListDelegate{ _filesView });
	_filesView->setRootIsDecorated(false);
	_filesView->setUniformRowHeights(true);
	_filesView->setAllColumnsShowFocus(true);
	_filesView->setSelectionBehavior(QAbstractItemView::SelectRows);
	_filesView->header()->hide();
	_filesView->header()->setSectionResizeMode(CommitFilesModel::StateColumn, QHeaderView::ResizeToContents);
	_filesView->header()->setSectionResizeMode(CommitFilesModel::PathColumn, QHeaderView::Stretch);
	filesLayout->addWidget(_filesView, 1);

	auto* diffPane = new QWidget;
	auto* diffLayout = new QVBoxLayout(diffPane);
	diffLayout->setContentsMargins(0, 0, 0, 0);
	diffLayout->setSpacing(0);

	auto* diffHeader = new QFrame;
	diffHeader->setObjectName(QStringLiteral("diffHeader"));
	auto* diffHeaderLayout = new QHBoxLayout(diffHeader);
	diffHeaderLayout->setContentsMargins(8, 6, 8, 6);
	_diffPathLabel = new CLabelMidElision;
	_diffPathLabel->setFont(monospaceFont());
	_diffTagLabel = new QLabel;
	_diffTagLabel->setObjectName(QStringLiteral("diffTagLabel"));
	diffHeaderLayout->addWidget(_diffPathLabel, 1);
	diffHeaderLayout->addWidget(_diffTagLabel);
	diffLayout->addWidget(diffHeader);

	_diffView = new QPlainTextEdit;
	_diffView->setObjectName(QStringLiteral("diffView"));
	_diffView->setReadOnly(true);
	_diffView->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	_diffView->setFont(monospaceFont());
	new DiffHighlighter(_diffView->document());
	diffLayout->addWidget(_diffView, 1);

	_detailSplitter = new QSplitter(Qt::Horizontal);
	_detailSplitter->setChildrenCollapsible(false);
	_detailSplitter->setHandleWidth(1);
	_detailSplitter->addWidget(filesPane);
	_detailSplitter->addWidget(diffPane);
	_detailSplitter->setStretchFactor(0, 0);
	_detailSplitter->setStretchFactor(1, 1);
	if (const QByteArray state = Settings::splitterState(QStringLiteral("HistoryWindowDetail")); !state.isEmpty())
		_detailSplitter->restoreState(state);
	else
		_detailSplitter->setSizes({ FileListWidth, 860 });

	_splitter = new QSplitter(Qt::Vertical);
	_splitter->setChildrenCollapsible(false);
	_splitter->setHandleWidth(1);
	_splitter->addWidget(logPane);
	_splitter->addWidget(_detailSplitter);
	_splitter->setStretchFactor(0, 1);
	_splitter->setStretchFactor(1, 1);
	if (const QByteArray state = Settings::splitterState(QStringLiteral("HistoryWindow")); !state.isEmpty())
		_splitter->restoreState(state);
	else
		_splitter->setSizes({ 340, 420 });
	setCentralWidget(_splitter);
	resize(1180, 780);

	connect(refreshButton, &QPushButton::clicked, this, &HistoryWindow::reload);
	connect(_loadMoreButton, &QPushButton::clicked, this, [this] {
		_maxCommits *= 2;
		reload();
	});
	connect(_logView->selectionModel(), &QItemSelectionModel::currentChanged, this, &HistoryWindow::showFilesForCurrentCommit);
	connect(_filesView->selectionModel(), &QItemSelectionModel::currentChanged, this, &HistoryWindow::showDiffForCurrentFile);

	new QShortcut(QKeySequence(Qt::Key_F5), this, [this] { reload(); });
	new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] { close(); });
}

void HistoryWindow::closeEvent(QCloseEvent* event)
{
	Settings::setSplitterState(QStringLiteral("HistoryWindow"), _splitter->saveState());
	Settings::setSplitterState(QStringLiteral("HistoryWindowDetail"), _detailSplitter->saveState());
	QMainWindow::closeEvent(event);
}

void HistoryWindow::reload()
{
	if (_logJob)
		_logJob->cancel();

	_countLabel->setText(tr("Loading..."));
	_logJob = _repo.commitLog(_maxCommits, this, [this](const GitResult& result) {
		if (!result.ok)
		{
			_logModel.setCommits({}); // resetting the model clears the panes below through currentChanged
			_countLabel->clear();
			_loadMoreButton->setVisible(false);
			setDiffText({}, {}, result.errorText());
			return;
		}

		std::vector<CommitRecord> commits = parseCommitLog(result.out);
		// Exactly the limit means the walk was cut short, not that history ends here
		const bool capped = int(commits.size()) >= _maxCommits;
		_countLabel->setText(capped ? tr("%1 commits, more to load").arg(commits.size()) : tr("%1 commits").arg(commits.size()));
		_loadMoreButton->setVisible(capped);

		_logModel.setCommits(std::move(commits));
		if (_logModel.commitCount() > 0)
			_logView->setCurrentIndex(_logModel.index(0, CommitLogModel::ShaColumn));
	});
}

void HistoryWindow::showFilesForCurrentCommit()
{
	if (_filesJob)
		_filesJob->cancel();

	const QModelIndex current = _logView->currentIndex();
	if (!current.isValid() || current.row() >= _logModel.commitCount())
	{
		_filesModel.setEntries({});
		_fileCountLabel->clear();
		setDiffText({}, {}, {});
		return;
	}

	const CommitRecord& commit = _logModel.commitAt(current.row());
	if (commit.parents.size() > 1)
	{
		_filesModel.setEntries({});
		_fileCountLabel->setText(tr("merge commit"));
		setDiffText({}, shortSha(commit.sha),
			tr("Merge of %1 parents.\n\nA merge has no diff of its own - what it changed depends on which parent it is compared against.")
				.arg(commit.parents.size()));
		return;
	}

	// Neither the file list nor the diff may outlive the commit they describe, so both go before the query
	_filesModel.setEntries({});
	_fileCountLabel->setText(tr("Loading..."));
	setDiffText({}, shortSha(commit.sha), {});

	const QString sha = commit.sha;
	_filesJob = _repo.commitFiles(sha, this, [this, sha](const GitResult& result) {
		if (!result.ok)
		{
			_filesModel.setEntries({});
			_fileCountLabel->clear();
			setDiffText({}, shortSha(sha), result.errorText());
			return;
		}

		std::vector<NameStatusEntry> entries = parseNameStatusZ(result.out);
		const int fileCount = int(entries.size());
		_filesModel.setEntries(std::move(entries));
		_fileCountLabel->setText(fileCount == 1 ? tr("1 file") : tr("%1 files").arg(fileCount));
		if (fileCount > 0)
			_filesView->setCurrentIndex(_filesModel.index(0, CommitFilesModel::StateColumn));
	});
}

void HistoryWindow::showDiffForCurrentFile()
{
	if (_diffJob)
		_diffJob->cancel();

	const QModelIndex currentFile = _filesView->currentIndex();
	const QModelIndex currentCommit = _logView->currentIndex();
	if (!currentFile.isValid() || currentFile.row() >= _filesModel.rowCount()
		|| !currentCommit.isValid() || currentCommit.row() >= _logModel.commitCount())
	{
		return; // showFilesForCurrentCommit already put the commit-level state in the pane
	}

	const NameStatusEntry entry = _filesModel.entryAt(currentFile.row());
	const QString sha = _logModel.commitAt(currentCommit.row()).sha;
	const QString tag = shortSha(sha);

	setDiffText(entry.path, tag, tr("Loading..."));
	_diffJob = _repo.commitFileDiff(sha, entry, this, [this, entry, tag](const GitResult& result) {
		if (!result.ok)
			setDiffText(entry.path, tag, result.errorText());
		else if (result.out.size() > MaxDiffBytes)
			setDiffText(entry.path, tag, tr("The diff is too large to display (%1 MB).").arg(result.out.size() / (1024 * 1024)));
		else if (result.out.isEmpty())
			setDiffText(entry.path, tag, tr("No content changes (a mode-only change, or a rename with identical content)."));
		else
			setDiffText(entry.path, tag, QString::fromUtf8(result.out));
	});
}

void HistoryWindow::setDiffText(const QString& pathLabel, const QString& tag, const QString& text)
{
	_diffPathLabel->setText(pathLabel);
	_diffTagLabel->setText(tag);
	_diffView->setPlainText(text);
}
