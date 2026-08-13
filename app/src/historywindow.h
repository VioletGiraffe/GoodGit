#pragma once

#include "historymodels.h"
#include "repository.h"

#include <QMainWindow>
#include <QPointer>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QTreeView;
class CLabelMidElision;
class DiffHighlighter;

// The commit history of one repository, read-only. Owns its own Repository - it only ever asks
// read-only questions, and a submodule's history is opened without a CommitWindow on that submodule
// to borrow one from. Every job is scoped to this window, so closing it drops the pending queries
// instead of leaving them to finish against nothing.
class HistoryWindow final : public QMainWindow
{
	Q_OBJECT

public:
	HistoryWindow(const QString& repoPath, QWidget* parent);

	// Re-runs the log query from scratch. The commit window calls this after committing here.
	void reload();

protected:
	void closeEvent(QCloseEvent* event) override;

private:
	void buildUi();

	void showCommitContextMenu(const QPoint& pos);
	void showFilesForCurrentCommit();
	void showDiffForCurrentFile();
	// The two kinds of content the right-hand pane holds; each owns whether the diff highlighting applies
	void showCommitMessage(const CommitRecord& commit);
	void setDiffText(const QString& pathLabel, const QString& tag, const QString& text);

private:
	Repository _repo;
	CommitLogModel _logModel;
	CommitFilesModel _filesModel;

	// Widened by Load more. A date-ordered walk cannot be resumed from a cursor, so widening it
	// means re-running the whole query - see doc/ARCHITECTURE.md.
	int _maxCommits;

	QSplitter* _splitter = nullptr;       // log above, the commit's detail below
	QSplitter* _detailSplitter = nullptr; // file list beside the diff
	QTreeView* _logView = nullptr;
	QTreeView* _filesView = nullptr;
	QLabel* _countLabel = nullptr;
	QLabel* _fileCountLabel = nullptr;
	QPushButton* _loadMoreButton = nullptr;
	CLabelMidElision* _diffPathLabel = nullptr;
	QLabel* _diffTagLabel = nullptr;
	QPlainTextEdit* _diffView = nullptr;
	DiffHighlighter* _diffHighlighter = nullptr;

	QPointer<Git::Job> _logJob;
	QPointer<Git::Job> _filesJob;
	QPointer<Git::Job> _diffJob;
};
