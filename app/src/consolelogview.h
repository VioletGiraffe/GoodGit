#pragma once

#include <QPlainTextEdit>
#include <QStringDecoder>
#include <QTextFormat>

#include <stdint.h>

// A log view for raw process output, rendered the way a terminal would: a carriage return rewrites the
// line it is on instead of starting a new one, which is how git's progress meters redraw themselves.
// Output arrives in chunks that split anywhere - mid-line, or between a CR and the LF that follows it.
class ConsoleLogView final : public QPlainTextEdit
{
	Q_OBJECT

public:
	explicit ConsoleLogView(QWidget* parent = nullptr);

	// How an entry ended, which is what its closing line is colored by
	enum class NoteKind : uint8_t { Success, Failure };

	// Drops every entry. Not QPlainTextEdit::clear(), which would empty the document while this view still
	// held a line of it open for rewriting.
	void clearLog();
	// Opens a labelled entry, separated from the previous one by a blank line
	void beginEntry(const QString& label);
	// Process output, in arrival order
	void appendOutput(const QByteArray& chunk);
	// The entry's closing line, which the process did not produce itself: its verdict, a launch failure
	void appendNote(const QString& text, NoteKind kind);

private:
	void resetStreamState();
	void showPendingLine();
	// Appends `text` as a new block in `format`. Every insertion states its own format, since
	// QPlainTextEdit's own appends inherit theirs from wherever the user last put the cursor.
	void appendBlock(const QString& text, const QTextCharFormat& format);
	// Sampled before an edit: an edit at the end moves the maximum, and only a reader who was already
	// at the bottom wants to be carried along
	[[nodiscard]] bool atBottom() const;
	void scrollToBottom();

private:
	// A read can end anywhere, including inside a multi-byte character - this one carries the incomplete
	// tail over to the next chunk, which decoding each chunk on its own cannot do
	QStringDecoder _decoder{ QStringDecoder::Utf8 };
	QString _pendingLine;
	bool _lineOpen = false;      // the last block in the view is _pendingLine, and is rewritten in place
	bool _pendingCr = false;     // a CR is only a line rewrite if the next character is not the LF of a CRLF
};
