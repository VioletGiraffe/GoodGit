#include "commandlinetool_mac.h"

DISABLE_COMPILER_WARNINGS
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

#import <Foundation/Foundation.h>

namespace {

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

std::expected<Elevated, QString> createLinkAsAdministrator(const QString& target, const QString& linkPath)
{
	// -f replaces a dangling link that an unprivileged unlink() could not remove
	const QString command = QStringLiteral("mkdir -p %1 && ln -sfn %2 %3")
		.arg(shellQuoted(QFileInfo(linkPath).path()), shellQuoted(target), shellQuoted(linkPath));

	return runAsAdministrator(command);
}
