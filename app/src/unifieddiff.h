#pragma once

#include "movedblocks.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringView>
RESTORE_COMPILER_WARNINGS

#include <optional>
#include <stdint.h>
#include <vector>

// Reads a unified diff into the lines to show for it. A removed line and the added line one edit turned it
// into become a single line carrying both, where that still reads as one line; every other line is the
// diff's own, unchanged. A block removed in one place and added in another is reported as a move, its
// lines standing as the diff printed them, an edit on the way marked on the added line. Within a change
// set - every file's diff in one text - a block moved between two files is a move too, seen from either.
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
	bool moved = false; // within a block of `moves`: shown as a move, not as a change. Added or Removed only
};

// A range of text only one side of an edit has: on a merged line, either side's; on the added line of a
// moved block, what the block's removed line lacks. No other line carries one: a line the diff printed
// itself is marked by being printed at all.
struct DiffSpan
{
	int line = 0;   // index into the lines
	int start = 0;  // character offset into that line's text
	int length = 0;
	bool removed = false; // text the edit took out, as against what it put in
};

// A move's end in another file of the change set: where a jump from the end shown lands
struct ForeignEnd
{
	QString path;     // the other file, as ChangeSetDiff keys it
	int diffLine = 0; // the first line of the range there, in that file's own diff; its ParsedDiff::shownLine maps it
};

// A move as one file's view has it, in shown lines. A range in another file is empty here, and `foreign`
// says where it is.
struct DiffMove
{
	int removedFirst = 0;
	int removedCount = 0;
	int addedFirst = 0;
	int addedCount = 0;
	int group = 0; // as MovedBlock::group, over the whole change set
	std::optional<ForeignEnd> foreign;
};

struct ParsedDiff
{
	QString text;                // the lines to show, joined by '\n'
	std::vector<DiffLine> lines; // one per line of `text`
	std::vector<DiffSpan> spans; // ascending by line
	std::vector<DiffMove> moves; // ascending by the added range's place in the diff, the set's where there is one
	std::vector<int> shownLine;  // by line of the diff read: the line shown for it, -1 for one merged into a pair
};

// A whole change's diff, every file's in one text, cut into the files' sections. Finds the blocks moved
// between files as well as within one, once, for every section's parse to draw on.
// Sections start at "diff --git" lines with a/ and b/ prefixes, as git prints with the default prefixes
// and hg with --git; a rename is keyed by both its paths.
class ChangeSetDiff
{
public:
	explicit ChangeSetDiff(QString text);

	[[nodiscard]] int fileCount() const { return int(_files.size()); }
	[[nodiscard]] std::optional<int> fileIndex(const QString& path) const; // by either path of a rename
	[[nodiscard]] const QString& filePath(int file) const { return _files[size_t(file)].path; }
	[[nodiscard]] QStringView fileDiff(int file) const;

private:
	friend ParsedDiff parseUnifiedDiff(const ChangeSetDiff& set, int file);

	struct File
	{
		QString path;    // the new path of a rename
		QString oldPath; // renames only
		qsizetype start = 0; // of the section in `_text`
		qsizetype length = 0;
		int firstLine = 0;   // of the section's lines in the sequence `_moves` indexes
		int lineCount = 0;
	};

	[[nodiscard]] int fileOfLine(int line) const; // by a sequence index

	QString _text;
	std::vector<File> _files;       // in the diff's order, ascending by firstLine
	std::vector<MovedBlock> _moves; // over every file's lines in that order, so a block may span two files
};

[[nodiscard]] ParsedDiff parseUnifiedDiff(QStringView diff);
// One section, its moves those of the set that touch it
[[nodiscard]] ParsedDiff parseUnifiedDiff(const ChangeSetDiff& set, int file);

// Whether a diff carries a content change - a hunk, or a binary notice. A mode-only or a line-ending-only
// change prints headers alone.
[[nodiscard]] bool diffHasContent(QStringView diff);
