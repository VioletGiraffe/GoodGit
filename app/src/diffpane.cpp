#include "diffpane.h"
#include "difftextview.h"
#include "settings.h"
#include "theme.h"

#include "settingsui/csettingsdialog.h"
#include "widgets/clabelelided.h"

DISABLE_COMPILER_WARNINGS
#include <QFontMetricsF>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

QString formattedFileSize(qint64 bytes)
{
	return QLocale{}.formattedDataSize(bytes, 1);
}

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
	// Sized to its own text, so the size sits against it; CLabelElided's ellipsis floor still lets it shrink
	_pathLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
	// A plain QLabel's minimum width is its whole text, which would become the header's floor
	_sizeLabel = new CLabelElided;
	_sizeLabel->setObjectName(QStringLiteral("diffTagLabel")); // dimmed, as the tag it precedes is
	_tagLabel = new QLabel;
	_tagLabel->setObjectName(QStringLiteral("diffTagLabel"));
	headerLayout->addWidget(_pathLabel);
	// Holds the header's slack: its text is left-aligned, so the size stays against the path and the gap falls after it
	headerLayout->addWidget(_sizeLabel, 1);
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
		_sizeLabel->setFont(mono);
		_view->setFont(mono);
		const int tabWidthSpaces = QSettings{}.value(Settings::DiffTabWidthKey, Settings::DiffTabWidthDefault).toInt();
		_view->setTabStopDistance(tabWidthSpaces * QFontMetricsF{ mono }.horizontalAdvance(QLatin1Char(' ')));
	};
	applyFontSettings();
	connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, this, applyFontSettings);
}

void DiffPane::showDiff(const ItemInfo& item, const QString& text)
{
	setHeader(item);
	_view->showDiff(text);
	updateHunkNavigator();
}

void DiffPane::showFileText(const ItemInfo& item, const QString& text)
{
	setHeader(item);
	_view->showFileText(text);
	updateHunkNavigator();
}

void DiffPane::showMessage(const ItemInfo& item, const QString& text)
{
	setHeader(item);
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

void DiffPane::setHeader(const ItemInfo& item)
{
	_pathLabel->setText(item.path);
	_sizeLabel->setText(item.size);
	_tagLabel->setText(item.tag);
}
