#pragma once

#include "settingsui/csettingspage.h"

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
	MainSettingsPage();
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
	ThemeFontSettingsPage();
	void acceptSettings() override;

private:
	QComboBox* _colorScheme = nullptr;
	QCheckBox* _systemFont = nullptr;
	QFontComboBox* _fontFamily = nullptr;
	QSpinBox* _fontSize = nullptr;
	QSpinBox* _diffTabWidth = nullptr;
};
