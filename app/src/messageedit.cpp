#include "messageedit.h"

#include <QFontDatabase>
#include <QPainter>

static constexpr int SubjectGuideColumn = 50;

MessageEdit::MessageEdit(QWidget* parent) :
	QPlainTextEdit(parent)
{
	setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	setTabChangesFocus(true);
}

void MessageEdit::paintEvent(QPaintEvent* event)
{
	QPlainTextEdit::paintEvent(event);

	const qreal x = contentOffset().x() + document()->documentMargin()
		+ fontMetrics().horizontalAdvance(QLatin1Char('x')) * SubjectGuideColumn;
	if (x >= viewport()->width())
		return;

	QPainter painter{ viewport() };
	QColor color = palette().color(QPalette::PlaceholderText);
	color.setAlpha(90);
	painter.setPen(color);
	painter.drawLine(QPointF{ x, 0.0 }, QPointF{ x, qreal(viewport()->height()) });
}
