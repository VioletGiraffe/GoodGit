#pragma once

#include "unifieddiff.h"

DISABLE_COMPILER_WARNINGS
#include <QPlainTextEdit>
RESTORE_COMPILER_WARNINGS

#include <optional>
#include <stdint.h>
#include <vector>

class QPainter;
class QPaintEvent;

// A read-only monospace view of one file's text, in one of three kinds:
//   diff    - a unified diff: added and removed lines banded across the full width, an edit small enough
//             shown as one line with what it took out struck through beside what it put in, headers dimmed,
//             and a gutter carrying both files' line numbers. A moved block is banded in the rename color
//             instead, the copy it left fainter with its text dimmed, and has both its places bracketed in
//             the gutter and joined by a line with arrowheads the way it went, one color per move; a click
//             on either bracket, or on the line joining them, brings the other end to the top. A block moved
//             to or from another file has the one place here bracketed, with a stub and an arrowhead toward
//             that file, which a tooltip names and a click announces. An edit on the way is marked on the
//             added copy, as the text it put in.
//   file    - a file's own contents: one gutter column, no diff decoration
//   message - prose, such as a placeholder or an error: no gutter, no decoration
//
// Requires from its environment, for judging a reuse elsewhere:
//   - The diff colors come from the application's activeTheme() and are reapplied when CThemeController
//     announces a change. The text color of an undecorated line is the widget's own, so a stylesheet sets it.
//   - The font, the tab stop and any cap on the size of the text belong to the caller.
//   - Nothing is parsed beyond the unified format `unifieddiff` reads.
//   - A merged line's text is in neither file, so what is copied out of one is neither version.
//
// The implementation rests on:
//   - The bands are block backgrounds, which QPlainTextEdit paints to the full viewport width, so a
//     wrapped line stays banded to its last row.
//   - The strike through removed text is painted over the glyphs, not the font's own: a QTextCharFormat
//     offers no color or width for one, and the font's is a hairline in the text's own color.
//   - Numbers and formats are computed once per content, over the whole text: the text is never edited.
class DiffTextView final : public QPlainTextEdit
{
	Q_OBJECT

public:
	explicit DiffTextView(QWidget* parent = nullptr);

	void showDiff(ParsedDiff parsed);
	void showFileText(const QString& text);
	void showMessage(const QString& text);

	// The view's place among the hunks of the diff it shows. No hunks for anything but a diff, nor for a
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

	void scrollLineToTop(int line);
	// A line of the diff read, as ForeignEnd::diffLine names one. Does nothing for a line not shown.
	void scrollDiffLineToTop(int diffLine);

	// Called by the gutter widget, which owns nothing but its paint and mouse events
	void paintGutter(QPaintEvent* event);
	// A move's mark under a gutter position: the move, and the end the position is at - the bracket there, or
	// the nearer one where the position is on the line joining the two
	struct MarkHit
	{
		const DiffMove* move = nullptr;
		bool removedEnd = false;
	};
	// The whole lane is the target, over both brackets and the line joining them
	[[nodiscard]] std::optional<MarkHit> moveMarkAt(const QPoint& gutterPos) const;
	// Brings the mark's other end to the top, or emits foreignEndActivated where that end is in another file
	void followMoveMark(const MarkHit& hit);
	// Names the other file, where the mark's other end is in one; empty otherwise
	[[nodiscard]] QString moveMarkTooltip(const MarkHit& hit) const;

signals:
	void foreignEndActivated(const ForeignEnd& end);

protected:
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	enum class Content : uint8_t { Diff, FileText, Message };

	// One move's marks: the block's two places, in a lane of their own where moves overlap on screen
	struct MoveMark
	{
		DiffMove move;    // in lines of the document
		int lane = 0;
	};

	void setContent(const QString& text, Content content); // FileText or Message
	void resetContent(Content content); // before the text changes
	void finishContent();               // after it: the formats, the gutter
	void paintRemovedStrikes(const QRect& clip);
	void assignMoveLanes();
	void paintMoveMarks(QPainter& painter, const QRect& clip);
	[[nodiscard]] int moveColumnWidth() const; // 0 without moves
	[[nodiscard]] int lineAt(int y) const;     // the line at that height of the viewport, -1 below the last
	void applyDiffFormats();
	void updateNumberWidths();
	void updateGutterWidth();
	void updateGutterGeometry();

private:
	std::vector<DiffLine> _lines;       // one per block of the document, empty for a message
	std::vector<DiffSpan> _spans;       // ascending by line, so the formatting pass walks it in step
	std::vector<int> _hunkLines;        // the line each hunk header is on, ascending
	std::vector<MoveMark> _moveMarks;   // ascending by the first line of either place
	std::vector<int> _shownLine;        // ParsedDiff::shownLine of the diff shown, empty for the other kinds
	Content _content = Content::Message;
	int _maxOldLine = 0; // the widest number each column has to fit
	int _maxNewLine = 0;
	int _oldNumberWidth = 0; // in pixels, 0 where the column carries no numbers
	int _newNumberWidth = 0;
	int _moveLaneCount = 0;
	int _moveLaneWidth = 0;  // in pixels, from the font
	int _gutterWidth = 0;    // the viewport's left margin, and so the gutter's width
	QWidget* _gutter = nullptr;
};
