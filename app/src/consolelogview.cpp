#include "consolelogview.h"
#include "theme.h"

#include <QScrollBar>
#include <QTextCursor>

ConsoleLogView::ConsoleLogView(QWidget* parent) :
	QPlainTextEdit(parent)
{
	setReadOnly(true);
	setFont(monospaceFont());
	setMaximumBlockCount(500); // bounds a chatty remote's hook output
}

void ConsoleLogView::clearLog()
{
	clear();
	resetStreamState();
	_entryHasOutput = false;
}

void ConsoleLogView::beginEntry(const QString& label)
{
	resetStreamState();
	_entryHasOutput = false;

	if (blockCount() > 1) // an empty document still has one block
		appendPlainText({});
	appendPlainText(QStringLiteral("> ") + label);
}

void ConsoleLogView::appendOutput(const QByteArray& chunk)
{
	if (chunk.isEmpty())
		return;
	_entryHasOutput = true;

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
				_pendingLine.clear(); // the meter is redrawing the line from its start
			_pendingLine += c;
		}
		_pendingCr = false;
	}

	if (!_pendingLine.isEmpty())
		showPendingLine(); // a chunk ending mid-line still shows what it has
}

void ConsoleLogView::appendNote(const QString& text)
{
	resetStreamState(); // a note is never a rewrite of the line the process left open
	appendPlainText(text);
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
	// Sampled before the edit: an edit at the end moves the maximum, and only a reader who was already
	// at the bottom wants to be carried along
	const bool wasAtBottom = verticalScrollBar()->value() >= verticalScrollBar()->maximum();

	if (_lineOpen)
	{
		QTextCursor cursor{ document() };
		cursor.movePosition(QTextCursor::End);
		cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
		cursor.insertText(_pendingLine); // over the selection: replaces the block's text, keeps the block
	}
	else
	{
		appendPlainText(_pendingLine);
		_lineOpen = true;
	}

	if (wasAtBottom)
		verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}
