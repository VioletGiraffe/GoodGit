#include "recentrepositories.h"
#include "repositoryfactory.h"
#include "repositorywindows.h"
#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QIcon>

int main(int argc, char* argv[])
{
	QApplication app{ argc, argv };
	QApplication::setOrganizationName(QStringLiteral("GoodGit"));
	QApplication::setApplicationName(QStringLiteral("GoodGit"));
	QApplication::setWindowIcon(QIcon{ QStringLiteral(":/goodgit.svg") });
	applyTheme(app);

	// Not argv, which on Windows arrives in the local codepage and mangles anything outside it
	const QStringList arguments = QApplication::arguments();

	// A named path is a claim about that path, so nothing stands in for it and its failure is the
	// program's - this is a command line tool when it is used as one
	if (arguments.size() > 1)
		return openRepositoryWindowAt(arguments[1], nullptr) ? QApplication::exec() : 1;

	// Nothing was named, so the current directory is the first place to look rather than the only one:
	// started from a shortcut it is wherever that pointed, and what was worked on last is the better guess.
	if (const std::expected<RepositoryLocation, std::vector<ProcessResult>> location = findRepository(QDir::currentPath()))
		openRepositoryWindow(*location);
	else
	{
		const std::vector<RecentRepository> recent = RecentRepositories::list();
		const bool opened = !recent.empty() && openRecentRepository(recent.front().root, nullptr) != nullptr;
		if (!opened && !browseForRepository(nullptr))
			return 0; // nothing to show, and nothing chosen to show instead
	}

	return QApplication::exec();
}
