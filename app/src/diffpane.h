#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class CLabelElided;
class DiffHighlighter;

// A header naming what is shown and where it is from, over a read-only monospace view. The pane neither
// reads nor caps the text; the caller does both.
class DiffPane final : public QWidget
{
public:
	explicit DiffPane(QWidget* parent = nullptr);

	// `tag` is the short label at the right of the header: which revisions the text is of, or how the two
	// sides compare. Either label may be empty.
	void showDiff(const QString& pathLabel, const QString& tag, const QString& text);
	// Unhighlighted, for text that only looks like a diff: an untracked file's contents, or a commit message
	// with '-' bullets
	void showText(const QString& pathLabel, const QString& tag, const QString& text);

private:
	void setContent(const QString& pathLabel, const QString& tag, const QString& text, bool asDiff);

private:
	CLabelElided* _pathLabel = nullptr;
	QLabel* _tagLabel = nullptr;
	QPlainTextEdit* _view = nullptr;
	DiffHighlighter* _highlighter = nullptr;
};
