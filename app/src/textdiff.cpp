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
			else
				++pos;

			_ranges.push_back(TextRange{ int(start), int(pos - start) });
		}
	}

	[[nodiscard]] int count() const { return int(_ranges.size()); }
	[[nodiscard]] const TextRange& range(int index) const { return _ranges[size_t(index)]; }
	[[nodiscard]] QStringView token(int index) const { return _text.sliced(_ranges[size_t(index)].start, _ranges[size_t(index)].length); }

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

// Extends the last range where the token continues it, so a run of changed tokens becomes one range
void appendToken(std::vector<TextRange>& ranges, const TextRange& token)
{
	if (!ranges.empty() && ranges.back().start + ranges.back().length == token.start)
		ranges.back().length += token.length;
	else
		ranges.push_back(token);
}

} // namespace

TokenAlignment alignTokens(QStringView left, QStringView right)
{
	if (left.size() > MaxAlignedLineLength || right.size() > MaxAlignedLineLength)
		return {};

	const LineTokens leftTokens{ left }, rightTokens{ right };
	if (leftTokens.count() == 0 && rightTokens.count() == 0)
		return TokenAlignment{ 1.0, {}, {} };

	const CommonEnds ends = commonEnds(leftTokens, rightTokens);
	const int leftMiddle = leftTokens.count() - ends.prefix - ends.suffix;
	const int rightMiddle = rightTokens.count() - ends.prefix - ends.suffix;
	if (leftMiddle > MaxAlignedTokens || rightMiddle > MaxAlignedTokens)
		return {};

	// Cell (i, j) is the length of the longest common subsequence of the two middles from i and from j on.
	// Counting from the far end makes the walk below run forwards, in the order the ranges are emitted.
	const int width = rightMiddle + 1;
	std::vector<int> lcs(size_t(leftMiddle + 1) * size_t(width), 0);
	const auto cell = [width](int i, int j) { return size_t(i) * size_t(width) + size_t(j); };
	const auto sameToken = [&](int i, int j) { return leftTokens.token(ends.prefix + i) == rightTokens.token(ends.prefix + j); };

	for (int i = leftMiddle - 1; i >= 0; --i)
	{
		for (int j = rightMiddle - 1; j >= 0; --j)
		{
			lcs[cell(i, j)] = sameToken(i, j)
				? lcs[cell(i + 1, j + 1)] + 1
				: std::max(lcs[cell(i + 1, j)], lcs[cell(i, j + 1)]);
		}
	}

	TokenAlignment alignment;
	const int common = ends.prefix + ends.suffix + lcs[cell(0, 0)];
	alignment.similarity = 2.0 * common / (leftTokens.count() + rightTokens.count());

	int i = 0, j = 0;
	while (i < leftMiddle && j < rightMiddle)
	{
		if (sameToken(i, j))
		{
			++i;
			++j;
		}
		else if (lcs[cell(i + 1, j)] >= lcs[cell(i, j + 1)])
		{
			appendToken(alignment.leftChanges, leftTokens.range(ends.prefix + i));
			++i;
		}
		else
		{
			appendToken(alignment.rightChanges, rightTokens.range(ends.prefix + j));
			++j;
		}
	}
	for (; i < leftMiddle; ++i)
		appendToken(alignment.leftChanges, leftTokens.range(ends.prefix + i));
	for (; j < rightMiddle; ++j)
		appendToken(alignment.rightChanges, rightTokens.range(ends.prefix + j));

	return alignment;
}
