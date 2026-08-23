#pragma once

#include <QString>
#include <QStringView>

#include <stdint.h>
#include <vector>

// Reads a unified diff: what each line is, which line of either file it is, and what changed within a line
// that was edited rather than replaced.
// Backend-free: git and Mercurial (with --git) print the same format.

enum class DiffLineKind : uint8_t
{
	Context,
	Added,
	Removed,
	HunkHeader, // @@ -old[,count] +new[,count] @@
	FileHeader, // anything outside a hunk: the preamble, "Binary files ... differ", a second file's header
	NoNewline,  // "\ No newline at end of file": describes the line above and numbers nothing
};

// A line number of 0 means that file has no line here: a removed line is absent from the new file, an
// added one from the old, and a header line from both.
struct DiffLine
{
	DiffLineKind kind = DiffLineKind::FileHeader;
	int oldLine = 0;
	int newLine = 0;
};

// Lines must arrive in the order the diff prints them: numbering counts forward from each hunk header.
class UnifiedDiffScanner
{
public:
	[[nodiscard]] DiffLine scan(QStringView line);

private:
	int _oldLine = 0;
	int _newLine = 0;
	bool _inHunk = false;
};

// A range of one line to mark as changed within it
struct EmphasisSpan
{
	int line = 0;   // index into the lines scanned
	int start = 0;  // character offset into that line, counted from its marker character
	int length = 0;
};

// Pairs each removed line with the added line it was most likely edited into, and answers the spans in
// which the two differ, ascending by line. Lines too unalike to be one edit are left unpaired, and so are
// the lines of a run too large to be anything but a rewrite.
// `lines` is what scan() made of each line, `texts` the lines themselves; the two run parallel.
[[nodiscard]] std::vector<EmphasisSpan> intralineEmphasis(const std::vector<DiffLine>& lines, const std::vector<QString>& texts);
