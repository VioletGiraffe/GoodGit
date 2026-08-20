#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class CLabelElided;
class DiffHighlighter;

// The pane both windows show one file's text in: a header naming what is shown and where it is from,
// over a read-only monospace view. Which method put the text there decides whether it is highlighted as
// a diff; the pane knows nothing else about where it came from, and neither reads nor caps it.
class DiffPane final : public QWidget
{
public:
	explicit DiffPane(QWidget* parent = nullptr);

	// `tag` is the short label at the right of the header - which revisions the text is of, or how the
	// two sides compare. Either label may be empty.
	void showDiff(const QString& pathLabel, const QString& tag, const QString& text);
	// The same for text that only looks like a diff, where a leading '-' is not a deletion: an untracked
	// file's own lines, or a commit message opening lines with '-' for bullets.
	void showText(const QString& pathLabel, const QString& tag, const QString& text);

private:
	void setContent(const QString& pathLabel, const QString& tag, const QString& text, bool asDiff);

private:
	CLabelElided* _pathLabel = nullptr;
	QLabel* _tagLabel = nullptr;
	QPlainTextEdit* _view = nullptr;
	DiffHighlighter* _highlighter = nullptr;
};
