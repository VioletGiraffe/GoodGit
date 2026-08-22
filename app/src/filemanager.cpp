#include "filemanager.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

QString openInFileManagerActionText()
{
#ifdef Q_OS_WIN
	return QStringLiteral("Open in &Explorer");
#elif defined Q_OS_MACOS
	return QStringLiteral("Open in &Finder");
#else
	return QStringLiteral("Open in &file manager");
#endif
}

QString showInFileManagerActionText()
{
#ifdef Q_OS_WIN
	return QStringLiteral("Show in &Explorer");
#elif defined Q_OS_MACOS
	return QStringLiteral("&Reveal in Finder");
#else
	return QStringLiteral("Show in &file manager");
#endif
}

void openInFileManager(const QString& directory)
{
	QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

void showInFileManager(const QString& path)
{
#ifdef Q_OS_WIN
	QProcess::startDetached(QStringLiteral("explorer"), { QStringLiteral("/select,") + QDir::toNativeSeparators(path) });
#elif defined Q_OS_MACOS
	QProcess::startDetached(QStringLiteral("open"), { QStringLiteral("-R"), path });
#else
	openInFileManager(QFileInfo{ path }.absolutePath());
#endif
}
