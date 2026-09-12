#include "hgprocess.h"
#include "recentrepositories.h"
#include "repositoryfactory.h"
#include "repositorywindows.h"
#include "settings.h"
#include "theme.h"
#include "updatecheck.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QByteArrayList>
#include <QDir>
#include <QIcon>
#include <QString>
#include <QtGlobal> // qgetenv, qputenv
RESTORE_COMPILER_WARNINGS

namespace {

// Ends the hg command servers, which outlive the last window: static destruction runs after QApplication is gone
int runApplication()
{
	const int exitCode = QApplication::exec();
	Hg::shutdown();
	return exitCode;
}

#ifdef Q_OS_MACOS
// launchd's PATH lacks the Homebrew and MacPorts directories: an app started from Finder or the Dock inherits it
// Both package managers' shell setup puts these first: the app then runs the same git and hg as a terminal
// A directory already on PATH is not moved: a terminal launch keeps the user's order
void prependPackageManagerDirectoriesToPath()
{
	const QByteArray inheritedPath = qgetenv("PATH");
	const QByteArrayList inheritedDirectories = inheritedPath.split(':');
	QByteArrayList path;
	for (const char* directory : { "/opt/homebrew/bin", "/usr/local/bin", "/opt/local/bin" })
	{
		if (!inheritedDirectories.contains(directory))
			path.append(directory);
	}
	if (!inheritedPath.isEmpty()) // an empty entry would put the current directory on PATH
		path.append(inheritedPath);
	qputenv("PATH", path.join(':'));
}
#endif

} // namespace

// On Windows the application is a DLL that the launcher exe loads and calls into, see doc/BUILD.md
#ifdef Q_OS_WIN
extern "C" __declspec(dllexport) int ggMain(int argc, char* argv[], const wchar_t* libraryDirectory)
#else
int main(int argc, char* argv[])
#endif
{
#ifdef Q_OS_WIN
	// The Qt plugins deploy beside this dll. The default library paths name the exe's directory and the Qt prefix.
	QApplication::addLibraryPath(QString::fromWCharArray(libraryDirectory));
#endif
#ifdef Q_OS_MACOS
	prependPackageManagerDirectoriesToPath();
#endif
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
		return openRepositoryWindowAt(arguments[1], nullptr) ? runApplication() : 1;

	// The current directory is only the first guess: started from a shortcut it is wherever that pointed,
	// and the last repository worked on is the better guess then
	if (const std::expected<RepositoryLocation, std::vector<ProcessResult>> location = findRepository(QDir::currentPath()))
		return openRepositoryWindow(*location) ? runApplication() : 1;

	const std::vector<RecentRepository> recent = RecentRepositories::list();
	// The welcome screen also stands in where the repository to reopen is gone: a deleted folder, a drive not mounted yet
	if (!Settings::startsWithLastRepository() || recent.empty() || !openRecentRepository(recent.front().root, nullptr))
		showWelcomeWindow();

	return runApplication();
}
