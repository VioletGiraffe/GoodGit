#include "historywindow.h"
#include "diffhighlighter.h"
#include "filelistdelegate.h"
#include "settings.h"
#include "theme.h"

#include "widgets/clabelelided.h"
#include "widgets/cpersistentwindow.h"
#include "widgets/widgetutils.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
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
constexpr int MaxFilePathLabelWidth = 420; // beyond this the path elides rather than crowding the bar
constexpr int PickaxeEditWidth = 320;
constexpr qsizetype MaxShownPickaxeTerm = 24;

} // namespace

HistoryWindow::HistoryWindow(const QString& repoPath, QWidget* parent) :
	HistoryWindow(repoPath, {}, parent)
{
}

HistoryWindow::HistoryWindow(const QString& repoPath, const QString& filePath, QWidget* parent) :
	QMainWindow(parent, Qt::Window),
	_repo{ repoPath },
	_query{ .maxCommits = InitialMaxCommits, .path = filePath }
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(_query.path.isEmpty()
		? tr("History - %1 - GoodGit").arg(_repo.name())
		: tr("History of %1 - %2 - GoodGit").arg(_query.path, _repo.name()));
	buildUi();

	// One geometry per repo, shared by its file histories: they are the same window in every other respect
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
	_filesView->setContextMenuPolicy(Qt::CustomContextMenu);
	_filesView->header()->hide();
	_filesView->header()->setSectionResizeMode(CommitFilesModel::StateColumn, QHeaderView::ResizeToContents);
	_filesView->header()->setSectionResizeMode(CommitFilesModel::AddedColumn, QHeaderView::ResizeToContents);
	_filesView->header()->setSectionResizeMode(CommitFilesModel::RemovedColumn, QHeaderView::ResizeToContents);
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
	_diffPathLabel = new CLabelElided;
	_diffPathLabel->setFont(monospaceFont());
	// Eliding does not shrink a QLabel's minimum width, which would otherwise set the pane's
	_diffPathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
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
	_diffHighlighter = new DiffHighlighter(_diffView->document());
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
		_query.maxCommits *= 2;
		reload();
	});
	connect(_searchEdit, &QLineEdit::textChanged, this, &HistoryWindow::applySearch);
	connect(_pickaxeButton, &QPushButton::clicked, this, &HistoryWindow::showPickaxePopup);
	connect(_logView, &QWidget::customContextMenuRequested, this, &HistoryWindow::showCommitContextMenu);
	connect(_filesView, &QWidget::customContextMenuRequested, this, &HistoryWindow::showFileContextMenu);
	connect(_logView->selectionModel(), &QItemSelectionModel::currentChanged, this, &HistoryWindow::showFilesForCurrentCommit);
	connect(_filesView->selectionModel(), &QItemSelectionModel::currentChanged, this, &HistoryWindow::showDiffForCurrentFile);

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
	if (_unpushedJob)
		_unpushedJob->cancel();

	_unpushedJob = _repo.unpushedCommits(this, [this](const GitResult& result) {
		// A failure is the ordinary "no upstream to compare against", and marks nothing
		const QStringList shas = result.ok ? parseLineList(result.out) : QStringList{};
		_logModel.setUnpushedShas(QSet<QString>{ shas.begin(), shas.end() });
	});
}

void HistoryWindow::reload()
{
	if (_logJob)
		_logJob->cancel();
	if (_pickaxeJob)
		_pickaxeJob->cancel();

	refreshUnpushedMarks();
	_logLoaded = false;
	_countLabel->setText(tr("Loading..."));

	// The narrower -S half runs beside the listing, marking within it rather than producing a list of
	// its own; the two are independent walks, so they overlap instead of queueing behind each other
	_logModel.setAddingOrRemovingShas({});
	if (!_query.pickaxe.isEmpty())
	{
		_pickaxeJob = _repo.commitsAddingOrRemovingText(_query, this, [this](const GitResult& result) {
			const QStringList shas = result.ok ? parseLineList(result.out) : QStringList{};
			_logModel.setAddingOrRemovingShas(QSet<QString>{ shas.begin(), shas.end() });
			updateCountLabel();
		});
	}

	_logJob = _repo.commitLog(_query, this, [this](const GitResult& result) {
		if (!result.ok)
		{
			_logCapped = false;
			_logModel.setCommits({}); // resetting the model clears the panes below through currentChanged
			_countLabel->clear();
			_loadMoreButton->setVisible(false);
			setDiffText({}, {}, result.errorText());
			return;
		}

		std::vector<CommitRecord> commits = parseCommitLog(result.out);
		// Exactly the limit means the walk was cut short, not that history ends here
		_logCapped = int(commits.size()) >= _query.maxCommits;
		_loadMoreButton->setVisible(_logCapped);

		_logModel.setCommits(std::move(commits)); // re-applies the active search to the new records
		_logLoaded = true;
		updateCountLabel();
		if (_logModel.rowCount() > 0)
			_logView->setCurrentIndex(_logModel.index(0, CommitLogModel::ShaColumn));
	});
}

void HistoryWindow::applySearch()
{
	_logModel.setSearchText(_searchEdit->text());
	updateCountLabel();

	// Land on the first match, so typing walks the list and the arrows step between matches from there
	if (_logModel.rowCount() > 0)
		_logView->setCurrentIndex(_logModel.index(0, CommitLogModel::ShaColumn));
}

void HistoryWindow::updateCountLabel()
{
	if (!_logLoaded)
		return; // the marks can arrive first, and their counts would be measured against an empty list

	const int shown = _logModel.rowCount();
	const int total = _logModel.totalCount();

	QString text = shown == total ? tr("%1 commits").arg(total) : tr("%1 of %2 commits").arg(shown).arg(total);
	// Kept visible during a search too: finding nothing may only mean the commit is older than the limit
	if (_logCapped)
		text = tr("%1, more to load").arg(text);

	if (!_query.pickaxe.isEmpty())
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

	_pickaxeEdit->setText(_query.pickaxe);
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

	if (_query.pickaxe == text && _query.ignoreCase == ignoreCase)
		return; // re-running the identical walk would cost seconds and change nothing

	_query.pickaxe = text;
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
	const QString path = _filesModel.entryAt(index.row()).path;

	QMenu menu{ this };
	QAction* action = menu.addAction(tr("View file history"), this, [this, path] { openFileHistory(path); });
	action->setEnabled(path != _query.path); // this window already is that file's history
	menu.exec(_filesView->viewport()->mapToGlobal(pos));
}

void HistoryWindow::openFileHistory(const QString& filePath)
{
	auto* window = new HistoryWindow(_repo.path(), filePath, this);
	window->show();
}

void HistoryWindow::showFilesForCurrentCommit()
{
	if (_filesJob)
		_filesJob->cancel();
	if (_fileCountsJob)
		_fileCountsJob->cancel();

	const QModelIndex current = _logView->currentIndex();
	if (!current.isValid() || current.row() >= _logModel.rowCount())
	{
		_filesModel.clear();
		_fileCountLabel->clear();
		setDiffText({}, {}, {});
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
	_filesJob = _repo.commitFiles(sha, this, [this, sha](const GitResult& result) {
		if (!result.ok)
		{
			_filesModel.clear();
			_fileCountLabel->clear();
			setDiffText({}, shortSha(sha), result.errorText());
			return;
		}

		std::vector<NameStatusEntry> entries = parseNameStatusZ(result.out);
		const int fileCount = int(entries.size());
		_filesModel.setEntries(std::move(entries));
		_fileCountLabel->setText(fileCount == 1 ? tr("1 file") : tr("%1 files").arg(fileCount));
	});
	// Its own query, so the rows may appear before their counts do; failing costs the counts and nothing else
	_fileCountsJob = _repo.commitFileCounts(sha, this, [this](const GitResult& result) {
		_filesModel.setLineCounts(parseNumstatZ(result.out));
	});
}

void HistoryWindow::showDiffForCurrentFile()
{
	if (_diffJob)
		_diffJob->cancel();

	const QModelIndex currentFile = _filesView->currentIndex();
	const QModelIndex currentCommit = _logView->currentIndex();
	if (!currentFile.isValid() || currentFile.row() >= _filesModel.rowCount()
		|| !currentCommit.isValid() || currentCommit.row() >= _logModel.rowCount())
	{
		return; // no file picked: the pane keeps the commit message showFilesForCurrentCommit put there
	}

	const NameStatusEntry entry = _filesModel.entryAt(currentFile.row());
	const QString sha = _logModel.commitAt(currentCommit.row()).sha;
	const QString tag = shortSha(sha);

	setDiffText(entry.path, tag, tr("Loading..."));
	_diffJob = _repo.commitFileDiff(sha, entry, this, [this, entry, tag](const GitResult& result) {
		if (!result.ok)
			setDiffText(entry.path, tag, result.errorText());
		else if (result.out.size() > MaxDiffBytes)
			setDiffText(entry.path, tag, tr("The diff is too large to display (%1 MB).").arg(double(result.out.size()) / (1024 * 1024), 0, 'f', 1));
		else if (result.out.isEmpty())
			setDiffText(entry.path, tag, tr("No content changes (only the mode or the line endings differ, or a rename with identical content)."));
		else
			setDiffText(entry.path, tag, QString::fromUtf8(result.out));
	});
}

void HistoryWindow::showCommitMessage(const CommitRecord& commit)
{
	_diffHighlighter->setEnabled(false);
	_diffPathLabel->setText({});
	_diffTagLabel->setText(shortSha(commit.sha));
	_diffView->setPlainText(commit.message);
}

void HistoryWindow::setDiffText(const QString& pathLabel, const QString& tag, const QString& text)
{
	_diffHighlighter->setEnabled(true);
	_diffPathLabel->setText(pathLabel);
	_diffTagLabel->setText(tag);
	_diffView->setPlainText(text);
}
