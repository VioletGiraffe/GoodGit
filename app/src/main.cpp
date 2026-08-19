#include "commitwindow.h"
#include "repositoryfactory.h"
#include "theme.h"

#include "timing/profiler.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QMessageBox>

int main(int argc, char* argv[])
{
	Profiler::setLoggingFunc([](const char* message) { qInfo().noquote() << message; });
	Profiler::start();
	QApplication app{ argc, argv };
	QApplication::setOrganizationName(QStringLiteral("GoodGit"));
	QApplication::setApplicationName(QStringLiteral("GoodGit"));
	applyTheme(app);
	PROFILE_MARK("QApplication ready");

	// Not argv, which on Windows arrives in the local codepage and mangles anything outside it
	const QStringList arguments = QApplication::arguments();
	const QString startPath = arguments.size() > 1 ? arguments[1] : QDir::currentPath();

	const std::expected<RepositoryLocation, QString> location = findRepository(startPath);
	if (!location)
	{
		QMessageBox::critical(nullptr, QStringLiteral("GoodGit"),
			QStringLiteral("'%1' is not inside a repository.\n\n%2")
				.arg(QDir::toNativeSeparators(startPath), location.error()));
		return 1;
	}

	auto* window = new CommitWindow{ *location };
	window->show();
	PROFILE_MARK("window shown");

	return QApplication::exec();
}
