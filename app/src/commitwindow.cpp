#include "commitwindow.h"
#include "consolelogview.h"
#include "diffhighlighter.h"
#include "filelistdelegate.h"
#include "historymodels.h"
#include "historywindow.h"
#include "messageedit.h"
#include "settings.h"
#include "theme.h"

#include "dialogs/messagebox.h"
#include "widgets/clabelmidelision.h"
#include "widgets/cpersistentwindow.h"
#include "widgets/widgetutils.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QShortcut>
#include <QSplitter>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <assert.h>

namespace {

constexpr int LeftColumnWidth = 430; // sized so the 50-column subject guide fits the message editor
constexpr qsizetype MaxDiffBytes = 2 * 1024 * 1024;
constexpr int MaxListedPathsInDialog = 20;
constexpr int MaxIncomingCommits = 200; // a peek, not a history window - that is what History is for
constexpr int IncomingPopupWidth = 560;
constexpr int IncomingPopupHeight = 320;

// Ignore pattern candidates for one untracked file, most specific first. Literal path patterns
// are anchored to the repo root with a leading '/'.
QStringList gitIgnorePatterns(const QString& repoRelativePath)
{
	// Glob metacharacters in a file name must stay literal; '#' and '!' are special at line start
	const auto escaped = [](const QString& text) {
		QString out;
		out.reserve(text.size());
		for (const QChar c : text)
		{
			if (c == QLatin1Char('\\') || c == QLatin1Char('*') || c == QLatin1Char('?') || c == QLatin1Char('[') || c == QLatin1Char(']'))
				out += QLatin1Char('\\');
			out += c;
		}
		if (out.startsWith(QLatin1Char('#')) || out.startsWith(QLatin1Char('!')))
			out.prepend(QLatin1Char('\\'));
		return out;
	};

	const qsizetype slash = repoRelativePath.lastIndexOf(QLatin1Char('/'));
	const QString fileName = repoRelativePath.mid(slash + 1);
	const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));

	QStringList patterns;
	patterns.push_back(QLatin1Char('/') + escaped(repoRelativePath));
	if (dot > 0 && dot < fileName.size() - 1)
		patterns.push_back(QStringLiteral("*.") + escaped(fileName.mid(dot + 1)));
	patterns.push_back(escaped(fileName));
	if (slash >= 0)
		patterns.push_back(QLatin1Char('/') + escaped(repoRelativePath.left(slash)) + QLatin1Char('/'));
	return patterns;
}

// What discarding can act on. Untracked files are not git's to restore, and a submodule with changes inside
// would be checked out over. Mid-operation nothing is: restoring a path to HEAD there would silently
// drop the merge's or rebase's result for it, conflicted or not.
bool discardable(const FileEntry& entry, bool operationInProgress)
{
	if (operationInProgress)
		return false;
	if (entry.isSubmodule)
		return entry.committable();
	return entry.type != ChangeType::Untracked;
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

// The message completion pool: every changed file's path and basename, plus identifier-shaped words
// from the changed lines themselves. Length < 4 and a small stoplist weed out prose function words;
// hex-sha-shaped tokens are dropped so submodule pointer diffs don't pollute the pool.
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

} // namespace

CommitWindow::CommitWindow(const QString& repoPath) :
	_repo{ repoPath }
{
	setAttribute(Qt::WA_DeleteOnClose);
	buildUi();

	// Per-repo geometry, so a submodule window does not inherit or clobber the parent's placement
	const QString geometryKey = QStringLiteral("CommitWindow_")
		+ QString::fromLatin1(QCryptographicHash::hash(QDir::cleanPath(repoPath).toUtf8(), QCryptographicHash::Md5).toHex());
	installEventFilter(new CPersistenceEnabler(geometryKey, this));

	connect(&_repo, &Repository::refreshed, this, &CommitWindow::onRefreshed);
	_repo.refresh();
}

void CommitWindow::buildUi()
{
	auto* leftPane = new QWidget;
	auto* leftLayout = new QVBoxLayout(leftPane);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	leftLayout->setSpacing(0);

	// Repo header row: name, branch, ahead count, then the secondary actions
	auto* repoBar = new QFrame;
	repoBar->setObjectName(QStringLiteral("repoBar"));
	auto* repoBarLayout = new QHBoxLayout(repoBar);
	repoBarLayout->setContentsMargins(8, 6, 8, 6);
	_repoNameLabel = new QLabel;
	{
		QFont bold = _repoNameLabel->font();
		bold.setBold(true);
		_repoNameLabel->setFont(bold);
	}
	_branchLabel = new QLabel;
	_branchLabel->setObjectName(QStringLiteral("branchChip"));
	_branchLabel->setFont(monospaceFont());
	_aheadLabel = new QLabel;
	_aheadLabel->setObjectName(QStringLiteral("aheadLabel"));
	_pushButton = new QPushButton(tr("Push"));
	_peekButton = new QPushButton(tr("Peek"));
	_peekButton->setToolTip(tr("Fetch, then list what the upstream has and this branch does not"));
	_refreshButton = new QPushButton(tr("Refresh"));
	_refreshButton->setToolTip(QStringLiteral("F5"));
	_historyButton = new QPushButton(tr("History"));
	repoBarLayout->addWidget(_repoNameLabel);
	repoBarLayout->addWidget(_branchLabel);
	repoBarLayout->addWidget(_aheadLabel);
	repoBarLayout->addStretch();
	repoBarLayout->addWidget(_pushButton);
	repoBarLayout->addWidget(_peekButton);
	repoBarLayout->addWidget(_refreshButton);
	repoBarLayout->addWidget(_historyButton);
	leftLayout->addWidget(repoBar);

	const auto makeStrip = [](const QColor& background, const QColor& text) {
		auto* strip = new QLabel;
		strip->setWordWrap(true);
		strip->setMargin(6);
		strip->setStyleSheet(QStringLiteral("background-color: %1; color: %2;").arg(background.name(), text.name()));
		strip->setVisible(false);
		return strip;
	};
	_opStrip = makeStrip(activeTheme().errBg, activeTheme().errFg);
	_detachedStrip = makeStrip(activeTheme().warnBg, activeTheme().warnFg);
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
	leftLayout->addWidget(counterBar);

	_filesView = new QTreeView;
	_filesView->setModel(&_filesModel);
	_filesView->setItemDelegate(new FileListDelegate{ _filesView });
	_filesView->setRootIsDecorated(false);
	_filesView->setUniformRowHeights(true);
	_filesView->setAllColumnsShowFocus(true);
	_filesView->setSelectionMode(QAbstractItemView::ExtendedSelection);
	_filesView->setSelectionBehavior(QAbstractItemView::SelectRows);
	_filesView->setContextMenuPolicy(Qt::CustomContextMenu);
	_filesView->header()->hide();
	_filesView->header()->setSectionResizeMode(ChangedFilesModel::StateColumn, QHeaderView::ResizeToContents);
	_filesView->header()->setSectionResizeMode(ChangedFilesModel::PathColumn, QHeaderView::Stretch);
	_filesView->installEventFilter(this);
	leftLayout->addWidget(_filesView, 1);

	auto* messageHeader = new QWidget;
	messageHeader->setObjectName(QStringLiteral("messageHeader"));
	auto* messageHeaderLayout = new QHBoxLayout(messageHeader);
	messageHeaderLayout->setContentsMargins(8, 6, 8, 4);
	messageHeaderLayout->addWidget(new QLabel(tr("Commit message")));
	leftLayout->addWidget(messageHeader);

	auto* messageArea = new QWidget;
	auto* messageLayout = new QVBoxLayout(messageArea);
	messageLayout->setContentsMargins(8, 0, 8, 8);
	_messageEdit = new MessageEdit;
	_messageEdit->setObjectName(QStringLiteral("messageEdit"));
	_messageEdit->setMinimumHeight(90);
	_messageEdit->setMaximumHeight(160);
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
	rightLayout->addWidget(diffHeader);

	_diffView = new QPlainTextEdit;
	_diffView->setObjectName(QStringLiteral("diffView"));
	_diffView->setReadOnly(true);
	_diffView->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	_diffView->setFont(monospaceFont());
	new DiffHighlighter(_diffView->document());
	rightLayout->addWidget(_diffView, 1);

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
	if (const QByteArray state = Settings::splitterState(QStringLiteral("CommitWindow")); !state.isEmpty())
		_splitter->restoreState(state);
	else
		_splitter->setSizes({ LeftColumnWidth, 750 });
	setCentralWidget(_splitter);
	resize(1180, 740);

	connect(_refreshButton, &QPushButton::clicked, &_repo, &Repository::refresh);
	connect(_pushButton, &QPushButton::clicked, this, [this] { doPush(/*setUpstream=*/false); });
	connect(_peekButton, &QPushButton::clicked, this, &CommitWindow::peekIncoming);
	connect(_historyButton, &QPushButton::clicked, this, &CommitWindow::showHistoryWindow);
	connect(hidePushLogButton, &QPushButton::clicked, _pushLogPane, &QWidget::hide);
	connect(_commitButton, &QPushButton::clicked, this, [this] { startCommit(false); });
	connect(_commitPushButton, &QPushButton::clicked, this, [this] { startCommit(true); });
	connect(_messageEdit, &QPlainTextEdit::textChanged, this, &CommitWindow::updateButtons);
	connect(&_filesModel, &ChangedFilesModel::checksChanged, this, &CommitWindow::updateButtons);
	connect(_checkAllBox, &QCheckBox::clicked, this, [this] {
		_filesModel.setAllChecked(_filesModel.checkedCount() < _filesModel.checkableCount());
	});
	connect(modifiedOnlyButton, &QPushButton::clicked, &_filesModel, &ChangedFilesModel::checkAllExceptUntracked);
	connect(_filesView, &QAbstractItemView::activated, this, &CommitWindow::onRowActivated);
	connect(_filesView, &QWidget::customContextMenuRequested, this, &CommitWindow::showContextMenu);
	connect(_filesView->selectionModel(), &QItemSelectionModel::currentChanged, this, &CommitWindow::showDiffForCurrentRow);

	new QShortcut(QKeySequence(Qt::Key_F5), this, [this] { _repo.refresh(); });
	new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] { close(); });
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

	updateButtons();
	_messageEdit->setFocus(); // typing the message is what the window is opened to do
}

void CommitWindow::closeEvent(QCloseEvent* event)
{
	Settings::setSplitterState(QStringLiteral("CommitWindow"), _splitter->saveState());
	QMainWindow::closeEvent(event);
}

bool CommitWindow::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == _filesView && event->type() == QEvent::KeyPress)
	{
		const auto* keyEvent = static_cast<QKeyEvent*>(event);
		if (keyEvent->key() == Qt::Key_Space)
		{
			toggleCheckOnSelection();
			return true;
		}
		if (keyEvent->key() == Qt::Key_Delete)
		{
			deleteSelection();
			return true;
		}
	}
	return QMainWindow::eventFilter(watched, event);
}

void CommitWindow::onRefreshed()
{
	const SelectionByPath selection = captureSelectionByPath();

	const RepoState& state = _repo.state();
	_filesModel.setEntries(_repo.files(), state.operationInProgress());

	updateHeader();
	updateStrips();
	updateButtons();

	restoreSelectionByPath(selection);
	// Selecting a row is not what makes this needed: an emptied list leaves nothing to make current,
	// and content that changed under a selection that did not move has to be re-read anyway
	showDiffForCurrentRow();

	_repo.diffAllChanges(this, [this](const GitResult& result) {
		_messageEdit->setCompletionWords(completionWordsFor(_repo.files(), result.ok ? result.out : QByteArray{}));
	});
}

void CommitWindow::updateHeader()
{
	const RepoState& state = _repo.state();

	_repoNameLabel->setText(_repo.name());
	const QString branchText = state.detached ? tr("detached HEAD at %1").arg(state.headSha.left(7))
		: state.unborn ? state.branch + tr(" (no commits yet)")
		: state.branch;
	_branchLabel->setText(branchText);

	if (state.upstream.isEmpty())
		_aheadLabel->setText(state.unborn || state.detached ? QString{} : tr("no upstream"));
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
	_pushButton->setToolTip(unpushedTooltip);
	_aheadLabel->setToolTip(unpushedTooltip);

	setWindowTitle(QStringLiteral("%1 [%2] - GoodGit").arg(_repo.name(), state.detached ? QStringLiteral("detached") : state.branch));
}

void CommitWindow::updateStrips()
{
	const RepoState& state = _repo.state();

	QString opText;
	switch (state.op)
	{
	case RepoOp::Merge:      opText = tr("Merge in progress: everything must be committed together."); break;
	case RepoOp::CherryPick: opText = tr("Cherry-pick in progress: everything must be committed together."); break;
	case RepoOp::Revert:     opText = tr("Revert in progress: everything must be committed together."); break;
	case RepoOp::Rebase:     opText = tr("Rebase in progress: everything must be committed together."); break;
	case RepoOp::None:       break;
	}
	_opStrip->setText(opText);
	_opStrip->setVisible(!opText.isEmpty());

	QString detachedText;
	if (state.detached)
	{
		if (state.localBranchesAtHead.size() == 1)
			detachedText = tr("Not on a branch. '%1' points here and will be checked out when you commit.").arg(state.localBranchesAtHead.front());
		else if (state.localBranchesAtHead.size() > 1)
			detachedText = tr("Not on a branch. Several branches point here - you will choose one when committing.");
		else if (!state.remoteBranchesAtHead.isEmpty())
			detachedText = tr("Not on a branch. HEAD matches %1 - a local branch will be created when you commit.").arg(state.remoteBranchesAtHead.front());
		else
			detachedText = tr("Not on a branch, and no branch points at this commit. Committing is blocked - check out a branch first.");
	}
	_detachedStrip->setText(detachedText);
	_detachedStrip->setVisible(!detachedText.isEmpty());
}

void CommitWindow::updateButtons()
{
	const RepoState& state = _repo.state();
	const int checkedCount = _filesModel.checkedCount();
	const int checkableCount = _filesModel.checkableCount();

	{
		QSignalBlocker blocker{ _checkAllBox };
		_checkAllBox->setText(tr("%1 of %2 checked").arg(checkedCount).arg(checkableCount));
		_checkAllBox->setCheckState(checkedCount == 0 ? Qt::Unchecked
			: checkedCount == checkableCount ? Qt::Checked : Qt::PartiallyChecked);
	}

	const bool detachedAndStuck = state.detached && state.localBranchesAtHead.isEmpty() && state.remoteBranchesAtHead.isEmpty();
	const bool canCommit = checkedCount > 0 && !_messageEdit->toPlainText().trimmed().isEmpty()
		&& !detachedAndStuck && !_commitInFlight;
	_commitButton->setEnabled(canCommit);
	_commitPushButton->setEnabled(canCommit);
	_commitButton->setText(state.operationInProgress() ? tr("Commit (%1 files)").arg(checkedCount)
		: tr("Commit %1 file(s)").arg(checkedCount));

	// Without an upstream there is no ref for the incoming walk to name
	_peekButton->setEnabled(!state.upstream.isEmpty() && !_peekInFlight);
}

void CommitWindow::startCommit(bool pushAfterwards)
{
	if (_commitInFlight)
		return;

	if (_repo.state().detached)
		reattachHead([this, pushAfterwards] { confirmUntrackedThenCommit(pushAfterwards); });
	else
		confirmUntrackedThenCommit(pushAfterwards);
}

void CommitWindow::confirmUntrackedThenCommit(bool pushAfterwards)
{
	const QStringList untracked = _filesModel.checkedUntrackedPaths();
	if (!untracked.isEmpty())
	{
		const auto answer = MessageBox::question(this, tr("Start tracking new files?"),
			tr("%1 checked file(s) are not tracked by git yet. Committing will add them to the repository:\n\n%2")
				.arg(untracked.size()).arg(listedPaths(untracked)),
			{ tr("Track and commit") });
		if (answer != 0)
			return;
	}
	doCommit(pushAfterwards);
}

// Reattachment rule (doc/ARCHITECTURE.md): only ever attach to a branch whose tip is exactly HEAD,
// so the working tree never moves. Anything else refuses.
void CommitWindow::reattachHead(std::function<void()> onReattached)
{
	const RepoState& state = _repo.state();

	const auto checkoutAndGo = [this, onReattached](const QString& branch) {
		_repo.checkoutBranch(branch, [this, onReattached](const GitResult& result) {
			if (result.ok)
				onReattached();
			else
				showGitError(tr("Failed to check out the branch"), result);
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
		if (answer)
			checkoutAndGo(state.localBranchesAtHead[*answer]);
		return;
	}

	if (!state.remoteBranchesAtHead.isEmpty())
	{
		QString remoteBranch = state.remoteBranchesAtHead.front();
		if (state.remoteBranchesAtHead.size() > 1)
		{
			const auto answer = MessageBox::question(this, tr("Not on a branch"),
				tr("HEAD matches several remote branches. Create a local branch tracking which one?"),
				state.remoteBranchesAtHead);
			if (!answer)
				return;
			remoteBranch = state.remoteBranchesAtHead[*answer];
		}

		const QString localName = remoteBranch.mid(remoteBranch.indexOf(QLatin1Char('/')) + 1);
		_repo.localBranchExists(localName, this, [this, localName, remoteBranch, onReattached](bool exists) {
			if (exists)
			{
				// Taking the name would move the working tree - that disqualifies it
				MessageBox::notice(this, tr("Cannot reattach"),
					tr("HEAD matches %1, but the local branch '%2' already exists and points elsewhere.\n"
					   "Committing is blocked - resolve the branch state first.").arg(remoteBranch, localName), {});
				return;
			}
			_repo.createTrackingBranch(localName, remoteBranch, [this, onReattached](const GitResult& result) {
				if (result.ok)
					onReattached();
				else
					showGitError(tr("Failed to create the branch"), result);
			});
		});
		return;
	}

	MessageBox::notice(this, tr("Cannot commit"),
		tr("Not on a branch, and no branch points at this commit.\n"
		   "A commit made here could not be pushed. Check out a branch first."), {});
}

void CommitWindow::doCommit(bool pushAfterwards)
{
	const QString message = _messageEdit->toPlainText();
	const QStringList pathspec = _filesModel.checkedPathspec();
	const QStringList untracked = _filesModel.checkedUntrackedPaths();
	assert(!message.trimmed().isEmpty() && !pathspec.isEmpty());

	_commitInFlight = true;
	updateButtons();

	const auto onDone = [this, message, pushAfterwards](const GitResult& result) {
		_commitInFlight = false;
		if (!result.ok)
		{
			showGitError(tr("Commit failed"), result);
			_repo.refresh();
			return;
		}
		_messageEdit->clear();
		emit committed();
		_repo.refresh();
		if (pushAfterwards)
			doPush(/*setUpstream=*/false);
	};

	if (_repo.state().operationInProgress())
		_repo.commitMergeState(message, untracked, onDone);
	else
		_repo.commit(message, pathspec, untracked, onDone);
}

void CommitWindow::doPush(bool setUpstream)
{
	_pushButton->setEnabled(false);
	if (!setUpstream)
		_pushLogView->clearLog(); // the set-upstream retry continues the same push - its report joins the failed attempt's

	_pushLogPane->show(); // before the entry: the log's scrolling needs a laid-out viewport
	// A label, not the literal argv: the invocation invariants, --recurse-submodules and --progress are noise there
	_pushLogView->beginEntry(setUpstream ? QStringLiteral("git push --set-upstream origin HEAD") : QStringLiteral("git push"));

	const auto onDone = [this, setUpstream](const GitResult& result) {
		_pushButton->setEnabled(true);
		closePushLogEntry(result);

		if (result.ok)
		{
			_repo.refresh();
			emit pushed();
			return;
		}
		if (!setUpstream && QString::fromUtf8(result.err).contains(QLatin1String("no upstream")))
		{
			const auto answer = MessageBox::question(this, tr("No upstream branch"),
				tr("The current branch has no upstream configured. Push it to 'origin' and set the upstream?"),
				{ tr("Push and set upstream") });
			if (answer == 0)
				doPush(/*setUpstream=*/true);
			return;
		}
		showGitError(tr("Push failed"), result);
	};

	Git::Job* job = setUpstream ? _repo.pushSetUpstream(onDone) : _repo.push(onDone);
	// Before the event loop runs again, so the first chunk is not missed
	job->streamTo([this](const QByteArray& chunk) { _pushLogView->appendOutput(chunk); });
}

void CommitWindow::peekIncoming()
{
	_peekInFlight = true;
	updateButtons();

	_repo.fetch([this](const GitResult& fetchResult) {
		_peekInFlight = false;
		updateButtons();

		if (!fetchResult.ok)
		{
			showGitError(tr("Fetch failed"), fetchResult);
			return;
		}
		_repo.refresh(); // the fetch moved the remote-tracking ref, so the header's counts are stale

		_repo.incomingCommits(MaxIncomingCommits, this, [this](const GitResult& logResult) {
			if (!logResult.ok)
			{
				showGitError(tr("Could not list the incoming commits"), logResult);
				return;
			}
			const std::vector<CommitRecord> commits = parseCommitLog(logResult.out);
			showIncomingCommits(commits, int(commits.size()) >= MaxIncomingCommits);
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

	const QString upstream = _repo.state().upstream;
	_incomingHeaderLabel->setText(commits.empty()
		? tr("Nothing to pull from %1").arg(upstream)
		: tr("%1 commit(s) waiting in %2").arg(int(commits.size())).arg(upstream));

	QStringList lines;
	lines.reserve(qsizetype(commits.size()) + 1);
	for (const CommitRecord& commit : commits)
		lines.push_back(shortSha(commit.sha) + QStringLiteral("  ") + commit.subject());
	if (capped)
		lines.push_back(tr("... and older ones, not listed"));
	_incomingView->setPlainText(lines.join(QLatin1Char('\n')));

	// Nothing to list shrinks the popup to its one line, rather than framing a lot of nothing
	_incomingView->setVisible(!commits.empty());
	if (commits.empty())
		_incomingPopup->adjustSize();
	else
		_incomingPopup->resize(IncomingPopupWidth, IncomingPopupHeight);

	WidgetUtils::placeUnder(_incomingPopup, _peekButton);
	_incomingPopup->show();
}

void CommitWindow::closePushLogEntry(const GitResult& result)
{
	// Everything the push had to say is already in the log, streamed as it ran. What is left is what the
	// process could not say for itself.
	if (result.launchFailed)
		_pushLogView->appendNote(result.errorText());
	else if (!_pushLogView->entryHasOutput())
		_pushLogView->appendNote(tr("(no output; exit code %1)").arg(result.exitCode));
}

void CommitWindow::showDiffForCurrentRow()
{
	const int generation = ++_diffGeneration;
	if (_diffJob)
		_diffJob->cancel();

	const QModelIndex current = _filesView->currentIndex();
	if (!current.isValid() || current.row() >= _filesModel.rowCount())
	{
		setDiffText({}, {}, {});
		return;
	}

	const FileEntry entry = _filesModel.entryAt(current.row());

	if (entry.isSubmodule)
	{
		if (!entry.pointerMoved)
		{
			setDiffText(entry.path, tr("submodule"), tr("The submodule pointer has not moved.\nThere are uncommitted changes inside - double-click to open the submodule."));
			return;
		}
		setDiffText(entry.path, tr("new commits"), tr("Loading..."));
		_repo.submodulePointerLog(entry, this, [this, generation, entry](const GitResult& result) {
			if (generation != _diffGeneration)
				return;
			if (!result.ok)
				setDiffText(entry.path, tr("new commits"), result.errorText());
			else
				setDiffText(entry.path, tr("new commits"), tr("Commits being pulled in:\n\n") + QString::fromUtf8(result.out));
		});
		return;
	}

	const QString tag = entry.type == ChangeType::Untracked ? tr("new file") : tr("HEAD %1 working tree").arg(QChar(0x2192));
	setDiffText(entry.path, tag, tr("Loading..."));
	_diffJob = _repo.diffFile(entry, this, [this, generation, entry](const GitResult& result) {
		if (generation != _diffGeneration)
			return;
		if (!result.ok)
			setDiffText(entry.path, {}, result.errorText());
		else if (result.out.size() > MaxDiffBytes)
			setDiffText(entry.path, {}, tr("The diff is too large to display (%1 MB).").arg(result.out.size() / (1024 * 1024)));
		else if (result.out.isEmpty())
			setDiffText(entry.path, {}, tr("No content changes (only the mode or the line endings differ, or the file matches HEAD)."));
		else
			setDiffText(entry.path, entry.type == ChangeType::Untracked ? tr("new file") : tr("HEAD %1 working tree").arg(QChar(0x2192)),
				QString::fromUtf8(result.out));
	});
}

void CommitWindow::setDiffText(const QString& pathLabel, const QString& tag, const QString& text)
{
	_diffPathLabel->setText(pathLabel);
	_diffTagLabel->setText(tag);
	_diffView->setPlainText(text);
}

void CommitWindow::onRowActivated(const QModelIndex& index)
{
	if (!index.isValid())
		return;
	const FileEntry entry = _filesModel.entryAt(index.row());

	if (entry.isSubmodule)
	{
		openSubmoduleWindow(entry);
		return;
	}
	if (entry.type == ChangeType::Untracked)
	{
		QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath(entry)));
		return;
	}
	if (entry.type == ChangeType::Deleted)
		return;

	// External difftool, detached from the process queue: the tool blocks git until closed,
	// and a queue slot held for minutes would starve refreshes
	QProcess::startDetached(Settings::gitExecutable(),
		{ QStringLiteral("difftool"), QStringLiteral("-y"), QStringLiteral("HEAD"), QStringLiteral("--"), entry.path },
		_repo.path());
}

void CommitWindow::showHistoryWindow()
{
	if (!_historyWindow)
	{
		_historyWindow = new HistoryWindow(_repo.path(), this);
		connect(this, &CommitWindow::committed, _historyWindow, &HistoryWindow::reload);
		connect(this, &CommitWindow::pushed, _historyWindow, &HistoryWindow::refreshUnpushedMarks);
	}
	_historyWindow->show();
	_historyWindow->raise();
	_historyWindow->activateWindow();
}

void CommitWindow::openSubmoduleWindow(const FileEntry& entry)
{
	auto* window = new CommitWindow(absolutePath(entry));
	connect(window, &CommitWindow::committed, &_repo, &Repository::refresh);
	window->show();
}

void CommitWindow::showContextMenu(const QPoint& pos)
{
	const std::vector<int> rows = selectedRows();
	if (rows.empty())
		return;

	// Entries are captured by value: menu.exec() spins an event loop, so a completing refresh can reset
	// the model while the menu is open, invalidating row indexes. The slots for the git actions re-query
	// the selection instead, which the reset safely empties.
	std::vector<FileEntry> entries;
	entries.reserve(rows.size());
	for (const int row : rows)
		entries.push_back(_filesModel.entryAt(row));

	const bool operationInProgress = _repo.state().operationInProgress();
	bool anyUntracked = false, anyAdded = false, anyDeletable = false, anyDiscardable = false;
	for (const FileEntry& entry : entries)
	{
		anyDiscardable |= discardable(entry, operationInProgress);
		if (entry.isSubmodule)
			continue;
		anyUntracked |= entry.type == ChangeType::Untracked;
		anyAdded |= entry.type == ChangeType::Added;
		anyDeletable |= entry.type != ChangeType::Deleted;
	}
	const bool singleFile = entries.size() == 1 && !entries.front().isSubmodule && entries.front().type != ChangeType::Deleted;

	QMenu menu{ this };
	QAction* addAction = menu.addAction(tr("Add to index"), this, &CommitWindow::addSelectionToIndex);
	addAction->setEnabled(anyUntracked);
	QAction* unAddAction = menu.addAction(tr("Un-add"), this, &CommitWindow::unAddSelection);
	unAddAction->setEnabled(anyAdded);
	QMenu* ignoreMenu = menu.addMenu(tr("Add to .gitignore"));
	const bool singleUntracked = entries.size() == 1 && !entries.front().isSubmodule && entries.front().type == ChangeType::Untracked;
	ignoreMenu->setEnabled(singleUntracked);
	if (singleUntracked)
	{
		for (const QString& pattern : gitIgnorePatterns(entries.front().path))
		{
			// '&' doubled for display only - QMenu would otherwise eat it as an accelerator marker
			ignoreMenu->addAction(QString{ pattern }.replace(QLatin1Char('&'), QStringLiteral("&&")),
				this, [this, pattern] { appendToGitIgnore(pattern); });
		}
	}
	menu.addSeparator();
	QAction* openAction = menu.addAction(tr("Open"), this, [this, entry = entries.front()] {
		QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath(entry)));
	});
	openAction->setEnabled(singleFile);
	QAction* submoduleHistoryAction = menu.addAction(tr("View commit history"), this, [this, entry = entries.front()] {
		// Not deduplicated the way this repo's own history window is, matching openSubmoduleWindow
		auto* window = new HistoryWindow(absolutePath(entry), this);
		window->show();
	});
	submoduleHistoryAction->setEnabled(entries.size() == 1 && entries.front().isSubmodule);
	QAction* fileHistoryAction = menu.addAction(tr("View file history"), this, [this, entry = entries.front()] {
		auto* window = new HistoryWindow(_repo.path(), entry.path, this);
		window->show();
	});
	// Untracked files have no history to show, and a submodule's is its own repo's, offered above
	fileHistoryAction->setEnabled(entries.size() == 1 && !entries.front().isSubmodule
		&& entries.front().type != ChangeType::Untracked);
	QAction* explorerAction = menu.addAction(tr("Show in Explorer"), this, [this, entry = entries.front()] {
		const QString nativePath = QDir::toNativeSeparators(absolutePath(entry));
#ifdef Q_OS_WIN
		QProcess::startDetached(QStringLiteral("explorer"), { QStringLiteral("/select,") + nativePath });
#else
		QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(nativePath).absolutePath()));
#endif
	});
	explorerAction->setEnabled(entries.size() == 1 && !entries.front().isSubmodule);
	menu.addAction(tr("Copy path"), this, [this, entries] {
		QStringList paths;
		for (const FileEntry& entry : entries)
			paths.push_back(QDir::toNativeSeparators(absolutePath(entry)));
		QApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
	});
	menu.addSeparator();
	QAction* discardAction = menu.addAction(tr("Discard changes"), this, &CommitWindow::discardSelection);
	discardAction->setEnabled(anyDiscardable);
	QAction* deleteAction = menu.addAction(tr("Delete to Recycle Bin"), this, &CommitWindow::deleteSelection);
	deleteAction->setEnabled(anyDeletable);
	// Display-only: the view's event filter owns the actual Del handling; WidgetShortcut on an action
	// belonging to no widget never registers globally, so the key cannot trigger twice
	deleteAction->setShortcut(QKeySequence::Delete);
	deleteAction->setShortcutContext(Qt::WidgetShortcut);
	deleteAction->setShortcutVisibleInContextMenu(true);

	menu.exec(_filesView->viewport()->mapToGlobal(pos));
}

CommitWindow::SelectionByPath CommitWindow::captureSelectionByPath() const
{
	SelectionByPath selection;
	for (const int row : selectedRows())
		selection.paths.insert(_filesModel.entryAt(row).path);

	if (const QModelIndex current = _filesView->currentIndex();
		current.isValid() && current.row() < _filesModel.rowCount())
	{
		selection.currentPath = _filesModel.entryAt(current.row()).path;
	}
	return selection;
}

void CommitWindow::restoreSelectionByPath(const SelectionByPath& selection)
{
	// One pass over the new rows rather than a lookup per remembered path, so a large selection
	// over a large list stays linear
	QItemSelection restored;
	int currentRow = -1;
	for (int row = 0; row < _filesModel.rowCount(); ++row)
	{
		const QString& path = _filesModel.entryAt(row).path;
		if (selection.paths.contains(path))
			restored.select(_filesModel.index(row, 0), _filesModel.index(row, ChangedFilesModel::ColumnCount - 1));
		if (path == selection.currentPath)
			currentRow = row;
	}

	if (currentRow < 0 && _filesModel.rowCount() > 0)
		currentRow = 0; // the file is gone - committed, or discarded elsewhere
	if (currentRow >= 0)
		_filesView->setCurrentIndex(_filesModel.index(currentRow, ChangedFilesModel::StateColumn));

	// After setCurrentIndex, which selects the row it lands on and would otherwise be the selection
	if (!restored.isEmpty())
		_filesView->selectionModel()->select(restored, QItemSelectionModel::ClearAndSelect);
}

std::vector<int> CommitWindow::selectedRows() const
{
	std::vector<int> rows;
	const auto indexes = _filesView->selectionModel()->selectedRows(ChangedFilesModel::StateColumn);
	rows.reserve(size_t(indexes.size()));
	for (const QModelIndex& index : indexes)
		rows.push_back(index.row());
	std::sort(rows.begin(), rows.end());
	return rows;
}

void CommitWindow::toggleCheckOnSelection()
{
	const std::vector<int> rows = selectedRows();

	bool allChecked = true;
	for (const int row : rows)
	{
		if (_filesModel.isUserCheckable(row) && !_filesModel.isChecked(row))
		{
			allChecked = false;
			break;
		}
	}
	for (const int row : rows)
		_filesModel.setRowChecked(row, !allChecked);
}

void CommitWindow::deleteSelection()
{
	// Per-state delete rules. Submodules and already-deleted rows are skipped.
	QStringList untrackedPaths, addedPaths, trackedPaths;
	for (const int row : selectedRows())
	{
		const FileEntry& entry = _filesModel.entryAt(row);
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

	if (!trackedPaths.isEmpty() || !addedPaths.isEmpty())
	{
		const QStringList prompted = trackedPaths + addedPaths;
		const auto answer = MessageBox::question(this, tr("Delete files?"),
			tr("Move %1 tracked file(s) to the Recycle Bin?\n\n%2").arg(prompted.size()).arg(listedPaths(prompted)),
			{ tr("Delete") });
		if (answer != 0)
			return;
	}

	const auto trashAll = [this](const QStringList& paths) {
		for (const QString& path : paths)
		{
			if (QFile::moveToTrash(QDir(_repo.path()).filePath(path)))
				continue;
			// Never fall back to a permanent delete
			MessageBox::notice(this, tr("Delete failed"),
				tr("Could not move '%1' to the Recycle Bin (the file may be locked, or the volume has no Recycle Bin).\n"
				   "Remaining files were not touched.").arg(path), {});
			return;
		}
	};

	if (!addedPaths.isEmpty())
	{
		// Un-add first: trashing a staged-as-added file would leave the index pointing at a file that no longer exists
		_repo.unAdd(addedPaths, [this, paths = untrackedPaths + trackedPaths + addedPaths, trashAll](const GitResult& result) {
			if (!result.ok)
			{
				showGitError(tr("Failed to un-add before deleting"), result);
				return;
			}
			trashAll(paths);
			_repo.refresh();
		});
		return;
	}

	trashAll(untrackedPaths + trackedPaths);
	_repo.refresh();
}

void CommitWindow::discardSelection()
{
	const bool operationInProgress = _repo.state().operationInProgress();

	// Added rows are un-added rather than restored: `git restore` deletes such a file outright, and nothing
	// in this window destroys content that was never committed. Everything the rest of this needs is read
	// from the model here - the dialog below spins an event loop, and a refresh in it resets the rows.
	QStringList pathspec, promptPaths, addedPaths;
	bool anySubmodule = false;
	int skippedRows = 0;
	for (const int row : selectedRows())
	{
		const FileEntry& entry = _filesModel.entryAt(row);
		if (!discardable(entry, operationInProgress))
		{
			++skippedRows; // rows in a mixed selection that discarding does not cover are a deliberate no-op
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

	if (!promptPaths.isEmpty()) // un-adding on its own loses nothing, so it needs no confirmation
	{
		QString text = tr("Discard all changes to %1 file(s)? This cannot be undone.\n\n%2")
			.arg(promptPaths.size()).arg(listedPaths(promptPaths));
		if (anySubmodule)
			text += tr("\n\nDiscarding a submodule's pointer change checks the recorded commit out inside it, which leaves it on a detached HEAD.");
		if (!addedPaths.isEmpty())
			text += tr("\n\n%1 added file(s) will be un-added and left on disk.").arg(addedPaths.size());
		if (skippedRows > 0)
			text += tr("\n\n%1 other selected row(s) stay as they are: untracked files, and submodules whose content is "
					   "modified or could not be read, cannot be discarded.").arg(skippedRows);

		const auto answer = MessageBox::question(this, tr("Discard changes?"), text, { tr("Discard") });
		if (answer != 0)
			return;
	}

	const auto unAddThenRefresh = [this, addedPaths] {
		if (addedPaths.isEmpty())
		{
			_repo.refresh();
			return;
		}
		_repo.unAdd(addedPaths, [this](const GitResult& result) {
			if (!result.ok)
				showGitError(tr("Un-add failed"), result);
			_repo.refresh();
		});
	};

	if (pathspec.isEmpty())
	{
		unAddThenRefresh();
		return;
	}

	_repo.discardChanges(pathspec, [this, unAddThenRefresh](const GitResult& result) {
		if (!result.ok)
		{
			showGitError(tr("Discard failed"), result);
			_repo.refresh();
			return;
		}
		unAddThenRefresh();
	});
}

void CommitWindow::addSelectionToIndex()
{
	QStringList paths;
	for (const int row : selectedRows())
	{
		const FileEntry& entry = _filesModel.entryAt(row);
		if (!entry.isSubmodule && entry.type == ChangeType::Untracked)
			paths.push_back(entry.path); // tracked rows in a mixed selection are a deliberate no-op
	}
	if (paths.isEmpty())
		return;
	_repo.addToIndex(paths, [this](const GitResult& result) {
		if (!result.ok)
			showGitError(tr("Add to index failed"), result);
		_repo.refresh();
	});
}

void CommitWindow::unAddSelection()
{
	QStringList paths;
	for (const int row : selectedRows())
	{
		const FileEntry& entry = _filesModel.entryAt(row);
		if (!entry.isSubmodule && entry.type == ChangeType::Added)
			paths.push_back(entry.path);
	}
	if (paths.isEmpty())
		return;
	_repo.unAdd(paths, [this](const GitResult& result) {
		if (!result.ok)
			showGitError(tr("Un-add failed"), result);
		_repo.refresh();
	});
}

void CommitWindow::appendToGitIgnore(const QString& pattern)
{
	QFile file{ QDir{ _repo.path() }.filePath(QStringLiteral(".gitignore")) };
	if (!file.open(QIODevice::ReadWrite)) // ReadWrite: the existing contents decide the EOL style and the need for a leading newline
	{
		MessageBox::notice(this, tr("Failed to update .gitignore"),
			tr("Could not open '%1' for writing.").arg(QDir::toNativeSeparators(file.fileName())), {});
		return;
	}
	const QByteArray existing = file.readAll();
	const QByteArray eol = existing.contains("\r\n") ? QByteArrayLiteral("\r\n") : QByteArrayLiteral("\n");
	QByteArray addition;
	if (!existing.isEmpty() && !existing.endsWith('\n'))
		addition += eol;
	addition += pattern.toUtf8() + eol;
	file.write(addition);
	file.close();
	_repo.refresh();
}

void CommitWindow::showGitError(const QString& title, const GitResult& result)
{
	// The command's own stderr, verbatim - hook output is the only thing that makes a rejected commit diagnosable
	MessageBox::notice(this, title, title + QLatin1Char('.'), result.errorText());
}

QString CommitWindow::absolutePath(const FileEntry& entry) const
{
	return QDir{ _repo.path() }.filePath(entry.path);
}
