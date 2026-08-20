#include "gitprocess.h"
#include "settings.h"

#include "settings/csettings.h"

namespace {

void applyInvariants(QStringList& args, bool readOnlyQuery)
{
	if (readOnlyQuery)
		args.prepend(QStringLiteral("--no-optional-locks"));
	args.prepend(QStringLiteral("core.quotepath=false"));
	args.prepend(QStringLiteral("-c"));
}

Vcs::Tool gitTool()
{
	auto environment = QProcessEnvironment::systemEnvironment();
	// A credential miss fails fast instead of hanging on a prompt nothing here would show
	environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
	QString executable = CSettings{}.value(QLatin1String(Settings::GitExecutableKey)).toString();
	if (executable.isEmpty())
		executable = QLatin1String(Settings::GitExecutableDefault);
	return { std::move(executable), QStringLiteral("git"), std::move(environment) };
}

} // namespace

namespace Git {

Vcs::Job* run(const QString& workDir, QStringList args, const QObject* context, Vcs::Callback callback,
	QByteArray stdinData, bool readOnlyQuery)
{
	applyInvariants(args, readOnlyQuery);
	return Vcs::run(gitTool(), workDir, std::move(args), context, std::move(callback), std::move(stdinData));
}

ProcessResult runSync(const QString& workDir, QStringList args, int timeoutMs)
{
	applyInvariants(args, /*readOnlyQuery=*/true);
	return Vcs::runSync(gitTool(), workDir, std::move(args), timeoutMs);
}

} // namespace Git
