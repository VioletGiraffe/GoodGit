#include "difftextview.h"
#include "theme.h"

#include "theme/cthemecontroller.h"

DISABLE_COMPILER_WARNINGS
#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextLayout>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <assert.h>

// Padding outside the first and last number column
static constexpr int GutterOuterMargin = 6;
// Between the two number columns
static constexpr int GutterColumnGap = 8;

// The strike through removed text follows the font's height, floored so it stays a line at small sizes
static constexpr qreal StrikeThicknessDivisor = 9.0;
static constexpr qreal MinStrikeThickness = 2.0;

namespace {

class DiffGutter final : public QWidget
{
public:
	explicit DiffGutter(DiffTextView* view) : QWidget(view), _view{ view } {}

protected:
	void paintEvent(QPaintEvent* event) override { _view->paintGutter(event); }

private:
	DiffTextView* _view = nullptr;
};

// One span's strike, in as many pieces as a wrapped block splits it into
void strikeSpan(QPainter& painter, const QTextLayout& layout, QPointF origin, const DiffSpan& span, qreal strikeOutPos)
{
	const int end = span.start + span.length;
	for (int pos = span.start; pos < end; )
	{
		const QTextLine line = layout.lineForTextPosition(pos);
		const int pieceEnd = std::min(end, line.textStart() + line.textLength());
		if (pieceEnd <= pos)
			break;

		const qreal y = origin.y() + line.y() + line.ascent() - strikeOutPos;
		painter.drawLine(QPointF{ origin.x() + line.cursorToX(pos), y },
			QPointF{ origin.x() + line.cursorToX(pieceEnd), y });
		pos = pieceEnd;
	}
}

int digitCount(int value)
{
	int digits = 1;
	while (value >= 10)
	{
		value /= 10;
		++digits;
	}
	return digits;
}

} // namespace

DiffTextView::DiffTextView(QWidget* parent) :
	QPlainTextEdit(parent)
{
	setReadOnly(true);
	// A read-only view has nothing to do with a drop, and one it registers for never reaches the containing window
	setAcceptDrops(false);
	setLineWrapMode(QPlainTextEdit::WidgetWidth);
	// Read-only, and the formatting pass would otherwise fill an undo stack nothing can reach
	document()->setUndoRedoEnabled(false);

	_gutter = new DiffGutter(this);

	connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
		if (dy != 0)
			_gutter->scroll(0, dy);
		else
			_gutter->update(0, rect.y(), _gutter->width(), rect.height());
	});

	// The formats are stored in the document, so a theme switch must apply them again
	connect(&CThemeController::instance(), &CThemeController::themeChanged, this, [this] {
		applyDiffFormats();
		_gutter->update();
	});
}

void DiffTextView::showDiff(const QString& text)
{
	setContent(text, Content::Diff);
}

void DiffTextView::showFileText(const QString& text)
{
	setContent(text, Content::FileText);
}

void DiffTextView::showMessage(const QString& text)
{
	setContent(text, Content::Message);
}

void DiffTextView::setContent(const QString& text, Content content)
{
	_content = content;

	// Cleared before the text changes, so a paint arriving in between indexes nothing
	_lines.clear();
	_spans.clear();
	_hunkLines.clear();
	_maxOldLine = 0;
	_maxNewLine = 0;

	if (content == Content::Diff)
	{
		// The lines shown are not the diff's own: an edit the parse could merge arrives as one line
		ParsedDiff parsed = parseUnifiedDiff(text);
		setPlainText(parsed.text);
		_lines = std::move(parsed.lines);
		_spans = std::move(parsed.spans);
		for (int index = 0, count = int(_lines.size()); index < count; ++index)
		{
			const DiffLine& line = _lines[size_t(index)];
			_maxOldLine = std::max(_maxOldLine, line.oldLine);
			_maxNewLine = std::max(_maxNewLine, line.newLine);
			if (line.kind == DiffLineKind::HunkHeader)
				_hunkLines.push_back(index);
		}
	}
	else
	{
		// A block ends at its terminator, so a trailing one would add an empty last block to number.
		// QTextDocument reads a CRLF as one terminator, so both characters go.
		QStringView body{ text };
		if (body.endsWith(QLatin1Char('\n')))
			body.chop(body.endsWith(QLatin1String("\r\n")) ? 2 : 1);
		setPlainText(body.toString());

		if (content == Content::FileText)
		{
			_maxNewLine = document()->blockCount();
			_lines.reserve(size_t(_maxNewLine));
			for (int number = 1; number <= _maxNewLine; ++number)
				_lines.push_back(DiffLine{ DiffLineKind::Context, 0, number });
		}
	}

	assert(content == Content::Message ? _lines.empty() : _lines.size() == size_t(document()->blockCount()));

	// setPlainText seeds the whole text with the char format at the caret: a line applyDiffFormats leaves alone would keep it
	QTextCursor cursor{ document() };
	cursor.select(QTextCursor::Document);
	cursor.setCharFormat(QTextCharFormat{});

	applyDiffFormats();
	updateNumberWidths();
	updateGutterWidth();
	_gutter->update();
}

DiffTextView::HunkPosition DiffTextView::hunkPosition() const
{
	HunkPosition position;
	position.count = int(_hunkLines.size());
	if (position.count == 0)
		return position;

	const int topLine = firstVisibleBlock().blockNumber();
	const auto begun = std::upper_bound(_hunkLines.begin(), _hunkLines.end(), topLine);
	const auto passed = std::lower_bound(_hunkLines.begin(), _hunkLines.end(), topLine);

	// The hunk being read is the last one begun at or above the top of the viewport. Above the first of
	// them stands only the file's own header, which belongs with the hunk it introduces.
	position.current = std::max(1, int(begun - _hunkLines.begin()));
	position.hasPrevious = passed != _hunkLines.begin();
	// A hunk already as high as the viewport can bring it is not one to go to: a diff shorter than the
	// viewport holds hunks below the top line that no scrolling will reach.
	position.hasNext = begun != _hunkLines.end() && verticalScrollBar()->value() < verticalScrollBar()->maximum();
	return position;
}

void DiffTextView::goToPreviousHunk()
{
	const auto passed = std::lower_bound(_hunkLines.begin(), _hunkLines.end(), firstVisibleBlock().blockNumber());
	if (passed != _hunkLines.begin())
		scrollLineToTop(*std::prev(passed));
}

void DiffTextView::goToNextHunk()
{
	const auto begun = std::upper_bound(_hunkLines.begin(), _hunkLines.end(), firstVisibleBlock().blockNumber());
	if (begun != _hunkLines.end())
		scrollLineToTop(*begun);
}

// A cursor is scrolled to by the least the viewport can move, so one reached from the bottom lands at the top
void DiffTextView::scrollLineToTop(int line)
{
	verticalScrollBar()->setValue(verticalScrollBar()->maximum());
	setTextCursor(QTextCursor{ document()->findBlockByNumber(line) });
	ensureCursorVisible();
}

void DiffTextView::applyDiffFormats()
{
	if (_content != Content::Diff)
		return;

	const Theme& theme = activeTheme();

	QTextCharFormat addedText, removedText, hunkText, dimmedText;
	addedText.setForeground(theme.diffAddFg);
	removedText.setForeground(theme.diffDelFg);
	hunkText.setForeground(theme.diffHunk);
	dimmedText.setForeground(theme.palette.textDim);

	QTextBlockFormat addedBand, removedBand;
	addedBand.setBackground(theme.diffAddBg);
	removedBand.setBackground(theme.diffDelBg);

	// A merged line carries no band, so its spans are the only thing naming either side, and they take the
	// diff colors themselves. The strike over removed text is painted rather than set here, in paintRemovedStrikes.
	QTextCharFormat removedSpan, addedSpan;
	removedSpan.setBackground(theme.diffDelBg);
	removedSpan.setForeground(theme.diffDelFg);
	addedSpan.setBackground(theme.diffAddBg);
	addedSpan.setForeground(theme.diffAddFg);

	QTextCursor cursor{ document() };
	cursor.beginEditBlock();
	size_t spanIndex = 0;
	for (QTextBlock block = document()->begin(); block.isValid(); block = block.next())
	{
		const DiffLineKind kind = _lines[size_t(block.blockNumber())].kind;
		const QTextCharFormat* textFormat = nullptr;
		const QTextBlockFormat* bandFormat = nullptr;
		switch (kind)
		{
		case DiffLineKind::Context:
		case DiffLineKind::Edited:
			break; // the widget's own text color, unbanded
		case DiffLineKind::Added:
			textFormat = &addedText;
			bandFormat = &addedBand;
			break;
		case DiffLineKind::Removed:
			textFormat = &removedText;
			bandFormat = &removedBand;
			break;
		case DiffLineKind::HunkHeader:
			textFormat = &hunkText;
			break;
		case DiffLineKind::FileHeader:
		case DiffLineKind::NoNewline:
			textFormat = &dimmedText;
			break;
		}

		if (textFormat)
		{
			cursor.setPosition(block.position());
			cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
			cursor.setCharFormat(*textFormat);
			if (bandFormat)
				cursor.setBlockFormat(*bandFormat);
		}
		else if (kind == DiffLineKind::Edited)
		{
			// Its marker stands in a column the diff's own lines fill, and belongs to neither version of the line
			cursor.setPosition(block.position());
			cursor.setPosition(block.position() + 1, QTextCursor::KeepAnchor);
			cursor.setCharFormat(dimmedText);
		}

		// After the line's own format, which covers the whole block and would otherwise replace these
		while (spanIndex < _spans.size() && _spans[spanIndex].line == block.blockNumber())
		{
			assert(kind == DiffLineKind::Edited); // no other kind of line carries spans
			const DiffSpan& span = _spans[spanIndex++];
			const QTextCharFormat& spanFormat = span.removed ? removedSpan : addedSpan;
			cursor.setPosition(block.position() + span.start);
			cursor.setPosition(block.position() + span.start + span.length, QTextCursor::KeepAnchor);
			cursor.mergeCharFormat(spanFormat);
		}
	}
	cursor.endEditBlock();
}

void DiffTextView::updateNumberWidths()
{
	const int digitWidth = fontMetrics().horizontalAdvance(QLatin1Char('9'));
	_oldNumberWidth = _maxOldLine == 0 ? 0 : digitCount(_maxOldLine) * digitWidth;
	_newNumberWidth = _maxNewLine == 0 ? 0 : digitCount(_maxNewLine) * digitWidth;
}

void DiffTextView::updateGutterWidth()
{
	int width = 0;
	if (_oldNumberWidth != 0 || _newNumberWidth != 0)
	{
		width = 2 * GutterOuterMargin + _oldNumberWidth + _newNumberWidth;
		if (_oldNumberWidth != 0 && _newNumberWidth != 0)
			width += GutterColumnGap;
	}

	// setViewportMargins relayouts the scroll area, and this runs on every selection change
	if (width == _gutterWidth)
		return;

	_gutterWidth = width;
	setViewportMargins(width, 0, 0, 0);
	updateGutterGeometry(); // setViewportMargins makes the margin but does not fill it
}

void DiffTextView::updateGutterGeometry()
{
	const QRect rect = contentsRect();
	_gutter->setGeometry(rect.left(), rect.top(), _gutterWidth, rect.height());
}

void DiffTextView::paintEvent(QPaintEvent* event)
{
	QPlainTextEdit::paintEvent(event);

	paintRemovedStrikes(event->rect());
}

void DiffTextView::paintRemovedStrikes(const QRect& clip)
{
	if (_spans.empty())
		return;

	const QFontMetricsF metrics{ font() };
	QPainter painter{ viewport() };
	painter.setClipRect(clip);
	// The color the file lists strike a deleted row with, which reads against the text it crosses
	painter.setPen(QPen{ activeTheme().stDeleted, std::max(MinStrikeThickness, metrics.height() / StrikeThicknessDivisor) });

	for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next())
	{
		const QRectF blockRect = blockBoundingGeometry(block).translated(contentOffset());
		if (blockRect.top() > clip.bottom())
			break;
		if (blockRect.bottom() < clip.top())
			continue;

		auto span = std::lower_bound(_spans.begin(), _spans.end(), block.blockNumber(),
			[](const DiffSpan& span, int line) { return span.line < line; });
		if (span == _spans.end() || span->line != block.blockNumber())
			continue;

		// A layout places its lines from its own origin. A cursor at the start of the block says where that
		// origin sits in the viewport, and carries none of the ambiguity a position at a wrap does.
		const QTextLayout& layout = *block.layout();
		const QPointF origin{ cursorRect(QTextCursor{ block }).left() - layout.lineAt(0).cursorToX(0), blockRect.top() };

		for (; span != _spans.end() && span->line == block.blockNumber(); ++span)
		{
			if (span->removed)
				strikeSpan(painter, layout, origin, *span, metrics.strikeOutPos());
		}
	}
}

void DiffTextView::resizeEvent(QResizeEvent* event)
{
	QPlainTextEdit::resizeEvent(event);

	updateGutterGeometry();
}

void DiffTextView::changeEvent(QEvent* event)
{
	QPlainTextEdit::changeEvent(event);

	// Both columns are as wide as the font makes them, and nothing else recomputes that
	if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange)
	{
		updateNumberWidths();
		updateGutterWidth();
		_gutter->update();
	}
}

void DiffTextView::paintGutter(QPaintEvent* event)
{
	if (_lines.empty())
		return;

	const Theme& theme = activeTheme();

	QPainter painter{ _gutter };
	painter.fillRect(event->rect(), theme.palette.windowBg);
	painter.setPen(theme.palette.borderSubtle);
	painter.drawLine(_gutterWidth - 1, event->rect().top(), _gutterWidth - 1, event->rect().bottom());
	painter.setPen(theme.palette.textDim);

	const int numberHeight = fontMetrics().height();
	const int oldColumnRight = GutterOuterMargin + _oldNumberWidth;
	const int newColumnRight = _gutterWidth - GutterOuterMargin;

	for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next())
	{
		const QRectF blockRect = blockBoundingGeometry(block).translated(contentOffset());
		if (blockRect.top() > event->rect().bottom())
			break;
		if (blockRect.bottom() < event->rect().top())
			continue;

		// Against the top of the block, so a wrapped line numbers its first row alone
		const DiffLine& line = _lines[size_t(block.blockNumber())];
		const int top = qRound(blockRect.top());
		if (line.oldLine != 0)
			painter.drawText(0, top, oldColumnRight, numberHeight, Qt::AlignRight, QString::number(line.oldLine));
		if (line.newLine != 0)
			painter.drawText(0, top, newColumnRight, numberHeight, Qt::AlignRight, QString::number(line.newLine));
	}
}
