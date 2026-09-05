#include "welcomewindow.h"
#ifdef Q_OS_MACOS
#include "commandlinetool_mac.h"
#endif
#include "recentrepositories.h"
#include "recentrepositoriespanel.h"
#include "repositorywindows.h"
#include "settings.h"
#include "version.h"

#include "settingsui/csettingsdialog.h"
#include "widgets/widgetutils.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QCheckBox>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

namespace {

constexpr int WindowWidth = 520;
constexpr int IntroOnlyHeight = 300;
constexpr int WithRecentListHeight = 500;
constexpr int AppIconSize = 56;
constexpr int TitlePointSizeIncrease = 8;
constexpr int Inset = 24; // the introduction is inset; the list runs to the window's edges, as in the dock

} // namespace

WelcomeWindow::WelcomeWindow()
{
	setObjectName(QStringLiteral("welcomeWindow")); // the stylesheet's window background reaches a plain QWidget only by name
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(QApplication::applicationName());

	auto* iconLabel = new QLabel;
	iconLabel->setPixmap(QIcon{ QStringLiteral(":/goodgit.svg") }.pixmap(AppIconSize, AppIconSize));
	auto* titleLabel = new QLabel(QApplication::applicationName());
	QFont titleFont = titleLabel->font();
	titleFont.setPointSize(titleFont.pointSize() + TitlePointSizeIncrease);
	titleFont.setBold(true);
	titleLabel->setFont(titleFont);

	auto* versionLabel = new QLabel(QStringLiteral(GG_VERSION));
	versionLabel->setObjectName(QStringLiteral("appVersionLabel")); // dimmed by the stylesheet

	// Its own row: the version belongs to the name, and sits closer to it than the name does to the icon
	auto* nameRow = new QHBoxLayout;
	nameRow->setSpacing(6);
	nameRow->addWidget(titleLabel);
	versionLabel->setContentsMargins(0, 10, 0, 0); // off the title's top line, by eye
	nameRow->addWidget(versionLabel, 0, Qt::AlignTop);

	auto* titleRow = new QHBoxLayout;
	titleRow->setSpacing(12);
	titleRow->addStretch();
	titleRow->addWidget(iconLabel);
	titleRow->addLayout(nameRow);
	titleRow->addStretch();

	auto* introLabel = new QLabel(tr("Drop a Git or Mercurial repository - or any folder inside a repository - onto this window, or choose a repository below."));
	introLabel->setWordWrap(true);

	auto* openButton = new QPushButton(tr("Open Repository..."));
	auto* scanButton = new QPushButton(tr("Scan Folder for Repositories..."));
	scanButton->setToolTip(tr("Scans the folder and adds every repository to the recent list below (not recursive)"));

	auto* buttonRow = new QHBoxLayout;
	buttonRow->addWidget(openButton);
	buttonRow->addWidget(scanButton);
	buttonRow->addStretch();

	auto* intro = new QWidget;
	auto* introLayout = new QVBoxLayout(intro);
	introLayout->setContentsMargins(Inset, Inset, Inset, Inset);
	introLayout->setSpacing(16);
	introLayout->addLayout(titleRow);
	introLayout->addWidget(introLabel);
	introLayout->addLayout(buttonRow);

#ifdef Q_OS_MACOS
	if (commandLineToolLinkMissingOrBroken())
	{
		auto* toolLabel = new QLabel(tr("The 'gg' command line tool opens a repository from a terminal."));
		toolLabel->setWordWrap(true);
		auto* installButton = new QPushButton(tr("Install 'gg' Command Line Tool..."));

		auto* toolSection = new QWidget;
		auto* toolLayout = new QVBoxLayout(toolSection);
		toolLayout->setContentsMargins(0, 0, 0, 0);
		toolLayout->setSpacing(8);
		toolLayout->addWidget(toolLabel);
		toolLayout->addWidget(installButton, 0, Qt::AlignLeft);
		introLayout->addWidget(toolSection);

		connect(installButton, &QPushButton::clicked, this, [this, toolSection] {
			installCommandLineToolAndReport(this);
			// The install can be cancelled or refused, so the section stays until the link is really there
			toolSection->setVisible(commandLineToolLinkMissingOrBroken());
		});
	}
#endif

	auto* skipOnStartup = new QCheckBox(tr("Open the last used repository on startup (skip this window; you can restore this in Preferences)"));
	skipOnStartup->setChecked(Settings::startsWithLastRepository());
	skipOnStartup->setToolTip(tr("This window still opens when there is no repository to reopen, and View > Show "
		"Welcome Screen opens it at any time."));
	introLayout->addWidget(skipOnStartup);

	// Written as toggled: this window has no OK button to store it on
	connect(skipOnStartup, &QCheckBox::toggled, this, [](bool skip) {
		QSettings{}.setValue(Settings::StartupActionKey,
			QLatin1String(skip ? Settings::StartupActionLastRepository : Settings::StartupActionWelcomeScreen));
	});
	// Preferences holds the same setting, and a repository window can have it open while this window stands
	connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, this, [skipOnStartup] {
		skipOnStartup->setChecked(Settings::startsWithLastRepository());
	});

	auto* panel = new RecentRepositoriesPanel{ QString{} }; // no current repository to mark
	auto* filterEdit = panel->createFilterField();

	// The field shares the label's row, unlike the dock's: no buttons here whose minimum width it would add to
	auto* recentHeader = new QWidget;
	auto* recentHeaderLayout = new QHBoxLayout(recentHeader);
	recentHeaderLayout->setContentsMargins(Inset, 0, Inset, 6);
	recentHeaderLayout->setSpacing(12);
	recentHeaderLayout->addWidget(new QLabel(tr("Recent")));
	recentHeaderLayout->addWidget(filterEdit);

	auto* recentSection = new QWidget;
	auto* recentLayout = new QVBoxLayout(recentSection);
	recentLayout->setContentsMargins(0, 0, 0, 0);
	recentLayout->setSpacing(0);
	recentLayout->addWidget(recentHeader);
	recentLayout->addWidget(panel, 1);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(intro);
	layout->addWidget(recentSection, 1);

	const bool anyRecent = !RecentRepositories::list().empty();
	recentSection->setVisible(anyRecent);

	// The keyboard cursor starts on the first row: Enter then opens the most recently used repository
	if (anyRecent)
		panel->focusFirstRow(); // with no list, the first button keeps the focus

	// The list can fill while this window stands: a folder scan adds to it. It can also empty: a row's context menu drops it
	connect(&RecentRepositories::Notifier::instance(), &RecentRepositories::Notifier::changed, this,
		[this, recentSection, filterEdit, panel] {
			const bool showList = !RecentRepositories::list().empty();
			const bool appearing = showList && recentSection->isHidden();
			if (!showList)
				filterEdit->clear(); // hidden with the section, it must not still be filtering when the list comes back
			// The window sized for the introduction alone leaves the list no height to appear in
			if (appearing && height() < WithRecentListHeight)
				WidgetUtils::centerWidgetOnScreen(this, QSize{ width(), WithRecentListHeight });
			recentSection->setVisible(showList);
			if (appearing)
				panel->focusFirstRow();
		});

	connect(openButton, &QPushButton::clicked, this, [this] { browseForRepository(this); });
	connect(scanButton, &QPushButton::clicked, this, [this] { scanFolderForRepositories(this, ScanReport::ExceptAdditions); });
	acceptRepositoryFolderDrops(this);

	// The recent list's filter is the only thing here to find anything in
	new QShortcut(QKeySequence::Find, this, [filterEdit] {
		if (!filterEdit->isVisible())
			return; // no list, no filter

		filterEdit->setFocus();
		filterEdit->selectAll(); // a second Find replaces what the first one typed
	});

	resize(WindowWidth, anyRecent ? WithRecentListHeight : IntroOnlyHeight);
	WidgetUtils::centerWidgetOnScreen(this);
}
