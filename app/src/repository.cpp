#include "repository.h"

#include <QFileInfo>

#include <utility>

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
		_state.readFailure = std::move(state.readFailure); // the rest of it is the last run that answered in full

	_refreshing = false;
	emit refreshed();

	if (_refreshPending)
	{
		_refreshPending = false;
		refresh();
	}
}
