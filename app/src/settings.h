#pragma once

#include <QByteArray>
#include <QString>

// Key vocabulary over qtutils CSettings. Window geometry is handled separately by CPersistenceEnabler.
namespace Settings {

[[nodiscard]] QString gitExecutable(); // "git" unless overridden in the settings storage
[[nodiscard]] QString hgExecutable();  // "hg" unless overridden in the settings storage

// One entry per splitter; `name` identifies it across the whole app, not just within its window
[[nodiscard]] QByteArray splitterState(const QString& name);
void setSplitterState(const QString& name, const QByteArray& state);

} // namespace Settings
