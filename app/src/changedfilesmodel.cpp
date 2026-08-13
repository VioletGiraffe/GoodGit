#include "changedfilesmodel.h"
#include "theme.h"

#include <QApplication>
#include <QBrush>
#include <QFont>
#include <QStyle>

#include <unordered_map>

namespace {

QColor stateColor(const FileEntry& entry)
{
	if (!entry.isSubmodule)
		return changeTypeColor(entry.type);

	const Theme& t = activeTheme();
	return entry.dirtyTrackedInside ? t.stDeleted : t.stSubmodule;
}

} // namespace

QString changeTypeText(ChangeType type)
{
	switch (type)
	{
	case ChangeType::Modified:    return QStringLiteral("Modified");
	case ChangeType::Added:       return QStringLiteral("Added");
	case ChangeType::Untracked:   return QStringLiteral("Untracked");
	case ChangeType::Deleted:     return QStringLiteral("Deleted");
	case ChangeType::Renamed:     return QStringLiteral("Renamed");
	case ChangeType::TypeChanged: return QStringLiteral("Type changed");
	case ChangeType::Conflicted:  return QStringLiteral("Conflicted");
	}
	return {};
}

QColor changeTypeColor(ChangeType type)
{
	const Theme& t = activeTheme();
	switch (type)
	{
	case ChangeType::Modified:    return t.stModified;
	case ChangeType::Added:       return t.stAdded;
	case ChangeType::Untracked:   return t.stUntracked;
	case ChangeType::Deleted:     return t.stDeleted;
	case ChangeType::Renamed:     return t.stRenamed;
	case ChangeType::TypeChanged: return t.stSubmodule; // same amber family
	case ChangeType::Conflicted:  return t.stDeleted;
	}
	return {};
}

ChangedFilesModel::ChangedFilesModel(QObject* parent) :
	QAbstractTableModel(parent)
{
}

void ChangedFilesModel::setEntries(const std::vector<FileEntry>& entries, bool mergeMode)
{
	std::unordered_map<QString, bool> previousChecks;
	for (const Row& row : _rows)
	{
		if (row.entry.committable()) // an unchecked non-committable row records the block, not a user choice
			previousChecks[row.entry.path] = row.checked;
	}

	beginResetModel();
	_rows.clear();
	_rows.reserve(entries.size());
	for (const FileEntry& entry : entries)
	{
		Row row{ .entry = entry };
		if (mergeMode)
		{
			// All tracked changes go into the merge commit; untracked files keep their checkboxes
			row.forced = entry.type != ChangeType::Untracked;
			if (row.forced)
				row.checked = true;
			else if (const auto it = previousChecks.find(entry.path); it != previousChecks.end())
				row.checked = it->second;
		}
		else if (!entry.committable())
			row.checked = false;
		else if (const auto it = previousChecks.find(entry.path); it != previousChecks.end())
			row.checked = it->second;
		else
			row.checked = entry.type != ChangeType::Untracked;

		_rows.push_back(std::move(row));
	}
	endResetModel();
	emit checksChanged();
}

bool ChangedFilesModel::isUserCheckable(int row) const
{
	const Row& r = _rows[size_t(row)];
	return !r.forced && r.entry.committable();
}

int ChangedFilesModel::checkedCount() const
{
	int count = 0;
	for (const Row& row : _rows)
		count += row.checked ? 1 : 0;
	return count;
}

int ChangedFilesModel::checkableCount() const
{
	int count = 0;
	for (const Row& row : _rows)
		count += (row.forced || row.entry.committable()) ? 1 : 0;
	return count;
}

QStringList ChangedFilesModel::checkedPathspec() const
{
	QStringList paths;
	for (const Row& row : _rows)
	{
		if (!row.checked)
			continue;
		paths.push_back(row.entry.path);
		if (!row.entry.oldPath.isEmpty())
			paths.push_back(row.entry.oldPath); // a rename must carry both paths or the deletion of the old name is lost
	}
	return paths;
}

QStringList ChangedFilesModel::checkedUntrackedPaths() const
{
	QStringList paths;
	for (const Row& row : _rows)
	{
		if (row.checked && row.entry.type == ChangeType::Untracked)
			paths.push_back(row.entry.path);
	}
	return paths;
}

void ChangedFilesModel::setRowChecked(int row, bool checked)
{
	if (!isUserCheckable(row) || _rows[size_t(row)].checked == checked)
		return;
	_rows[size_t(row)].checked = checked;
	emit dataChanged(index(row, StateColumn), index(row, StateColumn), { Qt::CheckStateRole });
	emit checksChanged();
}

void ChangedFilesModel::setAllChecked(bool checked)
{
	applyChecked([checked](const FileEntry&) { return checked; });
}

void ChangedFilesModel::checkAllExceptUntracked()
{
	applyChecked([](const FileEntry& entry) { return entry.type != ChangeType::Untracked; });
}

void ChangedFilesModel::applyChecked(const std::function<bool(const FileEntry&)>& shouldCheck)
{
	for (int row = 0; row < int(_rows.size()); ++row)
	{
		const bool checked = shouldCheck(_rows[size_t(row)].entry);
		if (isUserCheckable(row) && _rows[size_t(row)].checked != checked)
		{
			_rows[size_t(row)].checked = checked;
			emit dataChanged(index(row, StateColumn), index(row, StateColumn), { Qt::CheckStateRole });
		}
	}
	emit checksChanged();
}

int ChangedFilesModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : int(_rows.size());
}

int ChangedFilesModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : ColumnCount;
}

QVariant ChangedFilesModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() >= int(_rows.size()))
		return {};

	const Row& row = _rows[size_t(index.row())];
	const FileEntry& entry = row.entry;

	switch (role)
	{
	case Qt::DisplayRole:
		return index.column() == StateColumn ? stateText(entry) : pathText(entry);
	case Qt::CheckStateRole:
		if (index.column() == StateColumn && (row.forced || entry.committable()))
			return row.checked ? Qt::Checked : Qt::Unchecked;
		return {};
	case Qt::DecorationRole:
		if (index.column() == StateColumn && entry.isSubmodule)
			return QApplication::style()->standardIcon(QStyle::SP_DirIcon);
		return {};
	case Qt::ForegroundRole:
		if (index.column() == StateColumn)
			return QBrush{ stateColor(entry) };
		return {};
	case Qt::FontRole:
		if (index.column() == StateColumn)
		{
			QFont font = QApplication::font();
			font.setWeight(QFont::DemiBold);
			return font;
		}
		else // the path: monospace, struck through when deleted (FileListDelegate recolors the strike)
		{
			QFont font = monospaceFont();
			font.setStrikeOut(entry.type == ChangeType::Deleted);
			return font;
		}
	case Qt::BackgroundRole:
		if (entry.isSubmodule && entry.dirtyTrackedInside)
			return QBrush{ activeTheme().blockedRowTint() };
		return {};
	case Qt::ToolTipRole:
		if (entry.isSubmodule)
			return entry.dirtyTrackedInside
				? QStringLiteral("Commit or discard the changes inside this submodule first. Double-click to open it.")
				: QStringLiteral("Double-click to open this submodule.");
		return pathText(entry);
	default:
		return {};
	}
}

bool ChangedFilesModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
	if (role != Qt::CheckStateRole || index.column() != StateColumn)
		return false;
	setRowChecked(index.row(), value.toInt() == Qt::Checked);
	return true;
}

Qt::ItemFlags ChangedFilesModel::flags(const QModelIndex& index) const
{
	Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
	if (index.column() == StateColumn && index.isValid() && index.row() < int(_rows.size()) && isUserCheckable(index.row()))
		flags |= Qt::ItemIsUserCheckable;
	return flags;
}

QString ChangedFilesModel::stateText(const FileEntry& entry)
{
	if (entry.isSubmodule)
	{
		if (!entry.pointerMoved)
			return QStringLiteral("Uncommitted inside");
		return entry.dirtyTrackedInside ? QStringLiteral("Submodule - blocked") : QStringLiteral("Submodule");
	}
	return changeTypeText(entry.type);
}

QString ChangedFilesModel::pathText(const FileEntry& entry)
{
	QString text = entry.path;
	if (!entry.oldPath.isEmpty())
		text += QStringLiteral(" (was %1)").arg(entry.oldPath);
	if (entry.isSubmodule && entry.untrackedInside && !entry.dirtyTrackedInside)
		text += QStringLiteral(" (untracked files inside)");
	return text;
}
