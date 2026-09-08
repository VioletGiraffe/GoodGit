#include "unifieddiff.h"
#include "movedblocks.h"
#include "textdiff.h"

#include <algorithm>
#include <assert.h>
#include <utility>

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

// The lines [begin, end) past their marker character: it is not content, and the two sides never carry the same one
std::vector<QStringView> contents(const std::vector<QStringView>& texts, int begin, int end)
{
	std::vector<QStringView> result;
	result.reserve(size_t(end - begin));
	for (int k = begin; k < end; ++k)
		result.push_back(texts[size_t(k)].sliced(1));
	return result;
}

// Renders the removed lines [removedBegin, removedEnd) against the added lines [addedBegin, addedEnd),
// pairing each removed line with the added line it was most likely edited into. The lines between two
// pairs are emitted removed first, as the diff prints a modification.
void appendRun(ParsedDiff& parsed, const std::vector<DiffLine>& lines, const std::vector<QStringView>& texts,
	int removedBegin, int removedEnd, int addedBegin, int addedEnd)
{
	const std::vector<LinePair> pairs = pairSimilarLines(contents(texts, removedBegin, removedEnd), contents(texts, addedBegin, addedEnd));

	int i = removedBegin, j = addedBegin;
	const auto appendUnpairedUpTo = [&](int removedIndex, int addedIndex) {
		for (; i < removedIndex; ++i)
			appendLine(parsed, lines[size_t(i)], texts[size_t(i)]);
		for (; j < addedIndex; ++j)
			appendLine(parsed, lines[size_t(j)], texts[size_t(j)]);
	};

	for (const LinePair& pair : pairs)
	{
		appendUnpairedUpTo(removedBegin + pair.left, addedBegin + pair.right);
		appendPair(parsed, lines, texts, i, j, pair.alignment);
		++i;
		++j;
	}
	appendUnpairedUpTo(removedEnd, addedEnd);
}

// One file's diff read into its lines, before anything is rendered
struct ScannedDiff
{
	qsizetype textSize = 0; // of the diff read, which the text shown is about as long as
	std::vector<SplitLine> splits;
	std::vector<DiffLine> lines;
	std::vector<QStringView> texts;
};

ScannedDiff scanDiff(QStringView diff)
{
	ScannedDiff scanned;
	scanned.textSize = diff.size();
	scanned.splits = splitLines(diff);
	scanned.lines.reserve(scanned.splits.size());
	scanned.texts.reserve(scanned.splits.size());
	UnifiedDiffScanner scanner;
	for (const SplitLine& split : scanned.splits)
	{
		scanned.lines.push_back(scanner.scan(split));
		scanned.texts.push_back(split.text);
	}
	return scanned;
}

// The lines as the move detection takes them, one per line read: a continuation is a fragment without a
// marker char, never part of a block
std::vector<ChangeLine> changeLinesOf(const ScannedDiff& scanned)
{
	std::vector<ChangeLine> changeLines;
	changeLines.reserve(scanned.lines.size());
	for (size_t i = 0; i < scanned.lines.size(); ++i)
	{
		ChangeLine change;
		const DiffLineKind kind = scanned.lines[i].kind;
		if (!scanned.splits[i].continuation && (kind == DiffLineKind::Removed || kind == DiffLineKind::Added))
			change = { scanned.texts[i].sliced(1), kind == DiffLineKind::Removed ? ChangeSide::Removed : ChangeSide::Added };
		changeLines.push_back(change);
	}
	return changeLines;
}

// A move as the rendering takes it in: the block in the lines read, a range in another file empty, starting
// at 0, and that end named. A pair's index on an empty range is meaningless.
struct FileMove
{
	MovedBlock block;
	std::optional<ForeignEnd> foreign;
};

// Renders the lines read into the lines shown, the moves given standing as read: they are found first, or
// the pairing would merge a moved line with whatever was added in the block's place
ParsedDiff render(const ScannedDiff& scanned, std::vector<FileMove> moves)
{
	const std::vector<SplitLine>& splits = scanned.splits;
	const std::vector<DiffLine>& lines = scanned.lines;
	const std::vector<QStringView>& texts = scanned.texts;

	std::vector<bool> moved(lines.size(), false);
	for (const FileMove& move : moves)
	{
		std::fill_n(moved.begin() + move.block.removedFirst, move.block.removedCount, true);
		std::fill_n(moved.begin() + move.block.addedFirst, move.block.addedCount, true);
	}

	ParsedDiff parsed;
	parsed.text.reserve(scanned.textSize);
	parsed.lines.reserve(lines.size());
	parsed.shownLine.assign(lines.size(), -1);
	std::vector<int>& shownLine = parsed.shownLine;
	// A line that can join a run's pairing: a continuation has no marker char, and a moved line is spoken for
	const auto pairable = [&](int k, DiffLineKind kind) {
		return lines[size_t(k)].kind == kind && !splits[size_t(k)].continuation && !moved[size_t(k)];
	};

	const int count = int(lines.size());
	for (int i = 0; i < count; )
	{
		if (!pairable(i, DiffLineKind::Removed))
		{
			shownLine[size_t(i)] = int(parsed.lines.size());
			appendLine(parsed, lines[size_t(i)], texts[size_t(i)]);
			++i;
			continue;
		}

		// A modification prints its removed lines and then its added ones. Anything else between them - a
		// hunk header, a context line, a continuation, a moved line - ends the run, and the removed lines are
		// a deletion of their own.
		int removedEnd = i;
		while (removedEnd < count && pairable(removedEnd, DiffLineKind::Removed))
			++removedEnd;

		// Except the no-newline marker, which annotates the removed line before it rather than ending the
		// run; it is shown after the pair it interrupted
		int addedBegin = removedEnd;
		if (addedBegin + 1 < count && lines[size_t(addedBegin)].kind == DiffLineKind::NoNewline
			&& lines[size_t(addedBegin + 1)].kind == DiffLineKind::Added)
			++addedBegin;
		int addedEnd = addedBegin;
		while (addedEnd < count && pairable(addedEnd, DiffLineKind::Added))
			++addedEnd;

		appendRun(parsed, lines, texts, i, removedEnd, addedBegin, addedEnd);
		if (addedBegin != removedEnd)
			appendLine(parsed, lines[size_t(removedEnd)], texts[size_t(removedEnd)]);
		i = addedEnd;
	}

	// A block's lines are all shown as they stand, one after another, so each maps to the line shown for it
	for (FileMove& move : moves)
	{
		const MovedBlock& block = move.block;
		DiffMove shown;
		shown.removedCount = block.removedCount;
		shown.addedCount = block.addedCount;
		shown.group = block.group;
		shown.foreign = std::move(move.foreign);
		if (block.removedCount > 0)
		{
			shown.removedFirst = shownLine[size_t(block.removedFirst)];
			assert(shown.removedFirst >= 0);
			for (int k = 0; k < block.removedCount; ++k)
				parsed.lines[size_t(shown.removedFirst + k)].moved = true;
		}
		if (block.addedCount > 0)
		{
			shown.addedFirst = shownLine[size_t(block.addedFirst)];
			assert(shown.addedFirst >= 0);
			for (int k = 0; k < block.addedCount; ++k)
				parsed.lines[size_t(shown.addedFirst + k)].moved = true;

			// An edit on the way is marked on the added line alone: the edit belongs where the block now is
			for (const MovedLinePair& pair : block.pairs)
			{
				if (!pair.edited)
					continue;
				for (const MergeSegment& segment : pair.alignment.segments)
				{
					if (segment.kind == SegmentKind::Added)
						parsed.spans.push_back(DiffSpan{ shownLine[size_t(pair.added)], segment.range.start + 1, segment.range.length, false }); // past the marker
				}
			}
		}
		parsed.moves.push_back(std::move(shown));
	}
	// The runs' spans came in line order; the moves' were appended after them
	std::stable_sort(parsed.spans.begin(), parsed.spans.end(), [](const DiffSpan& l, const DiffSpan& r) { return l.line < r.line; });

	return parsed;
}

// The path of a "diff --git a/P b/P" header, empty where the two differ: a rename names its paths on lines
// of their own
QString samePathOf(QStringView header)
{
	const QStringView rest = header.sliced(11); // past "diff --git "
	const qsizetype length = (rest.size() - 5) / 2; // "a/" P " b/" P
	if (length < 0 || rest.size() != 2 * length + 5 || !rest.startsWith(QLatin1String("a/")) || rest.sliced(length + 2, 3) != QLatin1String(" b/"))
		return {};
	const QStringView path = rest.sliced(2, length);
	return path == rest.sliced(length + 5) ? path.toString() : QString{};
}

} // namespace

ParsedDiff parseUnifiedDiff(QStringView diff)
{
	const ScannedDiff scanned = scanDiff(diff);
	std::vector<FileMove> moves;
	for (MovedBlock& block : detectMovedBlocks(changeLinesOf(scanned)))
		moves.push_back(FileMove{ std::move(block), std::nullopt });
	return render(scanned, std::move(moves));
}

ChangeSetDiff::ChangeSetDiff(QString text) :
	_text{ std::move(text) }
{
	const QStringView whole{ _text };
	const QLatin1String sectionStart{ "diff --git " };
	// Every section start, then each section's paths from its header lines
	for (qsizetype pos = 0; pos < whole.size(); )
	{
		qsizetype lineEnd = whole.indexOf(QLatin1Char('\n'), pos);
		if (lineEnd < 0)
			lineEnd = whole.size();
		const QStringView line = whole.sliced(pos, lineEnd - pos);
		if (line.startsWith(sectionStart))
		{
			if (!_files.empty())
				_files.back().length = pos - _files.back().start;
			_files.push_back(File{ .path = samePathOf(line), .start = pos });
		}
		else if (!_files.empty() && _files.back().path.isEmpty())
		{
			// A rename's or copy's header lines, up to the first hunk or a binary notice
			if (line.startsWith(QLatin1String("rename from ")) || line.startsWith(QLatin1String("copy from ")))
				_files.back().oldPath = line.sliced(line.indexOf(QLatin1String("from ")) + 5).toString();
			else if (line.startsWith(QLatin1String("rename to ")) || line.startsWith(QLatin1String("copy to ")))
				_files.back().path = line.sliced(line.indexOf(QLatin1String("to ")) + 3).toString();
		}
		pos = lineEnd + 1;
	}
	if (!_files.empty())
		_files.back().length = whole.size() - _files.back().start;

	std::vector<ChangeLine> sequence;
	for (size_t i = 0; i < _files.size(); ++i)
	{
		const ScannedDiff scanned = scanDiff(fileDiff(int(i)));
		const std::vector<ChangeLine> changeLines = changeLinesOf(scanned);
		_files[i].firstLine = int(sequence.size());
		_files[i].lineCount = int(changeLines.size());
		sequence.insert(sequence.end(), changeLines.begin(), changeLines.end());
	}
	_moves = detectMovedBlocks(sequence);
}

std::optional<int> ChangeSetDiff::fileIndex(const QString& path) const
{
	for (size_t i = 0; i < _files.size(); ++i)
	{
		if (_files[i].path == path || (!_files[i].oldPath.isEmpty() && _files[i].oldPath == path))
			return int(i);
	}
	return std::nullopt;
}

QStringView ChangeSetDiff::fileDiff(int file) const
{
	return QStringView{ _text }.sliced(_files[size_t(file)].start, _files[size_t(file)].length);
}

int ChangeSetDiff::fileOfLine(int line) const
{
	const auto after = std::upper_bound(_files.begin(), _files.end(), line, [](int l, const File& file) { return l < file.firstLine; });
	assert(after != _files.begin());
	return int(after - _files.begin()) - 1;
}

ParsedDiff parseUnifiedDiff(const ChangeSetDiff& set, int file)
{
	const ChangeSetDiff::File& here = set._files[size_t(file)];
	const auto inThisFile = [&](int line) { return line >= here.firstLine && line < here.firstLine + here.lineCount; };
	const auto foreignEnd = [&](int line) {
		const int thereIndex = set.fileOfLine(line);
		const ChangeSetDiff::File& there = set._files[size_t(thereIndex)];
		return ForeignEnd{ there.path, line - there.firstLine, thereIndex > file };
	};

	std::vector<FileMove> moves;
	for (const MovedBlock& block : set._moves)
	{
		const bool removedHere = inThisFile(block.removedFirst), addedHere = inThisFile(block.addedFirst);
		if (!removedHere && !addedHere)
			continue;

		FileMove move{ block, std::nullopt };
		if (removedHere)
		{
			move.block.removedFirst -= here.firstLine;
			for (MovedLinePair& pair : move.block.pairs)
				pair.removed -= here.firstLine;
		}
		else
		{
			move.block.removedFirst = 0;
			move.block.removedCount = 0;
			move.foreign = foreignEnd(block.removedFirst);
		}
		if (addedHere)
		{
			move.block.addedFirst -= here.firstLine;
			for (MovedLinePair& pair : move.block.pairs)
				pair.added -= here.firstLine;
		}
		else
		{
			move.block.addedFirst = 0;
			move.block.addedCount = 0;
			move.foreign = foreignEnd(block.addedFirst);
		}
		moves.push_back(std::move(move));
	}

	return render(scanDiff(set.fileDiff(file)), std::move(moves));
}

bool diffHasContent(QStringView diff)
{
	const auto hasLineStarting = [&](QLatin1String start) {
		return diff.startsWith(start) || diff.contains(QLatin1Char('\n') + QString{ start });
	};
	return hasLineStarting(QLatin1String("@@")) || hasLineStarting(QLatin1String("Binary file")) || hasLineStarting(QLatin1String("GIT binary patch"));
}
