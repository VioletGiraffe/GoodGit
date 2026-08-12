#include "changedfilesmodel.h"

#include <QApplication>
#include <QBrush>
#include <QFont>
#include <QPalette>
#include <QStyle>

#include <unordered_map>

namespace {

bool isDarkTheme()
{
	return QApplication::palette().color(QPalette::Base).lightness() < 128;
}

QColor stateColor(const FileEntry& entry)
{
	const bool dark = isDarkTheme();
	if (entry.isSubmodule)
	{
		if (entry.dirtyTrackedInside)
			return dark ? QColor(0xef, 0x8c, 0x82) : QColor(0xb8, 0x30, 0x2a);
		return dark ? QColor(0xdd, 0xa4, 0x5c) : QColor(0xa1, 0x5c, 0x00);
	}
	switch (entry.type)
	{
	case ChangeType::Modified:    return dark ? QColor(0x6c, 0xb0, 0xf0) : QColor(0x16, 0x68, 0xc4);
	case ChangeType::Added:       return dark ? QColor(0x62, 0xc9, 0x8a) : QColor(0x12, 0x78, 0x3c);
	case ChangeType::Untracked:   return dark ? QColor(0xd4, 0xb3, 0x52) : QColor(0x8a, 0x6a, 0x08);
	case ChangeType::Deleted:     return dark ? QColor(0xef, 0x8c, 0x82) : QColor(0xb8, 0x30, 0x2a);
	case ChangeType::Renamed:     return dark ? QColor(0xb3, 0x94, 0xef) : QColor(0x73, 0x45, 0xc0);
	case ChangeType::TypeChanged: return dark ? QColor(0xe0, 0xa0, 0x5a) : QColor(0xa3, 0x58, 0x00);
	case ChangeType::Conflicted:  return dark ? QColor(0xef, 0x8c, 0x82) : QColor(0xb8, 0x30, 0x2a);
	}
	return {};
}

QColor blockedRowTint()
{
	QColor warn = isDarkTheme() ? QColor(0x3a, 0x30, 0x13) : QColor(0xff, 0xf4, 0xd6);
	warn.setAlpha(160); // let selection and alternating base show through
	return warn;
}

} // namespace

ChangedFilesModel::ChangedFilesModel(QObject* parent) :
	QAbstractTableModel(parent)
{
}

void ChangedFilesModel::setEntries(const std::vector<FileEntry>& entries, bool mergeMode)
{
	std::unordered_map<QString, bool> previousChecks;
	for (const Row& row : _rows)
		previousChecks[row.entry.path] = row.checked;

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
		else if (!_loadedOnce)
			row.checked = entry.type != ChangeType::Untracked;

		_rows.push_back(std::move(row));
	}
	_loadedOnce = true;
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
	for (int row = 0; row < int(_rows.size()); ++row)
	{
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
		if (index.column() == PathColumn && entry.type == ChangeType::Deleted)
		{
			QFont font = QApplication::font();
			font.setStrikeOut(true);
			return font;
		}
		return {};
	case Qt::BackgroundRole:
		if (entry.isSubmodule && entry.dirtyTrackedInside)
			return QBrush{ blockedRowTint() };
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
	switch (entry.type)
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

QString ChangedFilesModel::pathText(const FileEntry& entry)
{
	QString text = entry.path;
	if (!entry.oldPath.isEmpty())
		text += QStringLiteral(" (was %1)").arg(entry.oldPath);
	if (entry.isSubmodule && entry.untrackedInside && !entry.dirtyTrackedInside)
		text += QStringLiteral(" (untracked files inside)");
	return text;
}
