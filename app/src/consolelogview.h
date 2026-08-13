#pragma once

#include <QPlainTextEdit>
#include <QStringDecoder>

// A log view for raw process output, rendered the way a terminal would: a carriage return rewrites the
// line it is on instead of starting a new one, which is how git's progress meters redraw themselves.
// Output arrives in chunks that split anywhere - mid-line, or between a CR and the LF that follows it.
class ConsoleLogView final : public QPlainTextEdit
{
	Q_OBJECT

public:
	explicit ConsoleLogView(QWidget* parent = nullptr);

	// Drops every entry. Not QPlainTextEdit::clear(), which would empty the document while this view still
	// held a line of it open for rewriting.
	void clearLog();
	// Opens a labelled entry, separated from the previous one by a blank line
	void beginEntry(const QString& label);
	// Process output, in arrival order
	void appendOutput(const QByteArray& chunk);
	// A closing line the process did not produce - an exit code, a launch failure
	void appendNote(const QString& text);

	[[nodiscard]] bool entryHasOutput() const { return _entryHasOutput; }

private:
	void resetStreamState();
	void showPendingLine();

private:
	// A read can end anywhere, including inside a multi-byte character - this one carries the incomplete
	// tail over to the next chunk, which decoding each chunk on its own cannot do
	QStringDecoder _decoder{ QStringDecoder::Utf8 };
	QString _pendingLine;
	bool _lineOpen = false;      // the last block in the view is _pendingLine, and is rewritten in place
	bool _pendingCr = false;     // a CR is only a line rewrite if the next character is not the LF of a CRLF
	bool _entryHasOutput = false;
};
