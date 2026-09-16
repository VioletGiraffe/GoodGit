#define NO_TEST_MAIN
#include "3rdparty/catch2/test_main.hpp"

#include "theme.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

int main(int argc, char* argv[])
{
	// Before the Catch2 session: QApplication strips its own arguments (-platform) out of argv
	QApplication app{ argc, argv };

	// Settings go to a throwaway INI file: the tests must neither read nor write the user's own
	const QTemporaryDir settingsDirectory;
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
	QApplication::setOrganizationName(QStringLiteral("GoodGit tests"));
	QApplication::setApplicationName(QStringLiteral("GoodGit tests"));

	applyTheme(app);
	return runCatchSession(argc, argv);
}
