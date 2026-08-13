#include "diffhighlighter.h"
#include "theme.h"

DiffHighlighter::DiffHighlighter(QTextDocument* document) :
	QSyntaxHighlighter(document)
{
}

void DiffHighlighter::setEnabled(bool enabled)
{
	if (_enabled == enabled)
		return;

	_enabled = enabled;
	rehighlight();
}

void DiffHighlighter::highlightBlock(const QString& text)
{
	if (!_enabled || text.isEmpty())
		return;

	const Theme& theme = activeTheme();

	QTextCharFormat format;
	if (text.startsWith(QLatin1String("@@")))
	{
		format.setForeground(theme.diffHunk);
	}
	else if (text.startsWith(QLatin1String("+++")) || text.startsWith(QLatin1String("---"))
		|| text.startsWith(QLatin1String("diff ")) || text.startsWith(QLatin1String("index "))
		|| text.startsWith(QLatin1String("new file")) || text.startsWith(QLatin1String("deleted file"))
		|| text.startsWith(QLatin1String("old mode")) || text.startsWith(QLatin1String("new mode"))
		|| text.startsWith(QLatin1String("similarity ")) || text.startsWith(QLatin1String("rename ")))
	{
		format.setForeground(theme.dim);
	}
	else if (text[0] == QLatin1Char('+'))
	{
		format.setForeground(theme.diffAddFg);
		format.setBackground(theme.diffAddBg);
	}
	else if (text[0] == QLatin1Char('-'))
	{
		format.setForeground(theme.diffDelFg);
		format.setBackground(theme.diffDelBg);
	}
	else
	{
		return;
	}

	setFormat(0, int(text.length()), format);
}
