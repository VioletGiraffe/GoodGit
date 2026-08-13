#pragma once

#include "gitparsers.h"

#include <QAbstractTableModel>

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

	[[nodiscard]] const CommitRecord& commitAt(int row) const { return _commits[size_t(row)]; }
	[[nodiscard]] int commitCount() const { return int(_commits.size()); }

	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
	std::vector<CommitRecord> _commits;
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
