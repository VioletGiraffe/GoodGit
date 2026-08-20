#include "settingspages.h"
#include "settings.h"
#include "theme.h"

#include "settings/csettings.h"
#include "theme/cthemecontroller.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

namespace {

// An executable path edit with its Browse button, as one form-row widget. An empty field means the
// default from PATH, which the placeholder shows.
QWidget* executableRow(QWidget* parent, QLineEdit*& edit, const char* settingsKey, const char* defaultName)
{
	auto* row = new QWidget{ parent };
	auto* layout = new QHBoxLayout{ row };
	layout->setContentsMargins(0, 0, 0, 0);
	edit = new QLineEdit;
	edit->setText(CSettings{}.value(settingsKey).toString());
	edit->setPlaceholderText(QLatin1String(defaultName));
	auto* browseButton = new QPushButton{ MainSettingsPage::tr("Browse...") };
	layout->addWidget(edit, 1);
	layout->addWidget(browseButton);

	QObject::connect(browseButton, &QPushButton::clicked, edit, [edit] {
#ifdef Q_OS_WIN
		const QString filter = MainSettingsPage::tr("Executables (*.exe *.cmd *.bat);;All files (*)");
#else
		const QString filter;
#endif
		const QString file = QFileDialog::getOpenFileName(edit->window(), MainSettingsPage::tr("Select the executable"), edit->text(), filter);
		if (!file.isEmpty())
			edit->setText(QDir::toNativeSeparators(file));
	});
	return row;
}

// Index order of the "newly listed files start checked" combo
constexpr const char* NewRowCheckPolicyByIndex[] = {
	Settings::NewRowCheckPolicyTracked, Settings::NewRowCheckPolicyAll, Settings::NewRowCheckPolicyNone };

} // namespace

MainSettingsPage::MainSettingsPage()
{
	const CSettings settings;
	auto* layout = new QFormLayout{ this };

	layout->addRow(tr("Git executable:"), executableRow(this, _gitExecutable, Settings::GitExecutableKey, Settings::GitExecutableDefault));
	layout->addRow(tr("Mercurial executable:"), executableRow(this, _hgExecutable, Settings::HgExecutableKey, Settings::HgExecutableDefault));

	_historyDepth = new QSpinBox;
	_historyDepth->setRange(100, 1'000'000);
	_historyDepth->setSingleStep(1000);
	_historyDepth->setGroupSeparatorShown(true);
	_historyDepth->setValue(settings.value(Settings::HistoryMaxCommitsKey, Settings::HistoryMaxCommitsDefault).toInt());
	_historyDepth->setToolTip(tr("How many commits a history window loads. \"Load more\" doubles from here."));
	layout->addRow(tr("History depth, commits:"), _historyDepth);

	_maxDiffMb = new QSpinBox;
	_maxDiffMb->setRange(1, 64);
	_maxDiffMb->setSuffix(tr(" MB"));
	_maxDiffMb->setValue(int(settings.value(Settings::MaxShownDiffBytesKey, Settings::MaxShownDiffBytesDefault).toLongLong() / (1024 * 1024)));
	_maxDiffMb->setToolTip(tr("Diffs and files larger than this are reported instead of displayed."));
	layout->addRow(tr("Largest diff to display:"), _maxDiffMb);

	_showEolOnlyChanges = new QCheckBox{ tr("Show changes where only the line endings differ") };
	_showEolOnlyChanges->setChecked(settings.value(Settings::ShowLineEndingOnlyChangesKey, Settings::ShowLineEndingOnlyChangesDefault).toBool());
	_showEolOnlyChanges->setToolTip(tr("Applies to the shown diffs and the line counts. Either way the file "
		"lists as modified and commits its content byte for byte."));
	layout->addRow(_showEolOnlyChanges);

	_subjectGuideColumn = new QSpinBox;
	_subjectGuideColumn->setRange(20, 200);
	_subjectGuideColumn->setValue(settings.value(Settings::SubjectGuideColumnKey, Settings::SubjectGuideColumnDefault).toInt());
	layout->addRow(tr("Commit subject guide at column:"), _subjectGuideColumn);

	_completionAutoPopup = new QCheckBox{ tr("Suggest message completions while typing") };
	_completionAutoPopup->setChecked(settings.value(Settings::CompletionAutoPopupKey, Settings::CompletionAutoPopupDefault).toBool());
	_completionAutoPopup->setToolTip(tr("Ctrl+Space asks for completions either way."));
	layout->addRow(_completionAutoPopup);
	_completionMinPrefix = new QSpinBox;
	_completionMinPrefix->setRange(1, 10);
	_completionMinPrefix->setValue(settings.value(Settings::CompletionMinPrefixLengthKey, Settings::CompletionMinPrefixLengthDefault).toInt());
	_completionMinPrefix->setEnabled(_completionAutoPopup->isChecked());
	connect(_completionAutoPopup, &QCheckBox::toggled, _completionMinPrefix, &QWidget::setEnabled);
	layout->addRow(tr("...after this many characters:"), _completionMinPrefix);

	_newRowCheckPolicy = new QComboBox;
	_newRowCheckPolicy->addItems({ tr("Unless untracked"), tr("Always"), tr("Never") });
	const QString policy = settings.value(Settings::NewRowCheckPolicyKey).toString();
	_newRowCheckPolicy->setCurrentIndex(policy == QLatin1String(Settings::NewRowCheckPolicyAll) ? 1
		: policy == QLatin1String(Settings::NewRowCheckPolicyNone) ? 2 : 0);
	_newRowCheckPolicy->setToolTip(tr("Rows already listed keep their check state across a refresh; this is "
		"the state a newly appearing row starts with."));
	layout->addRow(tr("Newly listed files start checked:"), _newRowCheckPolicy);
}

void MainSettingsPage::acceptSettings()
{
	CSettings settings;
	settings.setValue(Settings::GitExecutableKey, _gitExecutable->text().trimmed());
	settings.setValue(Settings::HgExecutableKey, _hgExecutable->text().trimmed());
	settings.setValue(Settings::HistoryMaxCommitsKey, _historyDepth->value());
	settings.setValue(Settings::MaxShownDiffBytesKey, qlonglong{ _maxDiffMb->value() } * 1024 * 1024);
	settings.setValue(Settings::ShowLineEndingOnlyChangesKey, _showEolOnlyChanges->isChecked());
	settings.setValue(Settings::SubjectGuideColumnKey, _subjectGuideColumn->value());
	settings.setValue(Settings::CompletionAutoPopupKey, _completionAutoPopup->isChecked());
	settings.setValue(Settings::CompletionMinPrefixLengthKey, _completionMinPrefix->value());
	settings.setValue(Settings::NewRowCheckPolicyKey, QLatin1String(NewRowCheckPolicyByIndex[_newRowCheckPolicy->currentIndex()]));
}

ThemeFontSettingsPage::ThemeFontSettingsPage()
{
	auto* layout = new QFormLayout{ this };

	_colorScheme = new QComboBox;
	// Index order matches the acceptSettings() mapping below
	_colorScheme->addItems({ tr("System"), tr("Light"), tr("Dark") });
	switch (CThemeController::instance().schemePreference())
	{
	case Qt::ColorScheme::Unknown: _colorScheme->setCurrentIndex(0); break;
	case Qt::ColorScheme::Light: _colorScheme->setCurrentIndex(1); break;
	case Qt::ColorScheme::Dark: _colorScheme->setCurrentIndex(2); break;
	}
	layout->addRow(tr("Color scheme:"), _colorScheme);

	_systemFont = new QCheckBox{ tr("Use the system monospace font") };
	// An empty stored family means no override, exactly as monospaceFont() reads it
	_systemFont->setChecked(CSettings{}.value(Settings::MonospaceFontFamilyKey).toString().isEmpty());
	layout->addRow(_systemFont);

	const QFont currentMono = monospaceFont();
	_fontFamily = new QFontComboBox;
	_fontFamily->setFontFilters(QFontComboBox::MonospacedFonts);
	_fontFamily->setCurrentFont(currentMono);
	layout->addRow(tr("Monospace font:"), _fontFamily);
	_fontSize = new QSpinBox;
	_fontSize->setRange(6, 72);
	_fontSize->setSuffix(tr(" pt"));
	_fontSize->setValue(currentMono.pointSize());
	layout->addRow(tr("Font size:"), _fontSize);

	const auto enableFontPickers = [this](bool systemFont) {
		_fontFamily->setEnabled(!systemFont);
		_fontSize->setEnabled(!systemFont);
	};
	enableFontPickers(_systemFont->isChecked());
	connect(_systemFont, &QCheckBox::toggled, this, enableFontPickers);

	_diffTabWidth = new QSpinBox;
	_diffTabWidth->setRange(1, 16);
	_diffTabWidth->setSuffix(tr(" spaces"));
	_diffTabWidth->setValue(CSettings{}.value(Settings::DiffTabWidthKey, Settings::DiffTabWidthDefault).toInt());
	layout->addRow(tr("Tab width in diffs:"), _diffTabWidth);
}

void ThemeFontSettingsPage::acceptSettings()
{
	static constexpr Qt::ColorScheme schemeByIndex[] = {
		Qt::ColorScheme::Unknown, Qt::ColorScheme::Light, Qt::ColorScheme::Dark };
	CThemeController::instance().setSchemePreference(schemeByIndex[_colorScheme->currentIndex()]);

	CSettings settings;
	const bool systemFont = _systemFont->isChecked();
	settings.setValue(Settings::MonospaceFontFamilyKey, systemFont ? QString{} : _fontFamily->currentFont().family());
	settings.setValue(Settings::MonospaceFontPointSizeKey, systemFont ? 0 : _fontSize->value());
	settings.setValue(Settings::DiffTabWidthKey, _diffTabWidth->value());
}
