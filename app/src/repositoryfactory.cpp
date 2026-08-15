#include "repositoryfactory.h"
#include "gitprocess.h"
#include "gitrepository.h"
#include "hgprocess.h"
#include "hgrepository.h"

#include <QDir>

namespace {

// What one kind was asked and answered. A kind whose tool never started is not installed, and saying so
// where the question was "is this a repository?" would answer something nobody asked.
struct Claim
{
	QString root; // empty: this kind does not claim the path
	QString error; // why not, or empty where the tool itself never ran
};

Claim claimedByGit(const QString& startPath)
{
	const ProcessResult result = Git::runSync(startPath, { QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel") });
	if (result.ok)
		return { QString::fromUtf8(result.out.trimmed()), {} };
	return { {}, result.outcome == ProcessOutcome::LaunchFailed ? QString{} : result.errorText() };
}

Claim claimedByMercurial(const QString& startPath)
{
	const ProcessResult result = Hg::runSync(startPath, { QStringLiteral("root") });
	if (result.ok) // hg prints a native path, and everything downstream is forward-slashed
		return { QDir::fromNativeSeparators(QString::fromUtf8(result.out.trimmed())), {} };
	return { {}, result.outcome == ProcessOutcome::LaunchFailed ? QString{} : result.errorText() };
}

} // namespace

std::expected<RepositoryLocation, QString> findRepository(const QString& startPath)
{
	// Asked in turn rather than both at once: git is the cheaper process and the common answer, and one
	// that claims the path settles the question. The cost is that an hg repository nested inside a git one
	// resolves to the outer git root - the other way round, which is what a git subrepo of an hg parent
	// looks like, is found correctly.
	const Claim git = claimedByGit(startPath);
	if (!git.root.isEmpty())
		return RepositoryLocation{ VcsKind::Git, git.root };

	const Claim hg = claimedByMercurial(startPath);
	if (!hg.root.isEmpty())
		return RepositoryLocation{ VcsKind::Mercurial, hg.root };

	QStringList reasons;
	for (const QString& reason : { git.error, hg.error })
	{
		if (!reason.isEmpty())
			reasons << reason;
	}
	if (reasons.isEmpty())
		return std::unexpected(QStringLiteral("Neither git nor hg could be started. Check that one of them is installed and on PATH."));
	return std::unexpected(reasons.join(QStringLiteral("\n\n")));
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
