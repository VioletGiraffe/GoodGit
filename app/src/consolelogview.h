#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QPlainTextEdit>
#include <QStringDecoder>
#include <QTextFormat>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>

// A log view for raw process output, rendered like a terminal: a carriage return rewrites the current line,
// which is how progress meters redraw themselves.
// Chunks may split anywhere, mid-line or between a CR and its LF.
class ConsoleLogView final : public QPlainTextEdit
{
public:
	explicit ConsoleLogView(QWidget* parent = nullptr);

	enum class NoteKind : uint8_t { Success, Failure };

	// Rather than QPlainTextEdit::clear(), which leaves the stream state pointing at a line that is gone
	void clearLog();
	// Opens a labelled entry, separated from the previous one by a blank line
	void beginEntry(const QString& label);
	void appendOutput(const QByteArray& chunk);
	// What the chunks are; the tool that produces them decides. Call before the first chunk of an entry.
	void setEncoding(QStringConverter::Encoding encoding);
	// A closing line the process did not produce: its verdict, or a launch failure
	void appendNote(const QString& text, NoteKind kind);

private:
	void resetStreamState();
	void showPendingLine();
	// Every insertion states its own format: QPlainTextEdit's own appends inherit the format at the cursor
	void appendBlock(const QString& text, const QTextCharFormat& format);
	// Sampled before an edit, since an edit at the end moves the maximum
	[[nodiscard]] bool atBottom() const;
	void scrollToBottom();

private:
	// Carries an incomplete multi-byte character over to the next chunk
	QStringDecoder _decoder{ QStringDecoder::Utf8 };
	QString _pendingLine;
	bool _lineOpen = false;      // the last block in the view is _pendingLine, rewritten in place
	bool _pendingCr = false;     // a CR only rewrites the line if not followed by LF
};
