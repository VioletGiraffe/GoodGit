#include "repository.h"

DISABLE_COMPILER_WARNINGS
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

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

SubmoduleDiscardPlan discardPlanFor(const std::vector<CommitFileChange>& changes, bool nestedSubmoduleChanged)
{
	if (nestedSubmoduleChanged)
		return { .refusal = QObject::tr("It has a submodule of its own that is uncommitted or has moved. Open it and discard there.") };

	SubmoduleDiscardPlan plan;
	for (const CommitFileChange& change : changes)
	{
		if (change.type == ChangeType::Untracked)
			continue; // not in version control: nothing to restore, nothing to take out of it
		if (change.type == ChangeType::Added)
		{
			plan.keptOnDisk.push_back(change.path);
			continue;
		}

		plan.restored.push_back(change.oldPath.isEmpty() ? change.path : change.oldPath);
		if (!change.oldPath.isEmpty())
			plan.keptOnDisk.push_back(change.path); // a rename's new name is in no commit
	}

	if (plan.restored.isEmpty() && plan.keptOnDisk.isEmpty())
		return { .refusal = QObject::tr("What is uncommitted there is inside a submodule of its own. Open it and discard there.") };
	return plan;
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
		++_refreshGeneration;
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
