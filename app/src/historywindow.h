#pragma once

#include "historymodels.h"
#include "repository.h"

#include <QMainWindow>

#include <memory>

class QCheckBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QTreeView;
class CLabelElided;
class DiffHighlighter;

// The commit history of one repository, read-only. Owns its own Repository - it only ever asks
// read-only questions, and a submodule's history is opened without a CommitWindow on that submodule
// to borrow one from. Every job is scoped to this window, so closing it drops the pending queries
// instead of leaving them to finish against nothing.
class HistoryWindow final : public QMainWindow
{
	Q_OBJECT

public:
	HistoryWindow(const RepositoryLocation& location, QWidget* parent);
	// The history of one repo-relative path, traced across renames
	HistoryWindow(const RepositoryLocation& location, const QString& filePath, QWidget* parent);

	// Re-runs the log query from scratch. The commit window calls this after committing here.
	void reload();
	// Re-reads which commits are unpushed, leaving the list and the selection alone - a push changes
	// nothing else, so the commit window calls this rather than reload() after one.
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
	void showCommitContextMenu(const QPoint& pos);
	void showFileContextMenu(const QPoint& pos);
	void openFileHistory(const QString& filePath);
	void showFilesForCurrentCommit();
	void showDiffForCurrentFile();
	// The two kinds of content the right-hand pane holds; each owns whether the diff highlighting applies
	void showCommitMessage(const CommitRecord& commit);
	void setDiffText(const QString& pathLabel, const QString& tag, const QString& text);

private:
	const std::unique_ptr<Repository> _repo;
	CommitLogModel _logModel;
	CommitFilesModel _filesModel;

	// What this window shows. maxCommits is widened by Load more, which re-runs the whole query: a
	// date-ordered walk cannot be resumed from a cursor - see doc/ARCHITECTURE.md. The path is fixed
	// at construction; the pickaxe is not.
	Repository::LogQuery _query;
	bool _logCapped = false; // the last query returned its full limit, so older commits exist unread
	bool _logLoaded = false; // the marks query can land first, and its counts mean nothing until this

	QSplitter* _splitter = nullptr;       // log above, the commit's detail below
	QSplitter* _detailSplitter = nullptr; // file list beside the diff
	QTreeView* _logView = nullptr;
	QTreeView* _filesView = nullptr;
	CLabelElided* _filePathLabel = nullptr; // shown only in a file history, which is otherwise indistinguishable
	QLabel* _countLabel = nullptr;
	QLineEdit* _searchEdit = nullptr;
	QPushButton* _pickaxeButton = nullptr;
	QFrame* _pickaxePopup = nullptr; // built on first use; Qt::Popup, so a click outside dismisses it
	QLineEdit* _pickaxeEdit = nullptr;
	QCheckBox* _pickaxeIgnoreCaseBox = nullptr;
	QLabel* _fileCountLabel = nullptr;
	QPushButton* _loadMoreButton = nullptr;
	CLabelElided* _diffPathLabel = nullptr;
	QLabel* _diffTagLabel = nullptr;
	QPlainTextEdit* _diffView = nullptr;
	DiffHighlighter* _diffHighlighter = nullptr;

	Vcs::Query _logQuery;
	Vcs::Query _pickaxeQuery;
	Vcs::Query _unpushedQuery;
	Vcs::Query _filesQuery;
	Vcs::Query _fileCountsQuery;
	Vcs::Query _diffQuery;
};
