#include "historymodels.h"
#include "changedfilesmodel.h"
#include "theme.h"

#include "settingsui/csettingsdialog.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QBrush>
#include <QDateTime>
#include <QFont>
RESTORE_COMPILER_WARNINGS

namespace {

QString countedUnit(int count, const char* unit)
{
	QString text = QString::number(count) + QLatin1Char(' ') + QLatin1String(unit);
	if (count != 1)
		text += QLatin1Char('s');
	return text;
}

// Two units of granularity, coarsening with age.
// Calendar arithmetic goes through QDate: "2 months" is two calendar months, not sixty days.
// A date ahead of the clock (skew, or a rebased commit keeping its author date) reads as "just now".
QString ageText(const QDateTime& when, const QDateTime& now)
{
	const qint64 minutes = when.secsTo(now) / 60;
	if (minutes < 1)
		return QStringLiteral("just now");
	if (minutes < 60)
		return countedUnit(int(minutes), "minute") + QStringLiteral(" ago");
	if (minutes < 60 * 24)
		return countedUnit(int(minutes / 60), "hour") + QStringLiteral(" ago");

	const QDate from = when.date();
	const QDate to = now.date();
	int months = (to.year() - from.year()) * 12 + to.month() - from.month();
	if (from.addMonths(months) > to)
		--months; // the day of the month has not come round yet

	if (months < 1)
		return countedUnit(int(from.daysTo(to)), "day") + QStringLiteral(" ago");

	const bool underAYear = months < 12;
	const QString lead = underAYear ? countedUnit(months, "month") : countedUnit(months / 12, "year");
	const int remainder = underAYear ? int(from.addMonths(months).daysTo(to)) : months % 12;
	if (remainder == 0)
		return lead + QStringLiteral(" ago");
	return lead + QStringLiteral(" and ") + countedUnit(remainder, underAYear ? "day" : "month") + QStringLiteral(" ago");
}

QString displayedDate(const QString& isoDate, const QDateTime& now)
{
	const QDateTime dateTime = QDateTime::fromString(isoDate, Qt::ISODate);
	if (!dateTime.isValid())
		return isoDate;

	const QDateTime local = dateTime.toLocalTime();
	return QStringLiteral("%1 (%2)").arg(local.toString(QStringLiteral("yyyy-MM-dd hh:mm")), ageText(local, now));
}

QString subjectText(const CommitRecord& commit)
{
	const QString subject = commit.subject();
	return commit.refs.isEmpty() ? subject : QStringLiteral("(%1) %2").arg(commit.refs, subject);
}

// A numbered commit's sha is in the tooltip and the pane below
QString commitIdText(const CommitRecord& commit)
{
	return commit.revision ? QString::number(*commit.revision) : shortSha(commit.sha);
}

// Cheapest fields first; the message is by far the longest
bool matchesSearch(const CommitRecord& commit, const QString& text)
{
	return commit.sha.contains(text, Qt::CaseInsensitive)
		|| (commit.revision && QString::number(*commit.revision).contains(text))
		|| commit.author.contains(text, Qt::CaseInsensitive)
		|| commit.refs.contains(text, Qt::CaseInsensitive)
		|| commit.date.contains(text, Qt::CaseInsensitive)
		|| commit.message.contains(text, Qt::CaseInsensitive);
}

QString pathText(const CommitFileChange& entry)
{
	return entry.oldPath.isEmpty() ? entry.path : QStringLiteral("%1 (was %2)").arg(entry.path, entry.oldPath);
}

} // namespace

QString shortSha(const QString& sha)
{
	return sha.left(8);
}

CommitLogModel::CommitLogModel(QObject* parent) :
	QAbstractTableModel(parent)
{
	// The monospace font comes from the settings. layoutChanged, unlike dataChanged, makes the view recompute
	// its cached uniform row height, and unlike a reset keeps the selection.
	connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, this, [this] {
		emit layoutAboutToBeChanged();
		emit layoutChanged();
	});
}

void CommitLogModel::setCommits(std::vector<CommitRecord> commits)
{
	beginResetModel();
	_commits = std::move(commits);

	_loadedAt = QDateTime::currentDateTime();
	_displayedDates.assign(_commits.size(), QString{});

	_graph = buildCommitGraph(_commits);
	rebuildVisible();
	endResetModel();
}

bool CommitLogModel::extendCommits(std::vector<CommitRecord> commits)
{
	bool isPrefix = commits.size() >= _commits.size();
	for (size_t i = 0; isPrefix && i < _commits.size(); ++i)
		isPrefix = commits[i].sha == _commits[i].sha;
	if (!isPrefix)
	{
		setCommits(std::move(commits));
		return false;
	}

	const size_t firstNew = _commits.size();
	if (!_searchText.isEmpty())
	{
		// A prefix commit's refs can change between the walks and flip its search match; the insert-rows
		// announcement below requires the shown prefix to stay identical
		std::vector<int> prefixVisible;
		prefixVisible.reserve(_visible.size());
		for (size_t i = 0; i < firstNew; ++i)
		{
			if (matchesSearch(commits[i], _searchText))
				prefixVisible.push_back(int(i));
		}
		if (prefixVisible != _visible)
		{
			setCommits(std::move(commits));
			return false;
		}
	}

	// The appended commits' visible rows form one block at the end, since _visible is in index order
	int appendedVisible = 0;
	for (size_t i = firstNew; i < commits.size(); ++i)
	{
		if (_searchText.isEmpty() || matchesSearch(commits[i], _searchText))
			++appendedVisible;
	}

	const int oldVisibleCount = int(_visible.size());
	if (appendedVisible > 0)
		beginInsertRows({}, oldVisibleCount, oldVisibleCount + appendedVisible - 1);

	_displayedDates.resize(commits.size()); // the new rows format lazily, against the same _loadedAt
	_commits = std::move(commits); // the prefix is the same commits, at worst with fresher refs
	_graph = buildCommitGraph(_commits);
	rebuildVisible();

	if (appendedVisible > 0)
		endInsertRows();
	// Deeper history can extend lanes through rows already shown, and a prefix row's refs may be fresher,
	// so every column repaints
	if (!_visible.empty())
		emit dataChanged(index(0, 0), index(int(_visible.size()) - 1, ColumnCount - 1));
	return true;
}

void CommitLogModel::setSearchText(const QString& text)
{
	if (_searchText == text)
		return;

	beginResetModel();
	_searchText = text;
	rebuildVisible();
	endResetModel();
}

void CommitLogModel::setUnpushedShas(QSet<QString> shas)
{
	if (_unpushedShas == shas)
		return;

	_unpushedShas = std::move(shas);
	if (!_visible.empty())
		emit dataChanged(index(0, 0), index(int(_visible.size()) - 1, ColumnCount - 1));
}

void CommitLogModel::setAddingOrRemovingShas(QSet<QString> shas)
{
	if (_addingOrRemovingShas == shas)
		return;

	_addingOrRemovingShas = std::move(shas);
	if (!_visible.empty())
		emit dataChanged(index(0, 0), index(int(_visible.size()) - 1, ColumnCount - 1));
}

int CommitLogModel::rowOfSha(const QString& sha) const
{
	for (size_t row = 0; row < _visible.size(); ++row)
	{
		if (_commits[size_t(_visible[row])].sha == sha)
			return int(row);
	}
	return -1;
}

int CommitLogModel::addingOrRemovingCount() const
{
	int count = 0;
	for (const int commitIndex : _visible)
		count += _addingOrRemovingShas.contains(_commits[size_t(commitIndex)].sha) ? 1 : 0;
	return count;
}

int CommitLogModel::addingOrRemovingNotListedCount() const
{
	int listed = 0;
	for (const CommitRecord& commit : _commits)
		listed += _addingOrRemovingShas.contains(commit.sha) ? 1 : 0;
	return int(_addingOrRemovingShas.size()) - listed;
}

void CommitLogModel::rebuildVisible()
{
	_visible.clear();
	_visible.reserve(_commits.size());
	for (int i = 0; i < int(_commits.size()); ++i)
	{
		if (_searchText.isEmpty() || matchesSearch(_commits[size_t(i)], _searchText))
			_visible.push_back(i);
	}
	_searchGraph = _searchText.isEmpty() ? CommitGraph{} : filteredCommitGraph(_graph, _visible);
}

const QString& CommitLogModel::displayedDateAt(size_t commitIndex) const
{
	QString& cached = _displayedDates[commitIndex];
	if (cached.isEmpty())
		cached = displayedDate(_commits[commitIndex].date, _loadedAt);
	return cached;
}

const GraphRow& CommitLogModel::graphRowAt(int row) const
{
	const CommitGraph& graph = _searchText.isEmpty() ? _graph : _searchGraph;
	return graph.rows[size_t(row)];
}

int CommitLogModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : int(_visible.size());
}

int CommitLogModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : ColumnCount;
}

QVariant CommitLogModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() >= int(_visible.size()))
		return {};

	const CommitRecord& commit = commitAt(index.row());
	const bool unpushed = _unpushedShas.contains(commit.sha);
	const bool addsOrRemoves = _addingOrRemovingShas.contains(commit.sha);

	switch (role)
	{
	case Qt::DisplayRole:
		switch (index.column())
		{
		case CommitColumn:  return commitIdText(commit);
		// On the subject, since the commit column already carries the unpushed mark and a commit can be both
		case SubjectColumn: return addsOrRemoves ? QStringLiteral("± ") + subjectText(commit) : subjectText(commit);
		case AuthorColumn:  return commit.author;
		case DateColumn:    return displayedDateAt(size_t(commitIndexAt(index.row())));
		}
		return {};
	case Qt::TextAlignmentRole:
		// Numbers align right, shas left
		if (index.column() == CommitColumn && commit.revision)
			return int(Qt::AlignRight | Qt::AlignVCenter);
		return {};
	case Qt::FontRole:
	{
		if (index.column() == SubjectColumn)
		{
			if (!addsOrRemoves)
				return {};
			QFont font = QApplication::font();
			font.setWeight(QFont::DemiBold);
			return font;
		}
		if (index.column() != CommitColumn)
			return {};
		QFont font = monospaceFont();
		if (unpushed)
			font.setWeight(QFont::DemiBold);
		return font;
	}
	case Qt::ForegroundRole:
		// The same accent as the commit window's ahead count
		if (index.column() == CommitColumn && unpushed)
			return QBrush{ activeTheme().palette.accentText };
		return index.column() == SubjectColumn ? QVariant{} : QVariant{ QBrush{ activeTheme().palette.textDim } };
	case Qt::ToolTipRole:
	{
		QString tooltip = commit.sha + QLatin1Char('\n') + commit.subject();
		if (commit.parents.size() > 1)
			tooltip += QStringLiteral("\nMerge of %1 parents").arg(commit.parents.size());
		if (unpushed)
			tooltip += QStringLiteral("\nNot pushed to the upstream yet");
		if (!_addingOrRemovingShas.isEmpty())
			tooltip += addsOrRemoves ? QStringLiteral("\nAdds or removes the search text")
				: QStringLiteral("\nChanges a line containing the search text");
		return tooltip;
	}
	case GraphRole:
		return QVariant::fromValue(graphRowAt(index.row()));
	case GraphLaneCountRole:
		// The unfiltered width, so typing in the search box does not resize the column
		return _graph.laneCount;
	case UnpushedRole:
		return unpushed;
	default:
		return {};
	}
}

QVariant CommitLogModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return {};

	switch (section)
	{
	case CommitColumn:  return tr("Commit");
	case SubjectColumn: return tr("Subject");
	case AuthorColumn:  return tr("Author");
	case DateColumn:    return tr("Date");
	}
	return {};
}

CommitFilesModel::CommitFilesModel(QObject* parent) :
	QAbstractTableModel(parent)
{
	connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, this, [this] {
		emit layoutAboutToBeChanged();
		emit layoutChanged();
	});
}

void CommitFilesModel::setEntries(std::vector<CommitFileChange> entries)
{
	beginResetModel();
	_entries = std::move(entries);
	endResetModel();
}

void CommitFilesModel::setLineCounts(std::map<QString, LineCounts> counts)
{
	_lineCounts = std::move(counts);
	if (!_entries.empty()) // repaint rather than reset, which would drop the picked file
		emit dataChanged(index(0, AddedColumn), index(int(_entries.size()) - 1, RemovedColumn), { Qt::DisplayRole });
}

void CommitFilesModel::clear()
{
	beginResetModel();
	_entries.clear();
	_lineCounts.clear();
	endResetModel();
}

std::optional<LineCounts> CommitFilesModel::countsAt(int row) const
{
	const auto it = _lineCounts.find(_entries[size_t(row)].path);
	return it != _lineCounts.end() ? std::optional{ it->second } : std::nullopt;
}

int CommitFilesModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : int(_entries.size());
}

int CommitFilesModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : FileListColumnCount;
}

QVariant CommitFilesModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() >= int(_entries.size()))
		return {};

	const CommitFileChange& entry = _entries[size_t(index.row())];

	switch (role)
	{
	case Qt::DisplayRole:
		switch (index.column())
		{
		case StateColumn:   return changeTypeText(entry.type);
		case AddedColumn:   return lineCountText(countsAt(index.row()), /*added=*/true);
		case RemovedColumn: return lineCountText(countsAt(index.row()), /*added=*/false);
		case PathColumn:    return pathText(entry);
		}
		return {};
	case Qt::ForegroundRole:
		if (index.column() == StateColumn)
			return QBrush{ changeTypeColor(entry.type) };
		return fileListSharedRoleData(index.column(), role, entry.isSubmodule, entry.type);
	case Qt::TextAlignmentRole:
	case Qt::DecorationRole:
	case Qt::FontRole:
		return fileListSharedRoleData(index.column(), role, entry.isSubmodule, entry.type);
	case SortRankRole:
		return changeTypeRank(entry.type);
	case SortPathRole:
		return entry.path;
	case Qt::ToolTipRole:
		return pathText(entry);
	default:
		return {};
	}
}

QVariant CommitFilesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	return fileListHeaderData(section, orientation, role);
}
