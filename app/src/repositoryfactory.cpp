#include "repositoryfactory.h"
#include "gitprocess.h"
#include "gitrepository.h"

std::expected<RepositoryLocation, QString> findRepository(const QString& startPath)
{
	const ProcessResult root = Git::runSync(startPath, { QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel") });
	if (root.ok)
		return RepositoryLocation{ VcsKind::Git, QString::fromUtf8(root.out.trimmed()) };

	return std::unexpected(root.errorText());
}

std::unique_ptr<Repository> openRepository(const RepositoryLocation& location)
{
	switch (location.kind)
	{
	case VcsKind::Git:
		return std::make_unique<GitRepository>(location.root);
	}
	return {};
}
