#include "commitwindow.h"
#include "settings.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QProcess>

int main(int argc, char* argv[])
{
	QApplication app{ argc, argv };
	QApplication::setOrganizationName(QStringLiteral("GoodGit"));
	QApplication::setApplicationName(QStringLiteral("GoodGit"));

	const QString startPath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();

	// One synchronous git call before any window exists: resolve the repository root
	QProcess revParse;
	revParse.setWorkingDirectory(startPath);
	revParse.start(Settings::gitExecutable(), { QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel") });
	if (!revParse.waitForFinished(10000) || revParse.exitCode() != 0)
	{
		const QString detail = QString::fromUtf8(revParse.readAllStandardError()).trimmed();
		QMessageBox::critical(nullptr, QStringLiteral("GoodGit"),
			QStringLiteral("'%1' is not inside a git repository.\n\n%2").arg(QDir::toNativeSeparators(startPath), detail));
		return 1;
	}
	const QString repoRoot = QString::fromUtf8(revParse.readAllStandardOutput().trimmed());

	auto* window = new CommitWindow{ repoRoot };
	window->show();

	return QApplication::exec();
}
