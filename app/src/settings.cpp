#include "settings.h"

#include "settings/csettings.h"

namespace Settings {

QString gitExecutable()
{
	return CSettings{}.value(QStringLiteral("GitExecutable"), QStringLiteral("git")).toString();
}

QByteArray splitterState()
{
	return CSettings{}.value(QStringLiteral("CommitWindow/splitterState")).toByteArray();
}

void setSplitterState(const QByteArray& state)
{
	CSettings{}.setValue(QStringLiteral("CommitWindow/splitterState"), state);
}

} // namespace Settings
