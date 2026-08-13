#pragma once

#include <QByteArray>
#include <QString>

// Key vocabulary over qtutils CSettings. Window geometry is handled separately by CPersistenceEnabler.
namespace Settings {

[[nodiscard]] QString gitExecutable(); // "git" unless overridden in the settings storage

[[nodiscard]] QByteArray splitterState();
void setSplitterState(const QByteArray& state);

} // namespace Settings
