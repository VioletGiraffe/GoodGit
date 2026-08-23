#include "commandlinetool_mac.h"

#include "dialogs/messagebox.h"

DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

#import <Foundation/Foundation.h>

#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace {

// On the default PATH via /etc/paths, and unlike /usr/bin not protected by SIP, so root can write here
constexpr const char* LinkPath = "/usr/local/bin/gg";
constexpr const char* LinkDirectory = "/usr/local/bin";

// Single-quotes `text` for /bin/sh
QString shellQuoted(const QString& text)
{
	QString quoted = text;
	quoted.replace(QLatin1Char('\''), QLatin1String("'\\''"));
	return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

// Double-quotes `text` for AppleScript. Backslashes first, or the ones added for the quotes would be
// escaped in turn.
QString appleScriptQuoted(const QString& text)
{
	QString quoted = text;
	quoted.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
	quoted.replace(QLatin1Char('"'), QLatin1String("\\\""));
	return QLatin1Char('"') + quoted + QLatin1Char('"');
}

enum class Elevated { Ran, Cancelled };

constexpr NSInteger UserCancelledError = -128; // userCanceledErr

// Runs one /bin/sh command as root. Credentials are entered only in the dialog macOS raises; nothing here
// sees a password. Blocks until the dialog is dismissed.
// In-process, not via the osascript binary: the authorization dialog identifies the requesting process.
std::expected<Elevated, QString> runAsAdministrator(const QString& shellCommand)
{
	const QString source = QStringLiteral("do shell script %1 with administrator privileges")
		.arg(appleScriptQuoted(shellCommand));

	NSAppleScript* script = [[NSAppleScript alloc] initWithSource:source.toNSString()];
	NSDictionary* scriptError = nil;
	const bool ran = [script executeAndReturnError:&scriptError] != nil;
	[script release];

	if (ran)
		return Elevated::Ran;

	if ([scriptError[NSAppleScriptErrorNumber] integerValue] == UserCancelledError)
		return Elevated::Cancelled;

	NSString* message = scriptError[NSAppleScriptErrorBriefMessage];
	return std::unexpected(message ? QString::fromNSString(message) : QStringLiteral("AppleScript failed."));
}

}

bool commandLineToolLinkMissingOrBroken()
{
	return !QFileInfo::exists(QString::fromLatin1(LinkPath)); // follows the link, so a dangling one reads as absent
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

		if (link.exists()) // exists() follows the link: it points at something that is not ours to take
			return std::unexpected(QStringLiteral("%1 already points at %2.\n\nRemove it first if you want it to launch GoodGit.")
				.arg(linkPath, link.symLinkTarget()));

		// Dangling, so replacing it takes nothing away; symlink() below will not overwrite
		::unlink(LinkPath);
	}
	else if (link.exists())
	{
		return std::unexpected(QStringLiteral("%1 already exists and is not a symbolic link.\n\nRemove it first if you want it to launch GoodGit.")
			.arg(linkPath));
	}

	if (::symlink(QFile::encodeName(target).constData(), LinkPath) != 0)
	{
		// The directory is not writable, or does not exist on a Mac that never had one: both are what
		// administrator credentials fix. Anything else is not.
		if (errno != EACCES && errno != EPERM && errno != ENOENT)
			return std::unexpected(QStringLiteral("Could not create %1: %2").arg(linkPath, QString::fromLocal8Bit(std::strerror(errno))));

		// -f replaces a dangling link the unprivileged unlink() above was not allowed to remove
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

void installCommandLineToolAndReport(QWidget* dialogParent)
{
	const std::expected<QString, QString> result = installCommandLineTool();
	MessageBox::notice(dialogParent, QStringLiteral("Command line tool"), result ? *result : result.error(), {},
		result ? QMessageBox::Information : QMessageBox::Warning);
}
