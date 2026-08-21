#include "hgprocess.h"
#include "hgcommandserver.h"
#include "settings.h"

#include "settings/csettings.h"

namespace {

Vcs::Tool hgTool()
{
	auto environment = QProcessEnvironment::systemEnvironment();
	// No localisation, user aliases or defaults rewriting the command
	environment.insert(QStringLiteral("HGPLAIN"), QStringLiteral("1"));
	QString executable = CSettings{}.value(Settings::HgExecutableKey).toString();
	if (executable.isEmpty())
		executable = QLatin1String(Settings::HgExecutableDefault);
	return { std::move(executable), QStringLiteral("hg"), std::move(environment) };
}

} // namespace

namespace Hg {

QStringList invariantArgs()
{
	// diff.nobinary: a binary file's diff is a one-line note (as with git) rather than the base85 patch --git
	// mode would otherwise put into the diff pane and every line count
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
