#include "historywindow.h"
#include "commitgraphdelegate.h"
#include "diffpane.h"
#include "filelistview.h"
#include "fileviewerwindow.h"
#include "repositoryfactory.h"
#include "settings.h"
#include "theme.h"

#include "settings/csettings.h"
#include "settingsui/csettingsdialog.h"
#include "widgets/clabelelided.h"
#include "widgets/cpersistentwindow.h"
#include "widgets/widgetutils.h"

DISABLE_COMPILER_WARNINGS
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {

// A cold open lists this many first, then extends to the full depth in the background: the walk's cost is
// proportional to its depth
constexpr int InitialCommitBatch = 500;
constexpr int SearchDebounceMs = 200; // a keystroke rescans every commit and relaunches the detail queries
constexpr int FileListWidth = 320;
constexpr int MaxFilePathLabelWidth = 420; // beyond this the path elides
constexpr int PickaxeEditWidth = 320;
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

} // namespace

HistoryWindow::HistoryWindow(const RepositoryLocation& location, QWidget* parent) :
	HistoryWindow(location, {}, parent)
{
}

HistoryWindow::HistoryWindow(const RepositoryLocation& location, const QString& filePath, QWidget* parent) :
	QMainWindow(parent, Qt::Window),
	_repo{ openRepository(location) },
	_query{ .maxCommits = CSettings{}.value(Settings::HistoryMaxCommitsKey, Settings::HistoryMaxCommitsDefault).toInt(), .path = filePath }
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(_query.path.isEmpty()
		? tr("History - %1 - GoodGit").arg(_repo->name())
		: tr("History of %1 - %2 - GoodGit").arg(_query.path, _repo->name()));
	buildUi();

	// One geometry for every history window
	installEventFilter(new CPersistenceEnabler(QStringLiteral("HistoryWindow"), this));

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

	_detailSplitter = new QSplitter(Qt::Horizontal);
	_detailSplitter->setChildrenCollapsible(false);
	_detailSplitter->setHandleWidth(1);
	_detailSplitter->addWidget(filesPane);
	_detailSplitter->addWidget(_diffPane);
	_detailSplitter->setStretchFactor(0, 0);
	_detailSplitter->setStretchFactor(1, 1);
	if (const QByteArray state = CSettings{}.value(Settings::HistoryWindowDetailSplitterKey).toByteArray(); !state.isEmpty())
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
	if (const QByteArray state = CSettings{}.value(Settings::HistoryWindowSplitterKey).toByteArray(); !state.isEmpty())
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
	connect(_logView, &QWidget::customContextMenuRequested, this, &HistoryWindow::showCommitContextMenu);
	connect(_filesView, &QWidget::customContextMenuRequested, this, &HistoryWindow::showFileContextMenu);
	connect(_logView->selectionModel(), &QItemSelectionModel::currentChanged, this, &HistoryWindow::showFilesForCurrentCommit);
	connect(_filesView->selectionModel(), &QItemSelectionModel::currentChanged, this, &HistoryWindow::showDiffForCurrentFile);
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

void HistoryWindow::closeEvent(QCloseEvent* event)
{
	CSettings settings;
	settings.setValue(Settings::HistoryWindowSplitterKey, _splitter->saveState());
	settings.setValue(Settings::HistoryWindowDetailSplitterKey, _detailSplitter->saveState());
	QMainWindow::closeEvent(event);
}

void HistoryWindow::refreshUnpushedMarks()
{
	_unpushedQuery.cancel();
	_unpushedQuery = _repo->unpushedCommits(this, [this](std::expected<QSet<QString>, QString> shas) {
		// A failure is the ordinary "no upstream to compare against"
		_logModel.setUnpushedShas(std::move(shas).value_or(QSet<QString>{}));
	});
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

void HistoryWindow::reload()
{
	_logQuery.cancel(); // including a pending full-depth phase
	_pickaxeQuery.cancel();

	refreshUnpushedMarks();
	_logLoaded = false;
	_fullLoadPending = false;
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
			// The next walk must not stay rooted at a revision this one could not resolve: F5 would
			// re-run the same failing query forever
			_query.startRevision.clear();
			_revealSha.clear();
			_logModel.setCommits({}); // the reset clears the panes below through currentChanged
			_countLabel->clear();
			_loadMoreButton->setVisible(false);
			_diffPane->showMessage({}, {}, result.error());
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

		if (!_logModel.extendCommits(std::move(commits)))
			selectLoadedCommit(); // the extension fell back to a reset, which dropped the selection
		else if (!_revealSha.isEmpty())
			selectLoadedCommit(); // a reveal the first batch missed; otherwise the user's selection stands
		updateCountLabel();
	});
}

void HistoryWindow::revealCommit(const QString& sha)
{
	_revealSha = sha;
	if (_logLoaded)
		selectLoadedCommit(); // otherwise the listing's completion does it
}

void HistoryWindow::selectLoadedCommit()
{
	if (!_revealSha.isEmpty())
	{
		if (const int row = _logModel.rowOfSha(_revealSha); row >= 0)
		{
			const QModelIndex index = _logModel.index(row, CommitLogModel::CommitColumn);
			_revealSha.clear();
			_logView->setCurrentIndex(index); // updates the panes below through currentChanged
			_logView->scrollTo(index, QAbstractItemView::PositionAtCenter);
			return;
		}
		if (_fullLoadPending)
			return; // the full depth may list it; decided when it lands
		if (_query.startRevision != _revealSha)
		{
			// Not on the line of history the walk covered, so walk from the commit itself
			_query.startRevision = _revealSha;
			reload();
			return;
		}
		_revealSha.clear(); // walked from it and still not listed
	}

	if (_logModel.rowCount() > 0)
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

void HistoryWindow::openSubmoduleHistory(const CommitFileChange& entry)
{
	// Not deduplicated, matching the commit window's submodule windows
	auto* window = new HistoryWindow(_repo->submoduleLocation(entry.path), this);
	window->show();
	window->revealCommit(entry.submoduleSha);
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
	const QString sha = _logModel.commitAt(index.row()).sha;

	QMenu menu{ this };
	menu.addAction(tr("Copy long hash"), this, [sha] { QApplication::clipboard()->setText(sha); });
	menu.addAction(tr("Copy short hash"), this, [sha] { QApplication::clipboard()->setText(shortSha(sha)); });
	menu.exec(_logView->viewport()->mapToGlobal(pos));
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
	}
	menu.exec(_filesView->viewport()->mapToGlobal(pos));
}

void HistoryWindow::openFileHistory(const QString& filePath)
{
	auto* window = new HistoryWindow(_repo->location(), filePath, this);
	window->show();
}

void HistoryWindow::openFileViewer(const QString& sha, const QString& repoRelativePath)
{
	auto* window = new FileViewerWindow(*_repo, sha, repoRelativePath, this);
	window->show();
}

void HistoryWindow::showFilesForCurrentCommit()
{
	_filesQuery.cancel();
	_fileCountsQuery.cancel();

	const QModelIndex current = _logView->currentIndex();
	if (!current.isValid() || current.row() >= _logModel.rowCount())
	{
		_filesModel.clear();
		_fileCountLabel->clear();
		_diffPane->showMessage({}, {}, {});
		return;
	}

	const CommitRecord& commit = _logModel.commitAt(current.row());

	// Both replaced before the queries go out, so neither outlives the commit it describes. The pane shows
	// the message until a file is picked from the list.
	_filesModel.clear();
	showCommitMessage(commit);

	const bool isMerge = commit.parents.size() > 1;
	_fileCountLabel->setToolTip(isMerge
		? tr("A merge commit has no diff of its own: its changes depend on which parent it is compared against.")
		: QString{});
	if (isMerge)
	{
		_fileCountLabel->setText(tr("merge commit"));
		return;
	}

	_fileCountLabel->setText(tr("Loading..."));
	const QString sha = commit.sha;
	_filesQuery = _repo->commitFiles(sha, this, [this, sha](std::expected<std::vector<CommitFileChange>, QString> result) {
		if (!result)
		{
			_filesModel.clear();
			_fileCountLabel->clear();
			_diffPane->showMessage({}, shortSha(sha), result.error());
			return;
		}

		const int fileCount = int(result->size());
		_filesModel.setEntries(*std::move(result));
		_fileCountLabel->setText(fileCount == 1 ? tr("1 file") : tr("%1 files").arg(fileCount));
	});
	// A separate query, so the rows may appear before their counts; a failure costs only the counts
	_fileCountsQuery = _repo->commitFileCounts(sha, this, [this](std::expected<std::map<QString, LineCounts>, QString> counts) {
		_filesModel.setLineCounts(std::move(counts).value_or(std::map<QString, LineCounts>{}));
	});
}

void HistoryWindow::showDiffForCurrentFile()
{
	_diffQuery.cancel();

	const std::optional<CommitFileChange> currentFile = fileEntryAt(_filesView->currentSourceIndex());
	const std::optional<CommitRecord> commit = currentCommit();
	if (!currentFile || !commit)
		return; // no file picked: the pane keeps the commit message

	const CommitFileChange& entry = *currentFile;
	const QString sha = commit->sha;
	const QString tag = shortSha(sha);

	_diffPane->showMessage(entry.path, tag, tr("Loading..."));
	const qint64 maxBytes = CSettings{}.value(Settings::MaxShownDiffBytesKey, Settings::MaxShownDiffBytesDefault).toLongLong();
	_diffQuery = _repo->commitFileDiff(sha, entry, maxBytes, this, [this, entry, tag](std::expected<QByteArray, QString> diff) {
		if (!diff)
			_diffPane->showMessage(entry.path, tag, diff.error()); // an oversize diff fails here, never held whole
		else if (diff->isEmpty())
			_diffPane->showMessage(entry.path, tag, tr("No content changes (only the mode or the line endings differ, or a rename with identical content)."));
		else
			_diffPane->showDiff(entry.path, tag, QString::fromUtf8(*diff));
	});
}

void HistoryWindow::showCommitMessage(const CommitRecord& commit)
{
	_diffPane->showMessage({}, shortSha(commit.sha), commit.message);
}

