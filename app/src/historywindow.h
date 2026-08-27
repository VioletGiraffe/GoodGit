#pragma once

#include "historymodels.h"
#include "repository.h"

DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

#include <memory>

class QCheckBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QTimer;
class QTreeView;
class CLabelElided;
class DiffPane;
class FileListView;

// The commit history of one repository, read-only.
// Owns its own Repository: a submodule's history may be opened without a CommitWindow on that submodule.
// Every query is scoped to this window, so closing it drops the pending ones.
class HistoryWindow final : public QMainWindow
{
public:
	HistoryWindow(const RepositoryLocation& location, QWidget* parent);
	// The history of one repo-relative path, traced across renames
	HistoryWindow(const RepositoryLocation& location, const QString& filePath, QWidget* parent);

	// Selects the commit and scrolls it into view once the listing is in.
	// A commit the walk did not cover (the checkout has moved on, or it is older than the limit) re-runs the
	// walk from that commit, which is then the newest row.
	void revealCommit(const QString& sha);

	// Re-runs the log query from scratch
	void reload();
	// Re-reads which commits are unpushed, leaving the list and the selection alone
	void refreshUnpushedMarks();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

private:
	void buildUi();

	void applySearch();
	void updateCountLabel();
	void showPickaxePopup();
	void runPickaxe(const QString& text, bool ignoreCase);
	void startPickaxeQuery();
	void showCommitContextMenu(const QPoint& pos);
	void showFileContextMenu(const QPoint& pos);
	void openFileHistory(const QString& filePath);
	// One file's content as of one commit, in a window of its own
	void openFileViewer(const QString& sha, const QString& repoRelativePath);
	// A submodule row opens its own repository's history at the commit the pointer names; a file row opens
	// the viewer, on the parent commit where Shift is held
	void onFileRowActivated(const QModelIndex& sourceIndex, Qt::KeyboardModifiers modifiers);
	// Absent when the index is not a row of the listing shown
	[[nodiscard]] std::optional<CommitFileChange> fileEntryAt(const QModelIndex& sourceIndex) const;
	// The commit whose files are listed; absent while none is selected
	[[nodiscard]] std::optional<CommitRecord> currentCommit() const;
	void openSubmoduleHistory(const CommitFileChange& entry);
	// The same walk at the full _query.maxCommits, extending the shown batch in place: a cold open's second
	// phase, and every Load more
	void loadRemainingCommits();
	// Selects the commit revealCommit() asked for, or the newest row
	void selectLoadedCommit();
	void showFilesForCurrentCommit();
	void showDiffForCurrentFile();
	void showCommitMessage(const CommitRecord& commit);

private:
	const std::unique_ptr<Repository> _repo;
	CommitLogModel _logModel;
	CommitFilesModel _filesModel;

	// Load more doubles maxCommits and re-runs the whole query: the walk cannot be resumed from a cursor
	// (doc/ARCHITECTURE.md). The path is fixed at construction; the content search is not.
	Repository::LogQuery _query;
	bool _logCapped = false; // the last query returned its full limit, so older commits exist unread
	bool _logLoaded = false; // the marks query can land first, and its counts mean nothing until this
	// The full-limit walk of a cold open is still running: a reveal miss waits for it instead of re-walking,
	// and the count label says more is coming
	bool _fullLoadPending = false;
	// The commit the next finished listing should land on; cleared once it has, so a later reload or Load
	// more selects the newest row as usual
	QString _revealSha;

	QSplitter* _splitter = nullptr;       // log above, the commit's detail below
	QSplitter* _detailSplitter = nullptr; // file list beside the diff
	QTreeView* _logView = nullptr;
	FileListView* _filesView = nullptr;
	CLabelElided* _filePathLabel = nullptr; // shown only in a file history
	QLabel* _countLabel = nullptr;
	QLineEdit* _searchEdit = nullptr;
	QTimer* _searchDebounce = nullptr; // batches keystrokes before applySearch
	QPushButton* _pickaxeButton = nullptr;
	QFrame* _pickaxePopup = nullptr; // built on first use; Qt::Popup, so a click outside dismisses it
	QLineEdit* _pickaxeEdit = nullptr;
	QCheckBox* _pickaxeIgnoreCaseBox = nullptr;
	QLabel* _fileCountLabel = nullptr;
	QPushButton* _loadMoreButton = nullptr;
	DiffPane* _diffPane = nullptr;

	Vcs::Query _logQuery;
	Vcs::Query _pickaxeQuery;
	Vcs::Query _unpushedQuery;
	Vcs::Query _filesQuery;
	Vcs::Query _fileCountsQuery;
	Vcs::Query _diffQuery;
};
