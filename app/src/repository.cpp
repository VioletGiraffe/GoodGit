#include "repository.h"

#include <QFileInfo>

#include <utility>

QString escapedForRegex(const QString& literal)
{
	static const QString metacharacters = QStringLiteral(".^$*+?()[]{}|\\");

	QString escaped;
	escaped.reserve(literal.size() * 2);
	for (const QChar c : literal)
	{
		if (metacharacters.contains(c))
			escaped += QLatin1Char('\\');
		escaped += c;
	}
	return escaped;
}

bool sameRepositoryPath(const QString& left, const QString& right)
{
	return left.compare(right, Qt::CaseInsensitive) == 0;
}

Repository::Repository(QString rootPath, QObject* parent) :
	QObject(parent),
	_rootPath{ std::move(rootPath) }
{
}

QString Repository::name() const
{
	return QFileInfo{ _rootPath }.fileName();
}

void Repository::refresh()
{
	if (_refreshing)
	{
		_refreshPending = true;
		return;
	}

	_refreshing = true;
	startRefresh();
}

void Repository::completeRefresh(RepoState state, std::vector<FileEntry> files)
{
	if (state.known())
	{
		_state = std::move(state);
		_files = std::move(files);
	}
	else
		_state.readFailure = std::move(state.readFailure); // the rest stays from the last successful run

	_refreshing = false;
	emit refreshed();

	if (_refreshPending)
	{
		_refreshPending = false;
		refresh();
	}
}
