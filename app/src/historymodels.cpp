#include "historymodels.h"
#include "changedfilesmodel.h"
#include "theme.h"

#include <QApplication>
#include <QBrush>
#include <QDateTime>
#include <QFont>

namespace {

QString countedUnit(int count, const char* unit)
{
	QString text = QString::number(count) + QLatin1Char(' ') + QLatin1String(unit);
	if (count != 1)
		text += QLatin1Char('s');
	return text;
}

// Age at two units of granularity, coarsening as it grows. The calendar parts go through QDate, so
// "2 months" is two calendar months rather than sixty days. A date ahead of the clock - skew, or a
// rebased commit keeping its author date - reads as "just now" rather than as a negative count.
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

// What the row is labelled by: the system's own number for the commit where there is one, the abbreviated
// sha where there is not. Never both - a numbered commit's sha is on the row's tooltip and in the pane below.
QString commitIdText(const CommitRecord& commit)
{
	return commit.revision ? QString::number(*commit.revision) : shortSha(commit.sha);
}

// Cheapest fields first: || stops before scanning the message, which is far the longest of them
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
}

void CommitLogModel::setCommits(std::vector<CommitRecord> commits)
{
	beginResetModel();
	_commits = std::move(commits);

	// Formatted once, against one clock reading: parsing the date per paint costs more than the whole
	// list is worth, and rows sharing an instant is what makes their ages comparable
	const QDateTime now = QDateTime::currentDateTime();
	_displayedDates.clear();
	_displayedDates.reserve(_commits.size());
	for (const CommitRecord& commit : _commits)
		_displayedDates.push_back(displayedDate(commit.date, now));

	_graph = buildCommitGraph(_commits);
	rebuildVisible();
	endResetModel();
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
		// The mark rides the subject because the commit column already carries the unpushed one, and a
		// commit can be both
		case SubjectColumn: return addsOrRemoves ? QStringLiteral("± ") + subjectText(commit) : subjectText(commit);
		case AuthorColumn:  return commit.author;
		case DateColumn:    return _displayedDates[size_t(commitIndexAt(index.row()))];
		}
		return {};
	case Qt::TextAlignmentRole:
		// A number reads by magnitude, so it lines up on the right; a sha reads by its leading characters
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
		// Unpushed commits wear the same accent the commit window's ahead count does
		if (index.column() == CommitColumn && unpushed)
			return QBrush{ activeTheme().accent };
		return index.column() == SubjectColumn ? QVariant{} : QVariant{ QBrush{ activeTheme().dim } };
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
		// The unfiltered width in either case, so typing in the search box does not resize the column
		return _graph.laneCount;
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
	if (!_entries.empty()) // repaint rather than reset: a reset here would drop the file the user picked
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
	return parent.isValid() ? 0 : ColumnCount;
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
	case Qt::TextAlignmentRole:
		if (index.column() == AddedColumn || index.column() == RemovedColumn)
			return int(Qt::AlignRight | Qt::AlignVCenter);
		return {};
	case Qt::ForegroundRole:
		if (index.column() == StateColumn)
			return QBrush{ changeTypeColor(entry.type) };
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
		else if (index.column() == PathColumn)
		{
			QFont font = monospaceFont();
			font.setStrikeOut(entry.type == ChangeType::Deleted);
			return font;
		}
		return monospaceFont(); // the counts, where digits of one width line up down the column
	case Qt::ToolTipRole:
		return pathText(entry);
	default:
		return {};
	}
}
