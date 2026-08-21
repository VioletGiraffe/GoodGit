#include "commandlinetool_mac.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace {

// On the default PATH via /etc/paths, and unlike /usr/bin not protected by SIP, so root can write here.
constexpr const char* LinkPath = "/usr/local/bin/gg";
constexpr const char* LinkDirectory = "/usr/local/bin";

// Wraps `text` in single quotes for /bin/sh, escaping any single quote it contains.
QString shellQuoted(const QString& text)
{
	QString quoted = text;
	quoted.replace(QLatin1Char('\''), QLatin1String("'\\''"));
	return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

// Escapes `text` for an AppleScript double-quoted string literal. Backslashes first, or the ones this
// adds for the quotes would be escaped in turn.
QString appleScriptQuoted(const QString& text)
{
	QString quoted = text;
	quoted.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
	quoted.replace(QLatin1Char('"'), QLatin1String("\\\""));
	return QLatin1Char('"') + quoted + QLatin1Char('"');
}

enum class Elevated { Ran, Cancelled };

// Runs one /bin/sh command as root. The dialog macOS raises is the only place credentials are entered:
// nothing here sees a password. Blocks until the dialog is dismissed.
std::expected<Elevated, QString> runAsAdministrator(const QString& shellCommand)
{
	const QString script = QStringLiteral("do shell script %1 with administrator privileges")
		.arg(appleScriptQuoted(shellCommand));

	QProcess osascript;
	osascript.start(QStringLiteral("/usr/bin/osascript"), { QStringLiteral("-e"), script });
	if (!osascript.waitForStarted() || !osascript.waitForFinished(-1))
		return std::unexpected(osascript.errorString());

	if (osascript.exitStatus() == QProcess::NormalExit && osascript.exitCode() == 0)
		return Elevated::Ran;

	const QString error = QString::fromLocal8Bit(osascript.readAllStandardError()).trimmed();
	// -128 is userCanceledErr: the dialog was dismissed, which is a decision and not a failure.
	if (error.contains(QLatin1String("-128")))
		return Elevated::Cancelled;

	return std::unexpected(error.isEmpty() ? QStringLiteral("osascript failed.") : error);
}

}

std::expected<QString, QString> installCommandLineTool()
{
	const QString target = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
	const QString linkPath = QString::fromLatin1(LinkPath);

	const QFileInfo link{ linkPath };
	if (link.isSymbolicLink())
	{
		if (link.symLinkTarget() == target)
			return QStringLiteral("%1 already points at this copy of GoodGit.").arg(linkPath);

		if (link.exists()) // exists() follows the link: something is on the other end, so it is not ours to take
			return std::unexpected(QStringLiteral("%1 already points at %2.\n\nRemove it first if you want it to launch GoodGit.")
				.arg(linkPath, link.symLinkTarget()));

		// Dangling, so replacing it takes nothing away - and symlink() below will not overwrite.
		::unlink(LinkPath);
	}
	else if (link.exists())
	{
		return std::unexpected(QStringLiteral("%1 already exists and is not a symbolic link.\n\nRemove it first if you want it to launch GoodGit.")
			.arg(linkPath));
	}

	if (::symlink(QFile::encodeName(target).constData(), LinkPath) != 0)
	{
		// The directory is not writable, or does not exist at all on a Mac that never had one; both are
		// what administrator credentials are for. Anything else is not something more privilege would fix.
		if (errno != EACCES && errno != EPERM && errno != ENOENT)
			return std::unexpected(QStringLiteral("Could not create %1: %2").arg(linkPath, QString::fromLocal8Bit(std::strerror(errno))));

		// -f replaces a dangling link that the unprivileged unlink() above was not allowed to remove.
		const QString command = QStringLiteral("mkdir -p %1 && ln -sfn %2 %3")
			.arg(shellQuoted(QString::fromLatin1(LinkDirectory)), shellQuoted(target), shellQuoted(linkPath));

		const std::expected<Elevated, QString> elevated = runAsAdministrator(command);
		if (!elevated)
			return std::unexpected(QStringLiteral("Could not create %1: %2").arg(linkPath, elevated.error()));

		if (*elevated == Elevated::Cancelled)
			return QStringLiteral("Cancelled - %1 was not created.").arg(linkPath);
	}

	return QStringLiteral("The gg command is now available: %1").arg(linkPath);
}
