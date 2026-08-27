#include "gitprocess.h"
#include "settings.h"

#include "settings/csettings.h"

DISABLE_COMPILER_WARNINGS
#include <QVersionNumber>
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
	QString executable = CSettings{}.value(Settings::GitExecutableKey).toString();
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

QueryRound::Launcher readOnlyQueries(const QObject* context)
{
	return [context](const QString& workDir, QStringList args, Vcs::Callback onResult) {
		run(workDir, std::move(args), context, std::move(onResult), {}, /*readOnlyQuery=*/true);
	};
}

std::optional<QString> versionProblem(const QString& workDir)
{
	// --pathspec-from-file and --pathspec-file-nul, which staging, un-staging and discarding all pass, arrived in git 2.25
	const QVersionNumber minimum{ 2, 25 };

	const ProcessResult result = runSync(workDir, { QStringLiteral("--version") });
	// "git version 2.37.1.windows.1": fromString reads the leading numbers and ignores a vendor's extra fields
	const QStringList fields = QString::fromUtf8(result.out).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
	const QVersionNumber version = fields.size() >= 3 ? QVersionNumber::fromString(fields[2]) : QVersionNumber{};
	if (version.isNull() || version >= minimum)
		return {};

	return QObject::tr("This git is version %1, but %2 or newer is required.\n\nStaging and discarding changes pass the file list to git in a form older versions reject.")
		.arg(version.toString(), minimum.toString());
}

} // namespace Git
