#include "updatecheck.h"
#include "settings.h"
#include "version.h"

#include "updaterUI/cupdaterdialog.h"

DISABLE_COMPILER_WARNINGS
#include <QDateTime>
#include <QSettings>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>

namespace {

constexpr int64_t SecondsPerDay = 24 * 60 * 60;

QString repositoryName()
{
	return QStringLiteral("VioletGiraffe/GoodGit");
}

void recordCheckTime()
{
	QSettings{}.setValue(Settings::LastUpdateCheckTimestampKey, QDateTime::currentDateTime());
}

} // namespace

void checkForUpdatesInteractively(QWidget* parent)
{
	recordCheckTime();
	CUpdaterDialog{ parent, repositoryName(), QStringLiteral(GG_VERSION) }.exec();
}

void checkForUpdatesIfDue()
{
	const QSettings settings;
	if (!settings.value(Settings::CheckForUpdatesAutomaticallyKey, Settings::CheckForUpdatesAutomaticallyDefault).toBool())
		return;

	const QDateTime lastCheck = settings.value(Settings::LastUpdateCheckTimestampKey).toDateTime();
	const int64_t secondsSinceCheck = lastCheck.secsTo(QDateTime::currentDateTime());
	// A stored time in the future - the clock moved back, or the settings came from another machine - is due, not postponed until it passes
	if (lastCheck.isValid() && secondsSinceCheck >= 0 && secondsSinceCheck < SecondsPerDay)
		return;

	recordCheckTime();
	// Parentless: the check starts before any window is up
	auto* dialog = new CUpdaterDialog{ nullptr, repositoryName(), QStringLiteral(GG_VERSION), true };
	QObject::connect(dialog, &QDialog::finished, dialog, &QDialog::deleteLater);
}
