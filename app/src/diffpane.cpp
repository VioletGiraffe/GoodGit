#include "diffpane.h"
#include "diffhighlighter.h"
#include "settings.h"
#include "theme.h"

#include "settings/csettings.h"
#include "settingsui/csettingsdialog.h"
#include "theme/cthemecontroller.h"
#include "widgets/clabelelided.h"

#include <QFontMetricsF>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
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

	_view = new QPlainTextEdit;
	_view->setObjectName(QStringLiteral("diffView"));
	_view->setReadOnly(true);
	// A read-only view has nothing to do with a drop, and one it registers for never reaches the containing window
	_view->setAcceptDrops(false);
	_view->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	_highlighter = new DiffHighlighter(_view->document());
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

	// QSyntaxHighlighter caches its formats in the document, so a theme switch must re-run it
	connect(&CThemeController::instance(), &CThemeController::themeChanged, this, [this] { _highlighter->rehighlight(); });
}

void DiffPane::showDiff(const QString& pathLabel, const QString& tag, const QString& text)
{
	setContent(pathLabel, tag, text, /*asDiff=*/true);
}

void DiffPane::showText(const QString& pathLabel, const QString& tag, const QString& text)
{
	setContent(pathLabel, tag, text, /*asDiff=*/false);
}

void DiffPane::setContent(const QString& pathLabel, const QString& tag, const QString& text, bool asDiff)
{
	_highlighter->setEnabled(asDiff);
	_pathLabel->setText(pathLabel);
	_tagLabel->setText(tag);
	_view->setPlainText(text);
}
