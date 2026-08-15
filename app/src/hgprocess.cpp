#include "hgprocess.h"
#include "settings.h"

namespace {

Vcs::Tool hgTool()
{
	auto environment = QProcessEnvironment::systemEnvironment();
	// Plain output: no localisation, no user aliases, no defaults that would rewrite what the app asked for
	environment.insert(QStringLiteral("HGPLAIN"), QStringLiteral("1"));
	return { Settings::hgExecutable(), QStringLiteral("hg"), std::move(environment) };
}

} // namespace

namespace Hg {

QStringList invariantArgs()
{
	return { QStringLiteral("--config"), QStringLiteral("ui.interactive=False") };
}

Vcs::Job* run(const QString& workDir, QStringList args, const QObject* context, Vcs::Callback callback,
	QByteArray stdinData)
{
	args = invariantArgs() + args; // global options come before the subcommand
	return Vcs::run(hgTool(), workDir, std::move(args), context, std::move(callback), std::move(stdinData));
}

ProcessResult runSync(const QString& workDir, QStringList args, int timeoutMs)
{
	args = invariantArgs() + args;
	return Vcs::runSync(hgTool(), workDir, std::move(args), timeoutMs);
}

} // namespace Hg
