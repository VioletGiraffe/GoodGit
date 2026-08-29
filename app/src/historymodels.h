#pragma once

#include "commitgraph.h"
#include "vcstypes.h"

DISABLE_COMPILER_WARNINGS
#include <QAbstractTableModel>
#include <QDateTime>
#include <QSet>
RESTORE_COMPILER_WARNINGS

#include <map>
#include <optional>
#include <vector>

[[nodiscard]] QString shortSha(const QString& sha);

// The commit list, in the backend's walk order: newest first, no commit before its children
class CommitLogModel final : public QAbstractTableModel
{
public:
	// CommitColumn shows the revision number where the system has one, else the short sha (see CommitRecord::revision)
	enum Column { GraphColumn = 0, CommitColumn, SubjectColumn, AuthorColumn, DateColumn, ColumnCount };
	// For CommitGraphDelegate: this row's slice of the diagram, the lane count of the whole list, and
	// whether the commit is unpushed
	enum Role { GraphRole = Qt::UserRole, GraphLaneCountRole, UnpushedRole };

	explicit CommitLogModel(QObject* parent = nullptr);

	void setCommits(std::vector<CommitRecord> commits);
	// For the same walk re-run with a longer limit: only the rows beyond the loaded prefix are inserted, so
	// the selection and scroll position survive.
	// Returns false if the prefix does not match, or an active search's matches within it changed (the
	// repository changed between the walks); the call was then an ordinary setCommits(), and the selection is
	// gone with the reset.
	[[nodiscard]] bool extendCommits(std::vector<CommitRecord> commits);
	// Hides every commit without `text` in its sha, author, refs, date or message. Empty text shows all.
	void setSearchText(const QString& text);
	// From a separate query, so it may arrive before or after the commits. Repaints rather than resets, so
	// a selection made while it was in flight survives.
	void setUnpushedShas(QSet<QString> shas);
	// The checked-out commit, from a query of its own like the unpushed set. Rebuilds the diagram: which
	// lines run above the checkout is part of it
	void setCurrentSha(QString sha);
	// The narrower half of a content search, applied like the unpushed set
	void setAddingOrRemovingShas(QSet<QString> shas);

	[[nodiscard]] int addingOrRemovingCount() const; // of the shown rows
	// Commits in the set the listing does not contain at all: -S reaches inside binary files, where -G has
	// no patch text to match
	[[nodiscard]] int addingOrRemovingNotListedCount() const;

	// Row indexes address the shown rows, with or without a search active
	[[nodiscard]] const CommitRecord& commitAt(int row) const { return _commits[size_t(commitIndexAt(row))]; }
	// -1 if not listed: the walk may not have covered that line of history, or it may be older than the limit
	[[nodiscard]] int rowOfSha(const QString& sha) const;
	[[nodiscard]] int totalCount() const { return int(_commits.size()); } // rowCount() is the shown count

	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
	void rebuildVisible();
	void rebuildSearchGraph(); // over the current _graph and _visible; empty while no search is active
	[[nodiscard]] int commitIndexAt(int row) const { return _visible[size_t(row)]; }
	[[nodiscard]] const QString& displayedDateAt(size_t commitIndex) const;
	[[nodiscard]] const GraphRow& graphRowAt(int row) const;

private:
	std::vector<CommitRecord> _commits;
	// Parallel to _commits, formatted on first paint: only visible rows need it, and eager formatting is the
	// bulk of a 20k-row reset. One clock reading (_loadedAt) keeps the ages comparable.
	mutable std::vector<QString> _displayedDates;
	QDateTime _loadedAt;
	std::vector<int> _visible; // indexes into _commits; all of them while the search is empty
	CommitGraph _graph;        // over _commits
	CommitGraph _searchGraph;  // over _visible, built only while a search is active
	QString _searchText;
	QString _currentSha;
	QSet<QString> _unpushedShas;
	QSet<QString> _addingOrRemovingShas;
};

// The files one commit touched, in the shared FileListColumn layout. ChangedFilesModel does not fit here:
// its rows carry pending-commit state (check boxes, submodule blocking) that a commit already made has no
// notion of.
class CommitFilesModel final : public QAbstractTableModel
{
public:
	explicit CommitFilesModel(QObject* parent = nullptr);

	// The counts come from a separate query and may land before or after the entries, so neither setter
	// disturbs the other's data; clear() drops both
	void setEntries(std::vector<CommitFileChange> entries);
	void setLineCounts(std::map<QString, LineCounts> counts);
	void clear();

	[[nodiscard]] const CommitFileChange& entryAt(int row) const { return _entries[size_t(row)]; }

	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
	[[nodiscard]] std::optional<LineCounts> countsAt(int row) const;

private:
	std::vector<CommitFileChange> _entries;
	std::map<QString, LineCounts> _lineCounts; // by path; absent where the query did not answer
};
