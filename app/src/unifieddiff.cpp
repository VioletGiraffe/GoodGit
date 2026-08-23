#include "unifieddiff.h"
#include "textdiff.h"

#include <algorithm>
#include <assert.h>
#include <utility>

// Below this the two lines are separate lines rather than one edited into the other, and every token of
// them would be marked
static constexpr double SimilarityThreshold = 0.5;
// A run offering more pairings than this is a rewritten block, where no pairing is worth finding
static constexpr int MaxRunPairings = 100;

namespace {

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

} // namespace

DiffLine UnifiedDiffScanner::scan(QStringView line)
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

namespace {

// Pairs the removed lines [removedBegin, addedBegin) with the added lines [addedBegin, addedEnd) and
// appends the spans of every pair accepted, the removed side first: the view walks the spans in step with
// the lines, and every removed line of a run precedes every added one.
void emphasizeRun(const std::vector<QString>& texts, int removedBegin, int addedBegin, int addedEnd,
	std::vector<EmphasisSpan>& spans)
{
	const int removedCount = addedBegin - removedBegin;
	const int addedCount = addedEnd - addedBegin;
	if (int64_t(removedCount) * addedCount > MaxRunPairings)
		return;

	// Every candidate pair, aligned once: the pairing scores them all, and an accepted pair reuses the
	// ranges the alignment already found
	std::vector<TokenAlignment> alignments;
	alignments.reserve(size_t(removedCount) * size_t(addedCount));
	for (int i = 0; i < removedCount; ++i)
	{
		for (int j = 0; j < addedCount; ++j)
		{
			// Past the marker character: it is not content, and the two sides never carry the same one
			alignments.push_back(alignTokens(QStringView{ texts[size_t(removedBegin + i)] }.sliced(1),
				QStringView{ texts[size_t(addedBegin + j)] }.sliced(1)));
		}
	}

	const auto alignment = [&](int i, int j) -> const TokenAlignment& { return alignments[size_t(i) * size_t(addedCount) + size_t(j)]; };

	// best(i, j) is the highest total similarity reachable by pairing the removed lines from i on with the
	// added lines from j on. Pairs never cross: a diff is a sequence, and a crossing pair would mark a line
	// against one it does not answer.
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

	std::vector<std::pair<int, int>> pairs;
	int i = 0, j = 0;
	while (i < removedCount && j < addedCount)
	{
		if (pairedScore(i, j) >= best[cell(i, j)])
		{
			pairs.emplace_back(i, j);
			++i;
			++j;
		}
		else if (best[cell(i + 1, j)] >= best[cell(i, j + 1)])
			++i;
		else
			++j;
	}

	for (const auto& [removed, added] : pairs)
		for (const TextRange& range : alignment(removed, added).leftChanges)
			spans.push_back(EmphasisSpan{ removedBegin + removed, range.start + 1, range.length });
	for (const auto& [removed, added] : pairs)
		for (const TextRange& range : alignment(removed, added).rightChanges)
			spans.push_back(EmphasisSpan{ addedBegin + added, range.start + 1, range.length });
}

} // namespace

std::vector<EmphasisSpan> intralineEmphasis(const std::vector<DiffLine>& lines, const std::vector<QString>& texts)
{
	assert(lines.size() == texts.size());

	std::vector<EmphasisSpan> spans;
	const int count = int(lines.size());
	for (int i = 0; i < count; )
	{
		if (lines[size_t(i)].kind != DiffLineKind::Removed)
		{
			++i;
			continue;
		}

		// A modification prints its removed lines and then its added ones. Anything else between them - a
		// hunk header, a context line - ends the run, and the removed lines are a deletion of their own.
		int addedBegin = i;
		while (addedBegin < count && lines[size_t(addedBegin)].kind == DiffLineKind::Removed)
			++addedBegin;
		int addedEnd = addedBegin;
		while (addedEnd < count && lines[size_t(addedEnd)].kind == DiffLineKind::Added)
			++addedEnd;

		if (addedEnd > addedBegin)
			emphasizeRun(texts, i, addedBegin, addedEnd, spans);
		i = addedEnd;
	}

	return spans;
}
