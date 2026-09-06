#include "appmenus.h"
#ifdef Q_OS_MACOS
#include "commandlinetool_mac.h"
#endif
#include "settingspages.h"
#include "updatecheck.h"
#include "version.h"

#include "aboutdialog/caboutdialog.h"
#include "settingsui/csettingsdialog.h"
#ifdef _DEBUG
#include "ui/widget-gallery/cwidgetgallery.h"
#endif

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
RESTORE_COMPILER_WARNINGS

namespace {

void showPreferencesDialog(QWidget* dialogParent)
{
	CSettingsDialog dialog{ dialogParent };
	dialog.addSettingsPage(new MainSettingsPage{ &dialog }, QObject::tr("Main"))
		.addSettingsPage(new ThemeFontSettingsPage{ &dialog }, QObject::tr("Theme & Font")); // a QListWidgetItem shows text verbatim, no mnemonic escaping
	dialog.exec();
}

} // namespace

QAction* addFileMenu(QMenuBar& menuBar, QWidget* dialogParent)
{
	QMenu* menu = menuBar.addMenu(QObject::tr("&File"));
	QAction* openRepositoryAction = menu->addAction(QObject::tr("&Open Repository..."), dialogParent,
		[dialogParent] { browseForRepository(dialogParent); });
	openRepositoryAction->setShortcut(QKeySequence::Open);
	menu->addSeparator();
#ifdef Q_OS_MACOS
	menu->addAction(QObject::tr("Install 'gg' Command Line Tool..."), dialogParent,
		[dialogParent] { installCommandLineToolAndReport(dialogParent); });
	menu->addSeparator();
#endif
	menu->addAction(QObject::tr("E&xit"), [] { QApplication::closeAllWindows(); }); // not quit(): closeEvent saves the layout state
	return openRepositoryAction;
}

void addEditMenu(QMenuBar& menuBar, QWidget* dialogParent)
{
	QMenu* menu = menuBar.addMenu(QObject::tr("&Edit"));
	menu->addAction(QObject::tr("&Preferences..."), dialogParent, [dialogParent] { showPreferencesDialog(dialogParent); })
		->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P));
}

QMenu* addRepositoryMenu(QMenuBar& menuBar, QWidget* dialogParent, QAction* openAction, ScanReport scanReport)
{
	QMenu* menu = menuBar.addMenu(QObject::tr("&Repository"));
	menu->addAction(openAction); // the same action as in File, where Ctrl+O is looked for
	menu->addAction(QObject::tr("&Scan Folder for Repositories..."), dialogParent,
		[dialogParent, scanReport] { scanFolderForRepositories(dialogParent, scanReport); });
	menu->addAction(QObject::tr("C&lear Recent Repositories..."), dialogParent, [dialogParent] { clearRecentRepositories(dialogParent); });
	return menu;
}

void addHelpMenu(QMenuBar& menuBar, QWidget* dialogParent)
{
	QMenu* menu = menuBar.addMenu(QObject::tr("&Help"));
	menu->addAction(QObject::tr("Check for &Updates..."), dialogParent, [dialogParent] { checkForUpdatesInteractively(dialogParent); });
#ifdef _DEBUG
	menu->addSeparator();
	// Not tr(): debug-only functionality is never translated
	menu->addAction(QStringLiteral("Widget &Gallery"), &CWidgetGalleryWindow::showNew);
#endif
	menu->addSeparator();
	// Always the last item in Help
	menu->addAction(QObject::tr("&About"), dialogParent, [dialogParent] {
		CAboutDialog aboutDialog{ QStringLiteral(GG_VERSION), dialogParent };
		aboutDialog.exec();
	});
}
