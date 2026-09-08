#pragma once

#include "diffpane.h"
#include "historymodels.h"
#include "repository.h"
#include "unifieddiff.h"

DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <vector>

class QCheckBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QTimer;
class QTreeView;
class CLabelElided;
class FileListView;

// What a reveal does where the listing does not hold the commit
enum class RevealMiss
{
	Report,    // name it in the count label
	ReloadOnce // re-run the log query first, for a caller that knows the commit exists
};

// The commit history of one repository, read-only.
// Owns its own Repository: a submodule's history may be opened without a CommitWindow on that submodule.
// Every query is scoped to this window, so closing it drops the pending ones.
// Parentless: it takes its own taskbar entry and outlives the window that opened it.
class HistoryWindow final : public QMainWindow
{
public:
	explicit HistoryWindow(const RepositoryLocation& location);
	// The history of one repo-relative path, traced across renames.
	// Never deduplicated: two views of one file's history may be wanted side by side.
	HistoryWindow(const RepositoryLocation& location, const QString& filePath);

	[[nodiscard]] const QString& repositoryPath() const { return _repo->path(); }
	// The repo-relative path whose history this traces; empty in a whole-repository history
	[[nodiscard]] const QString& filePath() const { return _query.path; }

	// Selects the commit and scrolls it into view once the listing is in.
	// A commit the listing does not hold (reachable from no ref, or older than the limit) is named in the
	// count label instead, and a selection already made stands.
	// ReloadOnce applies only where the listing predates the call: one still loading is already current.
	void revealCommit(const QString& sha, RevealMiss onMiss = RevealMiss::Report);

	// Re-runs the log query from scratch, returning to the commit the view was on
	void reload();
	// Re-reads which commits are unpushed, leaving the list and the selection alone
	void refreshUnpushedMarks();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

private:
	void buildUi();

	// Re-reads which commit the working tree is on, for the diagram's current-commit ring and the history
	// drawn above it
	void refreshCurrentCommitMark();
	void applySearch();
	void updateCountLabel();
	void showPickaxePopup();
	void runPickaxe(const QString& text, bool ignoreCase);
	void startPickaxeQuery();
	void showCommitContextMenu(const QPoint& pos);
	void showFileContextMenu(const QPoint& pos);
	void openFileHistory(const QString& filePath);
	// One file's content as of one commit, in a separate window
	void openFileViewer(const QString& sha, const QString& repoRelativePath);
	// A submodule row opens its own repository's history at the commit the pointer names; a file row opens
	// the viewer, on the parent commit where Shift is held
	void onFileRowActivated(const QModelIndex& sourceIndex, Qt::KeyboardModifiers modifiers);
	// Absent when the index is not a row of the listing shown
	[[nodiscard]] std::optional<CommitFileChange> fileEntryAt(const QModelIndex& sourceIndex) const;
	// The commit whose files are listed; absent while none is selected
	[[nodiscard]] std::optional<CommitRecord> currentCommit() const;
	// Its sha, empty while no row is current. Not the model's current sha, which is the checked-out commit.
	[[nodiscard]] QString selectedSha() const;
	void openSubmoduleHistory(const CommitFileChange& entry);
	// The same walk at the full _query.maxCommits, extending the shown batch in place: a cold open's second
	// phase, and every Load more
	void loadRemainingCommits();
	// Selects the commit revealCommit() asked for, the one a reload replaced, or the newest row
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
	// more does not land on it again
	QString _revealSha;
	// The commit the view returns to once a reload's listing lands, so a refresh does not move the reader.
	// A reveal outranks it.
	QString _reselectSha;
	// Downgraded to Report once the retry is spent, so a reveal re-runs the query at most once
	RevealMiss _revealOnMiss = RevealMiss::Report;
	// The reveal the finished listing could not satisfy, for the count label to name. Dropped as soon as the
	// listing it was decided against changes: a reload, or a deeper walk
	QString _missedRevealSha;

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
	Vcs::Query _currentCommitQuery;
	Vcs::Query _filesQuery;
	Vcs::Query _fileCountsQuery;
	Vcs::Query _diffQuery;
	Vcs::Query _sizeQuery;
	// The current commit's whole diff: a file's diff is cut out of it, and a block moved between files is
	// found across it. Absent where the query failed or the diff passed its cap; a file is then diffed on its own.
	std::optional<ChangeSetDiff> _changeSet;
	Vcs::Query _changeSetQuery;
	bool _changeSetPending = false; // the query is out: a file waits for it instead of being diffed on its own
	bool _fileAwaitsChangeSet = false; // the file shown is waiting, so the set's arrival shows it

	// What the pane's header states about the file being shown. A member rather than a value each callback
	// carries: the size arrives on its own query, and the diff must not restate a header without it.
	DiffPane::ItemInfo _currentItem;
};

// Every open history window on this repository, whole-repository and per-file alike
[[nodiscard]] std::vector<HistoryWindow*> historyWindowsFor(const QString& repositoryRoot);
// This repository's whole history, in the window already showing it or a new one, raised and activated
HistoryWindow* showRepositoryHistory(const RepositoryLocation& location);
