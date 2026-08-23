#include "diffpane.h"
#include "difftextview.h"
#include "settings.h"
#include "theme.h"

#include "settings/csettings.h"
#include "settingsui/csettingsdialog.h"
#include "widgets/clabelelided.h"

DISABLE_COMPILER_WARNINGS
#include <QFontMetricsF>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

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
	headerLayout->addWidget(buildHunkNavigator());
	layout->addWidget(header);

	_view = new DiffTextView;
	_view->setObjectName(QStringLiteral("diffView"));
	layout->addWidget(_view, 1);

	connect(_view, &QPlainTextEdit::updateRequest, this, &DiffPane::updateHunkNavigator);
	updateHunkNavigator();

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
	updateHunkNavigator();
}

void DiffPane::showFileText(const QString& pathLabel, const QString& tag, const QString& text)
{
	setHeader(pathLabel, tag);
	_view->showFileText(text);
	updateHunkNavigator();
}

void DiffPane::showMessage(const QString& pathLabel, const QString& tag, const QString& text)
{
	setHeader(pathLabel, tag);
	_view->showMessage(text);
	updateHunkNavigator();
}

QWidget* DiffPane::buildHunkNavigator()
{
	_hunkNavigator = new QWidget;
	auto* layout = new QHBoxLayout(_hunkNavigator);
	layout->setContentsMargins(12, 0, 0, 0); // set off from the tag it follows
	layout->setSpacing(4);

	_hunkLabel = new QLabel;
	_hunkLabel->setObjectName(QStringLiteral("diffTagLabel")); // dimmed, as the tag beside it is
	layout->addWidget(_hunkLabel);

	const auto addButton = [&](QChar glyph, const QString& tooltip, void (DiffTextView::*step)()) {
		auto* button = new QPushButton{ QString{ glyph } };
		button->setToolTip(tooltip);
		button->setFocusPolicy(Qt::NoFocus); // the file list keeps the keyboard
		connect(button, &QPushButton::clicked, this, [this, step] { (_view->*step)(); });
		layout->addWidget(button);
		return button;
	};
	_previousHunkButton = addButton(QChar(0x25B2), tr("Previous hunk"), &DiffTextView::goToPreviousHunk);
	_nextHunkButton = addButton(QChar(0x25BC), tr("Next hunk"), &DiffTextView::goToNextHunk);

	return _hunkNavigator;
}

void DiffPane::updateHunkNavigator()
{
	const DiffTextView::HunkPosition position = _view->hunkPosition();
	_hunkNavigator->setVisible(position.count > 0);
	if (position.count == 0)
		return;

	_hunkLabel->setText(tr("hunk %1/%2").arg(position.current).arg(position.count));
	_previousHunkButton->setEnabled(position.hasPrevious);
	_nextHunkButton->setEnabled(position.hasNext);
}

void DiffPane::setHeader(const QString& pathLabel, const QString& tag)
{
	_pathLabel->setText(pathLabel);
	_tagLabel->setText(tag);
}
