#include "commitwindow.h"
#include "appmenus.h"
#include "diffpane.h"
#include "externalapps.h"
#include "filelistview.h"
#include "historymodels.h"
#include "historywindow.h"
#include "incomingchangesdialog.h"
#include "messageedit.h"
#include "pushpane.h"
#include "recentrepositories.h"
#include "recentrepositoriespanel.h"
#include "repositoryfactory.h"
#include "repositorywindows.h"
#include "settings.h"
#include "theme.h"
#include "writeflow.h"

#include "assert/advanced_assert.h"
#include "dialogs/messagedialog.h"
#include "hash/wheathash.hpp"
#include "appdialogs/csettingsnotifier.h"
#include "string/stringutils.h"
#include "widgets/clabelelided.h"
#include "widgets/cpersistentwindow.h"
#include "widgets/widgetutils.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <iterator>
#include <utility>

namespace {

constexpr int RecentRepositoriesDockWidth = 175; // first-run default; persists with the window state
constexpr int SplitterWidth = 1180; // width of the two panes together until the first refresh widens the window
constexpr int FirstRunDiffPaneWidth = 440; // what the diff keeps while the left pane takes its preferred width
// The left pane's preferred width follows the repository and branch names, which are unbounded
constexpr int MaxInitialLeftPaneWidth = 1000;
constexpr int MaxListedPathsInDialog = 20;
constexpr size_t MaxListedReattachObstacles = 3; // the strip is one paragraph

// A gone upstream leaves the check's HEAD..@{upstream} nothing to resolve against
bool incomingCheckPossible(const RepoState& state)
{
	return !state.upstream.isEmpty() && !state.upstreamGone;
}

// Empty where the path is not a file on disk: a deleted entry, or a submodule's own directory
QString workingTreeSizeLabel(const QString& absolutePath)
{
	const QFileInfo info{ absolutePath };
	return info.isFile() ? formattedFileSize(info.size()) : QString{};
}

// The tag of every diff this window shows
QString workingTreeTag()
{
	return CommitWindow::tr("HEAD %1 working tree").arg(QChar(0x2192));
}

// The op strip opens with the operation's name, which a backend holds in its command's spelling
QString capitalized(const QString& text)
{
	return text.isEmpty() ? text : QString{ text.front().toUpper() } + text.sliced(1);
}

void setStripTextOrHide(QLabel* strip, const QString& text)
{
	strip->setText(text);
	strip->setVisible(!text.isEmpty());
}

// Where the candidate's branch stands against HEAD, as a sentence
QString reattachDescription(const ReattachCandidate& candidate)
{
	const QString refOnly = QString::number(candidate.refOnlyCommits);
	const QString headOnly = QString::number(candidate.headOnlyCommits);
	switch (candidate.kind)
	{
	case ReattachCandidate::Kind::AtHead:
		return CommitWindow::tr("'%1' points here.").arg(candidate.branch);
	case ReattachCandidate::Kind::Move:
		if (candidate.refOnlyCommits == 0)
			return CommitWindow::tr("'%1' is %2 commit(s) behind here, and %3 contains this commit.").arg(candidate.branch, headOnly, candidate.upstream);
		if (candidate.headOnlyCommits == 0)
			return CommitWindow::tr("'%1' is %2 commit(s) ahead of here, and %3 has all of them.").arg(candidate.branch, refOnly, candidate.upstream);
		return CommitWindow::tr("'%1' has %2 commit(s) this one lacks and lacks %3 of its; %4 contains both.")
			.arg(candidate.branch, refOnly, headOnly, candidate.upstream);
	case ReattachCandidate::Kind::Create:
		if (candidate.refOnlyCommits == 0)
			return CommitWindow::tr("%1 points here, and there is no local '%2' yet.").arg(candidate.upstream, candidate.branch);
		return CommitWindow::tr("%1 contains this commit, %2 commit(s) further on, and there is no local '%3' yet.")
			.arg(candidate.upstream, refOnly, candidate.branch);
	}
	return {};
}

// Why no branch is a candidate, as sentences
QString reattachObstaclesText(const std::vector<ReattachObstacle>& obstacles)
{
	if (obstacles.empty())
		return CommitWindow::tr("No local branch points here, and no remote branch contains this commit.");

	QStringList sentences;
	for (size_t i = 0; i < std::min(obstacles.size(), MaxListedReattachObstacles); ++i)
	{
		const ReattachObstacle& obstacle = obstacles[i];
		switch (obstacle.reason)
		{
		case ReattachObstacle::Reason::Unpushed:
			sentences.push_back(CommitWindow::tr("'%1' has %2 commit(s) that %3 lacks.")
				.arg(obstacle.branch, QString::number(obstacle.unpushedCommits), obstacle.upstream));
			break;
		case ReattachObstacle::Reason::CheckedOutElsewhere:
			sentences.push_back(CommitWindow::tr("'%1' is checked out in another worktree.").arg(obstacle.branch));
			break;
		case ReattachObstacle::Reason::NameTaken:
			sentences.push_back(CommitWindow::tr("%1 contains this commit, but the local '%2' does not track it.").arg(obstacle.upstream, obstacle.branch));
			break;
		}
	}
	if (obstacles.size() > MaxListedReattachObstacles)
		sentences.push_back(CommitWindow::tr("%1 more branch(es) are ruled out as well.").arg(obstacles.size() - MaxListedReattachObstacles));
	return sentences.join(QLatin1Char(' '));
}

// The candidate as an action, for a button or a dialog option
QString reattachActionText(const ReattachCandidate& candidate)
{
	switch (candidate.kind)
	{
	case ReattachCandidate::Kind::AtHead:
		return CommitWindow::tr("Check out '%1'").arg(candidate.branch);
	case ReattachCandidate::Kind::Move:
		return candidate.refOnlyCommits == 0 ? CommitWindow::tr("Fast-forward '%1' here").arg(candidate.branch)
			: CommitWindow::tr("Move '%1' here").arg(candidate.branch);
	case ReattachCandidate::Kind::Create:
		return CommitWindow::tr("Create '%1' tracking %2").arg(candidate.branch, candidate.upstream);
	}
	return {};
}

// Untracked files have nothing to restore to. A submodule with changes inside would be checked out over.
// Row shape only: an operation in progress blocks discarding every row, and each caller must gate on it.
bool discardable(const FileEntry& entry)
{
	if (entry.isSubmodule)
		return entry.committable();
	return entry.type != ChangeType::Untracked;
}

// A submodule row whose content, not its pointer, is what there is to discard. Whether the pointer moved is
// beside the point: the content blocks committing it either way.
// Unreadable content is not offered: nothing is known about what would be destroyed.
bool contentDiscardable(const FileEntry& entry)
{
	return entry.isSubmodule && entry.content == SubmoduleContent::DirtyTracked;
}

// The rows `matches` accepts, by path: a mixed selection is a no-op for the rows it rejects
template <typename Matches>
QStringList pathsWhere(const std::vector<FileEntry>& entries, Matches matches)
{
	QStringList paths;
	for (const FileEntry& entry : entries)
	{
		if (matches(entry))
			paths.push_back(entry.path);
	}
	return paths;
}

// A separator survives only between two visible items, so a group hidden whole leaves no gap behind it
void hideRedundantSeparators(QMenu& menu)
{
	QAction* lastSeparator = nullptr;
	bool anyVisibleItem = false;
	for (QAction* action : menu.actions())
	{
		if (action->isSeparator())
		{
			action->setVisible(false);
			lastSeparator = anyVisibleItem ? action : nullptr;
		}
		else if (action->isVisible())
		{
			if (lastSeparator)
				lastSeparator->setVisible(true);
			lastSeparator = nullptr;
			anyVisibleItem = true;
		}
	}
}

QString listedPaths(const QStringList& paths)
{
	QStringList shown = paths.mid(0, MaxListedPathsInDialog);
	if (paths.size() > shown.size())
		shown.push_back(QStringLiteral("... and %1 more").arg(paths.size() - shown.size()));
	return shown.join(QLatin1Char('\n'));
}

// A repository's draft group. The path is hashed: it is not a usable settings key. sameRepositoryPath() is
// case-insensitive, so the hash is of the lowercased path.
[[nodiscard]] QString draftGroup(const QString& repositoryPath)
{
	const QByteArray path = repositoryPath.toLower().toUtf8();
	const uint64_t hash = wheathash64(path.constData(), uint64_t(path.size()));
	return QString::fromLatin1(Settings::CommitDraftsGroupKey) + QLatin1Char('/') + QString::number(hash, 16);
}

// Linux: the X11 frame extents applyDefaultWindowSize depends on arrive only after a window-manager round
// trip Qt exposes no event for; 75 ms is an empirical bound
template <class F>
inline void delayIfNecessary([[maybe_unused]] QObject* context, F&& f) {
#ifdef __linux__
	QTimer::singleShot(75, context, std::forward<F>(f));
#else
	f();
#endif
}

} // namespace

CommitWindow::CommitWindow(const RepositoryLocation& location) :
	_repo{ sharedRepository(location) }
{
	setAttribute(Qt::WA_DeleteOnClose);
	buildUi();

	// One geometry for every commit window
	enablePersistence(this, QStringLiteral("CommitWindow"), CPersistenceEnabler::Delayed{ true }, CPersistenceEnabler::SetDefaultSize{ false });

	connect(_repo.get(), &Repository::refreshed, this, &CommitWindow::onRefreshed);
	connect(_repo.get(), &Repository::writeStateChanged, this, &CommitWindow::updateControlStates);
	WidgetUtils::callOnReturnFromOtherApp(this, [this] { _repo->refreshAfterWrites(); });

	_repo->refresh();
}

const QString& CommitWindow::repositoryPath() const
{
	return _repo->path();
}

void CommitWindow::refreshRepository()
{
	_repo->refresh();
}

void CommitWindow::followSubmoduleHistory(const CommitWindow& submodule)
{
	// Unique: every open of the submodule's window from here follows it again
	connect(submodule._repo.get(), &Repository::historyChanged, this, &CommitWindow::refreshRepository, Qt::UniqueConnection);
}

void CommitWindow::buildUi()
{
	buildMenuBar();

	_splitter = new QSplitter(Qt::Horizontal);
	_splitter->setChildrenCollapsible(false);
	_splitter->setHandleWidth(1);
	_splitter->addWidget(buildLeftPane());
	_splitter->addWidget(buildRightPane());
	_splitter->setStretchFactor(0, 0);
	_splitter->setStretchFactor(1, 1);
	if (const QByteArray state = QSettings{}.value(Settings::CommitWindowSplitterKey).toByteArray(); !state.isEmpty())
		_splitter->restoreState(state);
	else
		_initialWidthPending = true;
	setCentralWidget(_splitter);
	resize(SplitterWidth + RecentRepositoriesDockWidth, 740); // the dock is beside the two panes, not carved out of them
	acceptRepositoryFolderDrops(this);

	connect(&_filesModel, &ChangedFilesModel::checksChanged, this, &CommitWindow::updateControlStates);
	connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, this, [this] {
		_branchLabel->setFont(monospaceFont());
	});
	installShortcuts();

	updateControlStates();
	_messageEdit->setFocus();
}

void CommitWindow::buildMenuBar()
{
	QAction* openRepositoryAction = addFileMenu(*menuBar(), this);
	addEditMenu(*menuBar(), this);
	QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
	viewMenu->addAction(buildRecentRepositoriesDock());
	viewMenu->addAction(tr("Show &Welcome Screen"), &showWelcomeWindow);
	// Everything: the dock that would show a scan's finds can be hidden
	QMenu* repositoryMenu = addRepositoryMenu(*menuBar(), this, openRepositoryAction, ScanReport::Everything);
	repositoryMenu->addSeparator();
	repositoryMenu->addAction(tr("&Refresh"), this, [this] { _repo->refresh(); })->setShortcut(QKeySequence::Refresh);
	_checkIncomingAction = repositoryMenu->addAction(tr("Check for &Incoming Changes"), this, &CommitWindow::checkForIncomingChanges);
	_checkIncomingAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
	_checkIncomingAction->setToolTip(tr("Fetch and list the commits on the upstream that this branch does not have yet"));
	_pushOneByOneAction = repositoryMenu->addAction(tr("&Push One by One..."), this, [this] { _pushPane->startPush(PushMode::OneByOne); });
	_pushOneByOneAction->setToolTip(tr("Push the unpushed commits to the upstream one at a time, oldest first, so that CI runs for each"));
	repositoryMenu->setToolTipsVisible(true); // the names say neither that the check goes to the network nor what pushing one by one is for
	_uncommitAction = repositoryMenu->addAction(tr("&Undo Last Commit"), this, &CommitWindow::undoLastCommit);
	// Not one item per operation kind: the op strip names the one running, which no menu label has room to do
	_continueAction = repositoryMenu->addAction(tr("&Continue Operation"), this, &CommitWindow::continueOperation);
	_abortAction = repositoryMenu->addAction(tr("&Abort Operation..."), this, &CommitWindow::abortOperation);
	repositoryMenu->addSeparator();
	// Not created when absent: the editor does that on save
	repositoryMenu->addAction(tr("&Edit %1").arg(_repo->ignoreFileName()), this, [this] {
		openInTextEditor(QDir{ _repo->path() }.filePath(_repo->ignoreFileName()), this);
	});
	addHelpMenu(*menuBar(), this);
}

QWidget* CommitWindow::buildLeftPane()
{
	auto* leftPane = new QWidget;
	auto* leftLayout = new QVBoxLayout(leftPane);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	leftLayout->setSpacing(0);
	leftLayout->addWidget(buildRepoBar());

	// Colored by the stylesheet through the object names
	const auto makeStrip = [](const char* objectName) {
		auto* strip = new QLabel;
		strip->setObjectName(QLatin1String(objectName));
		strip->setWordWrap(true);
		strip->setMargin(6);
		strip->setVisible(false);
		return strip;
	};
	// First: the other strips describe a state this one says could not be read
	_readFailureStrip = makeStrip("errorStrip");
	_opStrip = makeStrip("errorStrip");
	leftLayout->addWidget(_readFailureStrip);
	leftLayout->addWidget(_opStrip);
	leftLayout->addWidget(buildDetachedStrip());
	_newHeadStrip = makeStrip("warningStrip");
	leftLayout->addWidget(_newHeadStrip);

	leftLayout->addWidget(buildCounterBar());

	_filesView = new FileListView;
	_filesView->setModel(&_filesModel);
	_filesView->setSelectionMode(QAbstractItemView::ExtendedSelection);
	_filesView->installEventFilter(this);
	leftLayout->addWidget(_filesView, 1);
	connect(_filesView, &FileListView::rowActivated, this, &CommitWindow::onRowActivated);
	connect(_filesView, &QWidget::customContextMenuRequested, this, &CommitWindow::showContextMenu);
	connect(_filesView->selectionModel(), &QItemSelectionModel::currentChanged, this, &CommitWindow::showDiffForCurrentRow);

	leftLayout->addWidget(buildMessageHeader());
	leftLayout->addWidget(buildMessageArea());
	return leftPane;
}

QWidget* CommitWindow::buildRepoBar()
{
	// Repo header row: name, branch, ahead count, then the secondary actions
	auto* repoBar = new QFrame;
	repoBar->setObjectName(QStringLiteral("repoBar"));
	auto* repoBarLayout = new QHBoxLayout(repoBar);
	repoBarLayout->setContentsMargins(8, 6, 8, 6);
	// Both elided: the bar's minimum width would otherwise follow the repository and branch names
	_repoNameLabel = new CLabelElided;
	_repoNameLabel->setElideMode(Qt::ElideRight); // a name reads from its start
	{
		QFont bold = _repoNameLabel->font();
		bold.setBold(true);
		_repoNameLabel->setFont(bold);
	}
	_branchLabel = new CLabelElided; // ElideMiddle, the default: both ends of a branch path carry meaning
	_branchLabel->setObjectName(QStringLiteral("branchChip"));
	_branchLabel->setFont(monospaceFont());
	_branchLabel->setMinimumTextSample(QStringLiteral("main")); // a branch name this short never elides
	_aheadLabel = new QLabel;
	_aheadLabel->setObjectName(QStringLiteral("aheadLabel"));
	_pushButton = new QPushButton(tr("Push"));
	auto* historyButton = new QPushButton(tr("History"));
	historyButton->setToolTip(QStringLiteral("Ctrl+H"));
	repoBarLayout->addWidget(_repoNameLabel);
	repoBarLayout->addWidget(_branchLabel);
	repoBarLayout->addWidget(_aheadLabel);
	repoBarLayout->addStretch();
	repoBarLayout->addWidget(_pushButton);
	repoBarLayout->addWidget(historyButton);

	connect(_pushButton, &QPushButton::clicked, this, [this] { _pushPane->startPush(PushMode::Branch); });
	connect(historyButton, &QPushButton::clicked, this, &CommitWindow::showHistoryWindow);
	return repoBar;
}

QWidget* CommitWindow::buildDetachedStrip()
{
	_detachedStrip = new QFrame;
	_detachedStrip->setObjectName(QStringLiteral("warningStrip"));
	auto* stripLayout = new QHBoxLayout(_detachedStrip);
	stripLayout->setContentsMargins(6, 6, 6, 6);
	_detachedLabel = new QLabel;
	_detachedLabel->setWordWrap(true);
	_reattachButton = new QPushButton;
	stripLayout->addWidget(_detachedLabel, 1);
	stripLayout->addWidget(_reattachButton);
	_detachedStrip->setVisible(false);

	connect(_reattachButton, &QPushButton::clicked, this, &CommitWindow::reattachHeadFromStrip);
	return _detachedStrip;
}

QWidget* CommitWindow::buildCounterBar()
{
	auto* counterBar = new QFrame;
	counterBar->setObjectName(QStringLiteral("counterBar"));
	auto* counterLayout = new QHBoxLayout(counterBar);
	counterLayout->setContentsMargins(8, 4, 8, 4);
	_checkAllBox = new QCheckBox(tr("0 of 0 checked"));
	counterLayout->addWidget(_checkAllBox);
	auto* modifiedOnlyButton = new QPushButton(tr("Modified only"));
	modifiedOnlyButton->setToolTip(tr("Check all tracked changes, uncheck untracked files"));
	counterLayout->addWidget(modifiedOnlyButton);
	counterLayout->addStretch();
	_lineTotalsLabel = new QLabel;
	_lineTotalsLabel->setToolTip(tr("Lines added and removed in the checked files. "
		"Untracked and binary files have no line counts and are not included."));
	counterLayout->addWidget(_lineTotalsLabel);

	connect(_checkAllBox, &QCheckBox::clicked, this, [this] {
		_filesModel.setAllChecked(_filesModel.checkedCount() < _filesModel.checkableCount());
	});
	connect(modifiedOnlyButton, &QPushButton::clicked, &_filesModel, &ChangedFilesModel::checkAllExceptUntracked);
	return counterBar;
}

QWidget* CommitWindow::buildMessageHeader()
{
	auto* messageHeader = new QWidget;
	messageHeader->setObjectName(QStringLiteral("messageHeader"));
	auto* messageHeaderLayout = new QHBoxLayout(messageHeader);
	messageHeaderLayout->setContentsMargins(8, 6, 8, 4);
	messageHeaderLayout->addWidget(new QLabel(tr("Commit message")));
	_parentCommitLabel = new CLabelElided;
	_parentCommitLabel->setElideMode(Qt::ElideRight); // a subject reads from its start, unlike the paths elsewhere
	_parentCommitLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	// A long subject must elide rather than widen the column
	_parentCommitLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	_parentCommitLabel->setContextMenuPolicy(Qt::CustomContextMenu);
	messageHeaderLayout->addWidget(_parentCommitLabel, 1);

	connect(_parentCommitLabel, &QWidget::customContextMenuRequested, this, &CommitWindow::showParentCommitContextMenu);
	return messageHeader;
}

QWidget* CommitWindow::buildMessageArea()
{
	auto* messageArea = new QWidget;
	auto* messageLayout = new QVBoxLayout(messageArea);
	messageLayout->setContentsMargins(8, 0, 8, 8);
	_messageEdit = new MessageEdit;
	_messageEdit->setObjectName(QStringLiteral("messageEdit"));
	_messageEdit->setMinimumHeight(90);
	_messageEdit->setMaximumHeight(160);
	// The editor takes text drops, so folder drags are taken from it before it sees them. On the viewport:
	// that is where a scroll area's drag events are delivered.
	acceptRepositoryFolderDrops(_messageEdit->viewport());
	messageLayout->addWidget(_messageEdit);
	_commitButton = new QPushButton(tr("Commit"));
	_commitButton->setObjectName(QStringLiteral("commitButton"));
	_commitButton->setDefault(false);
	_commitButton->setToolTip(QStringLiteral("Ctrl+Enter"));
	_commitPushButton = new QPushButton(tr("Commit && Push"));
	_commitPushButton->setObjectName(QStringLiteral("commitPushButton"));
	_commitPushButton->setToolTip(QStringLiteral("Ctrl+Shift+Enter"));
	messageLayout->addWidget(_commitButton);
	messageLayout->addWidget(_commitPushButton);

	connect(_messageEdit, &QPlainTextEdit::textChanged, this, &CommitWindow::updateControlStates);
	connect(_commitButton, &QPushButton::clicked, this, [this] { startCommit(false); });
	connect(_commitPushButton, &QPushButton::clicked, this, [this] { startCommit(true); });
	return messageArea;
}

QWidget* CommitWindow::buildRightPane()
{
	auto* rightPane = new QWidget;
	auto* rightLayout = new QVBoxLayout(rightPane);
	rightLayout->setContentsMargins(0, 0, 0, 0);
	rightLayout->setSpacing(0);
	_diffPane = new DiffPane;
	rightLayout->addWidget(_diffPane, 1);
	// The other file's row shows its section at once, from the set the end came from, so the scroll lands
	connect(_diffPane, &DiffPane::foreignEndActivated, this, [this](const ForeignEnd& end) {
		restoreSelectionByPath({ { end.path }, end.path });
		_diffPane->scrollDiffLineToTop(end.diffLine);
	});
	_pushPane = new PushPane{ _repo };
	rightLayout->addWidget(_pushPane);
	return rightPane;
}

void CommitWindow::installShortcuts()
{
	new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] { close(); });
	new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_H), this, [this] { showHistoryWindow(); });
	new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this, [this] {
		if (_pushButton->isEnabled())
			_pushPane->startPush(PushMode::Branch);
	});
	const auto commitShortcut = [this] {
		if (_commitButton->isEnabled())
			startCommit(false);
	};
	new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this, commitShortcut);
	new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Enter), this, commitShortcut); // the numpad Enter
	const auto commitPushShortcut = [this] {
		if (_commitPushButton->isEnabled())
			startCommit(true);
	};
	new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Return), this, commitPushShortcut);
	new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Enter), this, commitPushShortcut);
}

void CommitWindow::showParentCommitContextMenu(const QPoint& pos)
{
	// Copied before exec() spins an event loop, in which a finishing refresh could replace the state
	const RepoState& state = _repo->state();
	const QString sha = state.headSha;
	const QString subject = state.headSubject();
	if (sha.isEmpty())
		return;

	QMenu menu{ this };
	QAction* copyTitleAction = menu.addAction(tr("Copy commit title"), this, [subject] { QApplication::clipboard()->setText(subject); });
	copyTitleAction->setEnabled(!subject.isEmpty());
	menu.addAction(tr("Copy long hash"), this, [sha] { QApplication::clipboard()->setText(sha); });
	menu.addAction(tr("Copy short hash"), this, [sha] { QApplication::clipboard()->setText(shortSha(sha)); });
	menu.exec(_parentCommitLabel->mapToGlobal(pos));
}

QAction* CommitWindow::buildRecentRepositoriesDock()
{
	auto* dock = new QDockWidget{ this };
	dock->setWindowTitle(tr("Recent Repositories"));
	dock->setObjectName(QStringLiteral("recentRepositoriesDock")); // saveState() drops a dock that has no name
	dock->setFeatures(QDockWidget::DockWidgetClosable);
	dock->setAllowedAreas(Qt::LeftDockWidgetArea);

	// The stylesheet cannot style a dock's native title bar, so it gets a bar like the others in the window
	auto* header = new QFrame;
	header->setObjectName(QStringLiteral("dockHeader"));
	auto* headerLayout = new QHBoxLayout(header);
	headerLayout->setContentsMargins(8, 6, 8, 6);
	headerLayout->addWidget(new QLabel(tr("Recent")));
	headerLayout->addStretch();
	auto* openButton = new QPushButton(tr("Open..."));
	openButton->setToolTip(tr("Open another repository"));
	auto* hideButton = new QPushButton(tr("Hide"));
	headerLayout->addWidget(openButton);
	headerLayout->addWidget(hideButton);
	dock->setTitleBarWidget(header);

	auto* panel = new RecentRepositoriesPanel{ _repo->path() };

	// Its own row rather than the header's: the header row's buttons are what hold the dock's minimum width,
	// and a field beside them would add to it
	auto* filterRow = new QWidget;
	auto* filterRowLayout = new QHBoxLayout(filterRow);
	filterRowLayout->setContentsMargins(8, 6, 8, 6);
	filterRowLayout->addWidget(panel->createFilterField());

	auto* dockContents = new QWidget;
	auto* dockContentsLayout = new QVBoxLayout(dockContents);
	dockContentsLayout->setContentsMargins(0, 0, 0, 0); // the list runs to the dock's edges; only the filter is inset
	dockContentsLayout->setSpacing(0);
	dockContentsLayout->addWidget(filterRow);
	dockContentsLayout->addWidget(panel);

	dock->setWidget(dockContents);
	addDockWidget(Qt::LeftDockWidgetArea, dock);
	resizeDocks({ dock }, { RecentRepositoriesDockWidth }, Qt::Horizontal);

	connect(openButton, &QPushButton::clicked, this, [this] { browseForRepository(this); });
	connect(hideButton, &QPushButton::clicked, dock, &QWidget::hide);

	QAction* toggleAction = dock->toggleViewAction();
	toggleAction->setText(tr("&Recent Repositories"));
	return toggleAction;
}

void CommitWindow::closeEvent(QCloseEvent* event)
{
	// Nothing cancels a Vcs::Job: a write this window holds would answer after it is gone
	const bool pushRunning = _pushPane->pushRunning();
	if (_repo->writeHeldBy(this) || pushRunning)
	{
		event->ignore();
		if (pushRunning)
			_pushPane->show(); // may have been hidden mid-push; it shows how far the push has got

		MessageDialog::notice(this, tr("Cannot close yet"), pushRunning
			? tr("A push is running. Wait for it to finish, then close the window.")
			: tr("An operation that changes the repository is running. Wait for it to finish, then close the window."), {});
		return;
	}

	QSettings{}.setValue(Settings::CommitWindowSplitterKey, _splitter->saveState());
	saveDraftMessage();
	QMainWindow::closeEvent(event);
}

bool CommitWindow::eventFilter(QObject* watched, QEvent* event)
{
	const auto eventType = event->type();
	if (watched == _filesView && eventType == QEvent::KeyPress)
	{
		const auto* keyEvent = static_cast<QKeyEvent*>(event);
		if (keyEvent->key() == Qt::Key_Space)
		{
			toggleCheckOnSelection();
			return true;
		}
		if (keyEvent->key() == Qt::Key_Delete)
		{
			if (canActOnList()) // swallowed either way
				deleteSelection();
			return true;
		}
	}

	return QMainWindow::eventFilter(watched, event);
}

void CommitWindow::onRefreshed()
{
	const SelectionByPath selection = captureSelectionByPath();

	const RepoState& state = _repo->state();
	_filesModel.setEntries(_repo->files(), state.mergeCommitRequired());
	RecentRepositories::setSubmodules(*_repo); // the only place the recent list learns a repository's submodules

	updateHeader();
	updateStrips();
	updateControlStates();

	// Not during a push: its end refreshes
	if (!_incomingCheckInFlight && !_repo->pushInFlight() && incomingCheckPossible(state))
		startIncomingCheck(/*interactive=*/false);

	if (_initialWidthPending)
	{
		_initialWidthPending = false;
		delayIfNecessary(this, [this] {
			applyDefaultWindowSize();
		});
	}

	QStringList changedPaths;
	for (const FileEntry& file : _repo->files())
		changedPaths.push_back(file.path);

	// Cancelled, or refreshes in quick succession would leave the set built by whichever finished last.
	// Not reset: an unchanged diff keeps the set.
	_changeSetQuery.cancel();
	_changeSetPending = true;
	_changeSetQuery = _repo->workingTreeDiff(Settings::MaxChangeSetDiffBytes, this, [this, changedPaths](std::expected<QByteArray, QString> diff) {
		_changeSetPending = false;
		_messageEdit->setCompletionSources(changedPaths, diff ? *diff : QByteArray{}); // a failed diff leaves the paths alone

		const uint64_t diffHash = diff ? wheathash64(diff->constData(), uint64_t(diff->size())) : 0;
		const bool diffUnchanged = diff && _changeSet && diffHash == _changeSetHash;
		if (!diffUnchanged)
		{
			_changeSet.reset();
			if (diff)
			{
				_changeSet.emplace(QString::fromUtf8(*diff));
				_changeSetHash = diffHash;
			}
		}

		if (!_rowAwaitsChangeSet)
			return;
		// Text still shown is the waiting row's own, from the same bytes
		if (diffUnchanged && _diffPane->topLine())
			_rowAwaitsChangeSet = false;
		else
			showDiffForCurrentRow();
	});

	restoreSelectionByPath(selection);
	// Even if the current row did not move: the list may have emptied, or the content changed underneath
	showDiffForCurrentRow();

	if (!_stateWasRead && state.known())
	{
		_stateWasRead = true;
		restoreDraftIfParentUnchanged();
	}
}

void CommitWindow::applyDefaultWindowSize()
{
	if (!isVisible())
		return; // the widths below are the laid-out ones, which show() establishes; a closed window has none

	const QWidget* leftPane = _splitter->widget(0);
	const int preferred = std::min(leftPane->sizeHint().width(), MaxInitialLeftPaneWidth);
	const int available = _splitter->width() - _splitter->handleWidth();
	const QRect screenArea = screen()->availableGeometry();
	// The frame has to fit the screen, but resize() takes the client width; both grow by the same amount
	const QRect frame = frameGeometry();
	// The window follows the two panes in both directions: width the left pane does not take is not the diff's to keep
	const int delta = std::min(preferred + FirstRunDiffPaneWidth - available, std::max(0, screenArea.width() - frame.width()));
	if (delta != 0)
	{
		resize(width() + delta, height());
	}

	// A shortfall the screen leaves unfilled comes out of the left pane: it elides, the diff pane does not
	const int total = available + delta;
	const int left = std::min(preferred, total - FirstRunDiffPaneWidth);
	_splitter->setSizes({ left, total - left });

	WidgetUtils::centerWidgetOnScreen(this);
}

void CommitWindow::updateHeader()
{
	const RepoState& state = _repo->state();
	const QString shortHeadSha = shortSha(state.headSha);

	_repoNameLabel->setText(_repo->name());
	const QString branchText = state.detached ? tr("detached HEAD at %1").arg(shortHeadSha)
		: state.unborn ? state.branch + tr(" (no commits yet)")
		: state.branch;
	_branchLabel->setText(branchText);

	if (state.upstream.isEmpty())
		_aheadLabel->setText(state.unborn || state.detached ? QString{} : tr("no upstream"));
	else if (state.upstreamGone)
		_aheadLabel->setText(tr("upstream %1 is gone").arg(state.upstream));
	// No count is shown before a check has succeeded: the counts are only as current as the last one
	else if (_incomingCheckOutcome == IncomingCheckOutcome::None)
		_aheadLabel->setText(tr("checking %1...").arg(state.upstream));
	else if (_incomingCheckOutcome == IncomingCheckOutcome::Failed)
		_aheadLabel->setText(tr("could not check %1").arg(state.upstream));
	else if (state.ahead > 0 && state.behind > 0)
		_aheadLabel->setText(tr("%1 to push, %2 behind %3").arg(state.ahead).arg(state.behind).arg(state.upstream));
	else if (state.ahead > 0)
		_aheadLabel->setText(tr("%1 to push to %2").arg(state.ahead).arg(state.upstream));
	else if (state.behind > 0)
		_aheadLabel->setText(tr("%1 behind %2").arg(state.behind).arg(state.upstream));
	else
		_aheadLabel->setText(tr("in sync with %1").arg(state.upstream));

	_pushButton->setText(state.ahead > 0 ? tr("Push (%1)").arg(state.ahead) : tr("Push"));

	QString unpushedTooltip;
	if (!state.unpushedSubjects.isEmpty())
	{
		QStringList lines = state.unpushedSubjects;
		if (state.ahead > lines.size())
			lines.push_back(tr("... and %1 more").arg(state.ahead - lines.size()));
		unpushedTooltip = lines.join(QLatin1Char('\n'));
	}
	_pushButton->setToolTip(unpushedTooltip.isEmpty() ? QStringLiteral("Ctrl+Shift+P")
		: QStringLiteral("Ctrl+Shift+P\n") + unpushedTooltip);
	_aheadLabel->setToolTip(state.upstreamGone
		? tr("The upstream branch no longer exists on the remote - deleted or pruned. Pushing recreates it.")
		: !state.upstream.isEmpty() && _incomingCheckOutcome == IncomingCheckOutcome::Failed ? _incomingCheckError
		: unpushedTooltip);

	const QString parentSubject = subjectOrPlaceholder(state.headSubject());
	_parentCommitLabel->setText(state.unborn ? QString{} : tr("Parent: %1").arg(parentSubject));
	_parentCommitLabel->setToolTip(state.unborn ? QString{} : QStringLiteral("%1 %2").arg(shortHeadSha, parentSubject));

	setWindowTitle(QStringLiteral("%1 [%2] - GoodGit").arg(_repo->name(), state.detached ? QStringLiteral("detached") : state.branch));
}

void CommitWindow::updateStrips()
{
	const RepoState& state = _repo->state();

	const QString readFailureText = state.known() ? QString{}
		: tr("Could not read this repository: %1\nThe list below is from the last successful refresh. "
			 "Committing, discarding and deleting are disabled until a refresh (F5) succeeds.").arg(state.readFailure);
	setStripTextOrHide(_readFailureStrip, readFailureText);

	// The operation is named as its own system names it: an hg graft is not a cherry-pick to whoever started it
	QString opText;
	if (state.op == RepoOp::Bisect)
		opText = tr("Bisect in progress: committing is disabled until it ends, since a new commit would fall "
					"outside the search range.");
	else if (state.opBlocksCommit())
		opText = tr("%1 in progress: committing is disabled, since it is finished by continuing it. Use Continue in "
					"the Repository menu, or run %2.").arg(capitalized(state.opHint.name), state.opHint.continueCommand);
	else if (state.op != RepoOp::None)
		opText = tr("%1 in progress: all changes must be committed together.").arg(capitalized(state.opHint.name));
	// A row reads Conflicted only while an operation is in progress, so this always extends the line above
	if (const qsizetype unresolved = _filesModel.unresolvedConflictPaths().size(); unresolved > 0)
		opText += tr(" %1 file(s) are still conflicted and must be marked resolved first.").arg(unresolved);
	setStripTextOrHide(_opStrip, opText);

	// Not blocking: another head is ordinary in Mercurial
	setStripTextOrHide(_newHeadStrip, state.commitAddsHead && !state.opBlocksCommit()
		? tr("Not at a head of '%1': committing here adds another head to it. A push refuses a second head on the remote "
			 "until the two are merged.").arg(state.branch)
		: QString{});

	// A bisect and a rebase detach HEAD as a matter of course; the op strip explains, and no commit will reattach
	const bool showDetached = state.detached && !state.opBlocksCommit();
	_detachedStrip->setVisible(showDetached);
	if (!showDetached)
		return;

	const std::vector<ReattachCandidate>& candidates = state.reattachCandidates;
	_reattachButton->setVisible(!candidates.empty());
	if (candidates.empty())
	{
		_detachedLabel->setText(tr("Not on a branch, and no branch can take this commit without moving the working tree or "
			"dropping commits. %1 Committing is blocked - check out a branch first.").arg(reattachObstaclesText(state.reattachObstacles)));
		return;
	}

	const QString offer = candidates.size() == 1 ? reattachDescription(candidates.front())
		: tr("%1 branches can take this commit without moving the working tree.").arg(candidates.size());
	_reattachButton->setText(candidates.size() == 1 ? reattachActionText(candidates.front()) : tr("Choose a branch..."));

	// What reattachHeadForCommit() chooses from
	std::vector<ReattachCandidate> takenAtCommit;
	std::ranges::copy_if(candidates, std::back_inserter(takenAtCommit), &ReattachCandidate::tipIsHead);
	QString onCommit;
	if (takenAtCommit.empty())
		onCommit = tr("Committing is blocked until HEAD is on a branch.");
	else if (takenAtCommit.size() > 1)
		onCommit = tr("Committing asks which branch to put HEAD on.");
	else if (takenAtCommit.front().kind == ReattachCandidate::Kind::AtHead)
		onCommit = tr("Committing checks out '%1' first.").arg(takenAtCommit.front().branch);
	else
		onCommit = tr("Committing creates '%1' tracking %2 first.").arg(takenAtCommit.front().branch, takenAtCommit.front().upstream);

	_detachedLabel->setText(tr("Not on a branch. %1 %2").arg(offer, onCommit));
}

void CommitWindow::updateControlStates()
{
	const RepoState& state = _repo->state();
	const int checkedCount = _filesModel.checkedCount();
	const int checkableCount = _filesModel.checkableCount();

	{
		QSignalBlocker blocker{ _checkAllBox };
		_checkAllBox->setText(tr("%1 of %2 checked").arg(checkedCount).arg(checkableCount));
		_checkAllBox->setCheckState(checkedCount == 0 ? Qt::Unchecked
			: checkedCount == checkableCount ? Qt::Checked : Qt::PartiallyChecked);
	}

	// One rich-text label rather than two columns: a QLabel can color halves of a string without a delegate
	const std::optional<LineCounts> lineTotals = _filesModel.checkedLineTotals();
	_lineTotalsLabel->setText(lineTotals
		? QStringLiteral("<span style=\"color:%1\">%2</span>&nbsp;&nbsp;<span style=\"color:%3\">%4</span>")
			.arg(lineCountColor(true).name(), lineCountText(lineTotals, true),
				lineCountColor(false).name(), lineCountText(lineTotals, false))
		: QString{});

	const bool detachedAndStuck = state.detached && std::ranges::none_of(state.reattachCandidates, &ReattachCandidate::tipIsHead);
	const bool canCommit = checkedCount > 0 && !_messageEdit->toPlainText().trimmed().isEmpty()
		&& !detachedAndStuck && !state.opBlocksCommit() && canActOnList();
	_commitButton->setEnabled(canCommit);
	_commitPushButton->setEnabled(canCommit && !_repo->pushInFlight());
	_commitButton->setText(state.mergeCommitRequired() ? tr("Commit (%1 files)").arg(checkedCount)
		: tr("Commit %1 file(s)").arg(checkedCount));

	_pushButton->setEnabled(!_repo->writeInFlight());
	_checkIncomingAction->setEnabled(incomingCheckPossible(state));
	_pushOneByOneAction->setEnabled(!_repo->writeInFlight() && !state.upstream.isEmpty() && !state.upstreamGone && state.ahead > 0);
	// The history-changing actions wait for a push too: it is publishing the history they would rewrite
	const bool canChangeHistory = canActOnList() && !_repo->pushInFlight();
	// undoLastCommit() reports every refusal, so nothing else disables this
	_uncommitAction->setEnabled(canChangeHistory);
	_reattachButton->setEnabled(canChangeHistory);
	_abortAction->setEnabled(state.operationInProgress() && canChangeHistory);
	// A continue command is named exactly where continueOperation() can run it, and every such command refuses
	// while a conflict is still unresolved
	_continueAction->setEnabled(!state.opHint.continueCommand.isEmpty() && _filesModel.unresolvedConflictPaths().isEmpty()
		&& canChangeHistory);
}

QString CommitWindow::subjectOrPlaceholder(const QString& subject)
{
	return subject.isEmpty() ? tr("<no commit title>") : subject;
}

void CommitWindow::saveDraftMessage()
{
	if (!_stateWasRead)
		return; // no parent sha was ever read here: an unreadable repository must not drop the stored draft

	QSettings settings;
	const QString group = draftGroup(_repo->path());
	const QString message = _messageEdit->toPlainText();
	if (message.trimmed().isEmpty())
	{
		settings.remove(group);
		return;
	}

	settings.beginGroup(group);
	settings.setValue(Settings::CommitDraftMessageKey, message);
	// May be stale: a stale sha only means the next open finds no match
	settings.setValue(Settings::CommitDraftParentShaKey, _repo->state().headSha);
}

// Runs from the first refresh that establishes the state: before it there is no parent sha to compare against
void CommitWindow::restoreDraftIfParentUnchanged()
{
	if (!_messageEdit->toPlainText().isEmpty())
		return; // typed into while that refresh was still running

	QSettings settings;
	settings.beginGroup(draftGroup(_repo->path()));
	const QString message = settings.value(Settings::CommitDraftMessageKey).toString();
	// A group that is not there reads as an empty sha, the same value an unborn repository stores
	if (message.isEmpty() || settings.value(Settings::CommitDraftParentShaKey).toString() != _repo->state().headSha)
		return;

	_messageEdit->setPlainText(message);
	_messageEdit->moveCursor(QTextCursor::End);
}

bool CommitWindow::canActOnList() const
{
	return !_repo->mutationInFlight() && _repo->state().known();
}

Vcs::Answer<void> CommitWindow::mutationDone(const QString& errorTitle, bool changesHistory)
{
	return [this, errorTitle, changesHistory](std::expected<void, QString> result) {
		_repo->endMutation();
		if (!result)
			showError(errorTitle, result.error());
		else if (changesHistory)
			_repo->announceHistoryChange();
		_repo->refresh();
	};
}

void CommitWindow::startCommit(bool pushAfterwards)
{
	if (_repo->mutationInFlight())
		return;

	// doCommit re-checks this stamp: the reattach and confirm dialogs below outlive the state decided against
	const StateStamp stamp = _repo->stateStamp();

	// A merge commit takes every tracked change, so an unresolved row would go in with its conflict markers
	const QStringList unresolved = _filesModel.unresolvedConflictPaths();
	if (!unresolved.isEmpty())
	{
		MessageDialog::notice(this, tr("Unresolved conflicts"),
			tr("%1 file(s) still have conflicts. Edit each one, then mark it resolved from the file list's "
			   "context menu:\n\n%2").arg(unresolved.size()).arg(listedPaths(unresolved)), {});
		return;
	}

	_repo->beginMutation(this);
	if (!_repo->state().detached)
	{
		confirmUntrackedThenCommit(pushAfterwards, stamp);
		return;
	}
	reattachHeadForCommit([this, pushAfterwards, stamp](bool reattached) {
		if (reattached)
			confirmUntrackedThenCommit(pushAfterwards, stamp);
		else
			_repo->endMutation();
	});
}

void CommitWindow::confirmUntrackedThenCommit(bool pushAfterwards, StateStamp decisionStamp)
{
	const QStringList untracked = _filesModel.checkedUntrackedPaths();
	if (!untracked.isEmpty())
	{
		const auto answer = MessageDialog::question(this, tr("Start tracking new files?"),
			tr("%1 checked file(s) are not tracked yet. Committing will add them to the repository:\n\n%2")
				.arg(untracked.size()).arg(listedPaths(untracked)),
			{ tr("Track and commit") });
		if (answer != 0)
		{
			_repo->endMutation();
			return;
		}
	}
	doCommit(pushAfterwards, decisionStamp);
}

// Takes only a candidate whose ref already points at HEAD: moving a branch is left to the strip's button
void CommitWindow::reattachHeadForCommit(std::function<void(bool reattached)> onDone)
{
	std::vector<ReattachCandidate> candidates = _repo->state().reattachCandidates;
	std::erase_if(candidates, [](const ReattachCandidate& candidate) { return !candidate.tipIsHead(); });
	if (candidates.empty())
	{
		assert_unconditional_r("Committing is disabled on a detached HEAD without such a candidate");
		onDone(false);
		return;
	}

	const std::optional<ReattachCandidate> chosen = chooseReattachCandidate(std::move(candidates), _repo->stateStamp());
	if (!chosen)
	{
		onDone(false);
		return;
	}
	_repo->reattachHead(*chosen, [this, onDone](std::expected<void, QString> result) {
		if (!result)
			showError(tr("Could not put HEAD on a branch"), result.error());
		onDone(result.has_value());
	});
}

void CommitWindow::reattachHeadFromStrip()
{
	if (_repo->writeInFlight())
		return;

	const std::optional<ReattachCandidate> chosen = chooseReattachCandidate(_repo->state().reattachCandidates, _repo->stateStamp());
	if (!chosen)
		return;

	_repo->beginMutation(this);
	_repo->reattachHead(*chosen, mutationDone(tr("Could not put HEAD on a branch"), /*changesHistory=*/true));
}

std::optional<ReattachCandidate> CommitWindow::chooseReattachCandidate(std::vector<ReattachCandidate> candidates, const StateStamp& stamp)
{
	assert_and_return_r(!candidates.empty(), {});
	if (candidates.size() == 1)
		return candidates.front();

	QStringList descriptions, actions;
	for (const ReattachCandidate& candidate : candidates)
	{
		descriptions.push_back(reattachDescription(candidate));
		actions.push_back(reattachActionText(candidate));
	}
	const std::optional<int> answer = MessageDialog::question(this, tr("Not on a branch"),
		tr("Which branch should HEAD be put on?\n\n%1").arg(descriptions.join(QLatin1Char('\n'))), actions);
	if (!answer || stateMovedSince(this, *_repo, stamp))
		return {};
	return candidates[size_t(*answer)];
}

void CommitWindow::doCommit(bool pushAfterwards, StateStamp decisionStamp)
{
	// The last check before the write: the whole chain from startCommit - dialogs and the asynchronous
	// reattach - acted on the stamped state, and the commit mode below is chosen from it
	if (stateMovedSince(this, *_repo, decisionStamp))
	{
		_repo->endMutation();
		return;
	}

	const QString message = _messageEdit->toPlainText();
	const QStringList pathspec = _filesModel.checkedPathspec();
	const QStringList untracked = _filesModel.checkedUntrackedPaths();
	assert_r(!message.trimmed().isEmpty() && !pathspec.isEmpty());
	assert_r(_repo->mutationInFlight()); // held by startCommit across the reattach and the dialogs

	const auto onDone = [this, message, pushAfterwards](std::expected<void, QString> result) {
		_repo->endMutation();
		if (!result)
		{
			showError(tr("Commit failed"), result.error());
			_repo->refresh();
			return;
		}
		if (_messageEdit->toPlainText() == message) // text typed while the commit ran stays put
			_messageEdit->clear();
		_repo->announceHistoryChange();
		_repo->refresh();
		if (pushAfterwards)
			_pushPane->startPush(PushMode::Branch);
	};

	if (_repo->state().mergeCommitRequired())
		_repo->commitMergeState(message, untracked, onDone);
	else
		_repo->commit(message, pathspec, untracked, onDone);
}

void CommitWindow::checkForIncomingChanges()
{
	// Window-modal, but a menu shared across windows may still reach this one
	if (_incomingDialog)
	{
		_incomingDialog->activateWindow();
		return;
	}

	_incomingDialog = new IncomingChangesDialog(*_repo, this);
	_incomingDialog->open();
	if (!_incomingCheckInFlight)
		startIncomingCheck(/*interactive=*/true);
}

void CommitWindow::startIncomingCheck(bool interactive)
{
	assert_r(!_incomingCheckInFlight);
	_incomingCheckInFlight = true;

	// Never detached here: a detached HEAD has no upstream, and no check starts without one
	_repo->checkIncoming(interactive, /*detachedHeadBranch=*/{}, MaxListedIncomingCommits, this,
		[this](std::expected<std::vector<CommitRecord>, QString> commits) {
			_incomingCheckInFlight = false;
			_incomingCheckOutcome = commits ? IncomingCheckOutcome::Succeeded : IncomingCheckOutcome::Failed;
			_incomingCheckError = commits ? QString{} : commits.error();
			updateHeader();

			if (_incomingDialog)
				_incomingDialog->setOwnAnswer(commits, _repo->state().behind);
		});
}

void CommitWindow::showDiffForCurrentRow()
{
	_diffQuery.cancel();
	_rowAwaitsChangeSet = false;

	// Every change of row passes through here, so text still shown is the last shown row's
	if (const std::optional<int> topLine = _diffPane->topLine())
		_lastShownRow.topLine = *topLine;

	const std::optional<FileEntry> current = currentEntry();
	if (!current)
	{
		_diffPane->showMessage({}, {});
		return;
	}

	const FileEntry& entry = *current;
	// A refresh shows the same row again: its text stays up until replaced, at the reader's place
	const bool sameRowShown = entry.path == _lastShownRow.path && _diffPane->topLine();
	if (entry.path != _lastShownRow.path)
		_lastShownRow = { entry.path };

	if (entry.isSubmodule)
	{
		if (!entry.pointerMoved)
		{
			_diffPane->showMessage({ entry.path, tr("submodule") }, tr("The submodule pointer has not moved, but there are uncommitted changes inside the submodule.\nDouble-click to open it."));
			return;
		}
		const DiffPane::ItemInfo info{ entry.path, tr("new commits") };
		_diffPane->showMessage(info, tr("Loading..."));
		_diffQuery = _repo->submodulePointerLog(entry.path, this, [this, info](std::expected<QString, QString> log) {
			_diffPane->showMessage(info, log ? tr("New commits in the submodule:\n\n") + *log : log.error());
		});
		return;
	}

	if (entry.isUntrackedRepository)
	{
		_diffPane->showMessage({ entry.path, tr("repository") }, tr("An untracked repository inside this one.\nDouble-click to open it."));
		return;
	}
	if (entry.type == ChangeType::Untracked)
	{
		showFileContents(entry);
		return;
	}

	const DiffPane::ItemInfo info{ entry.path, workingTreeTag(), workingTreeSizeLabel(absolutePath(entry)) };
	const QString noContentText = tr("No content changes (only the mode or the line endings differ, or the file matches HEAD).");
	if (!sameRowShown)
		_diffPane->showMessage(info, tr("Loading..."));

	if (_changeSetPending) // the set from before the refresh is still held, possibly stale
	{
		_rowAwaitsChangeSet = true;
		return;
	}
	if (const std::optional<int> section = _changeSet ? _changeSet->fileIndex(entry.path) : std::nullopt)
	{
		_diffPane->showSection(info, *_changeSet, *section, Settings::maxShownDiffBytes(), noContentText);
		_diffPane->scrollLineToTop(_lastShownRow.topLine);
		return;
	}

	_diffQuery = _repo->diffFile(entry, Settings::maxShownDiffBytes(), this, [this, info, noContentText](std::expected<QByteArray, QString> diff) {
		if (!diff)
			_diffPane->showMessage(info, diff.error()); // an oversize diff fails here, never held whole
		else if (diff->isEmpty())
			_diffPane->showMessage(info, noContentText);
		else
		{
			if (const std::optional<int> topLine = _diffPane->topLine())
				_lastShownRow.topLine = *topLine; // scrolled while the query ran
			_diffPane->showDiff(info, QString::fromUtf8(*diff));
			_diffPane->scrollLineToTop(_lastShownRow.topLine);
		}
	});
}

void CommitWindow::showFileContents(const FileEntry& entry)
{
	const QString path = absolutePath(entry);
	QFile file{ path };
	const qint64 sizeBytes = file.size();
	const DiffPane::ItemInfo info{ entry.path, tr("new file"), formattedFileSize(sizeBytes) };
	if (sizeBytes > Settings::maxShownDiffBytes())
	{
		_diffPane->showMessage(info, tr("The file is too large to display (%1).").arg(formattedFileSize(sizeBytes)));
		return;
	}
	if (!file.open(QIODevice::ReadOnly))
	{
		_diffPane->showMessage(info, tr("Could not read '%1'.").arg(QDir::toNativeSeparators(path)));
		return;
	}
	const QByteArray contents = file.readAll();
	if (contents.isEmpty())
	{
		_diffPane->showMessage(info, tr("The file is empty."));
		return;
	}
	const std::optional<QString> text = decodedAsText(contents);
	if (!text)
	{
		_diffPane->showMessage(info, tr("Binary file (%1).").arg(formattedFileSize(sizeBytes)));
		return;
	}

	_diffPane->showFileText(info, *text);
	_diffPane->scrollLineToTop(_lastShownRow.topLine);
}

void CommitWindow::onRowActivated(const QModelIndex& sourceIndex)
{
	if (!sourceIndex.isValid())
		return;
	const FileEntry entry = _filesModel.entryAt(sourceIndex.row());

	if (entry.isSubmodule || entry.isUntrackedRepository)
	{
		openNestedRepositoryWindow(entry);
		return;
	}
	if (entry.type == ChangeType::Deleted)
		return; // nothing on disk to open

	openEntryExternally(entry);
}

void CommitWindow::openEntryExternally(const FileEntry& entry)
{
	const QString path = absolutePath(entry);
	if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
		showError(tr("Failed to open the file"), QDir::toNativeSeparators(path));
}

void CommitWindow::showHistoryWindow()
{
	showRepositoryHistory(_repo->location());
}

void CommitWindow::continueOperation()
{
	if (_repo->writeInFlight())
		return;

	// No confirmation: continuing is what the operation is waiting for, and it destroys nothing.
	// The command may open the user's editor, so the mutation can stay in flight for as long as that is up.
	_repo->beginMutation(this);
	_repo->continueOperation(mutationDone(tr("Continue failed"), /*changesHistory=*/true));
}

void CommitWindow::abortOperation()
{
	if (_repo->writeInFlight())
		return;

	const RepoOp op = _repo->state().op;
	const OperationHint hint = _repo->state().opHint; // copied: a refresh may land in the dialog's event loop
	assert_and_return_r(op != RepoOp::None, ); // the action is disabled without one

	const bool bisect = op == RepoOp::Bisect;
	const QString title = bisect ? tr("End the bisect?") : tr("Abort the %1?").arg(hint.name);

	// Ending a bisect loses nothing, but only git returns to the pre-bisect branch: hg's reset leaves the
	// working directory where the bisect left it
	QString text;
	if (bisect)
		text = _repo->kind() == VcsKind::Git
			? tr("The bisect session ends, and the branch that was checked out before it started is checked out again.")
			: tr("The bisect session ends. The working directory stays on the revision the bisect left it at.");
	else if (!hint.abortRewinds)
		text = tr("The steps it has not reached yet are dropped. What it has already committed stays where it is, "
				  "and ending it here does not undo that.");
	else
		text = tr("The repository goes back to where it was before the operation started, and every conflict "
				  "resolution goes with it.\n\nA change that was already uncommitted when the operation began may "
				  "not survive either.");

	const StateStamp stamp = _repo->stateStamp();
	const auto answer = MessageDialog::question(this, title, text, { bisect ? tr("End bisect") : tr("Abort") });
	if (answer != 0 || stateMovedSince(this, *_repo, stamp))
		return;

	_repo->beginMutation(this);
	_repo->abortOperation(mutationDone(tr("Abort failed"), /*changesHistory=*/true));
}

void CommitWindow::openNestedRepositoryWindow(const FileEntry& entry)
{
	followSubmoduleHistory(*openRepositoryWindow(_repo->nestedRepositoryLocation(entry.path)));
}

void CommitWindow::showContextMenu(const QPoint& pos)
{
	// The VCS action slots re-query the selection instead of using these entries: a refresh during menu.exec() safely empties it
	const std::vector<FileEntry> entries = selectedEntries();
	if (entries.empty())
		return;

	const bool operationInProgress = _repo->state().operationInProgress();
	// Only gates the writing actions; a stale row is still worth inspecting
	const bool canAct = canActOnList();
	bool anyAddable = false, anyAdded = false, anyDeletable = false, anyDiscardable = false, anyConflicted = false;
	for (const FileEntry& entry : entries)
	{
		anyDiscardable |= discardable(entry);
		if (entry.isSubmodule)
			continue;
		anyAddable |= entry.type == ChangeType::Untracked && !entry.isUntrackedRepository;
		anyAdded |= entry.type == ChangeType::Added;
		anyConflicted |= entry.type == ChangeType::Conflicted;
		anyDeletable |= entry.type != ChangeType::Deleted;
	}
	// The row the single-selection actions use: each of them is hidden unless `single`
	const FileEntry& first = entries.front();
	const bool single = entries.size() == 1;
	const bool singleFile = single && !first.isSubmodule && first.type != ChangeType::Deleted;

	// An action this selection can never reach is hidden; one only the repository's state blocks is disabled.
	QMenu menu{ this };

	QAction* markResolvedAction = menu.addAction(tr("Mark resolved"), this, &CommitWindow::markResolvedSelection);
	markResolvedAction->setVisible(anyConflicted);
	markResolvedAction->setEnabled(canAct);
	QAction* addAction = menu.addAction(tr("Add"), this, &CommitWindow::addSelectionToIndex);
	addAction->setVisible(anyAddable);
	addAction->setEnabled(canAct);

	QAction* unAddAction = menu.addAction(tr("Un-add"), this, &CommitWindow::unAddSelection);
	unAddAction->setVisible(anyAdded);
	unAddAction->setEnabled(canAct);

	const bool singleUntracked = single && !first.isSubmodule && first.type == ChangeType::Untracked;
	QMenu* ignoreMenu = menu.addMenu(tr("Add to %1").arg(_repo->ignoreFileName()));
	ignoreMenu->menuAction()->setVisible(singleUntracked);
	ignoreMenu->setEnabled(canAct); // the pattern comes from the row, so it is as stale as the row
	if (singleUntracked)
	{
		for (const IgnorePattern& pattern : _repo->ignorePatternsFor(first.path))
		{
			// '&' doubled for display, or QMenu takes it as an accelerator marker
			ignoreMenu->addAction(QString{ pattern.text }.replace(QLatin1Char('&'), QStringLiteral("&&")),
				this, [this, pattern] { addPatternToIgnoreFile(pattern); });
		}
	}

	menu.addSeparator();

	QAction* openAction = menu.addAction(tr("Open"), this, [this, entry = first] {
		openEntryExternally(entry);
	});
	openAction->setVisible(singleFile);

	QAction* editAction = menu.addAction(tr("Edit"), this, [this, entry = first] {
		openInTextEditor(absolutePath(entry), this);
	});
	editAction->setVisible(singleFile && !first.isUntrackedRepository);

	QAction* nestedHistoryAction = menu.addAction(tr("View commit history"), this, [this, entry = first] {
		showRepositoryHistory(_repo->nestedRepositoryLocation(entry.path));
	});
	nestedHistoryAction->setVisible(single && (first.isSubmodule || first.isUntrackedRepository));

	QAction* fileHistoryAction = menu.addAction(tr("View file history"), this, [this, entry = first] {
		// Nothing is committed at a rename's new path yet
		const QString& path = entry.oldPath.isEmpty() ? entry.path : entry.oldPath;
		auto* window = new HistoryWindow(_repo->location(), path);
		window->show();
	});
	// A submodule's history is its own repo's, offered above; an untracked or newly added file is in no commit
	fileHistoryAction->setVisible(single && !first.isSubmodule && first.type != ChangeType::Untracked && first.type != ChangeType::Added);

	QAction* showInFileManagerAction = menu.addAction(showInFileManagerActionText(), this, [this, entry = first] {
		showInFileManager(absolutePath(entry));
	});

	showInFileManagerAction->setVisible(singleFile); // a deleted path has nothing to reveal

	QStringList paths;
	for (const FileEntry& entry : entries)
		paths.push_back(entry.path);
	addCopyPathActions(menu, paths, _repo->path());
	menu.addSeparator();

	QAction* deleteAction = menu.addAction(tr("Delete to %1").arg(trashName()), this, &CommitWindow::deleteSelection);
	deleteAction->setVisible(anyDeletable);
	deleteAction->setEnabled(canAct);
	// Display only: the view's event filter handles the key. WidgetShortcut on an action belonging to no
	// widget never registers, so the key cannot trigger twice.
	deleteAction->setShortcut(QKeySequence::Delete);
	deleteAction->setShortcutContext(Qt::WidgetShortcut);
	deleteAction->setShortcutVisibleInContextMenu(true);

	menu.addSeparator();

	// A lone submodule row discards what is uncommitted inside it instead. One row only: the dialog lists the
	// paths inside that one submodule.
	const bool contentOfOneSubmodule = single && contentDiscardable(first);
	QAction* discardAction = menu.addAction(tr("Discard changes"), this, &CommitWindow::discardSelection);
	discardAction->setVisible(anyDiscardable || contentOfOneSubmodule);
	// Disabled, not hidden: the operation ends, and _opStrip says one is running
	discardAction->setEnabled(canAct && !operationInProgress && !_discardPlanInFlight);

	hideRedundantSeparators(menu);
	menu.exec(_filesView->viewport()->mapToGlobal(pos));
}

CommitWindow::SelectionByPath CommitWindow::captureSelectionByPath() const
{
	SelectionByPath selection;
	for (const FileEntry& entry : selectedEntries())
		selection.paths.insert(entry.path);

	if (const std::optional<FileEntry> current = currentEntry())
		selection.currentPath = current->path;
	return selection;
}

void CommitWindow::restoreSelectionByPath(const SelectionByPath& selection)
{
	// One pass over the rows, so a large selection over a large list stays linear
	std::vector<int> rows;
	int currentRow = -1;
	for (int row = 0; row < _filesModel.rowCount(); ++row)
	{
		const QString& path = _filesModel.entryAt(row).path;
		if (selection.paths.contains(path))
			rows.push_back(row);
		if (path == selection.currentPath)
			currentRow = row;
	}

	if (currentRow < 0) // the file is gone: committed, or discarded elsewhere
		currentRow = _filesView->firstShownSourceIndex().row();

	_filesView->setSelectedSourceRows(rows, currentRow);
}

std::vector<FileEntry> CommitWindow::selectedEntries() const
{
	const QModelIndexList indexes = _filesView->selectedSourceRows();
	std::vector<FileEntry> entries;
	entries.reserve(size_t(indexes.size()));
	for (const QModelIndex& index : indexes)
		entries.push_back(_filesModel.entryAt(index.row()));
	return entries;
}

std::optional<FileEntry> CommitWindow::currentEntry() const
{
	const QModelIndex current = _filesView->currentSourceIndex();
	if (!current.isValid() || current.row() >= _filesModel.rowCount())
		return {};
	return _filesModel.entryAt(current.row());
}

void CommitWindow::toggleCheckOnSelection()
{
	const QModelIndexList rows = _filesView->selectedSourceRows();

	bool allChecked = true;
	for (const QModelIndex& index : rows)
	{
		if (_filesModel.isUserCheckable(index.row()) && !_filesModel.isChecked(index.row()))
		{
			allChecked = false;
			break;
		}
	}
	_filesModel.setRowsChecked(rows, !allChecked);
}

void CommitWindow::deleteSelection()
{
	// Submodules and already-deleted rows are skipped
	QStringList untrackedPaths, addedPaths, trackedPaths;
	QSet<QString> repositoryPaths; // the only rows that are folders
	for (const FileEntry& entry : selectedEntries())
	{
		if (entry.isSubmodule || entry.type == ChangeType::Deleted)
			continue;
		if (entry.isUntrackedRepository)
			repositoryPaths.insert(entry.path);
		if (entry.type == ChangeType::Untracked)
			untrackedPaths.push_back(entry.path);
		else if (entry.type == ChangeType::Added)
			addedPaths.push_back(entry.path);
		else
			trackedPaths.push_back(entry.path);
	}
	if (untrackedPaths.isEmpty() && addedPaths.isEmpty() && trackedPaths.isEmpty())
		return;

	// A single untracked file goes to the trash unprompted, as it would from a file manager.
	// Anything more asks first, and the dialog names the untracked files too: they are deleted with the rest.
	// A repository always asks: it may hold unpushed commits
	if (!trackedPaths.isEmpty() || !addedPaths.isEmpty() || untrackedPaths.size() > 1 || !repositoryPaths.isEmpty())
	{
		QStringList prompted = trackedPaths + addedPaths + untrackedPaths;
		for (QString& path : prompted)
		{
			if (repositoryPaths.contains(path))
				path += QLatin1Char('/');
		}
		const qsizetype repositoryCount = repositoryPaths.size();
		const qsizetype fileCount = prompted.size() - repositoryCount;
		const QString files = fileCount == 1 ? tr("1 file") : tr("%1 files").arg(fileCount);
		const QString repositories = repositoryCount == 1 ? tr("1 repository") : tr("%1 repositories").arg(repositoryCount);
		const QString title = repositoryCount == 0 ? tr("Delete files?")
			: fileCount == 0 ? (repositoryCount == 1 ? tr("Delete repository?") : tr("Delete repositories?"))
			: tr("Delete files and repositories?");
		const QString counted = repositoryCount == 0 ? files : fileCount == 0 ? repositories : tr("%1 and %2").arg(files, repositories);
		const auto answer = MessageDialog::question(this, title,
			tr("Move %1 to the %2?\n\n%3").arg(counted, trashName(), listedPaths(prompted)), { tr("Delete") });
		if (answer != 0)
			return;
	}

	const auto trashAllThenRefresh = [this](const QStringList& paths) {
		for (const QString& path : paths)
		{
			if (QFile::moveToTrash(QDir(_repo->path()).filePath(path)))
				continue;
			// Never fall back to a permanent delete
			MessageDialog::notice(this, tr("Delete failed"),
				tr("Could not move '%1' to the %2 (it may be locked, or the volume has no %2).\n"
				   "The remaining items were not deleted.").arg(path, trashName()), {});
			break;
		}
		_repo->refresh();
	};

	if (!addedPaths.isEmpty())
	{
		// Un-add first, or the index would point at a file that no longer exists
		_repo->beginMutation(this);
		_repo->unAdd(addedPaths, [this, paths = untrackedPaths + trackedPaths + addedPaths, trashAllThenRefresh](std::expected<void, QString> result) {
			_repo->endMutation();
			if (!result)
			{
				showError(tr("Failed to un-add before deleting"), result.error());
				return;
			}
			trashAllThenRefresh(paths);
		});
		return;
	}

	trashAllThenRefresh(untrackedPaths + trackedPaths);
}

void CommitWindow::discardSelection()
{
	// Restoring a path to HEAD mid-operation would silently drop the operation's result for it. Re-read rather
	// than taken from the menu: a refresh during the menu's event loop can start one.
	if (_repo->state().operationInProgress())
		return;

	// Everything is read from the model up front: the dialog below spins an event loop, and a refresh in it resets the rows.
	const std::vector<FileEntry> selection = selectedEntries();

	// A lone submodule row discards what is uncommitted inside it. No row is ever both: a submodule whose
	// content blocks its pointer is not discardable() at all.
	if (selection.size() == 1 && contentDiscardable(selection.front()))
	{
		discardSubmoduleContent(selection.front());
		return;
	}

	// Added rows are un-added rather than restored: discardChanges() would delete the file, and nothing in this
	// window destroys content that was never committed.
	QStringList pathspec, promptPaths, addedPaths;
	bool anySubmodule = false;
	int skippedRows = 0;
	for (const FileEntry& entry : selection)
	{
		if (!discardable(entry))
		{
			++skippedRows; // a deliberate no-op in a mixed selection
			continue;
		}
		if (!entry.isSubmodule && entry.type == ChangeType::Added)
		{
			addedPaths.push_back(entry.path);
			continue;
		}
		promptPaths.push_back(entry.path);
		pathspec.push_back(entry.path);
		if (!entry.oldPath.isEmpty())
			pathspec.push_back(entry.oldPath); // both sides, or the old name of a rename stays deleted
		anySubmodule |= entry.isSubmodule;
	}
	if (pathspec.isEmpty() && addedPaths.isEmpty())
		return;

	if (!promptPaths.isEmpty()) // un-adding alone loses nothing, so it needs no confirmation
	{
		QString text = tr("Discard all changes to %1 file(s)? This cannot be undone.\n\n%2")
			.arg(promptPaths.size()).arg(listedPaths(promptPaths));
		if (anySubmodule)
			text += tr("\n\nDiscarding a submodule's pointer change checks out the recorded commit inside it, leaving the submodule on a detached HEAD.");
		if (!addedPaths.isEmpty())
			text += tr("\n\n%1 added file(s) will be un-added and left on disk.").arg(addedPaths.size());
		if (skippedRows > 0)
			text += tr("\n\n%1 other selected row(s) will be left as they are: untracked files, and submodules with "
					   "modified or unreadable content, cannot be discarded.").arg(skippedRows);

		const StateStamp stamp = _repo->stateStamp();
		const auto answer = MessageDialog::question(this, tr("Discard changes?"), text, { tr("Discard") });
		if (answer != 0 || stateMovedSince(this, *_repo, stamp))
			return;
	}

	const auto unAddThenRefresh = [this, addedPaths] {
		if (addedPaths.isEmpty())
		{
			_repo->endMutation();
			_repo->refresh();
			return;
		}
		_repo->unAdd(addedPaths, mutationDone(tr("Un-add failed")));
	};

	_repo->beginMutation(this);
	if (pathspec.isEmpty())
	{
		unAddThenRefresh();
		return;
	}

	_repo->discardChanges(pathspec, [this, unAddThenRefresh](std::expected<void, QString> result) {
		if (!result)
		{
			_repo->endMutation();
			showError(tr("Discard failed"), result.error());
			_repo->refresh();
			return;
		}
		unAddThenRefresh();
	});
}

void CommitWindow::discardSubmoduleContent(const FileEntry& submodule)
{
	const QString path = submodule.path;
	// The stamp spans the plan queries and the dialog: the state the plan describes must still stand at the write
	const StateStamp stamp = _repo->stateStamp();
	_discardPlanInFlight = true;
	_repo->submoduleDiscardPlan(path, this, [this, path, stamp](SubmoduleDiscardPlan plan) {
		_discardPlanInFlight = false;
		if (!plan.refusal.isEmpty())
		{
			showError(tr("Cannot discard the changes inside '%1'").arg(path), plan.refusal);
			return;
		}

		if (!plan.restored.isEmpty()) // taking files out of tracking alone loses nothing, as in discardSelection()
		{
			QString text = tr("Discard all changes inside '%1'? This cannot be undone.\n\n%2")
				.arg(path, listedPaths(plan.restored));
			if (!plan.keptOnDisk.isEmpty())
				text += tr("\n\n%1 file(s) its last commit does not have will be taken out of version control and left on disk.")
					.arg(plan.keptOnDisk.size());
			text += tr("\n\nUntracked files are left alone, and the submodule stays on its branch.");

			if (MessageDialog::question(this, tr("Discard changes?"), text, { tr("Discard") }) != 0)
				return;
		}
		if (stateMovedSince(this, *_repo, stamp))
			return;

		startSubmoduleContentDiscard(path, plan);
	});
}

void CommitWindow::startSubmoduleContentDiscard(const QString& path, const SubmoduleDiscardPlan& plan)
{
	_repo->beginMutation(this);
	_repo->discardSubmoduleContent(path, plan, [this, path, done = mutationDone(tr("Discard failed"))](std::expected<void, QString> result) {
		done(std::move(result));
		if (CommitWindow* window = repositoryWindow(_repo->nestedRepositoryLocation(path).root))
			window->refreshRepository(); // its rows are stale: its working tree was changed from here
	});
}

void CommitWindow::undoLastCommit()
{
	// A push in flight blocks this too: the refusal is computed from pre-push state, and undoing the
	// commit a running push is publishing is the rewrite AlreadyPushed exists to prevent
	if (_repo->writeInFlight())
		return;

	// Decided on a fresh state: a commit or push made outside the app since the last refresh changes the
	// refusal and the message restored
	_repo->beginMutation(this);
	_repo->refresh([this] { confirmThenUndoLastCommit(); });
}

void CommitWindow::confirmThenUndoLastCommit()
{
	assert_r(_repo->mutationInFlight());

	const RepoState& state = _repo->state();
	if (!state.known())
	{
		_repo->endMutation(); // the refresh could not read the state; the read failure strip says why
		return;
	}

	const UndoRefusal refusal = state.lastCommitUndoRefusal();
	if (refusal != UndoRefusal::None)
	{
		_repo->endMutation();
		const auto reason = [&state, refusal] {
			switch (refusal)
			{
			case UndoRefusal::Unborn: return tr("There is nothing to undo: this repository has no commits yet.");
			case UndoRefusal::Detached:
				return tr("HEAD is detached, so there is no upstream to check whether the last commit has been pushed.");
			case UndoRefusal::OperationInProgress:
				return tr("A merge, rebase, cherry-pick, revert or bisect is in progress. Finish or abort it first.");
			case UndoRefusal::MergeCommit: return tr("The last commit is a merge, and undoing it would leave the merge half done.");
			case UndoRefusal::RootCommit:
				return tr("The last commit is the only one in this repository, so there is no earlier commit to go back to.");
			case UndoRefusal::AlreadyPushed:
				return tr("The last commit has already been pushed to %1. Undoing it would rewrite published history.").arg(state.upstream);
			case UndoRefusal::UpstreamGone:
				return tr("The upstream branch %1 is gone - deleted on the remote - so whether the last commit "
					"was pushed cannot be checked.").arg(state.upstream);
			case UndoRefusal::None: break;
			}
			return QString{};
		}();
		MessageDialog::notice(this, tr("Cannot undo the last commit"), reason, {}, QMessageBox::Information);
		return;
	}

	const StateStamp stamp = _repo->stateStamp();
	const auto answer = MessageDialog::question(this, tr("Undo the last commit?"),
		tr("'%1' will be undone. Its changes return to this list as uncommitted changes; the working tree "
			"is not modified.").arg(subjectOrPlaceholder(state.headSubject())),
		{ tr("Undo commit") });
	if (answer != 0 || stateMovedSince(this, *_repo, stamp))
	{
		_repo->endMutation();
		return;
	}

	_repo->undoLastCommit([this, message = state.headMessage, done = mutationDone(tr("Undo failed"), /*changesHistory=*/true)](std::expected<void, QString> result) {
		if (result && _messageEdit->toPlainText().trimmed().isEmpty())
		{
			_messageEdit->setPlainText(message);
			_messageEdit->moveCursor(QTextCursor::End);
		}
		done(std::move(result));
	});
}

void CommitWindow::addSelectionToIndex()
{
	const QStringList paths = pathsWhere(selectedEntries(),
		[](const FileEntry& entry) { return !entry.isSubmodule && entry.type == ChangeType::Untracked && !entry.isUntrackedRepository; });
	if (paths.isEmpty())
		return;
	_repo->beginMutation(this);
	_repo->addToIndex(paths, mutationDone(tr("Add failed")));
}

void CommitWindow::markResolvedSelection()
{
	const QStringList paths = pathsWhere(selectedEntries(),
		[](const FileEntry& entry) { return entry.type == ChangeType::Conflicted; });
	if (paths.isEmpty())
		return;
	_repo->beginMutation(this);
	_repo->markResolved(paths, mutationDone(tr("Failed to mark resolved")));
}

void CommitWindow::unAddSelection()
{
	const QStringList paths = pathsWhere(selectedEntries(),
		[](const FileEntry& entry) { return !entry.isSubmodule && entry.type == ChangeType::Added; });
	if (paths.isEmpty())
		return;
	_repo->beginMutation(this);
	_repo->unAdd(paths, mutationDone(tr("Un-add failed")));
}

void CommitWindow::addPatternToIgnoreFile(const IgnorePattern& pattern)
{
	QFile file{ QDir{ _repo->path() }.filePath(_repo->ignoreFileName()) };
	if (!file.open(QIODevice::ReadWrite)) // read too: the backend places the pattern by the existing content
	{
		MessageDialog::notice(this, tr("Failed to update %1").arg(_repo->ignoreFileName()),
			tr("Could not open '%1' for writing.").arg(QDir::toNativeSeparators(file.fileName())), {});
		return;
	}
	const QByteArray updated = _repo->ignoreFileWithPatternAdded(file.readAll(), pattern);
	file.seek(0);
	file.write(updated);
	file.resize(updated.size());
	file.close();
	_repo->refresh();
}

void CommitWindow::showError(const QString& title, const QString& details)
{
	// The command's output verbatim: hook output is what makes a rejected commit diagnosable
	MessageDialog::notice(this, title, title + QLatin1Char('.'), details);
}

QString CommitWindow::absolutePath(const FileEntry& entry) const
{
	return QDir{ _repo->path() }.filePath(entry.path);
}
