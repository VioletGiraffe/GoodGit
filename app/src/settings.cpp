#include "settings.h"

#include "settings/csettings.h"

namespace Settings {

namespace {

QString splitterKey(const QString& name)
{
	return name + QStringLiteral("/splitterState");
}

} // namespace

QString gitExecutable()
{
	return CSettings{}.value(QStringLiteral("GitExecutable"), QStringLiteral("git")).toString();
}

QString hgExecutable()
{
	return CSettings{}.value(QStringLiteral("HgExecutable"), QStringLiteral("hg")).toString();
}

QByteArray splitterState(const QString& name)
{
	return CSettings{}.value(splitterKey(name)).toByteArray();
}

void setSplitterState(const QString& name, const QByteArray& state)
{
	CSettings{}.setValue(splitterKey(name), state);
}

} // namespace Settings
