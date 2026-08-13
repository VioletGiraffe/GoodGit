#pragma once

#include <QSyntaxHighlighter>

// Prefix-driven unified diff highlighting: +/- lines, @@ hunk headers, dimmed file headers.
class DiffHighlighter final : public QSyntaxHighlighter
{
public:
	explicit DiffHighlighter(QTextDocument* document);

	// Off for text that only looks like a diff - a commit message body opens lines with '-' for bullets
	void setEnabled(bool enabled);

protected:
	void highlightBlock(const QString& text) override;

private:
	bool _enabled = true;
};
