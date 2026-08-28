#include "commitwindow.h"
#ifdef Q_OS_MACOS
#include "commandlinetool_mac.h"
#endif
#include "consolelogview.h"
#include "diffpane.h"
#include "externalapps.h"
#include "filelistview.h"
#include "historymodels.h"
#include "historywindow.h"
#include "messageedit.h"
#include "recentrepositories.h"
#include "recentrepositoriespanel.h"
#include "repositoryfactory.h"
#include "repositorywindows.h"
#include "settings.h"
#include "settingspages.h"
#include "theme.h"

#include "aboutdialog/caboutdialog.h"
#include "dialogs/messagebox.h"
#include "hash/wheathash.hpp"
#include "settings/csettings.h"
#include "settingsui/csettingsdialog.h"
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
#include <QRegularExpression>
#include <QScreen>
#include <QSet>
#include <QShortcut>
#include <QSplitter>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <assert.h>

namespace {

constexpr int RecentRepositoriesDockWidth = 175; // first-run default; persists with the window state
constexpr int SplitterWidth = 1180; // width of the two panes together until the first refresh widens the window
constexpr int FirstRunDiffPaneWidth = 440; // what the diff keeps while the left pane takes its preferred width
// The left pane's preferred width follows the repository and branch names, which are unbounded
constexpr int MaxInitialLeftPaneWidth = 1000;
constexpr int MaxListedPathsInDialog = 20;
constexpr int MaxIncomingCommits = 200; // a peek, not a history window
constexpr int IncomingPopupWidth = 560;
constexpr int IncomingPopupHeight = 320;

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

bool looksLikeHexSha(const QString& token)
{
	if (token.length() < 7)
		return false;
	for (const QChar c : token)
	{
		if (!c.isDigit() && !(c >= QLatin1Char('a') && c <= QLatin1Char('f')) && !(c >= QLatin1Char('A') && c <= QLatin1Char('F')))
			return false;
	}
	return true;
}

// Every changed file's path and basename, plus identifier-shaped words from the changed lines.
// Length < 4 and a small stoplist weed out prose function words.
// Hex-sha-shaped tokens are dropped: submodule pointer diffs would pollute the pool.
QStringList completionWordsFor(const std::vector<FileEntry>& files, QByteArray diff)
{
	constexpr qsizetype MaxDiffBytesForWords = 8 * 1024 * 1024;
	constexpr qsizetype MaxWords = 20000;
	static const QSet<QString> stopWords = {
		QStringLiteral("with"), QStringLiteral("from"), QStringLiteral("this"), QStringLiteral("that"),
		QStringLiteral("when"), QStringLiteral("then"), QStringLiteral("than"), QStringLiteral("they"),
		QStringLiteral("them"), QStringLiteral("their"), QStringLiteral("there"), QStringLiteral("these"),
		QStringLiteral("those"), QStringLiteral("have"), QStringLiteral("been"), QStringLiteral("being"),
		QStringLiteral("will"), QStringLiteral("would"), QStringLiteral("should"), QStringLiteral("could"),
		QStringLiteral("into"), QStringLiteral("onto"), QStringLiteral("only"), QStringLiteral("over"),
		QStringLiteral("also"), QStringLiteral("each"), QStringLiteral("some"), QStringLiteral("such"),
		QStringLiteral("must"), QStringLiteral("does"), QStringLiteral("done"), QStringLiteral("upon"),
		QStringLiteral("very"), QStringLiteral("more"), QStringLiteral("most"), QStringLiteral("here"),
		QStringLiteral("where"), QStringLiteral("which"), QStringLiteral("while"), QStringLiteral("after"),
		QStringLiteral("before"), QStringLiteral("about"), QStringLiteral("because"),
	};
	static const QRegularExpression wordRe(QStringLiteral("[A-Za-z_][A-Za-z0-9_]{3,}"));

	QSet<QString> words;
	for (const FileEntry& file : files)
	{
		words.insert(file.path);
		words.insert(file.path.mid(file.path.lastIndexOf(QLatin1Char('/')) + 1));
	}

	diff.truncate(MaxDiffBytesForWords);
	// Wontfix: split copies the capped diff into lines on every refresh; a skip-when-unchanged check is not worth the complication
	for (const QByteArray& rawLine : diff.split('\n'))
	{
		if (words.size() >= MaxWords)
			break;
		if (rawLine.size() < 2 || (rawLine[0] != '+' && rawLine[0] != '-')
			|| rawLine.startsWith("+++") || rawLine.startsWith("---"))
			continue;

		const QString line = QString::fromUtf8(rawLine);
		for (auto it = wordRe.globalMatch(line); it.hasNext(); )
		{
			const QString token = it.next().captured();
			if (!stopWords.contains(token.toLower()) && !looksLikeHexSha(token))
				words.insert(token);
		}
	}
	return QStringList{ words.begin(), words.end() };
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
	_repo{ openRepository(location) }
{
	setAttribute(Qt::WA_DeleteOnClose);
	buildUi();

	// One geometry for every commit window
	installEventFilter(new CPersistenceEnabler(QStringLiteral("CommitWindow"), this, CPersistenceEnabler::Delayed{ true }, CPersistenceEnabler::SetDefaultSize{ false }));

	connect(_repo.get(), &Repository::refreshed, this, &CommitWindow::onRefreshed);
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

void CommitWindow::buildUi()
{
	QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
	QAction* openRepositoryAction = fileMenu->addAction(tr("&Open Repository..."), this, [this] { browseForRepository(this); });
	openRepositoryAction->setShortcut(QKeySequence::Open);
	fileMenu->addSeparator();
#ifdef Q_OS_MACOS
	fileMenu->addAction(tr("Install 'gg' Command Line Tool..."), this, [this] { installCommandLineToolAndReport(this); });
	fileMenu->addSeparator();
#endif
	fileMenu->addAction(tr("E&xit"), [] { QApplication::closeAllWindows(); }); // not quit(): closeEvent saves the layout state
	QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
	editMenu->addAction(tr("&Preferences..."), this, &CommitWindow::showPreferencesDialog)
		->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P));
	QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
	viewMenu->addAction(buildRecentRepositoriesDock());
	viewMenu->addAction(tr("Show &Welcome Screen"), &showWelcomeWindow);
	QMenu* repositoryMenu = menuBar()->addMenu(tr("&Repository"));
	repositoryMenu->addAction(openRepositoryAction); // the same action as in File, where Ctrl+O is looked for
	repositoryMenu->addAction(tr("&Scan Folder for Repositories..."), this, [this] { scanFolderForRepositories(this); });
	repositoryMenu->addSeparator();
	repositoryMenu->addAction(tr("&Refresh"), _repo.get(), &Repository::refresh)->setShortcut(QKeySequence::Refresh);
	_uncommitAction = repositoryMenu->addAction(tr("&Undo Last Commit"), this, &CommitWindow::undoLastCommit);
	// One item for every operation: the dialog names the one running, which no menu label has room to do
	_abortAction = repositoryMenu->addAction(tr("&Abort Operation..."), this, &CommitWindow::abortOperation);
	QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
	helpMenu->addAction(tr("&About"), this, [this] {
		CAboutDialog aboutDialog{ QStringLiteral(GG_VERSION), this };
		aboutDialog.exec();
	});

	auto* leftPane = new QWidget;
	auto* leftLayout = new QVBoxLayout(leftPane);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	leftLayout->setSpacing(0);

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
	_aheadLabel = new QLabel;
	_aheadLabel->setObjectName(QStringLiteral("aheadLabel"));
	_pushButton = new QPushButton(tr("Push"));
	_peekButton = new QPushButton(tr("Peek"));
	_peekButton->setToolTip(tr("Fetch and list the commits on the upstream that this branch does not have yet"));
	_historyButton = new QPushButton(tr("History"));
	_historyButton->setToolTip(QStringLiteral("Ctrl+H"));
	repoBarLayout->addWidget(_repoNameLabel);
	repoBarLayout->addWidget(_branchLabel);
	repoBarLayout->addWidget(_aheadLabel);
	repoBarLayout->addStretch();
	repoBarLayout->addWidget(_pushButton);
	repoBarLayout->addWidget(_peekButton);
	repoBarLayout->addWidget(_historyButton);
	leftLayout->addWidget(repoBar);

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
	_detachedStrip = makeStrip("warningStrip");
	leftLayout->addWidget(_readFailureStrip);
	leftLayout->addWidget(_opStrip);
	leftLayout->addWidget(_detachedStrip);

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
	leftLayout->addWidget(counterBar);

	_filesView = new FileListView;
	_filesView->setModel(&_filesModel);
	_filesView->setSelectionMode(QAbstractItemView::ExtendedSelection);
	_filesView->installEventFilter(this);
	leftLayout->addWidget(_filesView, 1);

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
	leftLayout->addWidget(messageHeader);

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
	leftLayout->addWidget(messageArea);

	auto* rightPane = new QWidget;
	auto* rightLayout = new QVBoxLayout(rightPane);
	rightLayout->setContentsMargins(0, 0, 0, 0);
	rightLayout->setSpacing(0);

	_diffPane = new DiffPane;
	rightLayout->addWidget(_diffPane, 1);

	_pushLogPane = new QWidget;
	auto* pushLogLayout = new QVBoxLayout(_pushLogPane);
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
	rightLayout->addWidget(_pushLogPane);
	_pushLogPane->hide();

	_splitter = new QSplitter(Qt::Horizontal);
	_splitter->setChildrenCollapsible(false);
	_splitter->setHandleWidth(1);
	_splitter->addWidget(leftPane);
	_splitter->addWidget(rightPane);
	_splitter->setStretchFactor(0, 0);
	_splitter->setStretchFactor(1, 1);
	if (const QByteArray state = CSettings{}.value(Settings::CommitWindowSplitterKey).toByteArray(); !state.isEmpty())
		_splitter->restoreState(state);
	else
		_initialWidthPending = true;
	setCentralWidget(_splitter);
	resize(SplitterWidth + RecentRepositoriesDockWidth, 740); // the dock is beside the two panes, not carved out of them
	acceptRepositoryFolderDrops(this);

	connect(_pushButton, &QPushButton::clicked, this, &CommitWindow::startPush);
	connect(_peekButton, &QPushButton::clicked, this, &CommitWindow::peekIncoming);
	connect(_historyButton, &QPushButton::clicked, this, &CommitWindow::showHistoryWindow);
	connect(hidePushLogButton, &QPushButton::clicked, _pushLogPane, &QWidget::hide);
	connect(_commitButton, &QPushButton::clicked, this, [this] { startCommit(false); });
	connect(_commitPushButton, &QPushButton::clicked, this, [this] { startCommit(true); });
	connect(_messageEdit, &QPlainTextEdit::textChanged, this, &CommitWindow::updateControlStates);
	connect(&_filesModel, &ChangedFilesModel::checksChanged, this, &CommitWindow::updateControlStates);
	connect(_checkAllBox, &QCheckBox::clicked, this, [this] {
		_filesModel.setAllChecked(_filesModel.checkedCount() < _filesModel.checkableCount());
	});
	connect(modifiedOnlyButton, &QPushButton::clicked, &_filesModel, &ChangedFilesModel::checkAllExceptUntracked);
	connect(_filesView, &FileListView::rowActivated, this, &CommitWindow::onRowActivated);
	connect(_filesView, &QWidget::customContextMenuRequested, this, &CommitWindow::showContextMenu);
	connect(_filesView->selectionModel(), &QItemSelectionModel::currentChanged, this, &CommitWindow::showDiffForCurrentRow);
	connect(_parentCommitLabel, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
		// Copied before exec() spins an event loop, in which a finishing refresh could replace the state
		const RepoState& state = _repo->state();
		const QString sha = state.headSha;
		const QString subject = state.headSubject;
		if (sha.isEmpty())
			return;

		QMenu menu{ this };
		QAction* copyTitleAction = menu.addAction(tr("Copy commit title"), this, [subject] { QApplication::clipboard()->setText(subject); });
		copyTitleAction->setEnabled(!subject.isEmpty());
		menu.addAction(tr("Copy long hash"), this, [sha] { QApplication::clipboard()->setText(sha); });
		menu.addAction(tr("Copy short hash"), this, [sha] { QApplication::clipboard()->setText(shortSha(sha)); });
		menu.exec(_parentCommitLabel->mapToGlobal(pos));
	});

	connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, this, [this] {
		_branchLabel->setFont(monospaceFont());
		if (_incomingView)
			_incomingView->setFont(monospaceFont());
	});

	new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] { close(); });
	new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_H), this, [this] { showHistoryWindow(); });
	new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this, [this] {
		if (_pushButton->isEnabled())
			startPush();
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

	updateControlStates();
	_messageEdit->setFocus();
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

	// Its own row rather than the header's: the header row's buttons are what hold the dock's minimum width,
	// and a field beside them would add to it
	auto* filterEdit = new QLineEdit;
	filterEdit->setPlaceholderText(tr("Filter"));
	filterEdit->setToolTip(tr("Show only the repositories whose path contains this text"));
	filterEdit->setClearButtonEnabled(true);
	auto* filterRow = new QWidget;
	auto* filterRowLayout = new QHBoxLayout(filterRow);
	filterRowLayout->setContentsMargins(8, 6, 8, 6);
	filterRowLayout->addWidget(filterEdit);

	auto* panel = new RecentRepositoriesPanel{ _repo->path() };
	connect(filterEdit, &QLineEdit::textChanged, panel, &RecentRepositoriesPanel::setFilter);

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
	// A write in flight would be orphaned: nothing cancels a Vcs::Job, and its process outlives the window
	if (writeInFlight())
	{
		event->ignore();
		if (_pushInFlight)
			_pushLogPane->show(); // may have been hidden mid-push; it shows how far the push has got

		MessageBox::notice(this, tr("Cannot close yet"), _pushInFlight
			? tr("A push is running. Wait for it to finish, then close the window.")
			: tr("An operation that changes the repository is running. Wait for it to finish, then close the window."), {});
		return;
	}

	CSettings{}.setValue(Settings::CommitWindowSplitterKey, _splitter->saveState());
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

	if (_initialWidthPending)
	{
		_initialWidthPending = false;
		delayIfNecessary(this, [this] {
			applyDefaultWindowSize();
		});
	}

	restoreSelectionByPath(selection);
	// Even if the current row did not move: the list may have emptied, or the content changed underneath
	showDiffForCurrentRow();

	if (!_stateWasRead && state.known())
	{
		_stateWasRead = true;
		restoreDraftIfParentUnchanged();
	}

	// Cancelled, or refreshes in quick succession would leave the pool built by whichever finished last
	_wordPoolQuery.cancel();
	_wordPoolQuery = _repo->diffAllChanges(this, [this](std::expected<QByteArray, QString> diff) {
		_messageEdit->setCompletionWords(completionWordsFor(_repo->files(), std::move(diff).value_or(QByteArray{})));
	});
}

void CommitWindow::applyDefaultWindowSize()
{
	if (!isVisible())
		return; // the widths below are the laid-out ones, which show() establishes; a closed window has none

	const QWidget* leftPane = _splitter->widget(0);
	const int preferred = std::min(leftPane->sizeHint().width(), MaxInitialLeftPaneWidth);
	const int available = _splitter->width() - _splitter->handleWidth();
	const QRect screenArea = screen()->availableGeometry();
	// What has to fit the screen is the frame, while resize() takes the client width; both grow by the same amount
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
		: unpushedTooltip);

	const QString parentSubject = subjectOrPlaceholder(state.headSubject);
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
	_readFailureStrip->setText(readFailureText);
	_readFailureStrip->setVisible(!readFailureText.isEmpty());

	QString opText;
	switch (state.op)
	{
	case RepoOp::Merge:      opText = tr("Merge in progress: all changes must be committed together."); break;
	case RepoOp::CherryPick: opText = tr("Cherry-pick in progress: all changes must be committed together."); break;
	case RepoOp::Revert:     opText = tr("Revert in progress: all changes must be committed together."); break;
	case RepoOp::Rebase:     opText = tr("Rebase in progress: committing is disabled, since GoodGit cannot continue "
									     "a rebase. Finish it from a command line, or abort it here."); break;
	case RepoOp::Bisect:     opText = tr("Bisect in progress: committing is disabled until it ends, since a new commit "
									     "would fall outside the search range."); break;
	case RepoOp::None:       break;
	}
	// A row reads Conflicted only while an operation is in progress, so this always extends the line above
	if (const qsizetype unresolved = _filesModel.unresolvedConflictPaths().size(); unresolved > 0)
		opText += tr(" %1 file(s) are still conflicted and must be marked resolved first.").arg(unresolved);
	_opStrip->setText(opText);
	_opStrip->setVisible(!opText.isEmpty());

	QString detachedText;
	// A bisect and a rebase detach HEAD as a matter of course; the op strip explains, and no commit will reattach
	if (state.detached && !state.opBlocksCommit())
	{
		if (state.localBranchesAtHead.size() == 1)
			detachedText = tr("Not on a branch. '%1' points here and will be checked out when you commit.").arg(state.localBranchesAtHead.front());
		else if (state.localBranchesAtHead.size() > 1)
			detachedText = tr("Not on a branch. Several branches point here; you will be asked which one to check out when you commit.");
		else if (!state.remoteBranchesAtHead.isEmpty())
			detachedText = tr("Not on a branch. HEAD matches %1; a local branch tracking it will be created when you commit.").arg(state.remoteBranchesAtHead.front());
		else
			detachedText = tr("Not on a branch, and no branch points at this commit. Committing is blocked - check out a branch first.");
	}
	_detachedStrip->setText(detachedText);
	_detachedStrip->setVisible(!detachedText.isEmpty());
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

	const bool detachedAndStuck = state.detached && state.localBranchesAtHead.isEmpty() && state.remoteBranchesAtHead.isEmpty();
	const bool canCommit = checkedCount > 0 && !_messageEdit->toPlainText().trimmed().isEmpty()
		&& !detachedAndStuck && !state.opBlocksCommit() && canActOnList();
	_commitButton->setEnabled(canCommit);
	_commitPushButton->setEnabled(canCommit && !_pushInFlight);
	_commitButton->setText(state.mergeCommitRequired() ? tr("Commit (%1 files)").arg(checkedCount)
		: tr("Commit %1 file(s)").arg(checkedCount));

	_pushButton->setEnabled(!writeInFlight());
	// A gone upstream leaves Peek's HEAD..@{upstream} nothing to resolve against
	_peekButton->setEnabled(!state.upstream.isEmpty() && !state.upstreamGone && !_peekInFlight);
	// undoLastCommit() reports every refusal, so only a write in flight disables this; a push counts as one,
	// since its success invalidates the pre-push AlreadyPushed answer
	_uncommitAction->setEnabled(canActOnList() && !_pushInFlight);
	_abortAction->setEnabled(state.operationInProgress() && canActOnList() && !_pushInFlight);
}

QString CommitWindow::subjectOrPlaceholder(const QString& subject)
{
	return subject.isEmpty() ? tr("<no commit title>") : subject;
}

void CommitWindow::saveDraftMessage()
{
	if (!_stateWasRead)
		return; // no parent sha was ever read here: an unreadable repository must not drop the stored draft

	CSettings settings;
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

	CSettings settings;
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
	return !_mutationInFlight && _repo->state().known();
}

bool CommitWindow::writeInFlight() const
{
	return _mutationInFlight || _pushInFlight;
}

void CommitWindow::beginMutation()
{
	assert(!_mutationInFlight);
	_mutationInFlight = true;
	updateControlStates();
}

void CommitWindow::endMutation()
{
	_mutationInFlight = false;
	updateControlStates();
}

Vcs::Answer<void> CommitWindow::mutationDone(const QString& errorTitle, bool changesHistory)
{
	return [this, errorTitle, changesHistory](std::expected<void, QString> result) {
		endMutation();
		if (!result)
			showError(errorTitle, result.error());
		else if (changesHistory)
			emit historyChanged();
		_repo->refresh();
	};
}

CommitWindow::StateStamp CommitWindow::stateStamp() const
{
	return { _repo->refreshGeneration(), _repo->probeOperation() };
}

bool CommitWindow::stateMovedSince(const StateStamp& stamp)
{
	if (_repo->refreshGeneration() == stamp.refreshGeneration && _repo->probeOperation() == stamp.probedOp)
		return false;

	MessageBox::notice(this, tr("Repository changed"),
		tr("The repository changed while the dialog was open, so nothing was done.\n"
		   "Review the new state and retry."), {});
	_repo->refresh();
	return true;
}

void CommitWindow::startCommit(bool pushAfterwards)
{
	if (_mutationInFlight)
		return;

	// doCommit re-checks this stamp: the reattach and confirm dialogs below outlive the state decided against
	const StateStamp stamp = stateStamp();

	// A merge commit takes every tracked change, so an unresolved row would go in with its conflict markers
	const QStringList unresolved = _filesModel.unresolvedConflictPaths();
	if (!unresolved.isEmpty())
	{
		MessageBox::notice(this, tr("Unresolved conflicts"),
			tr("%1 file(s) still have conflicts. Edit each one, then mark it resolved from the file list's "
			   "context menu:\n\n%2").arg(unresolved.size()).arg(listedPaths(unresolved)), {});
		return;
	}

	beginMutation();
	if (!_repo->state().detached)
	{
		confirmUntrackedThenCommit(pushAfterwards, stamp);
		return;
	}
	reattachHead([this, pushAfterwards, stamp](bool reattached) {
		if (reattached)
			confirmUntrackedThenCommit(pushAfterwards, stamp);
		else
			endMutation();
	});
}

void CommitWindow::confirmUntrackedThenCommit(bool pushAfterwards, StateStamp decisionStamp)
{
	const QStringList untracked = _filesModel.checkedUntrackedPaths();
	if (!untracked.isEmpty())
	{
		const auto answer = MessageBox::question(this, tr("Start tracking new files?"),
			tr("%1 checked file(s) are not tracked yet. Committing will add them to the repository:\n\n%2")
				.arg(untracked.size()).arg(listedPaths(untracked)),
			{ tr("Track and commit") });
		if (answer != 0)
		{
			endMutation();
			return;
		}
	}
	doCommit(pushAfterwards, decisionStamp);
}

// Reattachment rule (doc/ARCHITECTURE.md): only ever attach to a branch whose tip is exactly HEAD, so the
// working tree never moves. Anything else refuses.
void CommitWindow::reattachHead(std::function<void(bool reattached)> onDone)
{
	// `state` is a reference into the live Repository; every branch below that passed through a dialog or an
	// asynchronous query re-checks the stamp before acting on what was read from it
	const StateStamp stamp = stateStamp();
	const RepoState& state = _repo->state();

	const auto checkoutAndGo = [this, onDone](const QString& branch) {
		_repo->checkoutBranch(branch, [this, onDone](std::expected<void, QString> result) {
			if (!result)
				showError(tr("Failed to check out the branch"), result.error());
			onDone(result.has_value());
		});
	};

	if (state.localBranchesAtHead.size() == 1)
	{
		checkoutAndGo(state.localBranchesAtHead.front());
		return;
	}

	if (state.localBranchesAtHead.size() > 1)
	{
		const auto answer = MessageBox::question(this, tr("Not on a branch"),
			tr("Several branches point at the current commit. Which one should be checked out for this commit?"),
			state.localBranchesAtHead);
		if (answer && !stateMovedSince(stamp))
			checkoutAndGo(state.localBranchesAtHead[*answer]);
		else
			onDone(false);
		return;
	}

	if (!state.remoteBranchesAtHead.isEmpty())
	{
		QString remoteBranch = state.remoteBranchesAtHead.front();
		if (state.remoteBranchesAtHead.size() > 1)
		{
			const auto answer = MessageBox::question(this, tr("Not on a branch"),
				tr("HEAD matches several remote branches. Which one should the new local branch track?"),
				state.remoteBranchesAtHead);
			if (!answer || stateMovedSince(stamp))
			{
				onDone(false);
				return;
			}
			remoteBranch = state.remoteBranchesAtHead[*answer];
		}

		const QString localName = remoteBranch.mid(remoteBranch.indexOf(QLatin1Char('/')) + 1);
		_repo->localBranchExists(localName, this, [this, stamp, localName, remoteBranch, onDone](bool exists) {
			if (exists)
			{
				// Checking it out would move the working tree
				MessageBox::notice(this, tr("Cannot reattach"),
					tr("HEAD matches %1, but the local branch '%2' already exists and points elsewhere.\n"
					   "Committing is blocked - resolve the branch state first.").arg(remoteBranch, localName), {});
				onDone(false);
				return;
			}
			if (stateMovedSince(stamp))
			{
				onDone(false);
				return;
			}
			_repo->createTrackingBranch(localName, remoteBranch, [this, onDone](std::expected<void, QString> result) {
				if (!result)
					showError(tr("Failed to create the branch"), result.error());
				onDone(result.has_value());
			});
		});
		return;
	}

	MessageBox::notice(this, tr("Cannot commit"),
		tr("Not on a branch, and no branch points at this commit.\n"
		   "A commit made here could not be pushed. Check out a branch first."), {});
	onDone(false);
}

void CommitWindow::doCommit(bool pushAfterwards, StateStamp decisionStamp)
{
	// The last check before the write: the whole chain from startCommit - dialogs and the asynchronous
	// reattach - acted on the stamped state, and the commit mode below is chosen from it
	if (stateMovedSince(decisionStamp))
	{
		endMutation();
		return;
	}

	const QString message = _messageEdit->toPlainText();
	const QStringList pathspec = _filesModel.checkedPathspec();
	const QStringList untracked = _filesModel.checkedUntrackedPaths();
	assert(!message.trimmed().isEmpty() && !pathspec.isEmpty());
	assert(_mutationInFlight); // held by startCommit across the reattach and the dialogs

	const auto onDone = [this, message, pushAfterwards](std::expected<void, QString> result) {
		endMutation();
		if (!result)
		{
			showError(tr("Commit failed"), result.error());
			_repo->refresh();
			return;
		}
		if (_messageEdit->toPlainText() == message) // text typed while the commit ran stays put
			_messageEdit->clear();
		emit historyChanged();
		_repo->refresh();
		if (pushAfterwards)
			startPush();
	};

	if (_repo->state().mergeCommitRequired())
		_repo->commitMergeState(message, untracked, onDone);
	else
		_repo->commit(message, pathspec, untracked, onDone);
}

void CommitWindow::startPush()
{
	assert(!_pushInFlight); // doCommit reaches this too, but Commit & Push is off while a push runs
	_pushInFlight = true;
	updateControlStates();
	_pushLogView->clearLog();

	_repo->planPush([this](std::expected<std::vector<PushStep>, QString> steps) {
		if (!steps)
		{
			_pushInFlight = false;
			updateControlStates();
			showError(tr("Cannot push"), steps.error());
			return;
		}

		_pushSteps = std::move(*steps);
		runPushStep(0, /*setUpstream=*/false);
	});
}

void CommitWindow::runPushStep(size_t index, bool setUpstream)
{
	assert(index < _pushSteps.size());
	const PushStep& step = _pushSteps[index];

	_pushLogPane->show(); // before the entry: the log's scrolling needs a laid-out viewport
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
			_pushInFlight = false;
			updateControlStates();
			_repo->refresh();
			emit pushed();
			return;
		}

		const bool upstreamOffered = !setUpstream && QString::fromUtf8(result.err).contains(QLatin1String("no upstream"));
		if (upstreamOffered && offerUpstreamThenRetry(index))
			return;

		_pushInFlight = false;
		updateControlStates();
		if (!upstreamOffered) // a declined offer already named this failure
			showError(tr("Push failed"), result.errorText());
	};

	Vcs::Job* job = _repo->runPushStep(step, setUpstream, onDone);
	job->streamTo([this](const QByteArray& chunk) { _pushLogView->appendOutput(chunk); });
}

bool CommitWindow::offerUpstreamThenRetry(size_t index)
{
	const PushStep& step = _pushSteps[index];
	const QString upstream = QStringLiteral("origin/") + step.branch;
	const QString text = step.subject.isEmpty()
		? tr("The branch '%1' has no upstream configured. Push it to '%2' and set the upstream?").arg(step.branch, upstream)
		: tr("Submodule '%1' is on branch '%2', which has no upstream configured. Push it to '%3' and set the upstream?")
			.arg(step.subject, step.branch, upstream);

	if (MessageBox::question(this, tr("No upstream branch"), text, { tr("Push and set upstream") }) != 0)
		return false;

	runPushStep(index, /*setUpstream=*/true);
	return true;
}

void CommitWindow::peekIncoming()
{
	_peekInFlight = true;
	updateControlStates();

	_repo->fetch([this](std::expected<void, QString> fetchResult) {
		if (!fetchResult)
		{
			_peekInFlight = false;
			updateControlStates();
			showError(tr("Fetch failed"), fetchResult.error());
			return;
		}

		// _peekInFlight is held to the answer: a second peek would otherwise race this one and re-show a
		// dismissed popup
		_repo->incomingCommits(MaxIncomingCommits, this, [this](std::expected<std::vector<CommitRecord>, QString> commits) {
			_peekInFlight = false;
			updateControlStates();
			// After the answer: hg's behind count comes from it, and the fetch moved git's remote-tracking
			// refs - the header counts are stale until this refresh either way
			_repo->refresh();
			if (!commits)
			{
				showError(tr("Could not list the incoming commits"), commits.error());
				return;
			}
			showIncomingCommits(*commits, int(commits->size()) >= MaxIncomingCommits);
		});
	});
}

void CommitWindow::showIncomingCommits(const std::vector<CommitRecord>& commits, bool capped)
{
	if (!_incomingPopup)
	{
		_incomingPopup = new QFrame(this, Qt::Popup);
		_incomingPopup->setFrameShape(QFrame::StyledPanel);
		auto* popupLayout = new QVBoxLayout(_incomingPopup);
		popupLayout->setContentsMargins(8, 8, 8, 8);
		popupLayout->setSpacing(6);
		_incomingHeaderLabel = new QLabel;
		_incomingView = new QPlainTextEdit;
		_incomingView->setReadOnly(true);
		_incomingView->setFont(monospaceFont());
		_incomingView->setLineWrapMode(QPlainTextEdit::NoWrap);
		popupLayout->addWidget(_incomingHeaderLabel);
		popupLayout->addWidget(_incomingView, 1);
	}

	const QString upstream = _repo->state().upstream;
	_incomingHeaderLabel->setText(commits.empty()
		? tr("Nothing to pull from %1").arg(upstream)
		: tr("%1 commit(s) to pull from %2").arg(int(commits.size())).arg(upstream));

	QStringList lines;
	lines.reserve(qsizetype(commits.size()) + 1);
	for (const CommitRecord& commit : commits)
		lines.push_back(shortSha(commit.sha) + QStringLiteral("  ") + commit.subject());
	if (capped)
		lines.push_back(tr("... and more; only the newest %1 are listed").arg(MaxIncomingCommits));
	_incomingView->setPlainText(lines.join(QLatin1Char('\n')));

	// With nothing to list the popup shrinks to its header line
	_incomingView->setVisible(!commits.empty());
	if (commits.empty())
		_incomingPopup->adjustSize();
	else
		_incomingPopup->resize(IncomingPopupWidth, IncomingPopupHeight);

	WidgetUtils::placeUnder(_incomingPopup, _peekButton);
	_incomingPopup->show();
}

void CommitWindow::closePushLogEntry(const ProcessResult& result)
{
	// The output was streamed into the log as it ran; only the verdict is left to add
	if (result.ok)
		_pushLogView->appendNote(tr("Succeeded"), ConsoleLogView::NoteKind::Success);
	else if (result.outcome == ProcessOutcome::Exited)
		_pushLogView->appendNote(tr("Process failed with exit code %1").arg(result.exitCode), ConsoleLogView::NoteKind::Failure);
	else // no exit code: never started, or died without exiting
		_pushLogView->appendNote(result.errorText(), ConsoleLogView::NoteKind::Failure);
}

void CommitWindow::showDiffForCurrentRow()
{
	_diffQuery.cancel();

	const std::optional<FileEntry> current = currentEntry();
	if (!current)
	{
		_diffPane->showMessage({}, {}, {});
		return;
	}

	const FileEntry& entry = *current;

	if (entry.isSubmodule)
	{
		if (!entry.pointerMoved)
		{
			_diffPane->showMessage(entry.path, tr("submodule"), tr("The submodule pointer has not moved, but there are uncommitted changes inside the submodule.\nDouble-click to open it."));
			return;
		}
		_diffPane->showMessage(entry.path, tr("new commits"), tr("Loading..."));
		_diffQuery = _repo->submodulePointerLog(entry.path, this, [this, entry](std::expected<QString, QString> log) {
			_diffPane->showMessage(entry.path, tr("new commits"), log ? tr("New commits in the submodule:\n\n") + *log : log.error());
		});
		return;
	}

	if (entry.type == ChangeType::Untracked)
	{
		showFileContents(entry);
		return;
	}

	const QString tag = tr("HEAD %1 working tree").arg(QChar(0x2192));
	_diffPane->showMessage(entry.path, tag, tr("Loading..."));
	_diffQuery = _repo->diffFile(entry, this, [this, entry, tag](std::expected<QByteArray, QString> diff) {
		if (!diff)
			_diffPane->showMessage(entry.path, tag, diff.error());
		else if (diff->size() > CSettings{}.value(Settings::MaxShownDiffBytesKey, Settings::MaxShownDiffBytesDefault).toLongLong())
			_diffPane->showMessage(entry.path, tag, tr("The diff is too large to display (%1 MB).").arg(double(diff->size()) / (1024 * 1024), 0, 'f', 1));
		else if (diff->isEmpty())
			_diffPane->showMessage(entry.path, tag, tr("No content changes (only the mode or the line endings differ, or the file matches HEAD)."));
		else
			_diffPane->showDiff(entry.path, tag, QString::fromUtf8(*diff));
	});
}

void CommitWindow::showFileContents(const FileEntry& entry)
{
	const QString tag = tr("new file");
	QFile file{ absolutePath(entry) };
	if (file.size() > CSettings{}.value(Settings::MaxShownDiffBytesKey, Settings::MaxShownDiffBytesDefault).toLongLong())
	{
		_diffPane->showMessage(entry.path, tag, tr("The file is too large to display (%1 MB).").arg(double(file.size()) / (1024 * 1024), 0, 'f', 1));
		return;
	}
	if (!file.open(QIODevice::ReadOnly))
	{
		_diffPane->showMessage(entry.path, tag, tr("Could not read '%1'.").arg(QDir::toNativeSeparators(file.fileName())));
		return;
	}
	const QByteArray contents = file.readAll();
	if (contents.isEmpty())
	{
		_diffPane->showMessage(entry.path, tag, tr("The file is empty."));
		return;
	}
	const std::optional<QString> text = decodedAsText(contents);
	if (!text)
	{
		_diffPane->showMessage(entry.path, tag, tr("Binary file (%1 bytes).").arg(contents.size()));
		return;
	}

	_diffPane->showFileText(entry.path, tag, *text);
}

void CommitWindow::onRowActivated(const QModelIndex& sourceIndex)
{
	if (!sourceIndex.isValid())
		return;
	const FileEntry entry = _filesModel.entryAt(sourceIndex.row());

	if (entry.isSubmodule)
	{
		openSubmoduleWindow(entry);
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
	if (!_historyWindow)
	{
		_historyWindow = new HistoryWindow(_repo->location(), this);
		connect(this, &CommitWindow::historyChanged, _historyWindow, &HistoryWindow::reload);
		connect(this, &CommitWindow::pushed, _historyWindow, &HistoryWindow::refreshUnpushedMarks);
	}
	_historyWindow->show();
	_historyWindow->raise();
	_historyWindow->activateWindow();
}

void CommitWindow::abortOperation()
{
	if (writeInFlight())
		return;

	const RepoOp op = _repo->state().op;
	assert(op != RepoOp::None); // the action is disabled without one

	const QString title = [op] {
		switch (op)
		{
		case RepoOp::Merge:      return tr("Abort the merge?");
		case RepoOp::CherryPick: return tr("Abort the cherry-pick?");
		case RepoOp::Revert:     return tr("Abort the revert?");
		case RepoOp::Rebase:     return tr("Abort the rebase?");
		case RepoOp::Bisect:     return tr("End the bisect?");
		case RepoOp::None:       break;
		}
		return QString{};
	}();

	// Ending a bisect loses nothing, but only git returns to the pre-bisect branch: hg's reset leaves the
	// working directory where the bisect left it
	const bool bisect = op == RepoOp::Bisect;
	const QString text = bisect
		? (_repo->kind() == VcsKind::Git
			? tr("The bisect session ends, and the branch that was checked out before it started is checked out again.")
			: tr("The bisect session ends. The working directory stays on the revision the bisect left it at."))
		: tr("The repository goes back to where it was before the operation started, and every conflict "
			 "resolution goes with it.\n\nA change that was already uncommitted when the operation began may "
			 "not survive either.");

	const StateStamp stamp = stateStamp();
	const auto answer = MessageBox::question(this, title, text, { bisect ? tr("End bisect") : tr("Abort") });
	if (answer != 0 || stateMovedSince(stamp))
		return;

	beginMutation();
	_repo->abortOperation(mutationDone(tr("Abort failed"), /*changesHistory=*/true));
}

void CommitWindow::showPreferencesDialog()
{
	CSettingsDialog dialog{ this };
	dialog.addSettingsPage(new MainSettingsPage{ &dialog }, tr("Main"))
		.addSettingsPage(new ThemeFontSettingsPage{ &dialog }, tr("Theme & Font")); // a QListWidgetItem shows text verbatim, no mnemonic escaping
	dialog.exec();
}

void CommitWindow::openSubmoduleWindow(const FileEntry& entry)
{
	CommitWindow* window = openRepositoryWindow(_repo->submoduleLocation(entry.path));
	// The window may already be open, and so already connected
	connect(window, &CommitWindow::historyChanged, this, &CommitWindow::refreshRepository, Qt::UniqueConnection);
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
	bool anyUntracked = false, anyAdded = false, anyDeletable = false, anyDiscardable = false, anyConflicted = false;
	for (const FileEntry& entry : entries)
	{
		anyDiscardable |= discardable(entry);
		if (entry.isSubmodule)
			continue;
		anyUntracked |= entry.type == ChangeType::Untracked;
		anyAdded |= entry.type == ChangeType::Added;
		anyConflicted |= entry.type == ChangeType::Conflicted;
		anyDeletable |= entry.type != ChangeType::Deleted;
	}
	const bool singleFile = entries.size() == 1 && !entries.front().isSubmodule && entries.front().type != ChangeType::Deleted;

	// An action this selection can never reach is hidden; one only the repository's state blocks is disabled.
	QMenu menu{ this };

	QAction* markResolvedAction = menu.addAction(tr("Mark resolved"), this, &CommitWindow::markResolvedSelection);
	markResolvedAction->setVisible(anyConflicted);
	markResolvedAction->setEnabled(canAct);
	QAction* addAction = menu.addAction(tr("Add"), this, &CommitWindow::addSelectionToIndex);
	addAction->setVisible(anyUntracked);
	addAction->setEnabled(canAct);

	QAction* unAddAction = menu.addAction(tr("Un-add"), this, &CommitWindow::unAddSelection);
	unAddAction->setVisible(anyAdded);
	unAddAction->setEnabled(canAct);

	const bool singleUntracked = entries.size() == 1 && !entries.front().isSubmodule && entries.front().type == ChangeType::Untracked;
	QMenu* ignoreMenu = menu.addMenu(tr("Add to %1").arg(_repo->ignoreFileName()));
	ignoreMenu->menuAction()->setVisible(singleUntracked);
	ignoreMenu->setEnabled(canAct); // the pattern comes from the row, so it is as stale as the row
	if (singleUntracked)
	{
		for (const IgnorePattern& pattern : _repo->ignorePatternsFor(entries.front().path))
		{
			// '&' doubled for display, or QMenu takes it as an accelerator marker
			ignoreMenu->addAction(QString{ pattern.text }.replace(QLatin1Char('&'), QStringLiteral("&&")),
				this, [this, pattern] { addPatternToIgnoreFile(pattern); });
		}
	}

	menu.addSeparator();

	QAction* openAction = menu.addAction(tr("Open"), this, [this, entry = entries.front()] {
		openEntryExternally(entry);
	});
	openAction->setVisible(singleFile);

	QAction* editAction = menu.addAction(tr("Edit"), this, [this, entry = entries.front()] {
		openInTextEditor(absolutePath(entry), this);
	});
	editAction->setVisible(singleFile);

	QAction* submoduleHistoryAction = menu.addAction(tr("View commit history"), this, [this, entry = entries.front()] {
		// Not deduplicated like this repo's own history window, matching openSubmoduleWindow
		auto* window = new HistoryWindow(_repo->submoduleLocation(entry.path), this);
		window->show();
	});
	submoduleHistoryAction->setVisible(entries.size() == 1 && entries.front().isSubmodule);

	QAction* fileHistoryAction = menu.addAction(tr("View file history"), this, [this, entry = entries.front()] {
		// Nothing is committed at a rename's new path yet
		const QString& path = entry.oldPath.isEmpty() ? entry.path : entry.oldPath;
		auto* window = new HistoryWindow(_repo->location(), path, this);
		window->show();
	});
	// A submodule's history is its own repo's, offered above; an untracked or newly added file is in no commit
	fileHistoryAction->setVisible(entries.size() == 1 && !entries.front().isSubmodule
		&& entries.front().type != ChangeType::Untracked && entries.front().type != ChangeType::Added);

	QAction* showInFileManagerAction = menu.addAction(showInFileManagerActionText(), this, [this, entry = entries.front()] {
		showInFileManager(absolutePath(entry));
	});

	showInFileManagerAction->setVisible(singleFile); // a deleted path has nothing to reveal
	// Only the full path is nativized: the relative one is pasted into an ignore file, a message or a command,
	// which take forward slashes.
	const auto copyPaths = [this, entries](bool full) {
		QStringList paths;
		for (const FileEntry& entry : entries)
			paths.push_back(full ? QDir::toNativeSeparators(absolutePath(entry)) : entry.path);
		QApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
	};

	menu.addAction(tr("Copy relative path"), this, [copyPaths] { copyPaths(false); });
	menu.addAction(tr("Copy full path"), this, [copyPaths] { copyPaths(true); });
	menu.addSeparator();

	QAction* deleteAction = menu.addAction(tr("Delete to Recycle Bin"), this, &CommitWindow::deleteSelection);
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
	const bool contentOfOneSubmodule = entries.size() == 1 && contentDiscardable(entries.front());
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
	for (const FileEntry& entry : selectedEntries())
	{
		if (entry.isSubmodule || entry.type == ChangeType::Deleted)
			continue;
		if (entry.type == ChangeType::Untracked)
			untrackedPaths.push_back(entry.path);
		else if (entry.type == ChangeType::Added)
			addedPaths.push_back(entry.path);
		else
			trackedPaths.push_back(entry.path);
	}
	if (untrackedPaths.isEmpty() && addedPaths.isEmpty() && trackedPaths.isEmpty())
		return;

	// A single untracked file goes to the Recycle Bin unprompted, as it would from a file manager.
	// Anything more asks first, and the dialog names the untracked files too: they are deleted with the rest.
	if (!trackedPaths.isEmpty() || !addedPaths.isEmpty() || untrackedPaths.size() > 1)
	{
		const QStringList prompted = trackedPaths + addedPaths + untrackedPaths;
		const auto answer = MessageBox::question(this, tr("Delete files?"),
			tr("Move %1 file(s) to the Recycle Bin?\n\n%2").arg(prompted.size()).arg(listedPaths(prompted)),
			{ tr("Delete") });
		if (answer != 0)
			return;
	}

	const auto trashAll = [this](const QStringList& paths) {
		for (const QString& path : paths)
		{
			if (QFile::moveToTrash(QDir(_repo->path()).filePath(path)))
				continue;
			// Never fall back to a permanent delete
			MessageBox::notice(this, tr("Delete failed"),
				tr("Could not move '%1' to the Recycle Bin (the file may be locked, or the volume has no Recycle Bin).\n"
				   "The remaining files were not deleted.").arg(path), {});
			return;
		}
	};

	if (!addedPaths.isEmpty())
	{
		// Un-add first, or the index would point at a file that no longer exists
		beginMutation();
		_repo->unAdd(addedPaths, [this, paths = untrackedPaths + trackedPaths + addedPaths, trashAll](std::expected<void, QString> result) {
			endMutation();
			if (!result)
			{
				showError(tr("Failed to un-add before deleting"), result.error());
				return;
			}
			trashAll(paths);
			_repo->refresh();
		});
		return;
	}

	trashAll(untrackedPaths + trackedPaths);
	_repo->refresh();
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

		const StateStamp stamp = stateStamp();
		const auto answer = MessageBox::question(this, tr("Discard changes?"), text, { tr("Discard") });
		if (answer != 0 || stateMovedSince(stamp))
			return;
	}

	const auto unAddThenRefresh = [this, addedPaths] {
		if (addedPaths.isEmpty())
		{
			endMutation();
			_repo->refresh();
			return;
		}
		_repo->unAdd(addedPaths, mutationDone(tr("Un-add failed")));
	};

	beginMutation();
	if (pathspec.isEmpty())
	{
		unAddThenRefresh();
		return;
	}

	_repo->discardChanges(pathspec, [this, unAddThenRefresh](std::expected<void, QString> result) {
		if (!result)
		{
			endMutation();
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
	const StateStamp stamp = stateStamp();
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

			if (MessageBox::question(this, tr("Discard changes?"), text, { tr("Discard") }) != 0)
				return;
		}
		if (stateMovedSince(stamp))
			return;

		startSubmoduleContentDiscard(path, plan);
	});
}

void CommitWindow::startSubmoduleContentDiscard(const QString& path, const SubmoduleDiscardPlan& plan)
{
	beginMutation();
	_repo->discardSubmoduleContent(path, plan, [this, path](std::expected<void, QString> result) {
		endMutation();
		if (!result)
			showError(tr("Discard failed"), result.error());
		_repo->refresh();
		if (CommitWindow* window = repositoryWindow(_repo->submoduleLocation(path).root))
			window->refreshRepository(); // its rows are stale: its working tree was changed from here
	});
}

void CommitWindow::undoLastCommit()
{
	// A push in flight blocks this too: the refusal below is computed from pre-push state, and undoing the
	// commit a running push is publishing is the rewrite AlreadyPushed exists to prevent
	if (writeInFlight())
		return;

	const RepoState& state = _repo->state();
	const UndoRefusal refusal = state.lastCommitUndoRefusal();
	if (refusal != UndoRefusal::None)
	{
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
		MessageBox::notice(this, tr("Cannot undo the last commit"), reason, {}, QMessageBox::Information);
		return;
	}

	const StateStamp stamp = stateStamp();
	const auto answer = MessageBox::question(this, tr("Undo the last commit?"),
		tr("'%1' will be undone. Its changes return to this list as uncommitted changes; the working tree "
			"is not modified.").arg(subjectOrPlaceholder(state.headSubject)),
		{ tr("Undo commit") });
	if (answer != 0 || stateMovedSince(stamp))
		return;

	beginMutation();
	_repo->undoLastCommit(mutationDone(tr("Undo failed"), /*changesHistory=*/true));
}

void CommitWindow::addSelectionToIndex()
{
	QStringList paths;
	for (const FileEntry& entry : selectedEntries())
	{
		if (!entry.isSubmodule && entry.type == ChangeType::Untracked)
			paths.push_back(entry.path); // tracked rows in a mixed selection are a no-op
	}
	if (paths.isEmpty())
		return;
	beginMutation();
	_repo->addToIndex(paths, mutationDone(tr("Add failed")));
}

void CommitWindow::markResolvedSelection()
{
	QStringList paths;
	for (const FileEntry& entry : selectedEntries())
	{
		if (entry.type == ChangeType::Conflicted)
			paths.push_back(entry.path);
	}
	if (paths.isEmpty())
		return;
	beginMutation();
	_repo->markResolved(paths, mutationDone(tr("Failed to mark resolved")));
}

void CommitWindow::unAddSelection()
{
	QStringList paths;
	for (const FileEntry& entry : selectedEntries())
	{
		if (!entry.isSubmodule && entry.type == ChangeType::Added)
			paths.push_back(entry.path);
	}
	if (paths.isEmpty())
		return;
	beginMutation();
	_repo->unAdd(paths, mutationDone(tr("Un-add failed")));
}

void CommitWindow::addPatternToIgnoreFile(const IgnorePattern& pattern)
{
	QFile file{ QDir{ _repo->path() }.filePath(_repo->ignoreFileName()) };
	if (!file.open(QIODevice::ReadWrite)) // read too: the backend places the pattern by the existing content
	{
		MessageBox::notice(this, tr("Failed to update %1").arg(_repo->ignoreFileName()),
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
	MessageBox::notice(this, title, title + QLatin1Char('.'), details);
}

QString CommitWindow::absolutePath(const FileEntry& entry) const
{
	return QDir{ _repo->path() }.filePath(entry.path);
}
