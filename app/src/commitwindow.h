#pragma once

#include "changedfilesmodel.h"
#include "repository.h"

#include <QMainWindow>
#include <QPointer>
#include <QSet>

class QCheckBox;
class QFrame;
class QLabel;
class QPushButton;
class QSplitter;
class QTreeView;
class QPlainTextEdit;
class MessageEdit;
class CLabelMidElision;
class HistoryWindow;

// One window = one repository. Submodule rows open another instance of this window.
class CommitWindow final : public QMainWindow
{
	Q_OBJECT

public:
	explicit CommitWindow(const QString& repoPath);

signals:
	void committed(); // a commit succeeded here; the parent window refreshes its gitlink row off this
	void pushed();    // a push succeeded here; the history window's unpushed marks are stale until it hears this

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

private:
	void buildUi();

	void onRefreshed();
	void updateHeader();
	void updateStrips();
	void updateButtons();

	void startCommit(bool pushAfterwards);
	void confirmUntrackedThenCommit(bool pushAfterwards);
	void reattachHead(std::function<void()> onReattached);
	void doCommit(bool pushAfterwards);
	void doPush(bool setUpstream);
	void peekIncoming();
	void showIncomingCommits(const std::vector<CommitRecord>& commits, bool capped);
	void appendPushLog(const QString& commandLabel, const GitResult& result);

	void showHistoryWindow();

	void showDiffForCurrentRow();
	void setDiffText(const QString& pathLabel, const QString& tag, const QString& text);
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
	void addSelectionToIndex();
	void unAddSelection();
	void appendToGitIgnore(const QString& pattern);

	void showGitError(const QString& title, const GitResult& result);
	[[nodiscard]] QString absolutePath(const FileEntry& entry) const;

private:
	Repository _repo;
	ChangedFilesModel _filesModel;

	QSplitter* _splitter = nullptr;
	QLabel* _repoNameLabel = nullptr;
	QLabel* _branchLabel = nullptr;
	QLabel* _aheadLabel = nullptr;
	QPushButton* _pushButton = nullptr;
	QPushButton* _peekButton = nullptr;
	QPushButton* _refreshButton = nullptr;
	QPushButton* _historyButton = nullptr;
	QLabel* _opStrip = nullptr;
	QLabel* _detachedStrip = nullptr;
	QCheckBox* _checkAllBox = nullptr;
	QTreeView* _filesView = nullptr;
	MessageEdit* _messageEdit = nullptr;
	QPushButton* _commitButton = nullptr;
	QPushButton* _commitPushButton = nullptr;
	CLabelMidElision* _diffPathLabel = nullptr;
	QLabel* _diffTagLabel = nullptr;
	QPlainTextEdit* _diffView = nullptr;
	QWidget* _pushLogPane = nullptr; // hidden until the first push of the session
	QPlainTextEdit* _pushLogView = nullptr;

	QFrame* _incomingPopup = nullptr; // built on the first peek; Qt::Popup, so it closes on a click outside
	QLabel* _incomingHeaderLabel = nullptr;
	QPlainTextEdit* _incomingView = nullptr;

	QPointer<HistoryWindow> _historyWindow; // at most one per repo window, raised again on a second click
	QPointer<Git::Job> _diffJob;
	int _diffGeneration = 0; // stale async diff results (incl. the two-step submodule log) are dropped by this
	bool _commitInFlight = false;
	bool _peekInFlight = false; // a fetch is slow, and a refresh landing meanwhile must not re-enable the button
};
