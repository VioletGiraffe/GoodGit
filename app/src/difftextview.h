#pragma once

#include "unifieddiff.h"

DISABLE_COMPILER_WARNINGS
#include <QPlainTextEdit>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>
#include <vector>

class QPaintEvent;

// A read-only monospace view of one file's text, in one of three kinds:
//   diff    - a unified diff: added and removed lines banded across the full width, an edit small enough
//             shown as one line with what it took out struck through beside what it put in, headers dimmed,
//             and a gutter carrying both files' line numbers
//   file    - a file's own contents: one gutter column, no diff decoration
//   message - prose, such as a placeholder or an error: no gutter, no decoration
//
// What it takes from its environment, for judging a reuse elsewhere:
//   - The diff colors come from the application's activeTheme() and are reapplied when CThemeController
//     announces a change. The text color of an undecorated line is the widget's own, so a stylesheet sets it.
//   - The font, the tab stop and any cap on the size of the text belong to the caller.
//   - Nothing is parsed beyond the unified format `unifieddiff` reads.
//   - A merged line's text is in neither file, so what is copied out of one is neither version.
//
// What the implementation rests on:
//   - The bands are block backgrounds, which QPlainTextEdit paints to the full viewport width, so a
//     wrapped line stays banded to its last row.
//   - The strike through removed text is painted over the glyphs, not the font's own: a QTextCharFormat
//     offers no color or width for one, and the font's is a hairline in the text's own color.
//   - Numbers and formats are computed once per content, over the whole text: the text is never edited.
class DiffTextView final : public QPlainTextEdit
{
public:
	explicit DiffTextView(QWidget* parent = nullptr);

	void showDiff(const QString& text);
	void showFileText(const QString& text);
	void showMessage(const QString& text);

	// Where the view sits among the hunks of the diff it shows. No hunks for anything but a diff, nor for a
	// diff holding none - a binary file's, or one that is headers alone.
	struct HunkPosition
	{
		int count = 0;
		int current = 0; // 1-based, 0 only where there are no hunks
		bool hasPrevious = false;
		bool hasNext = false;
	};
	[[nodiscard]] HunkPosition hunkPosition() const;

	// Brings the hunk before or after the one at the top of the viewport to the top. Does nothing where the
	// matching flag of hunkPosition() is false.
	void goToPreviousHunk();
	void goToNextHunk();

	// Called by the gutter widget, which owns nothing but its paint event
	void paintGutter(QPaintEvent* event);

protected:
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	enum class Content : uint8_t { Diff, FileText, Message };

	void setContent(const QString& text, Content content);
	void paintRemovedStrikes(const QRect& clip);
	void scrollLineToTop(int line);
	void applyDiffFormats();
	void updateNumberWidths();
	void updateGutterWidth();
	void updateGutterGeometry();

private:
	std::vector<DiffLine> _lines;       // one per block of the document, empty for a message
	std::vector<DiffSpan> _spans;       // ascending by line, so the formatting pass walks it in step
	std::vector<int> _hunkLines;        // the line each hunk header is on, ascending
	Content _content = Content::Message;
	int _maxOldLine = 0; // the widest number each column has to fit
	int _maxNewLine = 0;
	int _oldNumberWidth = 0; // in pixels, 0 where the column carries no numbers
	int _newNumberWidth = 0;
	int _gutterWidth = 0;    // the viewport's left margin, and so the gutter's width
	QWidget* _gutter = nullptr;
};
