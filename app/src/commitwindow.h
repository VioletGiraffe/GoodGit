#pragma once

#include "changedfilesmodel.h"
#include "repository.h"

#include <QMainWindow>
#include <QPointer>
#include <QSet>

#include <memory>

class QAction;
class QCheckBox;
class QFrame;
class QLabel;
class QPushButton;
class QSplitter;
class QTreeView;
class QPlainTextEdit;
class ConsoleLogView;
class DiffPane;
class MessageEdit;
class CLabelElided;
class HistoryWindow;

// One window = one repository. Submodule rows open another instance of this window.
class CommitWindow final : public QMainWindow
{
	Q_OBJECT

public:
	explicit CommitWindow(const RepositoryLocation& location);

	// Which repository this window is showing - how a second request to open one finds it already open
	[[nodiscard]] const QString& repositoryPath() const;
	// For the window whose submodule another window is showing: that window's commit moves a pointer here
	void refreshRepository();

signals:
	void committed(); // a commit succeeded here; the parent window refreshes its gitlink row off this
	void pushed();    // a push succeeded here; the history window's unpushed marks are stale until it hears this

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

private:
	void buildUi();
	// The recent repositories dock, added to the window; answers with the action that shows and hides it
	[[nodiscard]] QAction* buildRecentRepositoriesDock();

	void onRefreshed();
	void updateHeader();
	void updateStrips();
	void updateButtons();

	// Every flow that writes to the index or the working tree brackets itself with these, and they drive
	// the controls that would otherwise start a second one
	void beginMutation();
	void endMutation();
	// False while a write is in flight, and while the rows are only the last state that could be read
	[[nodiscard]] bool canActOnList() const;

	void startCommit(bool pushAfterwards);
	void confirmUntrackedThenCommit(bool pushAfterwards);
	// Always reports back, refusal included: the caller is mid-commit and has to end its flow either way
	void reattachHead(std::function<void(bool reattached)> onDone);
	void doCommit(bool pushAfterwards);
	// A push is however many commands the repository plans for it, run one after another and logged as one
	// console session. The first failure ends it, so nothing is published past a step that could not be.
	void startPush();
	void runPushStep(size_t index, bool setUpstream);
	// The offer to give a step's branch an upstream, and the retry if it is taken. Answers whether the push
	// goes on: a declined offer ends it.
	bool offerUpstreamThenRetry(size_t index);
	void peekIncoming();
	void showIncomingCommits(const std::vector<CommitRecord>& commits, bool capped);
	void closePushLogEntry(const ProcessResult& result);

	void showHistoryWindow();
	void undoLastCommit();
	void showPreferencesDialog();

	void showDiffForCurrentRow();
	// An untracked file is nothing's modification, so the pane shows what it holds, unhighlighted
	void showFileContents(const FileEntry& entry);
	void onRowActivated(const QModelIndex& index);
	void openSubmoduleWindow(const FileEntry& entry);
	void showContextMenu(const QPoint& pos);

	// A refresh resets the model, so row numbers do not survive it. The selection travels by path
	// instead - the same identity the check state is re-derived from.
	struct SelectionByPath
	{
		QSet<QString> paths;
		QString currentPath; // drives the diff pane, so it is restored even when nothing was selected
	};
	[[nodiscard]] SelectionByPath captureSelectionByPath() const;
	void restoreSelectionByPath(const SelectionByPath& selection);

	[[nodiscard]] std::vector<int> selectedRows() const;
	void toggleCheckOnSelection();
	void deleteSelection();
	void discardSelection();
	void addSelectionToIndex();
	void unAddSelection();
	void addPatternToIgnoreFile(const IgnorePattern& pattern);

	void showError(const QString& title, const QString& details);
	[[nodiscard]] QString absolutePath(const FileEntry& entry) const;

private:
	const std::unique_ptr<Repository> _repo;
	ChangedFilesModel _filesModel;

	QSplitter* _splitter = nullptr;
	QLabel* _repoNameLabel = nullptr;
	QLabel* _branchLabel = nullptr;
	QLabel* _aheadLabel = nullptr;
	QPushButton* _pushButton = nullptr;
	QPushButton* _peekButton = nullptr;
	QPushButton* _refreshButton = nullptr;
	QPushButton* _historyButton = nullptr;
	QPushButton* _uncommitButton = nullptr;
	QLabel* _readFailureStrip = nullptr;
	QLabel* _opStrip = nullptr;
	QLabel* _detachedStrip = nullptr;
	QCheckBox* _checkAllBox = nullptr;
	QLabel* _lineTotalsLabel = nullptr;
	QTreeView* _filesView = nullptr;
	CLabelElided* _lastCommitLabel = nullptr;
	MessageEdit* _messageEdit = nullptr;
	QPushButton* _commitButton = nullptr;
	QPushButton* _commitPushButton = nullptr;
	DiffPane* _diffPane = nullptr;
	QWidget* _pushLogPane = nullptr; // hidden until the first push of the session
	ConsoleLogView* _pushLogView = nullptr;

	QFrame* _incomingPopup = nullptr; // built on the first peek; Qt::Popup, so it closes on a click outside
	QLabel* _incomingHeaderLabel = nullptr;
	QPlainTextEdit* _incomingView = nullptr;

	std::vector<PushStep> _pushSteps; // the running push's plan, replaced wholesale when the next one is planned

	QPointer<HistoryWindow> _historyWindow; // at most one per repo window, raised again on a second click
	Vcs::Query _diffQuery; // whatever fills the diff pane: a file's diff, or a submodule's incoming commits
	Vcs::Query _wordPoolQuery;
	// Held for a whole writing flow, dialogs and the asynchronous reattach included - not just while a git
	// process runs. Two of them would meet at index.lock, and the second would commit a pathspec the first
	// has already taken.
	bool _mutationInFlight = false;
	bool _peekInFlight = false; // a fetch is slow, and a refresh landing meanwhile must not re-enable the button
};
