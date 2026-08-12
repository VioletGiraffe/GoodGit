#include "diffhighlighter.h"

#include <QApplication>
#include <QPalette>

DiffHighlighter::DiffHighlighter(QTextDocument* document) :
	QSyntaxHighlighter(document)
{
}

void DiffHighlighter::highlightBlock(const QString& text)
{
	if (text.isEmpty())
		return;

	const bool dark = QApplication::palette().color(QPalette::Base).lightness() < 128;

	QTextCharFormat format;
	if (text.startsWith(QLatin1String("@@")))
	{
		format.setForeground(dark ? QColor(0x9b, 0x8f, 0xe0) : QColor(0x6a, 0x5f, 0xb0));
	}
	else if (text.startsWith(QLatin1String("+++")) || text.startsWith(QLatin1String("---"))
		|| text.startsWith(QLatin1String("diff ")) || text.startsWith(QLatin1String("index "))
		|| text.startsWith(QLatin1String("new file")) || text.startsWith(QLatin1String("deleted file"))
		|| text.startsWith(QLatin1String("old mode")) || text.startsWith(QLatin1String("new mode"))
		|| text.startsWith(QLatin1String("similarity ")) || text.startsWith(QLatin1String("rename ")))
	{
		format.setForeground(QApplication::palette().color(QPalette::PlaceholderText));
	}
	else if (text[0] == QLatin1Char('+'))
	{
		format.setForeground(dark ? QColor(0x8f, 0xdc, 0xa8) : QColor(0x0f, 0x5f, 0x2e));
		format.setBackground(dark ? QColor(0x16, 0x34, 0x1f) : QColor(0xe3, 0xf7, 0xe8));
	}
	else if (text[0] == QLatin1Char('-'))
	{
		format.setForeground(dark ? QColor(0xf0, 0xa7, 0x9c) : QColor(0x8f, 0x23, 0x18));
		format.setBackground(dark ? QColor(0x3a, 0x1d, 0x1c) : QColor(0xfd, 0xea, 0xea));
	}
	else
	{
		return;
	}

	setFormat(0, int(text.length()), format);
}
