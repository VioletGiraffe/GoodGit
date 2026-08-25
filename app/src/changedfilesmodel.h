#pragma once

#include "vcstypes.h"

DISABLE_COMPILER_WARNINGS
#include <QAbstractTableModel>
#include <QColor>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <vector>

// Shared with the history window's file list, so a row looks the same in both
[[nodiscard]] QString changeTypeText(ChangeType type);
[[nodiscard]] QColor changeTypeColor(ChangeType type);
// Where a change type sorts when the list is ordered by status: lower first
[[nodiscard]] int changeTypeRank(ChangeType type);
// A submodule whose content blocks its pointer sorts ahead of every change type: it stops the commit
inline constexpr int BlockedSubmoduleRank = -1;

// For the two line-count columns; a file without counts gets an empty cell rather than a zero
[[nodiscard]] QString lineCountText(const std::optional<LineCounts>& counts, bool added);
[[nodiscard]] QColor lineCountColor(bool added);

// The layout of both file lists: checkbox + icon + state text, lines added, lines removed, path.
// The counts take a column each so that a data role can color them.
enum FileListColumn { StateColumn = 0, AddedColumn, RemovedColumn, PathColumn, FileListColumnCount };

// The sort keys, each a property of the row rather than of one column: the sort proxy orders by these,
// not by the text the columns show
enum FileListRole { SortRankRole = Qt::UserRole, SortPathRole };

// The headings both lists show, so their columns read the same
[[nodiscard]] QVariant fileListHeaderData(int section, Qt::Orientation orientation, int role);

// The checkable file list.
// Row styling comes from the theme via item data roles; FileListDelegate paints what roles cannot express.
class ChangedFilesModel final : public QAbstractTableModel
{
	Q_OBJECT

public:
	explicit ChangedFilesModel(QObject* parent = nullptr);

	// Rebuilds the rows. Check state is carried over by path; new rows follow the NewRowCheckPolicy setting.
	// A row that was not committable counts as new: its unchecked state was the block, not a choice.
	// In merge mode all tracked rows are forced on and not user-changeable.
	void setEntries(const std::vector<FileEntry>& entries, bool mergeMode);

	[[nodiscard]] const FileEntry& entryAt(int row) const { return _rows[size_t(row)].entry; }
	[[nodiscard]] bool isChecked(int row) const { return _rows[size_t(row)].checked; }
	[[nodiscard]] bool isUserCheckable(int row) const;

	[[nodiscard]] int checkedCount() const;
	[[nodiscard]] int checkableCount() const;
	// Across the checked rows; nothing when none of them has counts. Rows without counts are left out rather
	// than counted as zero, as their own cells are.
	[[nodiscard]] std::optional<LineCounts> checkedLineTotals() const;

	// Includes both sides of every rename
	[[nodiscard]] QStringList checkedPathspec() const;
	[[nodiscard]] QStringList checkedUntrackedPaths() const;
	// Every row still reading Conflicted. A merge commit takes all of them, so one left here blocks committing.
	[[nodiscard]] QStringList unresolvedConflictPaths() const;

	void setRowChecked(int row, bool checked);
	void setAllChecked(bool checked);
	void checkAllExceptUntracked();

	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
	bool setData(const QModelIndex& index, const QVariant& value, int role) override;
	Qt::ItemFlags flags(const QModelIndex& index) const override;

signals:
	void checksChanged();

private:
	struct Row
	{
		FileEntry entry;
		bool checked = false;
		bool forced = false; // merge mode: shown checked, not user-changeable
	};

	[[nodiscard]] static QString stateText(const FileEntry& entry);
	[[nodiscard]] static QString pathText(const FileEntry& entry);

	void applyChecked(const std::function<bool(const FileEntry&)>& shouldCheck);

private:
	std::vector<Row> _rows;
};
