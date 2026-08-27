#include "unifieddiff.h"
#include "textdiff.h"

#include <algorithm>
#include <assert.h>
#include <utility>

// Below this the two lines have nothing to do with each other. It is a floor and a way to rank candidates,
// not the test of whether one line was edited into the other: how many fragments the merge comes out in
// answers that far better, and does it after the two have been aligned.
static constexpr double SimilarityThreshold = 0.3;
// A run offering more pairings than this is a rewritten block, where no pairing is worth finding
static constexpr int MaxRunPairings = 100;

// A merged line stops reading as one line once it is a chain of alternating old and new fragments. A longer
// line carries more of them before that happens, so the allowance grows with it.
// A pair exceeding it is not one edit at all: its lines stand as the diff printed them, unmarked, since
// marking that many fragments is the same noise in another shape.
static constexpr int BaseInlineChanges = 3;
static constexpr int CharsPerExtraInlineChange = 80;
static constexpr int MaxInlineChanges = 8;

// Column 0 of a merged line, where the diff's own lines carry ' ', '+' or '-'
static constexpr char EditedMarker = '~';

namespace {

// One fragment of the diff text between block terminators. `continuation`: the fragment was cut off from
// the previous one by an in-line terminator rather than a line feed - it is the same diff line, continued.
struct SplitLine
{
	QStringView text;
	bool continuation = false;
};

// Reads a unified diff one line at a time, classifying each line and numbering it against both files.
// Lines must arrive in the order the diff prints them: numbering counts forward from each hunk header.
class UnifiedDiffScanner
{
public:
	[[nodiscard]] DiffLine scan(const SplitLine& line);

private:
	[[nodiscard]] DiffLine classify(QStringView line);

private:
	int _oldLine = 0;
	int _newLine = 0;
	bool _inHunk = false;
	DiffLineKind _lastKind = DiffLineKind::FileHeader;
};

// Reads the digits at `pos`, leaving it on the first character after them. -1 where there are none, or
// where the number does not fit an int.
int parseNumber(QStringView text, qsizetype& pos)
{
	const qsizetype start = pos;
	int64_t value = 0;
	while (pos < text.size() && text[pos].isDigit())
	{
		value = value * 10 + text[pos].digitValue();
		if (value > INT32_MAX)
			return -1;
		++pos;
	}
	return pos > start ? int(value) : -1;
}

// The two starting line numbers of "@@ -old[,count] +new[,count] @@", -1 each where the header is
// malformed. Only the part between the two markers is read: the context text after the second may hold
// anything, '+' included.
std::pair<int, int> parseHunkHeader(QStringView line)
{
	const qsizetype end = line.indexOf(QLatin1String("@@"), 2);
	if (end < 0)
		return { -1, -1 };

	const QStringView ranges = line.sliced(2, end - 2);
	const qsizetype oldMarker = ranges.indexOf(QLatin1Char('-'));
	const qsizetype newMarker = ranges.indexOf(QLatin1Char('+'));
	if (oldMarker < 0 || newMarker < 0)
		return { -1, -1 };

	qsizetype oldPos = oldMarker + 1, newPos = newMarker + 1;
	const int oldStart = parseNumber(ranges, oldPos);
	const int newStart = parseNumber(ranges, newPos);
	return { oldStart, newStart };
}

DiffLine UnifiedDiffScanner::scan(const SplitLine& line)
{
	// The same diff line, split for display at an in-line terminator: it keeps the kind, and the numbers
	// stay on the first fragment
	if (line.continuation)
		return { _lastKind };

	const DiffLine result = classify(line.text);
	_lastKind = result.kind;
	return result;
}

DiffLine UnifiedDiffScanner::classify(QStringView line)
{
	if (line.startsWith(QLatin1String("@@")))
	{
		const auto [oldStart, newStart] = parseHunkHeader(line);
		if (oldStart >= 0 && newStart >= 0)
		{
			_oldLine = oldStart;
			_newLine = newStart;
			_inHunk = true;
			return { DiffLineKind::HunkHeader };
		}
	}

	if (!_inHunk)
		return { DiffLineKind::FileHeader };

	// An empty line is a context line whose single leading space was stripped along the way
	const QChar marker = line.isEmpty() ? QLatin1Char(' ') : line.front();
	if (marker == QLatin1Char(' '))
		return { DiffLineKind::Context, _oldLine++, _newLine++ };
	if (marker == QLatin1Char('+'))
		return { DiffLineKind::Added, 0, _newLine++ };
	if (marker == QLatin1Char('-'))
		return { DiffLineKind::Removed, _oldLine++, 0 };
	if (marker == QLatin1Char('\\'))
		return { DiffLineKind::NoNewline };

	// The hunk is over: the next file's header, or a trailer the diff appends
	_inHunk = false;
	return { DiffLineKind::FileHeader };
}

// QTextDocument ends a block at any of these and reads a CRLF as one. Splitting the same way keeps one line
// of the answer to one block of whatever shows it.
bool isBlockTerminator(QChar c)
{
	return c == QLatin1Char('\n') || c == QLatin1Char('\r') || c == QChar::ParagraphSeparator
		|| c == QChar(0xfdd0) || c == QChar(0xfdd1); // QTextBeginningOfFrame, QTextEndOfFrame
}

// Views into `diff`, so nothing is copied. A trailing terminator ends the last line rather than opening
// an empty one.
std::vector<SplitLine> splitLines(QStringView diff)
{
	std::vector<SplitLine> lines;
	qsizetype start = 0;
	bool continuation = false;
	for (qsizetype pos = 0; pos < diff.size(); ++pos)
	{
		if (!isBlockTerminator(diff[pos]))
			continue;

		lines.push_back({ diff.sliced(start, pos - start), continuation });
		// Only a line feed ends the diff's own line; the other terminators are file content
		const bool crlf = diff[pos] == QLatin1Char('\r') && pos + 1 < diff.size() && diff[pos + 1] == QLatin1Char('\n');
		continuation = diff[pos] != QLatin1Char('\n') && !crlf;
		if (crlf)
			++pos;
		start = pos + 1;
	}

	if (start < diff.size())
		lines.push_back({ diff.sliced(start), continuation });
	if (lines.empty())
		lines.push_back({ diff, false }); // empty text is one empty line, as it is one empty block
	return lines;
}

void appendLine(ParsedDiff& parsed, const DiffLine& line, QStringView text)
{
	if (!parsed.lines.empty())
		parsed.text += QLatin1Char('\n');
	parsed.lines.push_back(line);
	parsed.text += text;
}

// Spans of the line appended last, which is where they were measured against
void appendSpans(ParsedDiff& parsed, std::vector<DiffSpan> spans)
{
	assert(!parsed.lines.empty());

	const int line = int(parsed.lines.size()) - 1;
	for (DiffSpan& span : spans)
	{
		span.line = line;
		parsed.spans.push_back(span);
	}
}

// One edit as one line: the marker, then the two lines interleaved
struct MergedLine
{
	QString text;
	std::vector<DiffSpan> spans; // measured against `text`, awaiting the line they land on
	int changeCount = 0;         // runs of text only one side has, however many segments each holds
};

MergedLine mergeLines(QStringView removed, QStringView added, const std::vector<MergeSegment>& segments)
{
	MergedLine merged;
	merged.text += QLatin1Char(EditedMarker);

	bool withinChange = false;
	for (const MergeSegment& segment : segments)
	{
		const QStringView source = segment.kind == SegmentKind::Added ? added : removed;
		if (segment.kind != SegmentKind::Common)
		{
			if (!withinChange)
				++merged.changeCount;
			merged.spans.push_back(DiffSpan{ 0, int(merged.text.size()), segment.range.length,
				segment.kind == SegmentKind::Removed });
		}
		withinChange = segment.kind != SegmentKind::Common;

		merged.text += source.sliced(segment.range.start, segment.range.length);
	}

	return merged;
}

int maxInlineChanges(qsizetype lineLength)
{
	return std::min(int(BaseInlineChanges + lineLength / CharsPerExtraInlineChange), MaxInlineChanges);
}

// One pair, merged into a line where that reads, and left as the diff's own two lines where it does not
void appendPair(ParsedDiff& parsed, const std::vector<DiffLine>& lines, const std::vector<QStringView>& texts,
	int removedIndex, int addedIndex, const TokenAlignment& alignment)
{
	const QStringView removedText = texts[size_t(removedIndex)], addedText = texts[size_t(addedIndex)];
	// Past the marker character, which is not content
	const MergedLine merged = mergeLines(removedText.sliced(1), addedText.sliced(1), alignment.segments);

	if (merged.changeCount <= maxInlineChanges(std::max(removedText.size(), addedText.size())))
	{
		appendLine(parsed, DiffLine{ DiffLineKind::Edited, lines[size_t(removedIndex)].oldLine,
			lines[size_t(addedIndex)].newLine }, merged.text);
		appendSpans(parsed, merged.spans);
		return;
	}

	appendLine(parsed, lines[size_t(removedIndex)], removedText);
	appendLine(parsed, lines[size_t(addedIndex)], addedText);
}

// Renders the removed lines [removedBegin, removedEnd) against the added lines [addedBegin, addedEnd),
// pairing each removed line with the added line it was most likely edited into.
void appendRun(ParsedDiff& parsed, const std::vector<DiffLine>& lines, const std::vector<QStringView>& texts,
	int removedBegin, int removedEnd, int addedBegin, int addedEnd)
{
	const int removedCount = removedEnd - removedBegin;
	const int addedCount = addedEnd - addedBegin;
	if (addedCount == 0 || int64_t(removedCount) * addedCount > MaxRunPairings)
	{
		for (int k = removedBegin; k < removedEnd; ++k)
			appendLine(parsed, lines[size_t(k)], texts[size_t(k)]);
		for (int k = addedBegin; k < addedEnd; ++k)
			appendLine(parsed, lines[size_t(k)], texts[size_t(k)]);
		return;
	}

	// Every candidate pair, aligned once: the pairing scores them all, and an accepted pair reuses the
	// segments the alignment already found
	std::vector<TokenAlignment> alignments;
	alignments.reserve(size_t(removedCount) * size_t(addedCount));
	for (int i = 0; i < removedCount; ++i)
	{
		for (int j = 0; j < addedCount; ++j)
		{
			// Past the marker character: it is not content, and the two sides never carry the same one
			alignments.push_back(alignTokens(texts[size_t(removedBegin + i)].sliced(1),
				texts[size_t(addedBegin + j)].sliced(1)));
		}
	}

	const auto alignment = [&](int i, int j) -> const TokenAlignment& { return alignments[size_t(i) * size_t(addedCount) + size_t(j)]; };

	// best(i, j) is the highest total similarity reachable by pairing the removed lines from i on with the
	// added lines from j on. Pairs never cross: a diff is a sequence, and a crossing pair would mark a line
	// against one it does not answer. It is also what lets the walk below emit the run in one order.
	const int width = addedCount + 1;
	std::vector<double> best(size_t(removedCount + 1) * size_t(width), 0.0);
	const auto cell = [width](int i, int j) { return size_t(i) * size_t(width) + size_t(j); };
	const auto pairedScore = [&](int i, int j) {
		const double similarity = alignment(i, j).similarity;
		return similarity >= SimilarityThreshold ? similarity + best[cell(i + 1, j + 1)] : -1.0;
	};

	for (int i = removedCount - 1; i >= 0; --i)
		for (int j = addedCount - 1; j >= 0; --j)
			best[cell(i, j)] = std::max({ best[cell(i + 1, j)], best[cell(i, j + 1)], pairedScore(i, j) });

	int i = 0, j = 0;
	while (i < removedCount || j < addedCount)
	{
		if (i < removedCount && j < addedCount && pairedScore(i, j) >= best[cell(i, j)])
		{
			appendPair(parsed, lines, texts, removedBegin + i, addedBegin + j, alignment(i, j));
			++i;
			++j;
		}
		else if (j >= addedCount || (i < removedCount && best[cell(i + 1, j)] >= best[cell(i, j + 1)]))
		{
			appendLine(parsed, lines[size_t(removedBegin + i)], texts[size_t(removedBegin + i)]);
			++i;
		}
		else
		{
			appendLine(parsed, lines[size_t(addedBegin + j)], texts[size_t(addedBegin + j)]);
			++j;
		}
	}
}

} // namespace

ParsedDiff parseUnifiedDiff(QStringView diff)
{
	const std::vector<SplitLine> splits = splitLines(diff);

	std::vector<DiffLine> lines;
	std::vector<QStringView> texts;
	lines.reserve(splits.size());
	texts.reserve(splits.size());
	UnifiedDiffScanner scanner;
	for (const SplitLine& split : splits)
	{
		lines.push_back(scanner.scan(split));
		texts.push_back(split.text);
	}

	ParsedDiff parsed;
	parsed.text.reserve(diff.size());
	parsed.lines.reserve(lines.size());

	const int count = int(lines.size());
	for (int i = 0; i < count; )
	{
		// A continuation carries no marker char, so it cannot join a run's pairing: shown as it stands
		if (lines[size_t(i)].kind != DiffLineKind::Removed || splits[size_t(i)].continuation)
		{
			appendLine(parsed, lines[size_t(i)], texts[size_t(i)]);
			++i;
			continue;
		}

		// A modification prints its removed lines and then its added ones. Anything else between them - a
		// hunk header, a context line, a continuation - ends the run, and the removed lines are a deletion
		// of their own.
		int removedEnd = i;
		while (removedEnd < count && lines[size_t(removedEnd)].kind == DiffLineKind::Removed && !splits[size_t(removedEnd)].continuation)
			++removedEnd;

		// Except the no-newline marker, which annotates the removed line before it rather than ending the
		// run; it is shown after the pair it interrupted
		int addedBegin = removedEnd;
		if (addedBegin + 1 < count && lines[size_t(addedBegin)].kind == DiffLineKind::NoNewline
			&& lines[size_t(addedBegin + 1)].kind == DiffLineKind::Added)
			++addedBegin;
		int addedEnd = addedBegin;
		while (addedEnd < count && lines[size_t(addedEnd)].kind == DiffLineKind::Added && !splits[size_t(addedEnd)].continuation)
			++addedEnd;

		appendRun(parsed, lines, texts, i, removedEnd, addedBegin, addedEnd);
		if (addedBegin != removedEnd)
			appendLine(parsed, lines[size_t(removedEnd)], texts[size_t(removedEnd)]);
		i = addedEnd;
	}

	return parsed;
}
