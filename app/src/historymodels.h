#pragma once

#include "gitparsers.h"

#include <QAbstractTableModel>
#include <QSet>

#include <vector>

// The abbreviation every history view shows a sha as
[[nodiscard]] QString shortSha(const QString& sha);

// The commit list, in the order git walked it - newest first.
class CommitLogModel final : public QAbstractTableModel
{
	Q_OBJECT

public:
	enum Column { ShaColumn = 0, SubjectColumn, AuthorColumn, DateColumn, ColumnCount };

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

private:
	std::vector<CommitRecord> _commits;
	std::vector<QString> _displayedDates; // parallel to _commits; the age each shows is as of the load
	std::vector<int> _visible; // indexes into _commits, every one of them while the search is empty
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
	enum Column { StateColumn = 0, PathColumn, ColumnCount };

	explicit CommitFilesModel(QObject* parent = nullptr);

	void setEntries(std::vector<NameStatusEntry> entries);

	[[nodiscard]] const NameStatusEntry& entryAt(int row) const { return _entries[size_t(row)]; }

	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role) const override;

private:
	std::vector<NameStatusEntry> _entries;
};
