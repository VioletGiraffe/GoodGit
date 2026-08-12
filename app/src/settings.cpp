#include "settings.h"

#include "settings/csettings.h"

#include <QCryptographicHash>
#include <QDir>

namespace {

constexpr int MaxRecentMessages = 20;

QString recentMessagesKey(const QString& repoPath)
{
	const QByteArray canonical = QDir::cleanPath(repoPath).toUtf8();
	return QStringLiteral("RecentMessages/") + QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Md5).toHex());
}

} // namespace

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

QStringList recentMessages(const QString& repoPath)
{
	return CSettings{}.value(recentMessagesKey(repoPath)).toStringList();
}

void addRecentMessage(const QString& repoPath, const QString& message)
{
	const QString key = recentMessagesKey(repoPath);
	CSettings settings;
	QStringList list = settings.value(key).toStringList();
	list.removeAll(message);
	list.prepend(message);
	while (list.size() > MaxRecentMessages)
		list.removeLast();
	settings.setValue(key, list);
}

} // namespace Settings
