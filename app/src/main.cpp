#include "commitwindow.h"
#include "gitprocess.h"
#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>

int main(int argc, char* argv[])
{
	QApplication app{ argc, argv };
	QApplication::setOrganizationName(QStringLiteral("GoodGit"));
	QApplication::setApplicationName(QStringLiteral("GoodGit"));
	applyTheme(app);

	// Not argv, which on Windows arrives in the local codepage and mangles anything outside it
	const QStringList arguments = QApplication::arguments();
	const QString startPath = arguments.size() > 1 ? arguments[1] : QDir::currentPath();

	const ProcessResult repoRootResult = Git::runSync(startPath, { QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel") });
	if (!repoRootResult.ok)
	{
		QMessageBox::critical(nullptr, QStringLiteral("GoodGit"),
			QStringLiteral("'%1' is not inside a git repository.\n\n%2")
				.arg(QDir::toNativeSeparators(startPath), repoRootResult.errorText()));
		return 1;
	}
	const QString repoRoot = QString::fromUtf8(repoRootResult.out.trimmed());

	auto* window = new CommitWindow{ repoRoot };
	window->show();

	return QApplication::exec();
}
