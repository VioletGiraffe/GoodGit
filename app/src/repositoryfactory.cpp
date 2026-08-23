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
	QString root = result.ok ? QString::fromUtf8(result.out.trimmed()) : QString{};
	return { std::move(root), std::move(result) };
}

// What git itself looks for in a repository directory, so an empty or half-deleted .git is not one
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

	const QString target = QString::fromUtf8(line.mid(prefix.size()).trimmed());
	const QFileInfo directory{ QDir{ dotGit.absolutePath() }, target };
	return directory.isDir() ? directory.absoluteFilePath() : QString{};
}

// Mercurial's format file, written by init: a repository whose requirements it cannot read does not open
bool isMercurialDirectory(const QString& path)
{
	return QFileInfo::exists(path + QStringLiteral("/requires"));
}

// When the repository was last worked in: git rewrites the index and Mercurial the dirstate on every add,
// commit, checkout and merge. Neither exists before the first of those, so a repository that has none falls
// back to the stamp init left on the directory.
// Declared in .hgsub, which is what a refresh lists them from as well. No process: Mercurial keeps the
// declaration in the working tree.
std::vector<Subrepo> mercurialSubrepos(const QString& root)
{
	QFile declarations{ root + QStringLiteral("/.hgsub") };
	if (!declarations.open(QIODevice::ReadOnly))
		return {};

	std::vector<Subrepo> subrepos;
	for (const auto& [path, source] : Hg::parseSubrepoSources(declarations.readAll()))
		subrepos.push_back({ path, Hg::subrepoKind(source) });
	return subrepos;
}

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
	QString root = result.ok ? QDir::fromNativeSeparators(QString::fromUtf8(result.out.trimmed())) : QString{};
	return { std::move(root), std::move(result) };
}

// Everything a scan can read off the filesystem alone; the git subrepos are the one thing left to ask for
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
			found.push_back({ { VcsKind::Mercurial, root }, mercurialSubrepos(root), lastActivity(hgDirectory, QStringLiteral("dirstate")) });
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
	// git first: the cheaper process and the common answer. The cost is that an hg repository nested inside a
	// git one resolves to the outer git root; a git subrepo inside an hg parent is found correctly.
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

	QueryRound round{
		[context](const QString& workDir, QStringList args, Vcs::Callback onResult) {
			Git::run(workDir, std::move(args), context, std::move(onResult), {}, /*readOnlyQuery=*/true);
		},
		[found, onDone = std::move(onDone)] { onDone(std::move(*found)); }
	};

	for (size_t index = 0; index < found->size(); ++index)
	{
		if ((*found)[index].location.kind != VcsKind::Git)
			continue; // Mercurial's came out of .hgsub with the rest

		// The index, which is where a refresh reads them from too: `git submodule status` is a shell script
		// in Git for Windows and costs more than the whole scan
		round.launch((*found)[index].location.root,
			{ QStringLiteral("ls-files"), QStringLiteral("--stage"), QStringLiteral("-z") },
			[found, index](const ProcessResult& result) {
				if (!result.ok)
					return; // listed without subrepos, as a repository never opened is

				for (const QString& path : Git::parseGitlinkPaths(result.out))
					(*found)[index].subrepos.push_back({ path, VcsKind::Git });
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
