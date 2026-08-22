#include "welcomewindow.h"
#ifdef Q_OS_MACOS
#include "commandlinetool_mac.h"
#endif
#include "recentrepositories.h"
#include "recentrepositoriespanel.h"
#include "repositorywindows.h"

#include <QApplication>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

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

	auto* titleRow = new QHBoxLayout;
	titleRow->setSpacing(12);
	titleRow->addWidget(iconLabel);
	titleRow->addWidget(titleLabel);
	titleRow->addStretch();

	auto* introLabel = new QLabel(tr("Each window shows one repository.\n\n"
		"Drop a folder from inside a Git or Mercurial repository onto this window, or choose one below."));
	introLabel->setWordWrap(true);

	auto* openButton = new QPushButton(tr("Open Repository..."));
	auto* scanButton = new QPushButton(tr("Scan Folder for Repositories..."));
	scanButton->setToolTip(tr("Add every repository directly inside a folder to the recent list, without opening them"));

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

	auto* recentLabel = new QLabel(tr("Recent"));
	recentLabel->setContentsMargins(Inset, 0, Inset, 6);
	auto* recentSection = new QWidget;
	auto* recentLayout = new QVBoxLayout(recentSection);
	recentLayout->setContentsMargins(0, 0, 0, 0);
	recentLayout->setSpacing(0);
	recentLayout->addWidget(recentLabel);
	recentLayout->addWidget(new RecentRepositoriesPanel{ QString{} }, 1); // no repository of its own to mark

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(intro);
	layout->addWidget(recentSection, 1);

	const bool anyRecent = !RecentRepositories::list().empty();
	recentSection->setVisible(anyRecent);
	// The list can empty while this window stands: a row's context menu drops it
	connect(&RecentRepositories::Notifier::instance(), &RecentRepositories::Notifier::changed, this,
		[recentSection] { recentSection->setVisible(!RecentRepositories::list().empty()); });

	connect(openButton, &QPushButton::clicked, this, [this] { browseForRepository(this); });
	connect(scanButton, &QPushButton::clicked, this, [this] { scanFolderForRepositories(this); });
	acceptRepositoryFolderDrops(this);

	resize(WindowWidth, anyRecent ? WithRecentListHeight : IntroOnlyHeight);
	if (const QScreen* screen = QApplication::primaryScreen())
		move(screen->availableGeometry().center() - rect().center());
}
