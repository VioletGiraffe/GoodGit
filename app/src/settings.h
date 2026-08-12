#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Key vocabulary over qtutils CSettings. Window geometry is handled separately by CPersistenceEnabler.
namespace Settings {

[[nodiscard]] QString gitExecutable(); // "git" unless overridden in the settings storage

[[nodiscard]] QByteArray splitterState();
void setSplitterState(const QByteArray& state);

[[nodiscard]] QStringList recentMessages(const QString& repoPath);
void addRecentMessage(const QString& repoPath, const QString& message);

} // namespace Settings
