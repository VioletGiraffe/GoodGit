#include "gitprocess.h"
#include "settings.h"

DISABLE_COMPILER_WARNINGS
#include <QSettings>
RESTORE_COMPILER_WARNINGS

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
	// A credential miss fails instead of hanging on a prompt nobody would see
	environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
	return { Git::executablePath(), QStringLiteral("git"), std::move(environment) };
}

// For a command passing --pathspec-from-file, which arrived in git 2.25.
// A usage error gets the likely cause prepended: a supported git never rejects the app's fixed argument lists.
// Prepended, not appended: git follows the error with its usage listing.
Vcs::Callback explainingUnsupportedVersion(Vcs::Callback callback)
{
	return [callback = std::move(callback)](const ProcessResult& result) {
		constexpr int UsageErrorExitCode = 129;
		if (result.outcome != ProcessOutcome::Exited || result.exitCode != UsageErrorExitCode)
		{
			callback(result);
			return;
		}

		ProcessResult explained = result;
		explained.err.prepend(QObject::tr("This git is probably too old: GoodGit requires git 2.25 or newer.").toUtf8() + "\n\n");
		callback(explained);
	};
}

} // namespace

namespace Git {

QString executablePath()
{
	QString executable = QSettings{}.value(Settings::GitExecutableKey).toString();
	if (executable.isEmpty())
		executable = QLatin1String(Settings::GitExecutableDefault);
	return executable;
}

Vcs::Job* run(const QString& workDir, QStringList args, const QObject* context, Vcs::Callback callback,
	QByteArray stdinData, bool readOnlyQuery)
{
	if (args.contains(QStringLiteral("--pathspec-from-file=-")))
		callback = explainingUnsupportedVersion(std::move(callback));
	applyInvariants(args, readOnlyQuery);
	return Vcs::run(gitTool(), workDir, std::move(args), context, std::move(callback), std::move(stdinData));
}

ProcessResult runSync(const QString& workDir, QStringList args, int timeoutMs)
{
	applyInvariants(args, /*readOnlyQuery=*/true);
	return Vcs::runSync(gitTool(), workDir, std::move(args), timeoutMs);
}

QueryRound::Launcher readOnlyQueries(const QObject* context)
{
	return [context](const QString& workDir, QStringList args, Vcs::Callback onResult) {
		run(workDir, std::move(args), context, std::move(onResult), {}, /*readOnlyQuery=*/true);
	};
}

} // namespace Git
