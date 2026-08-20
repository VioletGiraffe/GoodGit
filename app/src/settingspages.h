#pragma once

#include "settingsui/csettingspage.h"

#include <Qt>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLineEdit;
class QSpinBox;

// The Preferences pages. Each loads the current values in its constructor and stores them in
// acceptSettings(); CSettingsNotifier is what re-applies them to open windows.

class MainSettingsPage final : public CSettingsPage
{
public:
	explicit MainSettingsPage(QWidget* parent = nullptr);
	void acceptSettings() override;

private:
	QLineEdit* _gitExecutable = nullptr;
	QLineEdit* _hgExecutable = nullptr;
	QSpinBox* _historyDepth = nullptr;
	QSpinBox* _maxDiffMb = nullptr;
	QCheckBox* _showEolOnlyChanges = nullptr;
	QSpinBox* _subjectGuideColumn = nullptr;
	QCheckBox* _completionAutoPopup = nullptr;
	QSpinBox* _completionMinPrefix = nullptr;
	QComboBox* _newRowCheckPolicy = nullptr;
};

class ThemeFontSettingsPage final : public CSettingsPage
{
public:
	explicit ThemeFontSettingsPage(QWidget* parent = nullptr);
	void acceptSettings() override;
	void rejectSettings() override;

private:
	QComboBox* _colorScheme = nullptr;
	QCheckBox* _systemFont = nullptr;
	QFontComboBox* _fontFamily = nullptr;
	QSpinBox* _fontSize = nullptr;
	QSpinBox* _diffTabWidth = nullptr;
	// The scheme and theme picks apply as they are made, so cancelling has to put back what was in force on entry
	Qt::ColorScheme _schemeOnEntry = Qt::ColorScheme::Unknown;
	QString _lightThemeOnEntry;
	QString _darkThemeOnEntry;
};
