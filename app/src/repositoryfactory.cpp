#include "repositoryfactory.h"
#include "gitparsers.h"
#include "gitprocess.h"
#include "gitrepository.h"
#include "hgparsers.h"
#include "hgprocess.h"
#include "hgrepository.h"
#include "queryround.h"

DISABLE_COMPILER_WARNINGS
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {

struct Claim
{
	QString root; // empty: this kind does not claim the path, for the reason `result` gives
	ProcessResult result;
};

Claim claimedByGit(const QString& startPath)
{
	ProcessResult result = Git::runSync(startPath, { QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel") });
	QString root = result.ok ? Git::pathFromOutput(result.out.trimmed()) : QString{};
	return { std::move(root), std::move(result) };
}

// The markers git itself looks for, so an empty or half-deleted .git is not a repository directory
bool isGitDirectory(const QString& path)
{
	return QFileInfo::exists(path + QStringLiteral("/HEAD"))
		&& QFileInfo{ path + QStringLiteral("/objects") }.isDir()
		&& QFileInfo{ path + QStringLiteral("/refs") }.isDir();
}

// The repository directory a worktree's or a submodule's .git file names, which may be relative to it.
// Empty when the file names none that exists, a dangling link included.
QString gitLinkTarget(const QFileInfo& dotGit)
{
	constexpr qint64 MaxGitLinkBytes = 4096; // it holds a path, not a document
	static const QByteArray prefix = QByteArrayLiteral("gitdir:");

	QFile file{ dotGit.absoluteFilePath() };
	if (!file.open(QIODevice::ReadOnly))
		return {};

	const QByteArray line = file.readLine(MaxGitLinkBytes);
	if (!line.startsWith(prefix))
		return {};

	const QString target = Git::pathFromOutput(line.mid(prefix.size()).trimmed());
	const QFileInfo directory{ QDir{ dotGit.absolutePath() }, target };
	return directory.isDir() ? directory.absoluteFilePath() : QString{};
}

// Mercurial's format file, written by init: a repository whose requirements it cannot read does not open
bool isMercurialDirectory(const QString& path)
{
	return QFileInfo::exists(path + QStringLiteral("/requires"));
}

// Declared in .hgsub, the same file a refresh lists them from. No process: Mercurial keeps the declaration
// in the working tree.
std::vector<Submodule> mercurialSubmodules(const QString& root)
{
	QFile declarations{ root + QStringLiteral("/.hgsub") };
	if (!declarations.open(QIODevice::ReadOnly))
		return {};

	std::vector<Submodule> submodules;
	for (const auto& [path, source] : Hg::parseSubrepoSources(declarations.readAll()))
		submodules.push_back({ path, Hg::subrepoKind(source) });
	return submodules;
}

// When the repository was last worked in: git rewrites the index and Mercurial the dirstate on every add,
// commit, checkout and merge. Neither exists before the first of those, so a repository that has none falls
// back to the stamp init left on the directory.
int64_t lastActivity(const QString& repositoryDirectory, const QString& activityFileName)
{
	const QFileInfo activity{ repositoryDirectory + QLatin1Char('/') + activityFileName };
	const QDateTime modified = activity.exists() ? activity.lastModified() : QFileInfo{ repositoryDirectory }.lastModified();
	return modified.toMSecsSinceEpoch();
}

Claim claimedByMercurial(const QString& startPath)
{
	ProcessResult result = Hg::runSync(startPath, { QStringLiteral("root") });
	// hg prints a native path, and everything downstream is forward-slashed
	QString root = result.ok ? QDir::fromNativeSeparators(Hg::textFromOutput(result.out.trimmed())) : QString{};
	return { std::move(root), std::move(result) };
}

// Everything a scan can read off the filesystem alone; the git submodules are the one thing left to ask for
std::vector<FoundRepository> repositoriesInFolder(const QString& folder)
{
	std::vector<FoundRepository> found;
	for (const QFileInfo& entry : QDir{ folder }.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
	{
		const QString root = entry.absoluteFilePath();

		// A plain repository's .git is the repository directory; a worktree's and a submodule's is a file
		// naming one, and that it names a directory at all is the whole test: a worktree's holds neither
		// objects nor refs, both of which stay in the repository it was made from.
		const QFileInfo dotGit{ root + QStringLiteral("/.git") };
		QString gitDirectory;
		if (dotGit.isDir())
			gitDirectory = isGitDirectory(dotGit.absoluteFilePath()) ? dotGit.absoluteFilePath() : QString{};
		else if (dotGit.isFile())
			gitDirectory = gitLinkTarget(dotGit);

		if (!gitDirectory.isEmpty())
			found.push_back({ { VcsKind::Git, root }, {}, lastActivity(gitDirectory, QStringLiteral("index")) });
		else if (const QString hgDirectory = root + QStringLiteral("/.hg"); isMercurialDirectory(hgDirectory))
			found.push_back({ { VcsKind::Mercurial, root }, mercurialSubmodules(root), lastActivity(hgDirectory, QStringLiteral("dirstate")) });
	}

	// Stable, so repositories worked in at the same moment keep the name order entryInfoList returned them in
	std::ranges::stable_sort(found, [](const FoundRepository& left, const FoundRepository& right) {
		return left.lastUsedMSecs > right.lastUsedMSecs;
	});
	return found;
}

} // namespace

std::expected<RepositoryLocation, std::vector<ProcessResult>> findRepository(const QString& startPath)
{
	// Probing a vanished directory would fail with FailedToStart, whose default advice points at tool installation
	if (!QFileInfo{ startPath }.isDir())
	{
		ProcessResult missing; // Exited, so errorText() returns this err
		missing.err = (QFileInfo::exists(startPath)
			? QObject::tr("The path is a file; a repository is opened from a folder.")
			: QObject::tr("The folder does not exist - deleted, renamed, or on an unmounted drive.")).toUtf8();
		return std::unexpected(std::vector<ProcessResult>{ std::move(missing) });
	}

	// git first: the cheaper process and the common answer. The cost is that an hg repository nested inside a
	// git one resolves to the outer git root; a git submodule inside an hg parent is found correctly.
	Claim git = claimedByGit(startPath);
	if (!git.root.isEmpty())
		return RepositoryLocation{ VcsKind::Git, std::move(git.root) };

	Claim hg = claimedByMercurial(startPath);
	if (!hg.root.isEmpty())
		return RepositoryLocation{ VcsKind::Mercurial, std::move(hg.root) };

	return std::unexpected(std::vector<ProcessResult>{ std::move(git.result), std::move(hg.result) });
}

void findRepositoriesInFolder(const QString& folder, const QObject* context,
	std::function<void(std::vector<FoundRepository>)> onDone)
{
	// Shared with every query: each fills its own entry, and the last to answer hands the whole list over
	auto found = std::make_shared<std::vector<FoundRepository>>(repositoriesInFolder(folder));

	QueryRound round{ Git::readOnlyQueries(context),
		[found, onDone = std::move(onDone)] { onDone(std::move(*found)); } };

	for (size_t index = 0; index < found->size(); ++index)
	{
		if ((*found)[index].location.kind != VcsKind::Git)
			continue; // Mercurial's came out of .hgsub with the rest

		round.launch((*found)[index].location.root, Git::submoduleListingArgs(),
			[found, index](const ProcessResult& result) {
				if (!result.ok)
					return; // listed without submodules, as a repository never opened is

				for (const QString& path : Git::parseGitlinkPaths(result.out))
					(*found)[index].submodules.push_back({ path, VcsKind::Git });
			});
	}
}

std::unique_ptr<Repository> openRepository(const RepositoryLocation& location)
{
	switch (location.kind)
	{
	case VcsKind::Git:
		return std::make_unique<GitRepository>(location.root);
	case VcsKind::Mercurial:
		return std::make_unique<HgRepository>(location.root);
	}
	return {};
}
