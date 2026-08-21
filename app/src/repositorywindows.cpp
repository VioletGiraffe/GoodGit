#include "repositorywindows.h"
#include "commitwindow.h"
#include "gitprocess.h"
#include "recentrepositories.h"
#include "repositoryfactory.h"

#include "dialogs/messagebox.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>

#include <algorithm>
#include <optional>

namespace {

// "Not a repository" if at least one tool answered, "could not check" if none could be run
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
		// When another tool did answer, one that never started is a gap in the search, not something to install
		parts << (anyToolAnswered && attempt.outcome == ProcessOutcome::LaunchFailed
			? QStringLiteral("%1 could not be started, so no %1 repository was looked for: %2").arg(attempt.toolName, attempt.launchError)
			: attempt.errorText());
	}
	return parts.join(QStringLiteral("\n\n"));
}

// Beside the repository last worked on, where the next one tends to live too
QString browseStartDirectory()
{
	const std::vector<RecentRepository> recent = RecentRepositories::list();
	return recent.empty() ? QDir::homePath() : QFileInfo{ recent.front().root }.absolutePath();
}

} // namespace

CommitWindow* repositoryWindow(const QString& root)
{
	// The open windows are the registry: a closed one drops out without any bookkeeping
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		auto* window = qobject_cast<CommitWindow*>(widget);
		if (window && sameRepositoryPath(window->repositoryPath(), root))
			return window;
	}
	return nullptr;
}

CommitWindow* openRepositoryWindow(const RepositoryLocation& location, QWidget* dialogParent)
{
	if (location.kind == VcsKind::Git)
	{
		if (const std::optional<QString> problem = Git::versionProblem(location.root))
		{
			MessageBox::notice(dialogParent, QApplication::applicationName(), *problem, {}, QMessageBox::Critical);
			return nullptr;
		}
	}

	RecentRepositories::recordOpen(location);

	// Two windows on one repository would commit the same changes through both
	if (CommitWindow* existing = repositoryWindow(location.root))
	{
		existing->raise();
		existing->activateWindow();
		return existing;
	}

	auto* window = new CommitWindow{ location };
	window->show();
	return window;
}

CommitWindow* openRepositoryWindowAt(const QString& path, QWidget* dialogParent)
{
	const std::expected<RepositoryLocation, std::vector<ProcessResult>> location = findRepository(path);
	if (location)
		return openRepositoryWindow(*location, dialogParent);

	MessageBox::notice(dialogParent, QApplication::applicationName(), noRepositoryMessage(path, location.error()), {}, QMessageBox::Critical);
	return nullptr;
}

CommitWindow* openRecentRepository(const QString& root, QWidget* dialogParent)
{
	const std::expected<RepositoryLocation, std::vector<ProcessResult>> location = findRepository(root);
	if (location)
		return openRepositoryWindow(*location, dialogParent);

	// Keep is the default: an unmounted drive looks exactly like a deleted repository
	const std::optional<int> answer = MessageBox::question(dialogParent, QApplication::applicationName(),
		noRepositoryMessage(root, location.error()), { QStringLiteral("Remove from list"), QStringLiteral("Keep") }, 1);
	if (answer == 0)
		RecentRepositories::forget(root);
	return nullptr;
}

CommitWindow* browseForRepository(QWidget* dialogParent)
{
	const QString directory = QFileDialog::getExistingDirectory(dialogParent, QStringLiteral("Open Repository"), browseStartDirectory());
	return directory.isEmpty() ? nullptr : openRepositoryWindowAt(directory, dialogParent);
}
