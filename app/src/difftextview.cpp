#include "difftextview.h"
#include "theme.h"

#include "theme/cthemecontroller.h"

#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTextBlock>

#include <algorithm>
#include <assert.h>

// Padding outside the first and last number column
static constexpr int GutterOuterMargin = 6;
// Between the two number columns
static constexpr int GutterColumnGap = 8;

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

	// A block ends at its terminator, so a trailing one would add an empty last block to number.
	// QTextDocument reads a CRLF as one terminator, so both characters go.
	QStringView body{ text };
	if (body.endsWith(QLatin1Char('\n')))
		body.chop(body.endsWith(QLatin1String("\r\n")) ? 2 : 1);

	// Cleared before the text changes, so a paint arriving in between indexes nothing
	_lines.clear();
	_spans.clear();
	_maxOldLine = 0;
	_maxNewLine = 0;
	setPlainText(body.toString());

	// Built by walking the blocks rather than by splitting the text, so one record per block holds whatever
	// the document made of the terminators
	if (content == Content::Diff)
	{
		_lines.reserve(size_t(document()->blockCount()));
		// Kept only as long as the pairing needs them: every line again is several MB on a large diff
		std::vector<QString> texts;
		texts.reserve(size_t(document()->blockCount()));
		UnifiedDiffScanner scanner;
		for (QTextBlock block = document()->begin(); block.isValid(); block = block.next())
		{
			texts.push_back(block.text());
			const DiffLine line = scanner.scan(texts.back());
			_maxOldLine = std::max(_maxOldLine, line.oldLine);
			_maxNewLine = std::max(_maxNewLine, line.newLine);
			_lines.push_back(line);
		}
		_spans = intralineEmphasis(_lines, texts);
	}
	else if (content == Content::FileText)
	{
		_maxNewLine = document()->blockCount();
		_lines.reserve(size_t(_maxNewLine));
		for (int number = 1; number <= _maxNewLine; ++number)
			_lines.push_back(DiffLine{ DiffLineKind::Context, 0, number });
	}

	assert(content == Content::Message ? _lines.empty() : _lines.size() == size_t(document()->blockCount()));

	applyDiffFormats();
	updateNumberWidths();
	updateGutterWidth();
	_gutter->update();
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

	// Merged over the line's own format, so only the background changes
	QTextCharFormat addedSpan, removedSpan;
	addedSpan.setBackground(theme.diffAddEmphasisBg());
	removedSpan.setBackground(theme.diffDelEmphasisBg());

	QTextCursor cursor{ document() };
	cursor.beginEditBlock();
	size_t spanIndex = 0;
	for (QTextBlock block = document()->begin(); block.isValid(); block = block.next())
	{
		const QTextCharFormat* textFormat = nullptr;
		const QTextBlockFormat* bandFormat = nullptr;
		const QTextCharFormat* spanFormat = nullptr;
		switch (_lines[size_t(block.blockNumber())].kind)
		{
		case DiffLineKind::Context:
			break; // the widget's own text color, unbanded
		case DiffLineKind::Added:
			textFormat = &addedText;
			bandFormat = &addedBand;
			spanFormat = &addedSpan;
			break;
		case DiffLineKind::Removed:
			textFormat = &removedText;
			bandFormat = &removedBand;
			spanFormat = &removedSpan;
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

		// After the line's own format, which covers the whole block and would otherwise replace these
		while (spanIndex < _spans.size() && _spans[spanIndex].line == block.blockNumber())
		{
			assert(spanFormat); // spans are emitted for added and removed lines alone
			const EmphasisSpan& span = _spans[spanIndex++];
			cursor.setPosition(block.position() + span.start);
			cursor.setPosition(block.position() + span.start + span.length, QTextCursor::KeepAnchor);
			cursor.mergeCharFormat(*spanFormat);
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
