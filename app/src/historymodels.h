#pragma once

#include "commitgraph.h"
#include "vcstypes.h"

#include <QAbstractTableModel>
#include <QSet>

#include <map>
#include <optional>
#include <vector>

// The abbreviation every history view shows a sha as
[[nodiscard]] QString shortSha(const QString& sha);

// The commit list, in the order the backend walked it - newest first, and no commit before its children.
class CommitLogModel final : public QAbstractTableModel
{
	Q_OBJECT

public:
	// CommitColumn labels the row with whatever its system identifies a commit by - see CommitRecord::revision
	enum Column { GraphColumn = 0, CommitColumn, SubjectColumn, AuthorColumn, DateColumn, ColumnCount };
	// What CommitGraphDelegate paints from: this row's slice of the diagram, the lanes the whole list needs,
	// and whether its node stands for a commit no upstream has seen
	enum Role { GraphRole = Qt::UserRole, GraphLaneCountRole, UnpushedRole };

	explicit CommitLogModel(QObject* parent = nullptr);

	void setCommits(std::vector<CommitRecord> commits);
	// Hides every commit that does not contain `text` in its sha, author, refs, date or message.
	// Empty text shows all of them again.
	void setSearchText(const QString& text);
	// Marks the commits the upstream has not seen. Its query is separate from the log's, so this may
	// arrive before or after the commits, and it repaints rather than resets - a reset here would drop
	// a selection the user made while it was in flight.
	void setUnpushedShas(QSet<QString> shas);
	// The pickaxe's narrower half. Applied like the unpushed set - its own query, so it may land either
	// side of the commits, and it repaints rather than resets.
	void setAddingOrRemovingShas(QSet<QString> shas);

	[[nodiscard]] int addingOrRemovingCount() const; // of the rows on show
	// Of the set, the ones the listing does not contain at all: -S reaches inside binary files, where
	// -G has no patch text to match, so those commits have no row to mark.
	[[nodiscard]] int addingOrRemovingNotListedCount() const;

	// Row indexes address what is shown, so they mean the same thing with a search active as without.
	[[nodiscard]] const CommitRecord& commitAt(int row) const { return _commits[size_t(commitIndexAt(row))]; }
	[[nodiscard]] int totalCount() const { return int(_commits.size()); } // rowCount() is the shown count

	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
	void rebuildVisible();
	[[nodiscard]] int commitIndexAt(int row) const { return _visible[size_t(row)]; }
	// A search leaves the rows it hides out of the diagram, so the shown rows have one of their own
	[[nodiscard]] const GraphRow& graphRowAt(int row) const;

private:
	std::vector<CommitRecord> _commits;
	std::vector<QString> _displayedDates; // parallel to _commits; the age each shows is as of the load
	std::vector<int> _visible; // indexes into _commits, every one of them while the search is empty
	CommitGraph _graph;        // over _commits; _searchText is what decides which of the two the rows read
	CommitGraph _searchGraph;  // over _visible, built only while a search is narrowing it
	QString _searchText;
	QSet<QString> _unpushedShas;
	QSet<QString> _addingOrRemovingShas;
};

// The files one commit touched. ChangedFilesModel cannot serve here: its rows carry the state of a
// pending commit - check boxes, submodule blocking - which a commit already made has no notion of.
class CommitFilesModel final : public QAbstractTableModel
{
	Q_OBJECT

public:
	enum Column { StateColumn = 0, AddedColumn, RemovedColumn, PathColumn, ColumnCount };

	explicit CommitFilesModel(QObject* parent = nullptr);

	// The counts come from a query of their own and may land either side of the entries, so neither
	// setter disturbs what the other put there; clear() is what drops both between commits.
	void setEntries(std::vector<CommitFileChange> entries);
	void setLineCounts(std::map<QString, LineCounts> counts);
	void clear();

	[[nodiscard]] const CommitFileChange& entryAt(int row) const { return _entries[size_t(row)]; }

	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role) const override;

private:
	[[nodiscard]] std::optional<LineCounts> countsAt(int row) const;

private:
	std::vector<CommitFileChange> _entries;
	std::map<QString, LineCounts> _lineCounts; // by path; a file the query did not answer for has none
};
