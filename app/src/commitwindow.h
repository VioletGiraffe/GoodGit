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
class QPlainTextEdit;
class ConsoleLogView;
class DiffPane;
class FileListView;
class MessageEdit;
class CLabelElided;
class HistoryWindow;

// One window = one repository. Submodule rows open another instance of this window.
class CommitWindow final : public QMainWindow
{
	Q_OBJECT

public:
	explicit CommitWindow(const RepositoryLocation& location);

	[[nodiscard]] const QString& repositoryPath() const;
	// Called by a submodule's window after committing there, which moves a pointer here
	void refreshRepository();

signals:
	void committed(); // the parent window refreshes its gitlink row off this
	void pushed();    // the history window's unpushed marks are stale until it hears this

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

private:
	void buildUi();
	// Returns the action that shows and hides the dock
	[[nodiscard]] QAction* buildRecentRepositoriesDock();

	void onRefreshed();
	void updateHeader();
	void updateStrips();
	void updateControlStates();
	// The subject a commit is named by in the UI: a commit made with an empty message has none
	[[nodiscard]] static QString subjectOrPlaceholder(const QString& subject);

	// The message a window closes on outlives it, keyed by the repository and the commit it was written against
	void saveDraftMessage();
	void restoreDraftIfParentUnchanged();

	// Every flow that writes to the index or the working tree brackets itself with these; they disable the
	// controls that would start a second one
	void beginMutation();
	void endMutation();
	// False while a write is in flight, and while the rows are only the last state that could be read
	[[nodiscard]] bool canActOnList() const;

	void startCommit(bool pushAfterwards);
	void confirmUntrackedThenCommit(bool pushAfterwards);
	// Always calls back, refusal included: the caller is mid-commit and has to end its flow either way
	void reattachHead(std::function<void(bool reattached)> onDone);
	void doCommit(bool pushAfterwards);
	// Runs the steps the repository plans one after another, logged as one console session. The first
	// failure ends the push.
	void startPush();
	void runPushStep(size_t index, bool setUpstream);
	// Offers to set an upstream for the step's branch and retries if accepted. Returns whether the push
	// goes on.
	bool offerUpstreamThenRetry(size_t index);
	void peekIncoming();
	void showIncomingCommits(const std::vector<CommitRecord>& commits, bool capped);
	void closePushLogEntry(const ProcessResult& result);

	void showHistoryWindow();
	void undoLastCommit();
	void showPreferencesDialog();

	void showDiffForCurrentRow();
	// For an untracked file, which has no diff: the pane shows the file itself, unhighlighted
	void showFileContents(const FileEntry& entry);
	void onRowActivated(const QModelIndex& index);
	void openSubmoduleWindow(const FileEntry& entry);
	void showContextMenu(const QPoint& pos);

	// A refresh resets the model, so the selection is carried across it by path
	struct SelectionByPath
	{
		QSet<QString> paths;
		QString currentPath; // drives the diff pane, so it is restored even when nothing was selected
	};
	[[nodiscard]] SelectionByPath captureSelectionByPath() const;
	void restoreSelectionByPath(const SelectionByPath& selection);

	// Copies, not references: a menu or a dialog spins an event loop, and a refresh completing in it resets the rows
	[[nodiscard]] std::vector<FileEntry> selectedEntries() const;
	// The row the view is on; absent when there is none
	[[nodiscard]] std::optional<FileEntry> currentEntry() const;
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
	CLabelElided* _repoNameLabel = nullptr;
	CLabelElided* _branchLabel = nullptr;
	QLabel* _aheadLabel = nullptr;
	QPushButton* _pushButton = nullptr;
	QPushButton* _peekButton = nullptr;
	QPushButton* _historyButton = nullptr;
	QAction* _uncommitAction = nullptr;
	QLabel* _readFailureStrip = nullptr;
	QLabel* _opStrip = nullptr;
	QLabel* _detachedStrip = nullptr;
	QCheckBox* _checkAllBox = nullptr;
	QLabel* _lineTotalsLabel = nullptr;
	FileListView* _filesView = nullptr;
	CLabelElided* _parentCommitLabel = nullptr;
	MessageEdit* _messageEdit = nullptr;
	QPushButton* _commitButton = nullptr;
	QPushButton* _commitPushButton = nullptr;
	DiffPane* _diffPane = nullptr;
	QWidget* _pushLogPane = nullptr; // hidden until the first push of the session
	ConsoleLogView* _pushLogView = nullptr;

	QFrame* _incomingPopup = nullptr; // built on the first peek; Qt::Popup, so it closes on a click outside
	QLabel* _incomingHeaderLabel = nullptr;
	QPlainTextEdit* _incomingView = nullptr;

	std::vector<PushStep> _pushSteps; // the running push's plan

	QPointer<HistoryWindow> _historyWindow; // at most one per repo window, raised again on a second click
	Vcs::Query _diffQuery; // whatever fills the diff pane: a file's diff, or a submodule's incoming commits
	Vcs::Query _wordPoolQuery;
	// Held for a whole writing flow, dialogs and the asynchronous reattach included, not just while a process
	// runs: two flows would meet at index.lock, and the second would commit a pathspec the first already took
	bool _mutationInFlight = false;
	bool _peekInFlight = false; // a fetch is slow, and a refresh landing meanwhile must not re-enable the button
	// A refresh has established the state at least once: before that there is no parent sha to judge a stored draft by
	bool _stateWasRead = false;
};
