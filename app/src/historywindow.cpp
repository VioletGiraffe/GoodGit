#include "historywindow.h"
#include "commitgraphdelegate.h"
#include "diffpane.h"
#include "filelistdelegate.h"
#include "repositoryfactory.h"
#include "settings.h"
#include "theme.h"

#include "timing/profiler.h"
#include "widgets/clabelelided.h"
#include "widgets/cpersistentwindow.h"
#include "widgets/widgetutils.h"

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
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// A cold open lists the first batch as soon as that little is read, then extends to the full depth in
// the background - the walk's cost is proportional to how much of history it covers
constexpr int InitialCommitBatch = 500;
constexpr int FullMaxCommits = 20000;
constexpr int FileListWidth = 320;
constexpr int MaxFilePathLabelWidth = 420; // beyond this the path elides rather than crowding the bar
constexpr int PickaxeEditWidth = 320;
constexpr qsizetype MaxShownPickaxeTerm = 24;

} // namespace

HistoryWindow::HistoryWindow(const RepositoryLocation& location, QWidget* parent) :
	HistoryWindow(location, {}, parent)
{
}

HistoryWindow::HistoryWindow(const RepositoryLocation& location, const QString& filePath, QWidget* parent) :
	QMainWindow(parent, Qt::Window),
	_repo{ openRepository(location) },
	_query{ .maxCommits = FullMaxCommits, .path = filePath }
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(_query.path.isEmpty()
		? tr("History - %1 - GoodGit").arg(_repo->name())
		: tr("History of %1 - %2 - GoodGit").arg(_query.path, _repo->name()));
	buildUi();

	// One geometry for every history window, keyed like the splitters beside it
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
	_filePathLabel->setText(_query.path);
	_filePathLabel->setToolTip(_query.path);
	_filePathLabel->setMaximumWidth(MaxFilePathLabelWidth);
	_filePathLabel->setVisible(!_query.path.isEmpty());
	_countLabel = new QLabel;
	_searchEdit = new QLineEdit;
	_searchEdit->setPlaceholderText(tr("Search commits"));
	_searchEdit->setToolTip(tr("Ctrl+F. Matches the hash, author, refs, date or message; non-matching commits are hidden"));
	_searchEdit->setClearButtonEnabled(true);
	_searchEdit->setMinimumWidth(220);
	_searchEdit->installEventFilter(this);
	_pickaxeButton = new QPushButton(tr("Find in contents..."));
	_pickaxeButton->setToolTip(tr("Re-read the log, keeping only the commits whose diff touches the text"));
	_loadMoreButton = new QPushButton(tr("Load more"));
	_loadMoreButton->setToolTip(tr("Re-read the log with twice the limit"));
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
	_logView->header()->setStretchLastSection(false); // it would override Date's resize mode, and Subject is the one to grow
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

	_filesView = new QTreeView;
	_filesView->setModel(&_filesModel);
	_filesView->setItemDelegate(new FileListDelegate{ _filesView });
	_filesView->setRootIsDecorated(false);
	_filesView->setUniformRowHeights(true);
	_filesView->setAllColumnsShowFocus(true);
	_filesView->setSelectionBehavior(QAbstractItemView::SelectRows);
	_filesView->setContextMenuPolicy(Qt::CustomContextMenu);
	_filesView->header()->hide();
	_filesView->header()->setSectionResizeMode(CommitFilesModel::StateColumn, QHeaderView::ResizeToContents);
	_filesView->header()->setSectionResizeMode(CommitFilesModel::AddedColumn, QHeaderView::ResizeToContents);
	_filesView->header()->setSectionResizeMode(CommitFilesModel::RemovedColumn, QHeaderView::ResizeToContents);
	_filesView->header()->setSectionResizeMode(CommitFilesModel::PathColumn, QHeaderView::Stretch);
	filesLayout->addWidget(_filesView, 1);

	_diffPane = new DiffPane;

	_detailSplitter = new QSplitter(Qt::Horizontal);
	_detailSplitter->setChildrenCollapsible(false);
	_detailSplitter->setHandleWidth(1);
	_detailSplitter->addWidget(filesPane);
	_detailSplitter->addWidget(_diffPane);
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
		_query.maxCommits *= 2;
		reload();
	});
	connect(_searchEdit, &QLineEdit::textChanged, this, &HistoryWindow::applySearch);
	connect(_pickaxeButton, &QPushButton::clicked, this, &HistoryWindow::showPickaxePopup);
	connect(_logView, &QWidget::customContextMenuRequested, this, &HistoryWindow::showCommitContextMenu);
	connect(_filesView, &QWidget::customContextMenuRequested, this, &HistoryWindow::showFileContextMenu);
	connect(_logView->selectionModel(), &QItemSelectionModel::currentChanged, this, &HistoryWindow::showFilesForCurrentCommit);
	connect(_filesView->selectionModel(), &QItemSelectionModel::currentChanged, this, &HistoryWindow::showDiffForCurrentFile);
	connect(_filesView, &QAbstractItemView::activated, this, &HistoryWindow::onFileRowActivated);

	new QShortcut(QKeySequence(Qt::Key_F5), this, [this] { reload(); });
	new QShortcut(QKeySequence::Find, this, [this] {
		_searchEdit->setFocus();
		_searchEdit->selectAll();
	});
	new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] {
		if (_searchEdit->text().isEmpty())
			close();
		else
			_searchEdit->clear(); // closing the window out from under a search in progress would be a surprise
	});

	_logView->setFocus(); // the search box would otherwise take it, being first in the tab order
}

bool HistoryWindow::eventFilter(QObject* watched, QEvent* event)
{
	// Down and Enter hand the search box over to the list, closing the Ctrl+F - type - navigate loop.
	// applySearch has already made the first match current, so the arrows work straight away.
	if (watched == _searchEdit && event->type() == QEvent::KeyPress)
	{
		const int key = static_cast<QKeyEvent*>(event)->key();
		if (key == Qt::Key_Down || key == Qt::Key_Return || key == Qt::Key_Enter)
		{
			_logView->setFocus();
			return true;
		}
	}
	return QMainWindow::eventFilter(watched, event);
}

void HistoryWindow::closeEvent(QCloseEvent* event)
{
	Settings::setSplitterState(QStringLiteral("HistoryWindow"), _splitter->saveState());
	Settings::setSplitterState(QStringLiteral("HistoryWindowDetail"), _detailSplitter->saveState());
	QMainWindow::closeEvent(event);
}

void HistoryWindow::refreshUnpushedMarks()
{
	_unpushedQuery.cancel();
	_unpushedQuery = _repo->unpushedCommits(this, [this](std::expected<QSet<QString>, QString> shas) {
		// A failure is the ordinary "no upstream to compare against", and marks nothing
		_logModel.setUnpushedShas(std::move(shas).value_or(QSet<QString>{}));
	});
}

void HistoryWindow::reload()
{
	_logQuery.cancel(); // a pending full-depth phase with it
	_pickaxeQuery.cancel();

	refreshUnpushedMarks();
	_logLoaded = false;
	_fullLoadPending = false;
	_countLabel->setText(tr("Loading..."));

	// The diagram needs a listing holding every commit between the ones it draws. A path limit prunes them
	// and a content search names a scattered handful, so neither walk leaves lines that could be drawn.
	_logView->setColumnHidden(CommitLogModel::GraphColumn, !_query.path.isEmpty() || !_query.contentSearch.isEmpty());

	// The narrower -S half runs beside the listing, marking within it rather than producing a list of
	// its own; the two are independent walks, so they overlap instead of queueing behind each other
	_logModel.setAddingOrRemovingShas({});
	if (!_query.contentSearch.isEmpty())
	{
		_pickaxeQuery = _repo->commitsAddingOrRemovingText(_query, this, [this](std::expected<QSet<QString>, QString> shas) {
			_logModel.setAddingOrRemovingShas(std::move(shas).value_or(QSet<QString>{}));
			updateCountLabel();
		});
	}

	// A cold open pays for the walk in proportion to its limit, so it starts with a small batch and
	// extends to the full depth in the background. A window already showing rows re-runs in one phase:
	// resetting it down to the batch just to grow back would make every refresh a visible collapse.
	Repository::LogQuery firstQuery = _query;
	if (_logModel.totalCount() == 0 && _query.contentSearch.isEmpty())
		firstQuery.maxCommits = std::min(_query.maxCommits, InitialCommitBatch);

	_logQuery = _repo->commitLog(firstQuery, this,
		[this, phase1Limit = firstQuery.maxCommits](std::expected<std::vector<CommitRecord>, QString> result) {
		if (!result)
		{
			_logCapped = false;
			_logModel.setCommits({}); // resetting the model clears the panes below through currentChanged
			_countLabel->clear();
			_loadMoreButton->setVisible(false);
			_diffPane->showDiff({}, {}, result.error());
			return;
		}

		std::vector<CommitRecord> commits = *std::move(result);
		PROFILE_MARK(QStringLiteral("history log parsed (%1 commits)").arg(commits.size()).toUtf8().constData());
		// Exactly the limit means the walk was cut short, not that history ends here
		const bool capped = int(commits.size()) >= phase1Limit;
		_fullLoadPending = capped && phase1Limit < _query.maxCommits;
		_logCapped = capped && !_fullLoadPending;
		_loadMoreButton->setVisible(_logCapped);

		_logModel.setCommits(std::move(commits)); // re-applies the active search to the new records
		_logLoaded = true;
		updateCountLabel();
		selectLoadedCommit();
		PROFILE_MARK("history populated");

		if (_fullLoadPending)
			loadRemainingCommits();
	});
}

void HistoryWindow::loadRemainingCommits()
{
	_logQuery = _repo->commitLog(_query, this, [this](std::expected<std::vector<CommitRecord>, QString> result) {
		_fullLoadPending = false;
		if (!result)
		{
			// The batch on show stands; what failed is only the depth behind it, and the button offers it again
			_logCapped = true;
			_loadMoreButton->setVisible(true);
			updateCountLabel();
			return;
		}

		std::vector<CommitRecord> commits = *std::move(result);
		PROFILE_MARK(QStringLiteral("history full log parsed (%1 commits)").arg(commits.size()).toUtf8().constData());
		_logCapped = int(commits.size()) >= _query.maxCommits;
		_loadMoreButton->setVisible(_logCapped);

		if (!_logModel.extendCommits(std::move(commits)))
			selectLoadedCommit(); // the extension fell back to a reset, and the selection went with it
		else if (!_revealSha.isEmpty())
			selectLoadedCommit(); // a reveal the first batch missed; never re-selects over the user otherwise
		updateCountLabel();
		PROFILE_MARK("history fully populated");
	});
}

void HistoryWindow::revealCommit(const QString& sha)
{
	_revealSha = sha;
	if (_logLoaded)
		selectLoadedCommit(); // already listed, so nothing is coming to carry the reveal
}

void HistoryWindow::selectLoadedCommit()
{
	if (!_revealSha.isEmpty())
	{
		if (const int row = _logModel.rowOfSha(_revealSha); row >= 0)
		{
			const QModelIndex index = _logModel.index(row, CommitLogModel::CommitColumn);
			_revealSha.clear();
			_logView->setCurrentIndex(index); // carries the panes below with it, through currentChanged
			_logView->scrollTo(index, QAbstractItemView::PositionAtCenter);
			return;
		}
		if (_fullLoadPending)
			return; // the full depth is still coming and may list it; decided when it lands
		if (_query.startRevision != _revealSha)
		{
			// Not on the line of history the walk covered, so the walk has to start at the commit itself
			_query.startRevision = _revealSha;
			reload();
			return;
		}
		_revealSha.clear(); // walked from it and it is still not listed; the newest row is all that is left
	}

	if (_logModel.rowCount() > 0)
		_logView->setCurrentIndex(_logModel.index(0, CommitLogModel::CommitColumn));
}

void HistoryWindow::onFileRowActivated(const QModelIndex& index)
{
	if (!index.isValid() || index.row() >= _filesModel.rowCount())
		return;

	const CommitFileChange entry = _filesModel.entryAt(index.row());
	if (entry.isSubmodule)
		openSubmoduleHistory(entry);
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

	// Land on the first match, so typing walks the list and the arrows step between matches from there
	if (_logModel.rowCount() > 0)
		_logView->setCurrentIndex(_logModel.index(0, CommitLogModel::CommitColumn));
}

void HistoryWindow::updateCountLabel()
{
	if (!_logLoaded)
		return; // the marks can arrive first, and their counts would be measured against an empty list

	const int shown = _logModel.rowCount();
	const int total = _logModel.totalCount();

	QString text = shown == total ? tr("%1 commits").arg(total) : tr("%1 of %2 commits").arg(shown).arg(total);
	// Kept visible during a search too: finding nothing may only mean the commit is older than the limit
	if (_fullLoadPending)
		text = tr("%1, loading more...").arg(text);
	else if (_logCapped)
		text = tr("%1, more to load").arg(text);

	if (!_query.contentSearch.isEmpty())
	{
		text = tr("%1, %2 adding or removing it").arg(text).arg(_logModel.addingOrRemovingCount());
		// -S reaches into binary files, which -G cannot list for want of patch text to match
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

		auto* caption = new QLabel(tr("List the commits whose diff touches this text:"));
		_pickaxeEdit = new QLineEdit;
		_pickaxeEdit->setPlaceholderText(tr("Text to find, taken literally"));
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
			runPickaxe({}, _pickaxeIgnoreCaseBox->isChecked()); // clearing the term is not a reason to undo the choice
		});
	}

	_pickaxeEdit->setText(_query.contentSearch);
	_pickaxeIgnoreCaseBox->setChecked(_query.ignoreCase);
	_pickaxePopup->adjustSize();
	WidgetUtils::placeCenteredOn(_pickaxePopup, this); // its button is in the top-right corner, too far out to read from
	_pickaxePopup->show();
	_pickaxeEdit->setFocus();
	_pickaxeEdit->selectAll();
}

void HistoryWindow::runPickaxe(const QString& text, bool ignoreCase)
{
	_pickaxePopup->hide();
	_logView->setFocus(); // the results are what the user turns to next, and the popup restores focus to its button

	if (_query.contentSearch == text && _query.ignoreCase == ignoreCase)
		return; // re-running the identical walk would cost seconds and change nothing

	_query.contentSearch = text;
	_query.ignoreCase = ignoreCase;

	// The button doubles as the indicator that a content search is narrowing the list, so it carries the
	// term - clipped, since a whole pasted line would push the rest of the bar off the window
	const QString shown = text.size() > MaxShownPickaxeTerm ? text.left(MaxShownPickaxeTerm) + QStringLiteral("...") : text;
	_pickaxeButton->setText(text.isEmpty() ? tr("Find in contents...") : tr("Contents: %1").arg(shown));
	_pickaxeButton->setToolTip(text.isEmpty() ? tr("Re-read the log, keeping only the commits whose diff touches the text") : text);
	reload();
}

void HistoryWindow::showCommitContextMenu(const QPoint& pos)
{
	const QModelIndex index = _logView->indexAt(pos);
	if (!index.isValid() || index.row() >= _logModel.rowCount())
		return;

	// Read before exec() spins an event loop: a log query finishing then resets the model, and the
	// row index would no longer name this commit.
	const QString sha = _logModel.commitAt(index.row()).sha;

	QMenu menu{ this };
	menu.addAction(tr("Copy long hash"), this, [sha] { QApplication::clipboard()->setText(sha); });
	menu.addAction(tr("Copy short hash"), this, [sha] { QApplication::clipboard()->setText(shortSha(sha)); });
	menu.exec(_logView->viewport()->mapToGlobal(pos));
}

void HistoryWindow::showFileContextMenu(const QPoint& pos)
{
	const QModelIndex index = _filesView->indexAt(pos);
	if (!index.isValid() || index.row() >= _filesModel.rowCount())
		return;

	// Read before exec() spins an event loop, which a completing query could reset the model under
	const CommitFileChange entry = _filesModel.entryAt(index.row());

	QMenu menu{ this };
	if (entry.isSubmodule)
	{
		// A submodule's history is its own repository's; the parent's log of that path holds only the
		// pointer moves. Same window the row opens on activation.
		menu.addAction(tr("View commit history"), this, [this, entry] { openSubmoduleHistory(entry); });
	}
	else
	{
		QAction* action = menu.addAction(tr("View file history"), this, [this, path = entry.path] { openFileHistory(path); });
		action->setEnabled(entry.path != _query.path); // this window already is that file's history
	}
	menu.exec(_filesView->viewport()->mapToGlobal(pos));
}

void HistoryWindow::openFileHistory(const QString& filePath)
{
	auto* window = new HistoryWindow(_repo->location(), filePath, this);
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
		_diffPane->showDiff({}, {}, {});
		return;
	}

	const CommitRecord& commit = _logModel.commitAt(current.row());

	// Neither the file list nor the pane may outlive the commit they describe, so both are replaced
	// before the queries go out. The message is what a freshly selected commit shows; picking a file
	// from the list below swaps it for that file's diff.
	_filesModel.clear();
	showCommitMessage(commit);

	const bool isMerge = commit.parents.size() > 1;
	_fileCountLabel->setToolTip(isMerge
		? tr("A merge has no diff of its own - what it changed depends on which parent it is compared against.")
		: QString{});
	if (isMerge)
	{
		_fileCountLabel->setText(tr("merge commit")); // git shows no diff for one without --cc
		return;
	}

	_fileCountLabel->setText(tr("Loading..."));
	const QString sha = commit.sha;
	_filesQuery = _repo->commitFiles(sha, this, [this, sha](std::expected<std::vector<CommitFileChange>, QString> result) {
		if (!result)
		{
			_filesModel.clear();
			_fileCountLabel->clear();
			_diffPane->showDiff({}, shortSha(sha), result.error());
			return;
		}

		const int fileCount = int(result->size());
		_filesModel.setEntries(*std::move(result));
		_fileCountLabel->setText(fileCount == 1 ? tr("1 file") : tr("%1 files").arg(fileCount));
	});
	// Its own query, so the rows may appear before their counts do; failing costs the counts and nothing else
	_fileCountsQuery = _repo->commitFileCounts(sha, this, [this](std::expected<std::map<QString, LineCounts>, QString> counts) {
		_filesModel.setLineCounts(std::move(counts).value_or(std::map<QString, LineCounts>{}));
	});
}

void HistoryWindow::showDiffForCurrentFile()
{
	_diffQuery.cancel();

	const QModelIndex currentFile = _filesView->currentIndex();
	const QModelIndex currentCommit = _logView->currentIndex();
	if (!currentFile.isValid() || currentFile.row() >= _filesModel.rowCount()
		|| !currentCommit.isValid() || currentCommit.row() >= _logModel.rowCount())
	{
		return; // no file picked: the pane keeps the commit message showFilesForCurrentCommit put there
	}

	const CommitFileChange entry = _filesModel.entryAt(currentFile.row());
	const QString sha = _logModel.commitAt(currentCommit.row()).sha;
	const QString tag = shortSha(sha);

	_diffPane->showDiff(entry.path, tag, tr("Loading..."));
	_diffQuery = _repo->commitFileDiff(sha, entry, this, [this, entry, tag](std::expected<QByteArray, QString> diff) {
		if (!diff)
			_diffPane->showDiff(entry.path, tag, diff.error());
		else if (diff->size() > MaxDiffBytes)
			_diffPane->showDiff(entry.path, tag, tr("The diff is too large to display (%1 MB).").arg(double(diff->size()) / (1024 * 1024), 0, 'f', 1));
		else if (diff->isEmpty())
			_diffPane->showDiff(entry.path, tag, tr("No content changes (only the mode or the line endings differ, or a rename with identical content)."));
		else
			_diffPane->showDiff(entry.path, tag, QString::fromUtf8(*diff));
	});
}

void HistoryWindow::showCommitMessage(const CommitRecord& commit)
{
	_diffPane->showText({}, shortSha(commit.sha), commit.message);
}

