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

	// No fallback for an explicit path: used as a command line tool, the app fails like one
	if (arguments.size() > 1)
		return openRepositoryWindowAt(arguments[1], nullptr) ? QApplication::exec() : 1;

	// The current directory is only the first guess: started from a shortcut it is wherever that pointed,
	// and the last repository worked on is the better guess then
	if (const std::expected<RepositoryLocation, std::vector<ProcessResult>> location = findRepository(QDir::currentPath()))
		return openRepositoryWindow(*location) ? QApplication::exec() : 1;

	const std::vector<RecentRepository> recent = RecentRepositories::list();
	const bool opened = !recent.empty() && openRecentRepository(recent.front().root, nullptr) != nullptr;
	if (!opened && !browseForRepository(nullptr))
		return 0; // nothing to show, and nothing chosen

	return QApplication::exec();
}
