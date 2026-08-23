#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringView>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>
#include <vector>

// Reads a unified diff into the lines to show for it. A removed line and the added line one edit turned it
// into become a single line carrying both, where that still reads as one line; every other line is the
// diff's own, unchanged.
// Backend-free: git and Mercurial (with --git) print the same format.

enum class DiffLineKind : uint8_t
{
	Context,
	Added,
	Removed,
	Edited,     // one edit as one line, the text it took out and the text it put in interleaved
	HunkHeader, // @@ -old[,count] +new[,count] @@
	FileHeader, // anything outside a hunk: the preamble, "Binary files ... differ", a second file's header
	NoNewline,  // "\ No newline at end of file": describes the line above and numbers nothing
};

// A line number of 0 means that file has no line here: a removed line is absent from the new file, an added
// one from the old, and a header line from both. An edited line carries both.
struct DiffLine
{
	DiffLineKind kind = DiffLineKind::FileHeader;
	int oldLine = 0;
	int newLine = 0;
};

// A range of a merged line that only one side of the edit has. No other kind of line carries one: a line
// the diff printed itself is marked by being printed at all.
struct DiffSpan
{
	int line = 0;   // index into the lines
	int start = 0;  // character offset into that line's text
	int length = 0;
	bool removed = false; // text the edit took out, as against what it put in
};

struct ParsedDiff
{
	QString text;                // the lines to show, joined by '\n'
	std::vector<DiffLine> lines; // one per line of `text`
	std::vector<DiffSpan> spans; // ascending by line
};

[[nodiscard]] ParsedDiff parseUnifiedDiff(QStringView diff);
