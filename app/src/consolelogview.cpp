#include "consolelogview.h"
#include "theme.h"

#include "settingsui/csettingsdialog.h"

DISABLE_COMPILER_WARNINGS
#include <QScrollBar>
#include <QTextCursor>
RESTORE_COMPILER_WARNINGS

ConsoleLogView::ConsoleLogView(QWidget* parent) :
	QPlainTextEdit(parent)
{
	setReadOnly(true);
	// A read-only view has nothing to do with a drop, and one it registers for never reaches the containing window
	setAcceptDrops(false);
	setFont(monospaceFont());
	connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, this, [this] { setFont(monospaceFont()); });
	setMaximumBlockCount(500); // bounds a chatty remote's hook output
}

void ConsoleLogView::clearLog()
{
	clear();
	resetStreamState();
}

void ConsoleLogView::beginEntry(const QString& label)
{
	resetStreamState();

	if (blockCount() > 1) // an empty document still has one block
		appendBlock({}, {});
	appendBlock(QStringLiteral("> ") + label, {});
}

void ConsoleLogView::setEncoding(QStringConverter::Encoding encoding)
{
	_decoder = QStringDecoder{ encoding };
}

void ConsoleLogView::appendOutput(const QByteArray& chunk)
{
	if (chunk.isEmpty())
		return;

	const QString text = _decoder(chunk); // the decoder returns a lazy proxy, not something iterable
	for (const QChar c : text)
	{
		if (c == QLatin1Char('\r'))
		{
			_pendingCr = true;
			continue;
		}

		if (c == QLatin1Char('\n'))
		{
			showPendingLine(); // even when empty: a blank line in the output is a blank line in the log
			_pendingLine.clear();
			_lineOpen = false;
		}
		else
		{
			if (_pendingCr)
				_pendingLine.clear(); // the line is being redrawn from its start
			_pendingLine += c;
		}
		_pendingCr = false;
	}

	if (!_pendingLine.isEmpty())
		showPendingLine(); // a chunk ending mid-line still shows what it has
}

void ConsoleLogView::appendNote(const QString& text, NoteKind kind)
{
	resetStreamState(); // a note never rewrites the line the process left open

	QTextCharFormat format;
	format.setForeground(kind == NoteKind::Success ? activeTheme().stAdded : activeTheme().stDeleted);
	format.setFontWeight(QFont::Bold);
	appendBlock(text, format);
}

void ConsoleLogView::resetStreamState()
{
	_decoder.resetState();
	_pendingLine.clear();
	_lineOpen = false;
	_pendingCr = false;
}

void ConsoleLogView::showPendingLine()
{
	if (!_lineOpen)
	{
		appendBlock(_pendingLine, {});
		_lineOpen = true;
		return;
	}

	const bool wasAtBottom = atBottom();

	QTextCursor cursor{ document() };
	cursor.movePosition(QTextCursor::End);
	cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
	cursor.insertText(_pendingLine, {}); // replaces the selected block text, keeps the block

	if (wasAtBottom)
		scrollToBottom();
}

void ConsoleLogView::appendBlock(const QString& text, const QTextCharFormat& format)
{
	const bool wasAtBottom = atBottom();

	QTextCursor cursor{ document() };
	cursor.movePosition(QTextCursor::End);
	if (!document()->isEmpty()) // a fresh document already has one block, which becomes the first line
		cursor.insertBlock();
	cursor.insertText(text, format); // newlines within open further blocks, as a multi-line note needs

	if (wasAtBottom)
		scrollToBottom();
}

bool ConsoleLogView::atBottom() const
{
	return verticalScrollBar()->value() >= verticalScrollBar()->maximum();
}

void ConsoleLogView::scrollToBottom()
{
	verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}
