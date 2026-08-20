#include "commitwindow.h"
#include "repositoryfactory.h"
#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>

#include <algorithm>

namespace {

// Nothing claimed the path, and what that means depends on whether anything answered at all: where no
// tool ran there is no answer to report, only the reasons there is none.
QString noRepositoryMessage(const QString& startPath, const std::vector<ProcessResult>& attempts)
{
	const bool anyToolAnswered = std::ranges::any_of(attempts,
		[](const ProcessResult& attempt) { return attempt.outcome != ProcessOutcome::LaunchFailed; });

	const QString nativePath = QDir::toNativeSeparators(startPath);
	QStringList parts{ anyToolAnswered
		? QStringLiteral("'%1' is not inside a repository.").arg(nativePath)
		: QStringLiteral("Could not determine what '%1' is inside: no version control tool could be started.").arg(nativePath) };
	for (const ProcessResult& attempt : attempts)
	{
		// Where another tool did answer, one that never started is a gap in the search rather than
		// something the user is being told to go and install
		parts << (anyToolAnswered && attempt.outcome == ProcessOutcome::LaunchFailed
			? QStringLiteral("%1 could not be started, so no %1 repository was looked for: %2").arg(attempt.toolName, attempt.launchError)
			: attempt.errorText());
	}
	return parts.join(QStringLiteral("\n\n"));
}

} // namespace

int main(int argc, char* argv[])
{
	QApplication app{ argc, argv };
	QApplication::setOrganizationName(QStringLiteral("GoodGit"));
	QApplication::setApplicationName(QStringLiteral("GoodGit"));
	applyTheme(app);

	// Not argv, which on Windows arrives in the local codepage and mangles anything outside it
	const QStringList arguments = QApplication::arguments();
	const QString startPath = arguments.size() > 1 ? arguments[1] : QDir::currentPath();

	const std::expected<RepositoryLocation, std::vector<ProcessResult>> location = findRepository(startPath);
	if (!location)
	{
		QMessageBox::critical(nullptr, QStringLiteral("GoodGit"), noRepositoryMessage(startPath, location.error()));
		return 1;
	}

	auto* window = new CommitWindow{ *location };
	window->show();

	return QApplication::exec();
}
