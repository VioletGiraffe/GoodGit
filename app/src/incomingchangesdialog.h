#pragma once

#include "repository.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
#include <QElapsedTimer>
RESTORE_COMPILER_WARNINGS

#include <expected>
#include <map>
#include <memory>
#include <vector>

class QLabel;
class QProgressBar;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

// How many incoming commits one repository lists; its count covers them all
inline constexpr int MaxListedIncomingCommits = 200;

// The manual incoming check: the repository, and each cloned submodule one level down. Nested submodules are not visited.
// Opened with open(): window-modal, without the nested event loop exec() would spin.
// Rows are added as answers arrive. Closing drops the submodule checks still running.
class IncomingChangesDialog final : public QDialog
{
public:
	// Checks the submodules itself. The repository's own answer arrives through setOwnAnswer(): its check is the
	// commit window's, which the header shows.
	IncomingChangesDialog(Repository& repository, QWidget* parent);

	// `total`: the behind count, which the capped list may fall short of. Only the first answer is shown.
	void setOwnAnswer(const std::expected<std::vector<CommitRecord>, QString>& commits, int total);

private:
	struct SubmoduleCheck
	{
		QString path; // repo-relative
		RepositoryLocation location;
		std::unique_ptr<Repository> repository; // created when its check starts
		bool answered = false;
		bool hasRow = false; // something to pull, or a failure; one up to date is listed under the summary row
	};

	void startSubmoduleChecks(const std::expected<std::map<QString, QString>, QString>& updateBranches);
	void recordSubmoduleAnswer(size_t index, const std::expected<std::vector<CommitRecord>, QString>& commits);
	void updateProgress();

private:
	const QString _ownName;
	const QString _ownUpstream;
	std::vector<SubmoduleCheck> _submodules; // in path order
	int _answeredCount = 0; // the repository's own answer included
	bool _ownAnswered = false;

	QElapsedTimer _elapsed;
	QTimer* _ticker = nullptr; // keeps the elapsed time moving while nothing answers
	QLabel* _statusLabel = nullptr;
	QProgressBar* _progressBar = nullptr;
	QLabel* _waitingLabel = nullptr;
	QTreeWidget* _tree = nullptr;
	QTreeWidgetItem* _ownRow = nullptr;      // always the first row
	QTreeWidgetItem* _upToDateRow = nullptr; // always the last row; created with the first submodule found up to date
};
