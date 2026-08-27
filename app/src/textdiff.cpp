#include "textdiff.h"

#include <algorithm>

namespace {

// One line's tokens, as ranges into the line itself
class LineTokens
{
public:
	explicit LineTokens(QStringView text) : _text{ text }
	{
		qsizetype pos = 0;
		while (pos < _text.size())
		{
			const qsizetype start = pos;
			if (isWordCharacter(_text[pos]))
				while (pos < _text.size() && isWordCharacter(_text[pos]))
					++pos;
			else if (_text[pos].isSpace())
				while (pos < _text.size() && _text[pos].isSpace())
					++pos;
			else if (_text[pos].isHighSurrogate() && pos + 1 < _text.size() && _text[pos + 1].isLowSurrogate())
				pos += 2; // one non-BMP character is one token: half of a split pair would diff as a broken glyph
			else
				++pos;

			_ranges.push_back(TextRange{ int(start), int(pos - start) });
		}
	}

	[[nodiscard]] int count() const { return int(_ranges.size()); }
	[[nodiscard]] const TextRange& range(int index) const { return _ranges[size_t(index)]; }
	[[nodiscard]] QStringView token(int index) const { return _text.sliced(_ranges[size_t(index)].start, _ranges[size_t(index)].length); }

	// What sharing this token says about the two lines being one edit. Whitespace says nothing: two
	// sentences share their spacing, and counting it makes any two prose lines look alike.
	[[nodiscard]] int weight(int index) const
	{
		const TextRange& range = _ranges[size_t(index)];
		return _text[range.start].isSpace() ? 0 : range.length;
	}

	[[nodiscard]] int weight(int from, int to) const
	{
		int total = 0;
		for (int index = from; index < to; ++index)
			total += weight(index);
		return total;
	}

private:
	[[nodiscard]] static bool isWordCharacter(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

	QStringView _text;
	std::vector<TextRange> _ranges;
};

// How many tokens the two lines share at the front, and how many more they share at the back
struct CommonEnds
{
	int prefix = 0;
	int suffix = 0;
};

CommonEnds commonEnds(const LineTokens& left, const LineTokens& right)
{
	CommonEnds ends;
	const int limit = std::min(left.count(), right.count());
	while (ends.prefix < limit && left.token(ends.prefix) == right.token(ends.prefix))
		++ends.prefix;
	while (ends.prefix + ends.suffix < limit
		&& left.token(left.count() - 1 - ends.suffix) == right.token(right.count() - 1 - ends.suffix))
		++ends.suffix;
	return ends;
}

// Extends the last segment where the token continues it, so a run of tokens from one side becomes one
// segment. Ranges of one kind are contiguous in the line they read, so only the kind has to match.
void appendSegment(std::vector<MergeSegment>& segments, SegmentKind kind, const TextRange& token)
{
	if (!segments.empty() && segments.back().kind == kind
		&& segments.back().range.start + segments.back().range.length == token.start)
	{
		segments.back().range.length += token.length;
		return;
	}

	segments.push_back(MergeSegment{ kind, token });
}

} // namespace

TokenAlignment alignTokens(QStringView left, QStringView right)
{
	if (left.size() > MaxAlignedLineLength || right.size() > MaxAlignedLineLength)
		return {};

	const LineTokens leftTokens{ left }, rightTokens{ right };
	if (leftTokens.count() == 0 && rightTokens.count() == 0)
		return TokenAlignment{ 1.0, {} };

	const CommonEnds ends = commonEnds(leftTokens, rightTokens);
	const int leftMiddle = leftTokens.count() - ends.prefix - ends.suffix;
	const int rightMiddle = rightTokens.count() - ends.prefix - ends.suffix;
	if (leftMiddle > MaxAlignedTokens || rightMiddle > MaxAlignedTokens)
		return {};

	// Cell (i, j) is the greatest weight the two middles can share from i and from j on, counting a shared
	// token for its characters rather than for one.
	// Counting from the far end makes the walk below run forwards, in the order the segments are emitted.
	const int width = rightMiddle + 1;
	std::vector<int> lcs(size_t(leftMiddle + 1) * size_t(width), 0);
	const auto cell = [width](int i, int j) { return size_t(i) * size_t(width) + size_t(j); };
	const auto sameToken = [&](int i, int j) { return leftTokens.token(ends.prefix + i) == rightTokens.token(ends.prefix + j); };

	for (int i = leftMiddle - 1; i >= 0; --i)
	{
		for (int j = rightMiddle - 1; j >= 0; --j)
		{
			lcs[cell(i, j)] = sameToken(i, j)
				? lcs[cell(i + 1, j + 1)] + leftTokens.weight(ends.prefix + i)
				: std::max(lcs[cell(i + 1, j)], lcs[cell(i, j + 1)]);
		}
	}

	TokenAlignment alignment;
	const int shared = leftTokens.weight(0, ends.prefix) + lcs[cell(0, 0)]
		+ leftTokens.weight(leftTokens.count() - ends.suffix, leftTokens.count());
	const int total = leftTokens.weight(0, leftTokens.count()) + rightTokens.weight(0, rightTokens.count());
	// Two lines of nothing but whitespace differ only in their indentation, which is one edit
	alignment.similarity = total == 0 ? 1.0 : 2.0 * shared / total;

	for (int k = 0; k < ends.prefix; ++k)
		appendSegment(alignment.segments, SegmentKind::Common, leftTokens.range(k));

	int i = 0, j = 0;
	while (i < leftMiddle && j < rightMiddle)
	{
		if (sameToken(i, j))
		{
			appendSegment(alignment.segments, SegmentKind::Common, leftTokens.range(ends.prefix + i));
			++i;
			++j;
		}
		else if (lcs[cell(i + 1, j)] >= lcs[cell(i, j + 1)])
		{
			appendSegment(alignment.segments, SegmentKind::Removed, leftTokens.range(ends.prefix + i));
			++i;
		}
		else
		{
			appendSegment(alignment.segments, SegmentKind::Added, rightTokens.range(ends.prefix + j));
			++j;
		}
	}
	for (; i < leftMiddle; ++i)
		appendSegment(alignment.segments, SegmentKind::Removed, leftTokens.range(ends.prefix + i));
	for (; j < rightMiddle; ++j)
		appendSegment(alignment.segments, SegmentKind::Added, rightTokens.range(ends.prefix + j));

	for (int k = leftTokens.count() - ends.suffix; k < leftTokens.count(); ++k)
		appendSegment(alignment.segments, SegmentKind::Common, leftTokens.range(k));

	return alignment;
}
