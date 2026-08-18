#include "diffpane.h"
#include "diffhighlighter.h"
#include "theme.h"

#include "widgets/clabelelided.h"

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
	_pathLabel->setFont(monospaceFont());
	// Eliding does not shrink a QLabel's minimum width, which would otherwise set the pane's
	_pathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	_tagLabel = new QLabel;
	_tagLabel->setObjectName(QStringLiteral("diffTagLabel"));
	headerLayout->addWidget(_pathLabel, 1);
	headerLayout->addWidget(_tagLabel);
	layout->addWidget(header);

	_view = new QPlainTextEdit;
	_view->setObjectName(QStringLiteral("diffView"));
	_view->setReadOnly(true);
	_view->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	_view->setFont(monospaceFont());
	_highlighter = new DiffHighlighter(_view->document());
	layout->addWidget(_view, 1);
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
