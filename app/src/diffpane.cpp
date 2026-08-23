#include "diffpane.h"
#include "difftextview.h"
#include "settings.h"
#include "theme.h"

#include "settings/csettings.h"
#include "settingsui/csettingsdialog.h"
#include "widgets/clabelelided.h"

#include <QFontMetricsF>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QVBoxLayout>

DiffPane::DiffPane(QWidget* parent) :
	QWidget(parent)
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	auto* header = new QFrame;
	header->setObjectName(QStringLiteral("diffHeader"));
	auto* headerLayout = new QHBoxLayout(header);
	headerLayout->setContentsMargins(8, 6, 8, 6);
	_pathLabel = new CLabelElided;
	// Eliding does not shrink a QLabel's minimum width, which would otherwise become the pane's
	_pathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	_tagLabel = new QLabel;
	_tagLabel->setObjectName(QStringLiteral("diffTagLabel"));
	headerLayout->addWidget(_pathLabel, 1);
	headerLayout->addWidget(_tagLabel);
	layout->addWidget(header);

	_view = new DiffTextView;
	_view->setObjectName(QStringLiteral("diffView"));
	layout->addWidget(_view, 1);

	const auto applyFontSettings = [this] {
		const QFont mono = monospaceFont();
		_pathLabel->setFont(mono);
		_view->setFont(mono);
		const int tabWidthSpaces = CSettings{}.value(Settings::DiffTabWidthKey, Settings::DiffTabWidthDefault).toInt();
		_view->setTabStopDistance(tabWidthSpaces * QFontMetricsF{ mono }.horizontalAdvance(QLatin1Char(' ')));
	};
	applyFontSettings();
	connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, this, applyFontSettings);
}

void DiffPane::showDiff(const QString& pathLabel, const QString& tag, const QString& text)
{
	setHeader(pathLabel, tag);
	_view->showDiff(text);
}

void DiffPane::showFileText(const QString& pathLabel, const QString& tag, const QString& text)
{
	setHeader(pathLabel, tag);
	_view->showFileText(text);
}

void DiffPane::showMessage(const QString& pathLabel, const QString& tag, const QString& text)
{
	setHeader(pathLabel, tag);
	_view->showMessage(text);
}

void DiffPane::setHeader(const QString& pathLabel, const QString& tag)
{
	_pathLabel->setText(pathLabel);
	_tagLabel->setText(tag);
}
