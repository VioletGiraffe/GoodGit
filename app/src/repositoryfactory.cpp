#include "repositoryfactory.h"
#include "gitprocess.h"
#include "gitrepository.h"
#include "hgprocess.h"
#include "hgrepository.h"

#include <QDir>

namespace {

// What one kind was asked, and what came of asking it
struct Claim
{
	QString root; // empty: this kind does not claim the path, for whichever of the reasons `result` gives
	ProcessResult result;
};

Claim claimedByGit(const QString& startPath)
{
	ProcessResult result = Git::runSync(startPath, { QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel") });
	QString root = result.ok ? QString::fromUtf8(result.out.trimmed()) : QString{};
	return { std::move(root), std::move(result) };
}

Claim claimedByMercurial(const QString& startPath)
{
	ProcessResult result = Hg::runSync(startPath, { QStringLiteral("root") });
	// hg prints a native path, and everything downstream is forward-slashed
	QString root = result.ok ? QDir::fromNativeSeparators(QString::fromUtf8(result.out.trimmed())) : QString{};
	return { std::move(root), std::move(result) };
}

} // namespace

std::expected<RepositoryLocation, std::vector<ProcessResult>> findRepository(const QString& startPath)
{
	// Asked in turn rather than both at once: git is the cheaper process and the common answer, and one
	// that claims the path settles the question. The cost is that an hg repository nested inside a git one
	// resolves to the outer git root - the other way round, which is what a git subrepo of an hg parent
	// looks like, is found correctly.
	Claim git = claimedByGit(startPath);
	if (!git.root.isEmpty())
		return RepositoryLocation{ VcsKind::Git, std::move(git.root) };

	Claim hg = claimedByMercurial(startPath);
	if (!hg.root.isEmpty())
		return RepositoryLocation{ VcsKind::Mercurial, std::move(hg.root) };

	return std::unexpected(std::vector<ProcessResult>{ std::move(git.result), std::move(hg.result) });
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
