#include "repositorywindows.h"
#include "commitwindow.h"
#include "gitprocess.h"
#include "recentrepositories.h"
#include "repositoryfactory.h"
#include "welcomewindow.h"

#include "dialogs/messagebox.h"

#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMimeData>
#include <QPointer>
#include <QUrl>

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
		: QStringLiteral("Could not check whether '%1' is inside a repository: no version control tool could be started.").arg(nativePath) };
	for (const ProcessResult& attempt : attempts)
	{
		// When another tool did answer, one that never started is a gap in the search, not something to install
		parts << (anyToolAnswered && attempt.outcome == ProcessOutcome::LaunchFailed
			? QStringLiteral("%1 could not be started, so it is unknown whether this is a %1 repository: %2").arg(attempt.toolName, attempt.launchError)
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

// The local directories a drag carries, in the order it lists them; empty when it carries none
QStringList draggedFolders(const QMimeData* mimeData)
{
	QStringList folders;
	for (const QUrl& url : mimeData->urls())
	{
		const QString path = url.toLocalFile(); // empty for anything that is not a local file
		if (!path.isEmpty() && QFileInfo{ path }.isDir())
			folders << path;
	}
	return folders;
}

// Queued past the drop handler: the drag source is blocked until that returns, and opening reports a folder
// no repository claims in a modal dialog
void openDroppedFolders(const QStringList& folders, QWidget* dropTarget)
{
	QMetaObject::invokeMethod(dropTarget, [folders, dropTarget] {
		for (const QString& folder : folders)
			openRepositoryWindowAt(folder, dropTarget->window());
	}, Qt::QueuedConnection);
}

// Watches the one widget it is installed on
class FolderDropFilter final : public QObject
{
public:
	explicit FolderDropFilter(QWidget* target) : QObject{ target } {}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		const QEvent::Type type = event->type();
		if (type != QEvent::DragEnter && type != QEvent::DragMove && type != QEvent::Drop)
			return QObject::eventFilter(watched, event);

		auto* dragEvent = static_cast<QDropEvent*>(event); // the base of the drag-enter and drag-move events too
		const QStringList folders = draggedFolders(dragEvent->mimeData());
		if (folders.isEmpty())
			return QObject::eventFilter(watched, event);

		dragEvent->acceptProposedAction();
		if (type == QEvent::Drop)
			openDroppedFolders(folders, static_cast<QWidget*>(watched));
		return true;
	}
};

// The one up, or none
WelcomeWindow* welcomeWindow()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (auto* welcome = dynamic_cast<WelcomeWindow*>(widget)) // not qobject_cast: WelcomeWindow has no meta-object
			return welcome;
	}
	return nullptr;
}

void closeWelcomeWindow()
{
	if (WelcomeWindow* welcome = welcomeWindow())
		welcome->close();
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
	CommitWindow* window = repositoryWindow(location.root);
	if (window)
	{
		window->raise();
		window->activateWindow();
	}
	else
	{
		window = new CommitWindow{ location };
		window->show();
	}

	closeWelcomeWindow(); // after the repository window shows, so the application is never left with none
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

void scanFolderForRepositories(QWidget* dialogParent)
{
	const QString folder = QFileDialog::getExistingDirectory(dialogParent,
		QStringLiteral("Scan Folder for Repositories"), browseStartDirectory());
	if (folder.isEmpty())
		return;

	// One query per git repository is in flight from here; the window stays usable, and the cursor is what
	// says a scan is running
	QApplication::setOverrideCursor(Qt::BusyCursor);
	findRepositoriesInFolder(folder, dialogParent, [parent = QPointer<QWidget>{ dialogParent }, folder](std::vector<FoundRepository> found) {
		QApplication::restoreOverrideCursor();
		if (!parent)
			return; // the window the scan was started from is gone, and the answers with it

		const int added = int(RecentRepositories::recordFound(found));

		const QString nativePath = QDir::toNativeSeparators(folder);
		QString message;
		if (found.empty())
			message = QStringLiteral("No repositories directly inside '%1'.").arg(nativePath);
		else if (added == 0) // the cap can also have dropped every one of them, so neither reason is claimed alone
			message = QStringLiteral("Nothing added from '%1': its repositories are already in the list, or were used "
				"less recently than the ones the list keeps.").arg(nativePath);
		else
			message = QStringLiteral("Added %1 %2 from '%3'.")
				.arg(added)
				.arg(added == 1 ? QStringLiteral("repository") : QStringLiteral("repositories"), nativePath);

		MessageBox::notice(parent, QApplication::applicationName(), message, {}, QMessageBox::Information);
	});
}

void showWelcomeWindow()
{
	WelcomeWindow* welcome = welcomeWindow();
	if (!welcome)
		welcome = new WelcomeWindow;

	welcome->show(); // a no-op on one already up
	welcome->raise();
	welcome->activateWindow();
}

void acceptRepositoryFolderDrops(QWidget* target)
{
	target->setAcceptDrops(true);
	target->installEventFilter(new FolderDropFilter{ target });
}
