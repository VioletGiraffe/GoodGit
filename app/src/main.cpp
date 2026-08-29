#include "recentrepositories.h"
#include "repositoryfactory.h"
#include "repositorywindows.h"
#include "theme.h"
#include "updatecheck.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QDir>
#include <QIcon>
RESTORE_COMPILER_WARNINGS

int main(int argc, char* argv[])
{
	QApplication app{ argc, argv };
	QApplication::setOrganizationName(QStringLiteral("GoodGit"));
	QApplication::setApplicationName(QStringLiteral("GoodGit"));
	QApplication::setWindowIcon(QIcon{ QStringLiteral(":/goodgit.svg") });
	applyTheme(app);
	checkForUpdatesIfDue();   // not in a window constructor: one window per repository would mean one check per window

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
	if (recent.empty() || !openRecentRepository(recent.front().root, nullptr))
		showWelcomeWindow();

	return QApplication::exec();
}
