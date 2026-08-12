#pragma once

#include <QSyntaxHighlighter>

// Prefix-driven unified diff highlighting: +/- lines, @@ hunk headers, dimmed file headers.
class DiffHighlighter final : public QSyntaxHighlighter
{
public:
	explicit DiffHighlighter(QTextDocument* document);

protected:
	void highlightBlock(const QString& text) override;
};
