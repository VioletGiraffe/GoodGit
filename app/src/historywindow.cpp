#include "historywindow.h"
#include "commitgraphdelegate.h"
#include "diffpane.h"
#include "filelistview.h"
#include "fileviewerwindow.h"
#include "repositoryfactory.h"
#include "settings.h"
#include "theme.h"

#include "appdialogs/csettingsnotifier.h"
#include "dialogs/messagedialog.h"
#include "widgets/clabelelided.h"
#include "widgets/cpersistentwindow.h"
#include "widgets/widgetutils.h"

DISABLE_COMPILER_WARNINGS
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QTextDocument>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtMath>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <vector>

namespace {

// A cold open lists this many first, then extends to the full depth in the background: the walk's cost is
// proportional to its depth
constexpr int InitialCommitBatch = 500;
constexpr int SearchDebounceMs = 200; // a keystroke rescans every commit and relaunches the detail queries
constexpr int FileListWidth = 320;
constexpr int MaxFilePathLabelWidth = 420; // beyond this the path elides
constexpr int MaxScopeBoxWidth = 300;      // a longer branch name is cut
// The branch selector's item data
constexpr int ScopeRole = Qt::UserRole;
constexpr int TipRole = Qt::UserRole + 1;
constexpr int PickaxeEditWidth = 320;
constexpr int MaxShownBodyLines = 8; // a longer body scrolls
constexpr int CascadeStep = 28;  // about a title bar, so the window underneath stays identifiable
constexpr int CascadeLength = 8; // steps before the search gives up, which keeps the last one on screen
constexpr qsizetype MaxShownPickaxeTerm = 24;

// The revisions holding one listed change's content. An empty sha means that revision does not hold the file.
struct FileRevisionTargets
{
	QString thisSha;
	QString parentSha;
	QString pathInParent;
};

FileRevisionTargets fileRevisionTargets(const CommitFileChange& entry, const CommitRecord& commit)
{
	FileRevisionTargets targets;
	if (entry.type != ChangeType::Deleted)
		targets.thisSha = commit.sha;
	if (entry.type != ChangeType::Added)
		targets.parentSha = commit.parents.value(0); // a merge lists no files, so the first parent is the only one
	targets.pathInParent = entry.oldPath.isEmpty() ? entry.path : entry.oldPath; // a rename is under its old name there
	return targets;
}

// The whole repository's history, or none: a file history is not it.
// The open windows are the registry, as with the repository windows.
HistoryWindow* repositoryHistoryWindow(const QString& repositoryRoot)
{
	for (HistoryWindow* window : WidgetUtils::findTopLevelWindows<HistoryWindow>())
	{
		if (window->filePath().isEmpty() && sameDirectoryOnDisk(window->repositoryPath(), repositoryRoot))
			return window;
	}
	return nullptr;
}

} // namespace

HistoryWindow* showRepositoryHistory(const RepositoryLocation& location)
{
	HistoryWindow* window = repositoryHistoryWindow(location.root);
	if (!window)
		window = new HistoryWindow(location);

	WidgetUtils::bringWindowToFront(window);
	return window;
}

HistoryWindow::HistoryWindow(const RepositoryLocation& location) :
	HistoryWindow(location, {})
{
}

HistoryWindow::HistoryWindow(const RepositoryLocation& location, const QString& filePath) :
	_repo{ sharedRepository(location) },
	_query{ .maxCommits = QSettings{}.value(Settings::HistoryMaxCommitsKey, Settings::HistoryMaxCommitsDefault).toInt(),
		.scope = Repository::LogQuery::Scope::CheckedOut, .path = filePath }
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(_query.path.isEmpty()
		? tr("History - %1 - GoodGit").arg(_repo->name())
		: tr("History of %1 - %2 - GoodGit").arg(_query.path, _repo->name()));
	buildUi();

	// One geometry for every history window
	enablePersistence(this, QStringLiteral("HistoryWindow"));

	WidgetUtils::callOnReturnFromOtherApp(this, [this] { reloadIfHistoryChanged(); });
	connect(_repo.get(), &Repository::historyChanged, this, &HistoryWindow::reload);
	connect(_repo.get(), &Repository::pushed, this, &HistoryWindow::refreshUnpushedMarks);

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
	_scopeBox = new QComboBox;
	_scopeBox->setToolTip(tr("Whose history is listed: every branch's, the checked-out commit's, or one branch's"));
	_scopeBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	_scopeBox->setMaximumWidth(MaxScopeBoxWidth);
	rebuildScopeBox(); // the fixed entries, until the branches arrive
	_filePathLabel = new CLabelElided;
	_filePathLabel->setFont(monospaceFont());
	connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, this, [this] { _filePathLabel->setFont(monospaceFont()); });
	_filePathLabel->setText(_query.path);
	_filePathLabel->setToolTip(_query.path);
	_filePathLabel->setMaximumWidth(MaxFilePathLabelWidth);
	_filePathLabel->setVisible(!_query.path.isEmpty());
	_countLabel = new QLabel;
	_searchEdit = new QLineEdit;
	_searchEdit->setPlaceholderText(tr("Search commits"));
	_searchEdit->setToolTip(tr("Ctrl+F. Show only the commits whose hash, author, refs, date or message contain this text"));
	_searchEdit->setClearButtonEnabled(true);
	_searchEdit->setMinimumWidth(220);
	_searchEdit->installEventFilter(this);
	_pickaxeButton = new QPushButton(tr("Find in contents..."));
	_pickaxeButton->setToolTip(tr("Reload the log with only the commits whose diff contains a given text"));
	_loadMoreButton = new QPushButton(tr("Load more"));
	_loadMoreButton->setToolTip(tr("Reload the log with twice as many commits"));
	_loadMoreButton->setVisible(false);
	auto* refreshButton = new QPushButton(tr("Refresh"));
	refreshButton->setToolTip(QStringLiteral("F5"));
	logBarLayout->addWidget(new QLabel(tr("Branch:")));
	logBarLayout->addWidget(_scopeBox);
	logBarLayout->addWidget(_filePathLabel);
	logBarLayout->addWidget(_countLabel);
	logBarLayout->addStretch();
	logBarLayout->addWidget(_searchEdit);
	logBarLayout->addWidget(_pickaxeButton);
	logBarLayout->addWidget(_loadMoreButton);
	logBarLayout->addWidget(refreshButton);
	logLayout->addWidget(logBar);

	_logView = new QTreeView;
	_logView->setModel(&_logModel);
	_logView->setRootIsDecorated(false);
	_logView->setUniformRowHeights(true);
	_logView->setAllColumnsShowFocus(true);
	_logView->setSelectionBehavior(QAbstractItemView::SelectRows);
	_logView->setContextMenuPolicy(Qt::CustomContextMenu);
	_logView->setItemDelegateForColumn(CommitLogModel::GraphColumn, new CommitGraphDelegate{ _logView });
	_logView->header()->setStretchLastSection(false); // Subject is the column to grow, not Date
	_logView->header()->setSectionResizeMode(CommitLogModel::GraphColumn, QHeaderView::ResizeToContents);
	_logView->header()->setSectionResizeMode(CommitLogModel::CommitColumn, QHeaderView::ResizeToContents);
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

	_filesView = new FileListView;
	_filesView->setModel(&_filesModel);
	filesLayout->addWidget(_filesView, 1);

	_diffPane = new DiffPane;
	// The other file's row shows its section at once, from the set the end came from, so the scroll lands
	connect(_diffPane, &DiffPane::foreignEndActivated, this, [this](const ForeignEnd& end) {
		const int row = _filesModel.rowOfPath(end.path);
		if (row < 0)
			return;
		_filesView->setSelectedSourceRows({ row }, row);
		_diffPane->scrollDiffLineToTop(end.diffLine);
	});

	_detailSplitter = new QSplitter(Qt::Horizontal);
	_detailSplitter->setChildrenCollapsible(false);
	_detailSplitter->setHandleWidth(1);
	_detailSplitter->addWidget(filesPane);
	_detailSplitter->addWidget(_diffPane);
	_detailSplitter->setStretchFactor(0, 0);
	_detailSplitter->setStretchFactor(1, 1);
	_detailSplitter->setContentsMargins(4, 0, 4, 4);
	if (const QByteArray state = QSettings{}.value(Settings::HistoryWindowDetailSplitterKey).toByteArray(); !state.isEmpty())
		_detailSplitter->restoreState(state);
	else
		_detailSplitter->setSizes({ FileListWidth, 860 });

	_bodyPane = new QFrame;
	_bodyPane->setObjectName(QStringLiteral("commitBodyPane"));
	_bodyPane->setVisible(false);
	auto* bodyLayout = new QVBoxLayout(_bodyPane);
	bodyLayout->setContentsMargins(0, 0, 0, 1); // the pane's bottom border
	_bodyView = new QPlainTextEdit;
	_bodyView->setReadOnly(true);
	_bodyView->setFont(monospaceFont());
	bodyLayout->addWidget(_bodyView);
	connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, this, [this] {
		_bodyView->setFont(monospaceFont());
		showCommitBody(_bodyView->toPlainText()); // the height follows the line spacing
	});

	auto* detailPane = new QWidget;
	auto* detailLayout = new QVBoxLayout(detailPane);
	detailLayout->setContentsMargins(0, 0, 0, 0);
	detailLayout->setSpacing(0);
	detailLayout->addWidget(_bodyPane);
	detailLayout->addWidget(_detailSplitter, 1);

	_splitter = new QSplitter(Qt::Vertical);
	_splitter->setChildrenCollapsible(false);
	_splitter->setHandleWidth(1);
	_splitter->addWidget(logPane);
	_splitter->addWidget(detailPane);
	_splitter->setStretchFactor(0, 1);
	_splitter->setStretchFactor(1, 1);
	if (const QByteArray state = QSettings{}.value(Settings::HistoryWindowSplitterKey).toByteArray(); !state.isEmpty())
		_splitter->restoreState(state);
	else
		_splitter->setSizes({ 340, 420 });
	setCentralWidget(_splitter);
	resize(1180, 780);

	connect(refreshButton, &QPushButton::clicked, this, &HistoryWindow::reload);
	connect(_loadMoreButton, &QPushButton::clicked, this, [this] {
		// Disabled until the walk answers: a repeat click would cancel it and double the limit again
		_loadMoreButton->setEnabled(false);
		_query.maxCommits *= 2;
		_missedRevealSha.clear(); // the deeper walk may hold it
		// The pickaxe result is capped like the listing, so it deepens with it
		if (!_query.contentSearch.isEmpty())
			startPickaxeQuery();
		loadRemainingCommits();
	});
	_searchDebounce = new QTimer(this);
	_searchDebounce->setSingleShot(true);
	_searchDebounce->setInterval(SearchDebounceMs);
	connect(_searchDebounce, &QTimer::timeout, this, &HistoryWindow::applySearch);
	connect(_searchEdit, &QLineEdit::textChanged, _searchDebounce, qOverload<>(&QTimer::start));
	connect(_pickaxeButton, &QPushButton::clicked, this, &HistoryWindow::showPickaxePopup);
	connect(_scopeBox, &QComboBox::activated, this, &HistoryWindow::onScopeChosen); // a user's pick, never a refill
	connect(_logView, &QWidget::customContextMenuRequested, this, &HistoryWindow::showCommitContextMenu);
	connect(_filesView, &QWidget::customContextMenuRequested, this, &HistoryWindow::showFileContextMenu);
	connect(_logView->selectionModel(), &QItemSelectionModel::currentChanged, this, &HistoryWindow::showFilesForCurrentCommit);
	connect(_filesView->selectionModel(), &QItemSelectionModel::currentChanged, this, &HistoryWindow::showDiffForCurrentFile);
	// A model reset clears the current row without emitting currentChanged.
	// These run after the views' own reset handlers, which setModel connected first.
	connect(&_logModel, &QAbstractItemModel::modelReset, this, &HistoryWindow::showFilesForCurrentCommit);
	connect(&_filesModel, &QAbstractItemModel::modelReset, this, &HistoryWindow::showDiffForCurrentFile);
	connect(_filesView, &FileListView::rowActivated, this, &HistoryWindow::onFileRowActivated);

	new QShortcut(QKeySequence(Qt::Key_F5), this, [this] { reload(); });
	new QShortcut(QKeySequence::Find, this, [this] {
		_searchEdit->setFocus();
		_searchEdit->selectAll();
	});
	new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] {
		if (_searchEdit->text().isEmpty())
			close();
		else
			_searchEdit->clear();
	});

	_logView->setFocus(); // the search box would otherwise take it, being first in the tab order
}

bool HistoryWindow::eventFilter(QObject* watched, QEvent* event)
{
	// Down and Enter move focus from the search box to the list, applying a still-pending debounced search
	// first: applySearch makes the first match current, so the arrows work straight away
	if (watched == _searchEdit && event->type() == QEvent::KeyPress)
	{
		const int key = static_cast<QKeyEvent*>(event)->key();
		if (key == Qt::Key_Down || key == Qt::Key_Return || key == Qt::Key_Enter)
		{
			if (_searchDebounce->isActive())
			{
				_searchDebounce->stop();
				applySearch();
			}
			_logView->setFocus();
			return true;
		}
	}
	return QMainWindow::eventFilter(watched, event);
}

void HistoryWindow::showEvent(QShowEvent* event)
{
	QMainWindow::showEvent(event);
	if (_cascaded)
		return;
	_cascaded = true;

	// One stored geometry serves every history window, so a second one opens exactly on top of the first.
	// The persistence filter restores that geometry on this same event, ahead of this handler.
	const auto positionTaken = [this](QPoint position) {
		const std::vector<HistoryWindow*> windows = WidgetUtils::findTopLevelWindows<HistoryWindow>();
		return std::ranges::any_of(windows, [&](const HistoryWindow* window) {
			return window != this && window->isVisible() && window->pos() == position;
		});
	};

	QPoint position = pos();
	for (int step = 0; step < CascadeLength && positionTaken(position); ++step)
		position += QPoint{ CascadeStep, CascadeStep };
	if (position != pos())
		move(position);
}

void HistoryWindow::closeEvent(QCloseEvent* event)
{
	// Nothing cancels a Vcs::Job: a checkout this window holds would answer after it is gone
	if (_repo->writeHeldBy(this))
	{
		event->ignore();
		MessageDialog::notice(this, tr("Cannot close yet"), tr("A checkout is running. Wait for it to finish, then close the window."), {});
		return;
	}

	QSettings settings;
	settings.setValue(Settings::HistoryWindowSplitterKey, _splitter->saveState());
	settings.setValue(Settings::HistoryWindowDetailSplitterKey, _detailSplitter->saveState());
	QMainWindow::closeEvent(event);
}

void HistoryWindow::refreshCurrentCommitMark()
{
	_currentCommitQuery.cancel();
	_currentCommitQuery = _repo->currentCommit(this, [this](std::expected<QString, QString> sha) {
		// A failure is the ordinary "unborn repository": nothing is checked out to mark
		_logModel.setCurrentSha(std::move(sha).value_or(QString{}));
	});
}

void HistoryWindow::refreshUnpushedMarks()
{
	_unpushedQuery.cancel();
	_unpushedQuery = _repo->unpushedCommits(this, [this](std::expected<QSet<QString>, QString> shas) {
		// A failure is the ordinary "no upstream to compare against"
		_logModel.setUnpushedShas(std::move(shas).value_or(QSet<QString>{}));
	});
}

void HistoryWindow::refreshBranchTips()
{
	_branchTipsQuery.cancel();
	_branchTipsQuery = _repo->branchTips(this, [this](std::expected<std::vector<BranchTip>, QString> tips) {
		// A failure offers the commit itself alone
		_branchTips = std::move(tips).value_or(std::vector<BranchTip>{});
		rebuildScopeBox();
	});
}

void HistoryWindow::rebuildScopeBox()
{
	using Scope = Repository::LogQuery::Scope;
	_scopeBox->clear();
	const auto addEntry = [this](const QString& text, Scope scope, const QString& tip = {}) {
		_scopeBox->addItem(text);
		const int row = _scopeBox->count() - 1;
		_scopeBox->setItemData(row, int(scope), ScopeRole);
		_scopeBox->setItemData(row, tip, TipRole);
	};
	if (_query.path.isEmpty()) // a file history is walked from one tip (LogQuery::path)
		addEntry(tr("All branches"), Scope::EveryRef);
	// Named after the current branch: hg's active bookmark over its named branch, both being current there.
	// Nameless on a detached HEAD, and until the tips arrive.
	const auto current = std::ranges::find_if(_branchTips, [](const BranchTip& tip) { return tip.current && !tip.namedBranch; });
	const auto currentNamed = std::ranges::find_if(_branchTips, &BranchTip::current);
	const auto currentTip = current != _branchTips.end() ? current : currentNamed;
	addEntry(currentTip != _branchTips.end() ? tr("Checked out (%1)").arg(currentTip->name) : tr("Checked out"), Scope::CheckedOut);

	// Three groups, the current branch first in its group, the rest alphabetical
	std::vector<const BranchTip*> sorted;
	for (const BranchTip& tip : _branchTips)
		sorted.push_back(&tip);
	std::ranges::sort(sorted, [](const BranchTip* a, const BranchTip* b) {
		return a->current != b->current ? a->current : a->name.compare(b->name, Qt::CaseInsensitive) < 0;
	});
	const auto addGroup = [&](auto belongs) {
		bool first = true;
		for (const BranchTip* tip : sorted)
		{
			if (!belongs(*tip))
				continue;
			if (first)
				_scopeBox->insertSeparator(_scopeBox->count());
			first = false;
			addEntry(tip->name, Scope::Tip, tip->name);
		}
	};
	addGroup([](const BranchTip& tip) { return !tip.remoteTracking && !tip.namedBranch; });
	addGroup([](const BranchTip& tip) { return tip.namedBranch; });
	addGroup([](const BranchTip& tip) { return tip.remoteTracking; });

	const int row = scopeRow(_query.scope, _query.tip);
	if (row >= 0)
	{
		_scopeBox->setCurrentIndex(row);
		return;
	}
	// The selected branch is gone, and so is the listing: its walk fails.
	// Queued: this runs inside the branch query's answer, which reload() cancels.
	QMetaObject::invokeMethod(this, [this] { setScope(Scope::CheckedOut, {}); }, Qt::QueuedConnection);
}

int HistoryWindow::scopeRow(Repository::LogQuery::Scope scope, const QString& tip) const
{
	for (int row = 0; row < _scopeBox->count(); ++row)
	{
		if (_scopeBox->itemData(row, ScopeRole).toInt() == int(scope) && _scopeBox->itemData(row, TipRole).toString() == tip)
			return row;
	}
	return -1;
}

void HistoryWindow::setScope(Repository::LogQuery::Scope scope, const QString& tip)
{
	_query.scope = scope;
	_query.tip = tip;
	_scopeBox->setCurrentIndex(scopeRow(scope, tip));
	reload();
}

void HistoryWindow::onScopeChosen(int row)
{
	const auto scope = Repository::LogQuery::Scope(_scopeBox->itemData(row, ScopeRole).toInt());
	const QString tip = _scopeBox->itemData(row, TipRole).toString();
	if (scope != _query.scope || tip != _query.tip)
		setScope(scope, tip);
}

// The narrower half of the content search, run alongside the listing; its result marks rows within it
void HistoryWindow::startPickaxeQuery()
{
	_pickaxeQuery.cancel();
	_pickaxeQuery = _repo->commitsAddingOrRemovingText(_query, this, [this](std::expected<QSet<QString>, QString> shas) {
		_logModel.setAddingOrRemovingShas(std::move(shas).value_or(QSet<QString>{}));
		updateCountLabel();
	});
}

void HistoryWindow::reloadIfHistoryChanged()
{
	refreshUnpushedMarks(); // a push can publish commits without moving any ref the fingerprint covers
	_returnCheckQuery.cancel();
	_returnCheckQuery = _repo->historyFingerprint(this, [this](std::expected<QString, QString> fingerprint) {
		if (!fingerprint || *fingerprint != _listedFingerprint)
			reload();
	});
}

void HistoryWindow::reload()
{
	_logQuery.cancel(); // including a pending full-depth phase
	_pickaxeQuery.cancel();

	// The listing is replaced wholesale, dropping the selection with it; the row is found again by its sha
	_reselectSha = selectedSha();

	refreshUnpushedMarks();
	refreshCurrentCommitMark();
	refreshBranchTips();
	// Read in parallel with the walk: a change landing between the two reads goes unseen until the next change
	_listedFingerprintQuery.cancel();
	_listedFingerprintQuery = _repo->historyFingerprint(this, [this](std::expected<QString, QString> fingerprint) {
		_listedFingerprint = std::move(fingerprint).value_or(QString{});
	});
	_logLoaded = false;
	_fullLoadPending = false;
	_missedRevealSha.clear();
	_countLabel->setText(tr("Loading..."));

	// The diagram needs every commit between the ones it draws; a path limit or a content search leaves gaps
	_logView->setColumnHidden(CommitLogModel::GraphColumn, !_query.path.isEmpty() || !_query.contentSearch.isEmpty());

	// The narrower half of the search runs in parallel with the listing and marks rows within it
	_logModel.setAddingOrRemovingShas({});
	if (!_query.contentSearch.isEmpty())
		startPickaxeQuery();

	// A cold open starts with a small batch and extends to the full depth in the background. A window already
	// showing rows re-runs in one phase: shrinking to the batch and growing back would be a visible collapse.
	Repository::LogQuery firstQuery = _query;
	if (_logModel.totalCount() == 0 && _query.contentSearch.isEmpty())
		firstQuery.maxCommits = std::min(_query.maxCommits, InitialCommitBatch);

	_logQuery = _repo->commitLog(firstQuery, this,
		[this, phase1Limit = firstQuery.maxCommits](std::expected<std::vector<CommitRecord>, QString> result) {
		_loadMoreButton->setEnabled(true); // disabled since a Load-more click
		if (!result)
		{
			_logCapped = false;
			_revealSha.clear();
			_logModel.setCommits({}); // the reset clears the panes below
			_countLabel->clear();
			_loadMoreButton->setVisible(false);
			_diffPane->showMessage({}, result.error());
			return;
		}

		std::vector<CommitRecord> commits = *std::move(result);
		// Reaching the limit means the walk was cut short
		const bool capped = int(commits.size()) >= phase1Limit;
		_fullLoadPending = capped && phase1Limit < _query.maxCommits;
		_logCapped = capped && !_fullLoadPending;
		_loadMoreButton->setVisible(_logCapped);

		_logModel.setCommits(std::move(commits)); // re-applies the active search
		_logLoaded = true;
		updateCountLabel();
		selectLoadedCommit();

		if (_fullLoadPending)
			loadRemainingCommits();
	});
}

void HistoryWindow::loadRemainingCommits()
{
	_logQuery.cancel(); // a reload the Load-more click raced with
	_logQuery = _repo->commitLog(_query, this, [this](std::expected<std::vector<CommitRecord>, QString> result) {
		_loadMoreButton->setEnabled(true); // disabled since a Load-more click
		_fullLoadPending = false;
		if (!result)
		{
			// The shown batch stands; only the depth behind it failed, and the button offers it again
			_logCapped = true;
			_loadMoreButton->setVisible(true);
			updateCountLabel();
			return;
		}

		std::vector<CommitRecord> commits = *std::move(result);
		_logCapped = int(commits.size()) >= _query.maxCommits;
		_loadMoreButton->setVisible(_logCapped);

		// Read before the extension, which may fall back to a reset that drops the selection
		const QString selected = selectedSha();
		if (!_logModel.extendCommits(std::move(commits)))
		{
			_reselectSha = selected;
			selectLoadedCommit();
		}
		else if (!_revealSha.isEmpty())
			selectLoadedCommit(); // a reveal the first batch missed; otherwise the user's selection stands
		updateCountLabel();
	});
}

void HistoryWindow::revealCommit(const QString& sha, RevealMiss onMiss)
{
	_revealSha = sha;
	_missedRevealSha.clear();
	// A listing still loading was walked after this call was decided on, so a miss in it is real
	_revealOnMiss = _logLoaded ? onMiss : RevealMiss::Report;
	if (_logLoaded)
		selectLoadedCommit(); // otherwise the listing's completion does it
}

void HistoryWindow::selectLoadedCommit()
{
	const auto selectRow = [this](int row) {
		const QModelIndex index = _logModel.index(row, CommitLogModel::CommitColumn);
		_logView->setCurrentIndex(index); // updates the panes below through currentChanged
		_logView->scrollTo(index, QAbstractItemView::PositionAtCenter);
	};

	if (!_revealSha.isEmpty())
	{
		if (const int row = _logModel.rowOfSha(_revealSha); row >= 0)
		{
			_revealSha.clear();
			_reselectSha.clear(); // the reveal outranks the row the reload replaced
			selectRow(row);
			return;
		}
		if (_fullLoadPending)
			return; // the full depth may list it; decided when it lands

		// Outside the scope, maybe: a submodule's recorded commit need not be on its checkout's line.
		// The widened walk is fresh, so it spends the retry too.
		if (_query.scope != Repository::LogQuery::Scope::EveryRef && _query.path.isEmpty())
		{
			_revealOnMiss = RevealMiss::Report;
			setScope(Repository::LogQuery::Scope::EveryRef, {}); // reload() leaves _revealSha for the new listing
			return;
		}

		if (_revealOnMiss == RevealMiss::ReloadOnce)
		{
			_revealOnMiss = RevealMiss::Report; // the retry is spent; reload() leaves _revealSha for it
			reload();
			return;
		}

		// Reachable from no ref, or older than the limit
		_missedRevealSha = _revealSha;
		_revealSha.clear();
		updateCountLabel();
	}

	if (!_reselectSha.isEmpty())
	{
		// Absent where the commit no longer shows: undone, or hidden by the active search
		const int row = _logModel.rowOfSha(_reselectSha);
		_reselectSha.clear();
		if (row >= 0)
		{
			selectRow(row);
			return;
		}
	}

	// The newest row stands in where a reset left no selection
	if (!_logView->currentIndex().isValid() && _logModel.rowCount() > 0)
		_logView->setCurrentIndex(_logModel.index(0, CommitLogModel::CommitColumn));
}

void HistoryWindow::onFileRowActivated(const QModelIndex& sourceIndex, Qt::KeyboardModifiers modifiers)
{
	const std::optional<CommitFileChange> entry = fileEntryAt(sourceIndex);
	if (!entry)
		return;
	if (entry->isSubmodule)
	{
		openSubmoduleHistory(*entry); // a pointer has no content to view at either revision
		return;
	}

	const std::optional<CommitRecord> commit = currentCommit();
	if (!commit)
		return;

	const FileRevisionTargets targets = fileRevisionTargets(*entry, *commit);
	const bool atParent = modifiers.testFlag(Qt::ShiftModifier);
	const QString& sha = atParent ? targets.parentSha : targets.thisSha;
	if (!sha.isEmpty()) // the revision asked for does not hold the file
		openFileViewer(sha, atParent ? targets.pathInParent : entry->path);
}

std::optional<CommitFileChange> HistoryWindow::fileEntryAt(const QModelIndex& sourceIndex) const
{
	if (!sourceIndex.isValid() || sourceIndex.row() >= _filesModel.rowCount())
		return {};
	return _filesModel.entryAt(sourceIndex.row());
}

std::optional<CommitRecord> HistoryWindow::currentCommit() const
{
	const QModelIndex current = _logView->currentIndex();
	if (!current.isValid() || current.row() >= _logModel.rowCount())
		return {};
	return _logModel.commitAt(current.row());
}

QString HistoryWindow::selectedSha() const
{
	const std::optional<CommitRecord> commit = currentCommit();
	return commit ? commit->sha : QString{};
}

void HistoryWindow::openSubmoduleHistory(const CommitFileChange& entry)
{
	// The gitlink names a commit the submodule holds, so a listing without it is out of date
	showRepositoryHistory(_repo->nestedRepositoryLocation(entry.path))->revealCommit(entry.submoduleSha, RevealMiss::ReloadOnce);
}

void HistoryWindow::applySearch()
{
	_logModel.setSearchText(_searchEdit->text());
	updateCountLabel();

	// Land on the first match, so the arrows step between matches from there
	if (_logModel.rowCount() > 0)
		_logView->setCurrentIndex(_logModel.index(0, CommitLogModel::CommitColumn));
}

void HistoryWindow::updateCountLabel()
{
	if (!_logLoaded)
		return; // the marks can arrive first, and their counts mean nothing against an empty list

	const int shown = _logModel.rowCount();
	const int total = _logModel.totalCount();

	QString text = shown == total ? tr("%1 commits").arg(total) : tr("%1 of %2 commits").arg(shown).arg(total);
	// Also during a search: finding nothing may only mean the commit is older than the limit
	if (_fullLoadPending)
		text = tr("%1, loading more...").arg(text);
	else if (_logCapped)
		text = tr("%1, more to load").arg(text);

	if (!_query.contentSearch.isEmpty())
	{
		text = tr("%1, %2 adding or removing it").arg(text).arg(_logModel.addingOrRemovingCount());
		// -S reaches into binary files, which -G cannot list for want of patch text
		if (const int unlisted = _logModel.addingOrRemovingNotListedCount(); unlisted > 0)
			text = tr("%1 (+%2 in binary files, not listed)").arg(text).arg(unlisted);
	}

	if (!_missedRevealSha.isEmpty())
		text = tr("%1. Commit %2 is not among them").arg(text, shortSha(_missedRevealSha));
	_countLabel->setText(text);
}

void HistoryWindow::showPickaxePopup()
{
	if (!_pickaxePopup)
	{
		_pickaxePopup = new QFrame(this, Qt::Popup);
		_pickaxePopup->setFrameShape(QFrame::StyledPanel);
		auto* popupLayout = new QVBoxLayout(_pickaxePopup);
		popupLayout->setContentsMargins(8, 8, 8, 8);
		popupLayout->setSpacing(6);

		auto* caption = new QLabel(tr("Show only the commits whose diff contains this text:"));
		_pickaxeEdit = new QLineEdit;
		_pickaxeEdit->setPlaceholderText(tr("Text to find"));
		_pickaxeEdit->setMinimumWidth(PickaxeEditWidth);
		_pickaxeIgnoreCaseBox = new QCheckBox(tr("Ignore case"));
		auto* findButton = new QPushButton(tr("Find"));
		auto* clearButton = new QPushButton(tr("Clear"));

		auto* buttonLayout = new QHBoxLayout;
		buttonLayout->addWidget(_pickaxeIgnoreCaseBox);
		buttonLayout->addStretch();
		buttonLayout->addWidget(clearButton);
		buttonLayout->addWidget(findButton);

		popupLayout->addWidget(caption);
		popupLayout->addWidget(_pickaxeEdit);
		popupLayout->addLayout(buttonLayout);

		const auto find = [this] { runPickaxe(_pickaxeEdit->text(), _pickaxeIgnoreCaseBox->isChecked()); };
		connect(findButton, &QPushButton::clicked, this, find);
		connect(_pickaxeEdit, &QLineEdit::returnPressed, this, find);
		connect(clearButton, &QPushButton::clicked, this, [this] {
			_pickaxeEdit->clear();
			runPickaxe({}, _pickaxeIgnoreCaseBox->isChecked());
		});
	}

	_pickaxeEdit->setText(_query.contentSearch);
	_pickaxeIgnoreCaseBox->setChecked(_query.ignoreCase);
	_pickaxePopup->adjustSize();
	WidgetUtils::placeCenteredOn(_pickaxePopup, this); // its button is in the top-right corner, too far out
	_pickaxePopup->show();
	_pickaxeEdit->setFocus();
	_pickaxeEdit->selectAll();
}

void HistoryWindow::runPickaxe(const QString& text, bool ignoreCase)
{
	_pickaxePopup->hide();
	_logView->setFocus(); // the popup would otherwise restore focus to its button

	if (_query.contentSearch == text && _query.ignoreCase == ignoreCase)
		return; // the identical walk would cost seconds and change nothing

	_query.contentSearch = text;
	_query.ignoreCase = ignoreCase;

	// The button doubles as the indicator that a content search is active, so it carries the term - clipped,
	// since a whole pasted line would push the rest of the bar off the window
	const QString shown = text.size() > MaxShownPickaxeTerm ? text.left(MaxShownPickaxeTerm) + QStringLiteral("...") : text;
	_pickaxeButton->setText(text.isEmpty() ? tr("Find in contents...") : tr("Contents: %1").arg(shown));
	_pickaxeButton->setToolTip(text.isEmpty() ? tr("Reload the log with only the commits whose diff contains a given text") : text);
	reload();
}

void HistoryWindow::showCommitContextMenu(const QPoint& pos)
{
	const QModelIndex index = _logView->indexAt(pos);
	if (!index.isValid() || index.row() >= _logModel.rowCount())
		return;

	// Read before exec() spins an event loop, in which a finishing log query could reset the model
	const CommitRecord commit = _logModel.commitAt(index.row());
	const QString subject = commit.subject();
	const QString& sha = commit.sha;

	QMenu menu{ this };
	QAction* copyTitleAction = menu.addAction(tr("Copy commit title"), this, [subject] { QApplication::clipboard()->setText(subject); });
	copyTitleAction->setEnabled(!subject.isEmpty());
	QAction* copyMessageAction = menu.addAction(tr("Copy message"), this, [message = commit.message] { QApplication::clipboard()->setText(message); });
	copyMessageAction->setEnabled(!commit.message.isEmpty());
	menu.addAction(tr("Copy long hash"), this, [sha] { QApplication::clipboard()->setText(sha); });
	menu.addAction(tr("Copy short hash"), this, [sha] { QApplication::clipboard()->setText(shortSha(sha)); });
	menu.addSeparator();
	addCheckoutMenu(menu, commit);
	menu.exec(_logView->viewport()->mapToGlobal(pos));
}

void HistoryWindow::addCheckoutMenu(QMenu& menu, const CommitRecord& commit)
{
	QMenu* checkoutMenu = menu.addMenu(tr("Check out"));
	const auto addTarget = [this, checkoutMenu](const QString& text, const CheckoutTarget& target) {
		// '&' doubled for display, or QMenu takes it as an accelerator marker
		return checkoutMenu->addAction(QString{ text }.replace(QLatin1Char('&'), QStringLiteral("&&")), this, [this, target] { checkOut(target); });
	};

	QSet<QString> localNames;
	QHash<QString, QString> remoteTrackingShas; // by name
	for (const BranchTip& tip : _branchTips)
	{
		if (tip.remoteTracking)
			remoteTrackingShas.insert(tip.name, tip.sha);
		else
			localNames.insert(tip.name);
	}

	for (const BranchTip& tip : _branchTips)
	{
		if (tip.sha != commit.sha || tip.remoteTracking || tip.current)
			continue;
		QAction* action = addTarget(tip.checkedOutElsewhere ? tr("'%1' (checked out in another worktree)").arg(tip.name) : tr("'%1'").arg(tip.name),
			{ .kind = CheckoutTarget::Kind::Branch, .branch = tip.name, .sha = commit.sha });
		action->setEnabled(!tip.checkedOutElsewhere);
	}
	for (const BranchTip& tip : _branchTips)
	{
		if (!tip.fastForwardable || remoteTrackingShas.value(tip.upstream) != commit.sha)
			continue;
		const QString text = tr("'%1' fast-forwarded to %2").arg(tip.name, tip.upstream);
		QAction* action = addTarget(tip.checkedOutElsewhere ? tr("%1 (checked out in another worktree)").arg(text) : text,
			{ .kind = CheckoutTarget::Kind::FastForward, .branch = tip.name, .sha = commit.sha });
		action->setEnabled(!tip.checkedOutElsewhere);
	}
	for (const BranchTip& tip : _branchTips)
	{
		if (tip.sha == commit.sha && tip.remoteTracking && !localNames.contains(tip.localName))
			addTarget(tr("New '%1' tracking %2").arg(tip.localName, tip.name),
				{ .kind = CheckoutTarget::Kind::TrackingBranch, .branch = tip.localName, .upstream = tip.name, .sha = commit.sha });
	}
	if (commit.sha != _logModel.currentSha())
	{
		addTarget(_repo->kind() == VcsKind::Git ? tr("Commit %1 (detached HEAD)").arg(shortSha(commit.sha)) : tr("Revision %1").arg(commit.revision.value_or(0)),
			{ .kind = CheckoutTarget::Kind::Commit, .sha = commit.sha });
	}

	checkoutMenu->menuAction()->setVisible(!checkoutMenu->isEmpty());
	checkoutMenu->setEnabled(!_repo->writeInFlight() && _repo->probeOperation() == RepoOp::None);
}

void HistoryWindow::checkOut(const CheckoutTarget& target)
{
	if (_repo->writeInFlight())
		return; // started while the menu was up

	_repo->beginMutation(this);
	_repo->checkOut(target, [this](std::expected<void, QString> result) {
		_repo->endMutation();
		if (!result)
			MessageDialog::notice(this, tr("Checkout failed"), tr("Checkout failed."), result.error());
		else
			_repo->announceHistoryChange();
		_repo->refresh();
	});
}

void HistoryWindow::showFileContextMenu(const QPoint& pos)
{
	// Read before exec() spins an event loop, in which a completing query could reset the model
	const std::optional<CommitFileChange> rowEntry = fileEntryAt(_filesView->sourceIndexAt(pos));
	if (!rowEntry)
		return;
	const CommitFileChange& entry = *rowEntry;

	// No current commit leaves both shas empty, which disables the two items that need one
	const FileRevisionTargets targets = fileRevisionTargets(entry, currentCommit().value_or(CommitRecord{}));

	QMenu menu{ this };
	if (entry.isSubmodule)
	{
		// The parent's log of a submodule path holds only the pointer moves
		menu.addAction(tr("View commit history"), this, [this, entry] { openSubmoduleHistory(entry); });
	}
	else
	{
		QAction* action = menu.addAction(tr("View file history"), this, [this, path = entry.path] { openFileHistory(path); });
		action->setEnabled(entry.path != _query.path); // this window already is that file's history

		menu.addSeparator();
		QAction* atThisCommit = menu.addAction(tr("View file at this commit"), this,
			[this, sha = targets.thisSha, path = entry.path] { openFileViewer(sha, path); });
		atThisCommit->setEnabled(!targets.thisSha.isEmpty());

		QAction* atParentCommit = menu.addAction(tr("View file at parent commit"), this,
			[this, sha = targets.parentSha, path = targets.pathInParent] { openFileViewer(sha, path); });
		atParentCommit->setEnabled(!targets.parentSha.isEmpty());
		// Display only: the row's activation handles the key. WidgetShortcut on an action belonging to no
		// widget never registers, so the key cannot trigger twice.
		atParentCommit->setShortcut(Qt::SHIFT | Qt::Key_Return);
		atParentCommit->setShortcutContext(Qt::WidgetShortcut);
		atParentCommit->setShortcutVisibleInContextMenu(true);

		menu.addSeparator();
		QAction* saveAtThisCommit = menu.addAction(tr("Save file at this commit as..."), this,
			[this, sha = targets.thisSha, path = entry.path] { saveFileRevisionAs(sha, path); });
		saveAtThisCommit->setEnabled(!targets.thisSha.isEmpty());

		QAction* saveAtParentCommit = menu.addAction(tr("Save file at parent commit as..."), this,
			[this, sha = targets.parentSha, path = targets.pathInParent] { saveFileRevisionAs(sha, path); });
		saveAtParentCommit->setEnabled(!targets.parentSha.isEmpty());
	}

	menu.addSeparator();
	addCopyPathActions(menu, { entry.path }, _repo->path());
	menu.exec(_filesView->viewport()->mapToGlobal(pos));
}

void HistoryWindow::openFileHistory(const QString& filePath)
{
	auto* window = new HistoryWindow(_repo->location(), filePath);
	window->show();
}

void HistoryWindow::openFileViewer(const QString& sha, const QString& repoRelativePath)
{
	auto* window = new FileViewerWindow(*_repo, sha, repoRelativePath, this);
	window->show();
}

void HistoryWindow::saveFileRevisionAs(const QString& sha, const QString& repoRelativePath)
{
	const QString fileName = repoRelativePath.section(QLatin1Char('/'), -1);
	// Never the repository folder by default: accepting the proposed name there would overwrite the working copy
	const QString startDirectory = QSettings{}.value(Settings::LastSaveDirectoryKey, QDir::homePath()).toString();
	const QString target = QFileDialog::getSaveFileName(this, tr("Save %1 @ %2").arg(fileName, shortSha(sha)), QDir{ startDirectory }.filePath(fileName));
	if (target.isEmpty())
		return;
	QSettings{}.setValue(Settings::LastSaveDirectoryKey, QFileInfo{ target }.absolutePath());

	// Not QApplication::setOverrideCursor(): the callback that restores the cursor is skipped once the window closes
	setCursor(Qt::BusyCursor);
	_repo->fileAtRevision(sha, repoRelativePath, /*maxBytes=*/0, this, [this, target](std::expected<QByteArray, QString> content) {
		unsetCursor();
		const QString failureTitle = tr("Failed to save the file");
		if (!content)
		{
			MessageDialog::notice(this, failureTitle, failureTitle + QLatin1Char('.'), content.error());
			return;
		}

		QSaveFile file{ target };
		if (!file.open(QIODevice::WriteOnly) || file.write(*content) != content->size() || !file.commit())
			MessageDialog::notice(this, failureTitle, failureTitle + QLatin1Char('.'), QDir::toNativeSeparators(target) + QStringLiteral(": ") + file.errorString());
	});
}

void HistoryWindow::showFilesForCurrentCommit()
{
	_filesQuery.cancel();
	_fileCountsQuery.cancel();
	_changeSetQuery.cancel();
	_changeSet.reset();
	_changeSetPending = false;

	const std::optional<CommitRecord> commit = currentCommit();
	if (!commit)
	{
		_filesModel.clear();
		_fileCountLabel->clear();
		showCommitBody({});
		_diffPane->showMessage({}, {});
		return;
	}

	const bool isMerge = commit->parents.size() > 1;
	// Both replaced before the queries go out, so neither outlives the commit it describes
	_filesModel.clear();
	_diffPane->showMessage({ .tag = shortSha(commit->sha) },
		isMerge ? tr("A merge commit has no diff of its own: its changes depend on which parent it is compared against.") : QString{});
	showCommitBody(commit->body());

	if (isMerge)
	{
		_fileCountLabel->setText(tr("merge commit"));
		return;
	}

	_fileCountLabel->setText(tr("Loading..."));
	const QString sha = commit->sha;
	_filesQuery = _repo->commitFiles(sha, this, [this, sha](std::expected<std::vector<CommitFileChange>, QString> result) {
		if (!result)
		{
			_filesModel.clear();
			_fileCountLabel->clear();
			_diffPane->showMessage({ .tag = shortSha(sha) }, result.error());
			return;
		}

		const int fileCount = int(result->size());
		_filesModel.setEntries(*std::move(result));
		_fileCountLabel->setText(fileCount == 1 ? tr("1 file") : tr("%1 files").arg(fileCount));
		const int lastShownRow = sha == _lastShownFile.sha ? _filesModel.rowOfPath(_lastShownFile.path) : -1;
		const int row = lastShownRow >= 0 ? lastShownRow : _filesView->firstShownSourceIndex().row(); // -1 for an empty list
		if (row >= 0)
			_filesView->setSelectedSourceRows({ row }, row);
		else
			_diffPane->showMessage({ .tag = shortSha(sha) }, tr("This commit changes no files."));
	});
	// A separate query, so the rows may appear before their counts; a failure costs only the counts
	_fileCountsQuery = _repo->commitFileCounts(sha, this, [this](std::expected<std::map<QString, LineCounts>, QString> counts) {
		_filesModel.setLineCounts(std::move(counts).value_or(std::map<QString, LineCounts>{}));
	});
	_changeSetPending = true;
	_changeSetQuery = _repo->commitDiff(sha, Settings::MaxChangeSetDiffBytes, this, [this](std::expected<QByteArray, QString> diff) {
		_changeSetPending = false;
		if (diff)
			_changeSet.emplace(QString::fromUtf8(*diff));
		if (_fileAwaitsChangeSet)
			showDiffForCurrentFile();
	});
}

void HistoryWindow::showDiffForCurrentFile()
{
	_diffQuery.cancel();
	_sizeQuery.cancel(); // its callback writes the header, which the next selection owns from here on
	_fileAwaitsChangeSet = false;

	// Every change of file passes through here, so a diff still shown is the last shown file's
	if (const std::optional<int> topLine = _diffPane->topLine())
		_lastShownFile.topLine = *topLine;

	const std::optional<CommitFileChange> currentFile = fileEntryAt(_filesView->currentSourceIndex());
	const std::optional<CommitRecord> commit = currentCommit();
	if (!currentFile || !commit)
		return;

	const CommitFileChange& entry = *currentFile;
	const QString sha = commit->sha;
	_currentItem = { entry.path, shortSha(sha) };
	if (_lastShownFile.sha != sha || _lastShownFile.path != entry.path)
		_lastShownFile = { sha, entry.path };

	const QString noContentText = tr("No content changes (only the mode or the line endings differ, or a rename with identical content).");
	_diffPane->showMessage(_currentItem, tr("Loading..."));

	// A submodule row's diff is not the path's own, so the set never answers for it
	const std::optional<int> section = !entry.isSubmodule && _changeSet ? _changeSet->fileIndex(entry.path) : std::nullopt;
	if (section)
	{
		_diffPane->showSection(_currentItem, *_changeSet, *section, Settings::maxShownDiffBytes(), noContentText);
		_diffPane->scrollLineToTop(_lastShownFile.topLine);
	}
	else if (!entry.isSubmodule && _changeSetPending)
		_fileAwaitsChangeSet = true;
	else
	{
		_diffQuery = _repo->commitFileDiff(sha, entry, Settings::maxShownDiffBytes(), this, [this, noContentText](std::expected<QByteArray, QString> diff) {
			if (!diff)
				_diffPane->showMessage(_currentItem, diff.error()); // an oversize diff fails here, never held whole
			else if (diff->isEmpty())
				_diffPane->showMessage(_currentItem, noContentText);
			else
			{
				_diffPane->showDiff(_currentItem, QString::fromUtf8(*diff));
				_diffPane->scrollLineToTop(_lastShownFile.topLine);
			}
		});
	}

	// A deleted file and a submodule pointer have no blob to size, and the query would only fail on them
	if (entry.type != ChangeType::Deleted && !entry.isSubmodule)
	{
		_sizeQuery = _repo->commitFileSize(sha, entry.path, this, [this](std::expected<qint64, QString> size) {
			if (!size)
				return; // the header simply states none

			_currentItem.size = formattedFileSize(*size);
			_diffPane->setHeader(_currentItem);
		});
	}
}

void HistoryWindow::showCommitBody(const QString& body)
{
	_bodyView->setPlainText(body);
	_bodyPane->setVisible(!body.isEmpty());

	// Logical lines only: a body with wrapped lines scrolls
	const int lineCount = std::min(int(body.count(QLatin1Char('\n'))) + 1, MaxShownBodyLines);
	const qreal textHeight = lineCount * QFontMetricsF{ _bodyView->font() }.lineSpacing() + 2 * _bodyView->document()->documentMargin();
	_bodyView->setFixedHeight(qCeil(textHeight) + 2 * _bodyView->frameWidth());
}

