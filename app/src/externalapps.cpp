#include "externalapps.h"
#include "settings.h"

#include "dialogs/messagebox.h"
#include "settings/csettings.h"

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

void openInTextEditor(const QString& path, QWidget* dialogParent)
{
	QString command = CSettings{}.value(Settings::TextEditorCommandKey).toString().trimmed();
	if (command.isEmpty())
		command = QLatin1String(Settings::TextEditorCommandDefault);

	// splitCommand() yields nothing for a command that is only quotes or whitespace
	QStringList arguments = QProcess::splitCommand(command);
	if (arguments.isEmpty())
	{
		MessageBox::notice(dialogParent, QStringLiteral("No text editor configured"),
			QStringLiteral("Set the command to edit a file with in Preferences > Main, using %path% where the file goes."),
			{}, QMessageBox::Information);
		return;
	}

	const QString program = arguments.takeFirst();
	const QString nativePath = QDir::toNativeSeparators(path);
	// Substituted after the split, so a path with spaces in it stays one argument whether or not it is quoted
	if (command.contains(QStringLiteral("%path%"), Qt::CaseInsensitive))
	{
		for (QString& argument : arguments)
			argument.replace(QStringLiteral("%path%"), nativePath, Qt::CaseInsensitive);
	}
	else
		arguments << nativePath;

	if (!QProcess::startDetached(program, arguments))
	{
		MessageBox::notice(dialogParent, QStringLiteral("Could not start the text editor"),
			QStringLiteral("'%1' could not be started. Check the command in Preferences > Main.").arg(program),
			command);
	}
}
