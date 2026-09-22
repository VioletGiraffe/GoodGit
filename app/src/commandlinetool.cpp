#include "commandlinetool.h"
#ifdef Q_OS_MACOS
#include "commandlinetool_mac.h"
#endif

#include "dialogs/messagedialog.h"

DISABLE_COMPILER_WARNINGS
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace {

QString linkDirectory()
{
#ifdef Q_OS_MACOS
	return QStringLiteral("/usr/local/bin"); // on the default PATH via /etc/paths, and unlike /usr/bin not protected by SIP, so root can write here
#else
	return QDir::homePath() + QStringLiteral("/.local/bin"); // the per-user executable directory of the XDG base directory spec
#endif
}

QString linkFilePath()
{
	return linkDirectory() + QStringLiteral("/gg");
}

// Appends how to get the link's directory on PATH where it is not
QString withNoticeIfNotOnPath(const QString& message)
{
#ifndef Q_OS_MACOS // a macOS GUI app does not inherit the terminal's PATH, so its own says nothing about it
	const QStringList pathEntries = qEnvironmentVariable("PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts);
	const QString directory = linkDirectory();
	if (std::ranges::none_of(pathEntries, [&directory](const QString& entry) { return QDir::cleanPath(entry) == directory; }))
		return message + QStringLiteral("\n\n%1 is not on PATH. A new login may add it; if not, add it to PATH in your shell's startup file.").arg(directory);
#endif
	return message;
}

}

bool commandLineToolLinkMissingOrBroken()
{
	return !QFileInfo::exists(linkFilePath()); // follows the link, so a dangling one reads as absent
}

std::expected<QString, QString> installCommandLineTool()
{
	const QString target = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
	const QString linkPath = linkFilePath();
	const QByteArray encodedLinkPath = QFile::encodeName(linkPath);

	const QFileInfo link{ linkPath };
	if (link.isSymbolicLink())
	{
		if (link.symLinkTarget() == target)
			return withNoticeIfNotOnPath(QStringLiteral("%1 already points at this copy of GoodGit.").arg(linkPath));

		if (link.exists()) // exists() follows the link: it points at something that is not ours to take
			return std::unexpected(QStringLiteral("%1 already points at %2.\n\nRemove it first if you want it to launch GoodGit.")
				.arg(linkPath, link.symLinkTarget()));

		// Dangling, so replacing it takes nothing away; symlink() below will not overwrite
		::unlink(encodedLinkPath.constData());
	}
	else if (link.exists())
	{
		return std::unexpected(QStringLiteral("%1 already exists and is not a symbolic link.\n\nRemove it first if you want it to launch GoodGit.")
			.arg(linkPath));
	}

	QDir{}.mkpath(linkDirectory()); // a failure surfaces as symlink()'s ENOENT
	if (::symlink(QFile::encodeName(target).constData(), encodedLinkPath.constData()) != 0)
	{
		const int error = errno;
		const auto failed = [&linkPath](const QString& reason) { return std::unexpected(QStringLiteral("Could not create %1: %2").arg(linkPath, reason)); };
#ifdef Q_OS_MACOS
		// The directory is not writable, or does not exist on a Mac that never had one: both are what
		// administrator credentials fix. Anything else is not.
		if (error != EACCES && error != EPERM && error != ENOENT)
			return failed(QString::fromLocal8Bit(std::strerror(error)));

		const std::expected<Elevated, QString> elevated = createLinkAsAdministrator(target, linkPath);
		if (!elevated)
			return failed(elevated.error());

		if (*elevated == Elevated::Cancelled)
			return QStringLiteral("Cancelled - %1 was not created.").arg(linkPath);
#else
		return failed(QString::fromLocal8Bit(std::strerror(error)));
#endif
	}

	return withNoticeIfNotOnPath(QStringLiteral("The gg command is now available: %1").arg(linkPath));
}

void installCommandLineToolAndReport(QWidget* dialogParent)
{
	const std::expected<QString, QString> result = installCommandLineTool();
	MessageDialog::notice(dialogParent, QStringLiteral("Command line tool"), result ? *result : result.error(), {},
		result ? QMessageBox::Information : QMessageBox::Warning);
}
