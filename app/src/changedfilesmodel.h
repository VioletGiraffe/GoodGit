#pragma once

#include "repository.h"

#include <QAbstractTableModel>

#include <functional>
#include <vector>

// The checkable file list. Columns: 0 = checkbox + icon + state text, 1 = path.
// Row styling comes from the theme via item data roles (per-state colors, fonts, the folder icon,
// the blocked-row tint); FileListDelegate paints what roles cannot express - the red recolor of the
// deleted strikethrough and the selected-row accent stripe.
class ChangedFilesModel final : public QAbstractTableModel
{
	Q_OBJECT

public:
	enum Column { StateColumn = 0, PathColumn = 1, ColumnCount };

	explicit ChangedFilesModel(QObject* parent = nullptr);

	// Rebuilds the rows. Check state is re-derived by path: persisting rows keep their state, new rows
	// default to checked unless untracked. In merge mode all tracked rows are forced on and not
	// user-changeable (B1).
	void setEntries(const std::vector<FileEntry>& entries, bool mergeMode);

	[[nodiscard]] const FileEntry& entryAt(int row) const { return _rows[size_t(row)].entry; }
	[[nodiscard]] bool isChecked(int row) const { return _rows[size_t(row)].checked; }
	[[nodiscard]] bool isUserCheckable(int row) const;

	[[nodiscard]] int checkedCount() const;
	[[nodiscard]] int checkableCount() const;

	// All checked paths as a commit pathspec - includes both sides of every rename
	[[nodiscard]] QStringList checkedPathspec() const;
	[[nodiscard]] QStringList checkedUntrackedPaths() const;

	void setRowChecked(int row, bool checked);
	void setAllChecked(bool checked);
	void checkAllExceptUntracked();

	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role) const override;
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
