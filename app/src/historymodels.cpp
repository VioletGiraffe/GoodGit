#include "historymodels.h"
#include "changedfilesmodel.h"
#include "theme.h"

#include <QApplication>
#include <QBrush>
#include <QDateTime>
#include <QFont>

namespace {

QString displayedDate(const QString& isoDate)
{
	const QDateTime dateTime = QDateTime::fromString(isoDate, Qt::ISODate);
	return dateTime.isValid() ? dateTime.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm")) : isoDate;
}

QString subjectText(const CommitRecord& commit)
{
	return commit.refs.isEmpty() ? commit.subject : QStringLiteral("(%1) %2").arg(commit.refs, commit.subject);
}

QString pathText(const NameStatusEntry& entry)
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
	endResetModel();
}

int CommitLogModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : int(_commits.size());
}

int CommitLogModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : ColumnCount;
}

QVariant CommitLogModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() >= int(_commits.size()))
		return {};

	const CommitRecord& commit = _commits[size_t(index.row())];

	switch (role)
	{
	case Qt::DisplayRole:
		switch (index.column())
		{
		case ShaColumn:     return shortSha(commit.sha);
		case SubjectColumn: return subjectText(commit);
		case AuthorColumn:  return commit.author;
		case DateColumn:    return displayedDate(commit.date);
		}
		return {};
	case Qt::FontRole:
		return index.column() == ShaColumn ? QVariant{ monospaceFont() } : QVariant{};
	case Qt::ForegroundRole:
		return index.column() == SubjectColumn ? QVariant{} : QVariant{ QBrush{ activeTheme().dim } };
	case Qt::ToolTipRole:
		return commit.parents.size() > 1
			? QStringLiteral("%1\n%2\nMerge of %3 parents").arg(commit.sha, commit.subject).arg(commit.parents.size())
			: QStringLiteral("%1\n%2").arg(commit.sha, commit.subject);
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
	case ShaColumn:     return tr("Commit");
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

void CommitFilesModel::setEntries(std::vector<NameStatusEntry> entries)
{
	beginResetModel();
	_entries = std::move(entries);
	endResetModel();
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

	const NameStatusEntry& entry = _entries[size_t(index.row())];

	switch (role)
	{
	case Qt::DisplayRole:
		return index.column() == StateColumn ? changeTypeText(entry.type) : pathText(entry);
	case Qt::ForegroundRole:
		if (index.column() == StateColumn)
			return QBrush{ changeTypeColor(entry.type) };
		return {};
	case Qt::FontRole:
		if (index.column() == StateColumn)
		{
			QFont font = QApplication::font();
			font.setWeight(QFont::DemiBold);
			return font;
		}
		else
		{
			QFont font = monospaceFont();
			font.setStrikeOut(entry.type == ChangeType::Deleted);
			return font;
		}
	case Qt::ToolTipRole:
		return pathText(entry);
	default:
		return {};
	}
}
