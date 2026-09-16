#include "smoketest.h"
#include "commitwindow.h"
#include "filelistview.h"
#include "repositorywindows.h"
#include "theme.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QImageReader>
#include <QPointer>
#include <QSettings>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <cstdio>
#include <cstdlib>

namespace {

constexpr int TimeoutMs = 20'000;
constexpr int PollIntervalMs = 100;

void reportFailure(const QString& reason)
{
	std::fprintf(stderr, "Smoke test failed: %s\n", qUtf8Printable(reason));
	std::fflush(stderr);
}

// The checks for what only a deployed build can lack
[[nodiscard]] QString deploymentProblem()
{
	if (!QImageReader::supportedImageFormats().contains("svg"))
		return QStringLiteral("no svg image format plugin: the theme's glyphs would not render");
	if (QSslSocket::availableBackends().isEmpty())
		return QStringLiteral("no TLS backend plugin: the update check would fail");
	return {};
}

} // namespace

bool startSmokeTest(QApplication& app, const QString& repositoryPath)
{
	// Static: the settings are written until the event loop ends
	static const QTemporaryDir settingsDirectory;
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
	applyTheme(app);

	// Ends the process, not the event loop: a modal dialog, such as a failed open's report, runs a loop of its own
	QTimer::singleShot(TimeoutMs, [] {
		reportFailure(QStringLiteral("no file list row within %1 s").arg(TimeoutMs / 1000));
		std::_Exit(1);
	});

	if (const QString problem = deploymentProblem(); !problem.isEmpty())
	{
		reportFailure(problem);
		return false;
	}

	const QPointer<CommitWindow> window = openRepositoryWindowAt(repositoryPath, nullptr);
	if (!window)
	{
		reportFailure(QStringLiteral("could not open %1").arg(repositoryPath));
		return false;
	}

	auto* poll = new QTimer{ &app };
	QObject::connect(poll, &QTimer::timeout, &app, [poll, window] {
		const FileListView* fileList = window ? window->findChild<FileListView*>() : nullptr;
		if (!fileList || fileList->model()->rowCount() == 0)
			return;

		poll->stop();
		std::printf("Smoke test passed\n");
		std::fflush(stdout);
		QApplication::closeAllWindows();
		QApplication::exit(0);
	});
	poll->start(PollIntervalMs);
	return true;
}
