#include "changedfilesmodel.h"
#include "settings.h"
#include "theme.h"

#include "settings/csettings.h"
#include "settingsui/csettingsdialog.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QBrush>
#include <QFont>
RESTORE_COMPILER_WARNINGS

#include <unordered_map>

namespace {

QColor stateColor(const FileEntry& entry)
{
	if (!entry.isSubmodule)
		return changeTypeColor(entry.type);

	const Theme& t = activeTheme();
	return entry.contentBlocksPointer() ? t.stDeleted : t.stSubmodule;
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
	case ChangeType::Untracked:   return t.palette.textDim; // dim on purpose: not a tracked state
	case ChangeType::Deleted:     return t.stDeleted;
	case ChangeType::Renamed:     return t.stRenamed;
	case ChangeType::TypeChanged: return t.stSubmodule; // same amber family
	case ChangeType::Conflicted:  return t.stDeleted;
	}
	return {};
}

int changeTypeRank(ChangeType type)
{
	// What blocks or needs attention first, the ordinary edits next, what is not tracked last
	switch (type)
	{
	case ChangeType::Conflicted:  return 0;
	case ChangeType::Modified:    return 1;
	case ChangeType::Added:       return 2;
	case ChangeType::Renamed:     return 3;
	case ChangeType::TypeChanged: return 4;
	case ChangeType::Deleted:     return 5;
	case ChangeType::Untracked:   return 6;
	}
	return {};
}

QString lineCountText(const std::optional<LineCounts>& counts, bool added)
{
	if (!counts)
		return {};
	return added ? QStringLiteral("+%1").arg(counts->added) : QStringLiteral("-%1").arg(counts->removed);
}

QColor lineCountColor(bool added)
{
	const Theme& t = activeTheme();
	return added ? t.stAdded : t.stDeleted;
}

QVariant fileListHeaderData(int section, Qt::Orientation orientation, int role)
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return {};

	switch (section)
	{
	case StateColumn: return QObject::tr("Status");
	case PathColumn:  return QObject::tr("Path");
	}
	return {}; // the count columns carry no heading: the signs are in the counts, and neither column sorts
}

ChangedFilesModel::ChangedFilesModel(QObject* parent) :
	QAbstractTableModel(parent)
{
	// The monospace font comes from the settings. layoutChanged, unlike dataChanged, makes the view recompute
	// its cached uniform row height, and unlike a reset keeps the selection.
	connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, this, [this] {
		emit layoutAboutToBeChanged();
		emit layoutChanged();
	});
}

void ChangedFilesModel::setEntries(const std::vector<FileEntry>& entries, bool mergeMode)
{
	std::unordered_map<QString, bool> previousChecks;
	for (const Row& row : _rows)
	{
		if (row.entry.committable()) // a non-committable row's unchecked state is the block, not a user choice
			previousChecks[row.entry.path] = row.checked;
	}

	const QString newRowCheckPolicy = CSettings{}.value(Settings::NewRowCheckPolicyKey).toString();

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
		else if (newRowCheckPolicy == QLatin1String(Settings::NewRowCheckPolicyAll))
			row.checked = true;
		else if (newRowCheckPolicy == QLatin1String(Settings::NewRowCheckPolicyNone))
			row.checked = false;
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

QStringList ChangedFilesModel::unresolvedConflictPaths() const
{
	QStringList paths;
	for (const Row& row : _rows)
	{
		if (row.entry.type == ChangeType::Conflicted)
			paths.push_back(row.entry.path);
	}
	return paths;
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

std::optional<LineCounts> ChangedFilesModel::checkedLineTotals() const
{
	std::optional<LineCounts> totals;
	for (const Row& row : _rows)
	{
		if (!row.checked || !row.entry.lineCounts)
			continue;

		if (!totals)
			totals.emplace();
		totals->added += row.entry.lineCounts->added;
		totals->removed += row.entry.lineCounts->removed;
	}
	return totals;
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
			paths.push_back(row.entry.oldPath); // or the deletion of the old name is lost
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
	return parent.isValid() ? 0 : FileListColumnCount;
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
		switch (index.column())
		{
		case StateColumn:   return stateText(entry);
		case AddedColumn:   return lineCountText(entry.lineCounts, /*added=*/true);
		case RemovedColumn: return lineCountText(entry.lineCounts, /*added=*/false);
		case PathColumn:    return pathText(entry);
		}
		return {};
	case Qt::TextAlignmentRole:
		if (index.column() == AddedColumn || index.column() == RemovedColumn)
			return int(Qt::AlignRight | Qt::AlignVCenter);
		return {};
	case Qt::CheckStateRole:
		if (index.column() == StateColumn && (row.forced || entry.committable()))
			return row.checked ? Qt::Checked : Qt::Unchecked;
		return {};
	case Qt::DecorationRole:
		if (index.column() == StateColumn && entry.isSubmodule)
			return submoduleIcon();
		return {};
	case Qt::ForegroundRole:
		if (index.column() == StateColumn)
			return QBrush{ stateColor(entry) };
		if (index.column() == AddedColumn || index.column() == RemovedColumn)
			return QBrush{ lineCountColor(index.column() == AddedColumn) };
		return {};
	case Qt::FontRole:
		if (index.column() == StateColumn)
		{
			QFont font = QApplication::font();
			font.setWeight(QFont::DemiBold);
			return font;
		}
		else if (index.column() == PathColumn) // FileListDelegate recolors the strikethrough
		{
			QFont font = monospaceFont();
			font.setStrikeOut(entry.type == ChangeType::Deleted);
			return font;
		}
		return monospaceFont(); // the counts line up down the column
	case Qt::BackgroundRole:
		if (entry.isSubmodule && entry.contentBlocksPointer())
			return QBrush{ activeTheme().blockedRowTint() };
		return {};
	case SortRankRole:
		return entry.isSubmodule && entry.contentBlocksPointer() ? BlockedSubmoduleRank : changeTypeRank(entry.type);
	case SortPathRole:
		return entry.path;
	case Qt::ToolTipRole:
		if (!entry.isSubmodule)
			return pathText(entry);
		if (entry.content == SubmoduleContent::Unknown)
			return QStringLiteral("Could not read what is inside this submodule, so its pointer cannot be committed "
				"or discarded. Double-click to open it.");
		return entry.content == SubmoduleContent::DirtyTracked
			? QStringLiteral("Commit or discard the changes inside this submodule first. Double-click to open it.")
			: QStringLiteral("Double-click to open this submodule.");
	default:
		return {};
	}
}

QVariant ChangedFilesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	return fileListHeaderData(section, orientation, role);
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
	if (!entry.isSubmodule)
		return changeTypeText(entry.type);

	if (entry.content == SubmoduleContent::Unknown)
		return QStringLiteral("Submodule - unreadable");
	if (!entry.pointerMoved)
		return QStringLiteral("Uncommitted inside");
	return entry.content == SubmoduleContent::DirtyTracked ? QStringLiteral("Submodule - blocked") : QStringLiteral("Submodule");
}

QString ChangedFilesModel::pathText(const FileEntry& entry)
{
	QString text = entry.path;
	if (!entry.oldPath.isEmpty())
		text += QStringLiteral(" (was %1)").arg(entry.oldPath);
	if (entry.isSubmodule && entry.content == SubmoduleContent::Untracked)
		text += QStringLiteral(" (untracked files inside)");
	return text;
}
