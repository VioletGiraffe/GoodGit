#include "hgprocess.h"
#include "hgcommandserver.h"
#include "settings.h"

#include "settings/csettings.h"

namespace {

Vcs::Tool hgTool()
{
	auto environment = QProcessEnvironment::systemEnvironment();
	// Plain output: no localisation, no user aliases, no defaults that would rewrite what the app asked for
	environment.insert(QStringLiteral("HGPLAIN"), QStringLiteral("1"));
	QString executable = CSettings{}.value(QLatin1String(Settings::HgExecutableKey)).toString();
	if (executable.isEmpty())
		executable = QLatin1String(Settings::HgExecutableDefault);
	return { std::move(executable), QStringLiteral("hg"), std::move(environment) };
}

} // namespace

namespace Hg {

QStringList invariantArgs()
{
	// diff.nobinary: a binary file's diff is the one-line note git prints by default, rather than the base85
	// patch hg's --git mode would otherwise carry into a diff pane and into every line count
	return { QStringLiteral("--config"), QStringLiteral("ui.interactive=False"),
		QStringLiteral("--config"), QStringLiteral("diff.nobinary=True") };
}

Vcs::Job* run(const QString& workDir, QStringList args, const QObject* context, Vcs::Callback callback,
	QByteArray stdinData, Transport transport)
{
	args = invariantArgs() + args; // global options come before the subcommand
	if (transport == Transport::Server)
		return HgServerPool::instance().run(hgTool(), workDir, std::move(args), context, std::move(callback), std::move(stdinData));
	return Vcs::run(hgTool(), workDir, std::move(args), context, std::move(callback), std::move(stdinData));
}

ProcessResult runSync(const QString& workDir, QStringList args, int timeoutMs)
{
	args = invariantArgs() + args;
	return Vcs::runSync(hgTool(), workDir, std::move(args), timeoutMs);
}

} // namespace Hg
