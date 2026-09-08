#include "movedblocks.h"

DISABLE_COMPILER_WARNINGS
#include <QtCore/qhashfunctions.h> // std::hash<QStringView>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>

namespace {

int alnumCount(QStringView text)
{
	int count = 0;
	for (const QChar c : text)
	{
		if (c.isLetterOrNumber())
			++count;
	}
	return count;
}

// Union-find over block indices
class BlockGroups
{
public:
	explicit BlockGroups(size_t count) : _parent(count)
	{
		std::iota(_parent.begin(), _parent.end(), 0);
	}

	int find(int block)
	{
		while (_parent[size_t(block)] != block)
		{
			_parent[size_t(block)] = _parent[size_t(_parent[size_t(block)])];
			block = _parent[size_t(block)];
		}
		return block;
	}

	void unite(int a, int b)
	{
		a = find(a);
		b = find(b);
		if (a != b)
			_parent[size_t(b)] = a;
	}

private:
	std::vector<int> _parent;
};

// The per-line tables of one sequence, and the added lines the blocks reported so far took
class BlockGrower
{
public:
	explicit BlockGrower(const std::vector<ChangeLine>& lines) :
		_lines{ lines }, _count{ int(lines.size()) }, _texts(lines.size()), _trimmed(lines.size()), _alnum(lines.size()), _taken(lines.size(), false)
	{
		for (size_t i = 0; i < lines.size(); ++i)
		{
			_texts[i] = lines[i].text;
			_trimmed[i] = lines[i].text.trimmed();
			_alnum[i] = alnumCount(_trimmed[i]);
			if (lines[i].side == ChangeSide::Removed)
				_removedByText[_trimmed[i]].push_back(int(i));
		}
	}

	// The removed lines equal to the added line `a`, ascending. Null where `a` seeds no block: taken, without
	// content, matched by nothing, or by too much.
	[[nodiscard]] const std::vector<int>* seedCopies(int a) const
	{
		if (!addedAt(a) || _alnum[size_t(a)] == 0)
			return nullptr;
		const auto copies = _removedByText.find(_trimmed[size_t(a)]);
		if (copies == _removedByText.end() || copies->second.size() > size_t(MaxBlockStartCandidates))
			return nullptr;
		return &copies->second;
	}

	// The block around the anchor (r, a): everything pairing from it either way
	[[nodiscard]] MovedBlock grow(int r, int a) const
	{
		std::vector<MovedLinePair> pairs;
		extend(r - 1, a - 1, -1, pairs);
		std::reverse(pairs.begin(), pairs.end());
		pairs.push_back(MovedLinePair{ .removed = r, .added = a });
		extend(r + 1, a + 1, +1, pairs);

		MovedBlock block;
		block.removedFirst = pairs.front().removed;
		block.removedCount = pairs.back().removed - block.removedFirst + 1;
		block.addedFirst = pairs.front().added;
		block.addedCount = pairs.back().added - block.addedFirst + 1;
		block.pairs = std::move(pairs);
		return block;
	}

	// The block's anchors: its exact pairs of lines with content
	struct Anchors
	{
		int lines = 0;
		int chars = 0;
	};

	[[nodiscard]] Anchors anchors(const MovedBlock& block) const
	{
		Anchors result;
		for (const MovedLinePair& pair : block.pairs)
		{
			if (pair.edited || _alnum[size_t(pair.removed)] == 0)
				continue;
			++result.lines;
			result.chars += _alnum[size_t(pair.removed)];
		}
		return result;
	}

	void take(int addedFirst, int addedCount)
	{
		std::fill_n(_taken.begin() + addedFirst, addedCount, true);
	}

private:
	[[nodiscard]] bool removedAt(int i) const { return i >= 0 && i < _count && _lines[size_t(i)].side == ChangeSide::Removed; }
	[[nodiscard]] bool addedAt(int i) const { return i >= 0 && i < _count && _lines[size_t(i)].side == ChangeSide::Added && !_taken[size_t(i)]; }

	// Pairs lines from (r, a) on in the direction of `step` until nothing pairs, pushed in the order walked.
	// In order of preference: an anchor, the nearest anchor within a gap on either side, a line alike enough
	// its counterpart.
	void extend(int r, int a, int step, std::vector<MovedLinePair>& pairs) const
	{
		while (removedAt(r) && addedAt(a))
		{
			if (_trimmed[size_t(r)] == _trimmed[size_t(a)])
			{
				pairs.push_back(MovedLinePair{ .removed = r, .added = a });
			}
			else if (const auto anchor = nextAnchor(r, a, step))
			{
				const auto [anchorRemoved, anchorAdded] = *anchor;
				pairGap(step > 0 ? r : anchorRemoved + 1, std::abs(anchorRemoved - r), step > 0 ? a : anchorAdded + 1, std::abs(anchorAdded - a), step, pairs);
				pairs.push_back(MovedLinePair{ .removed = anchorRemoved, .added = anchorAdded });
				r = anchorRemoved;
				a = anchorAdded;
			}
			else
			{
				TokenAlignment alignment = alignTokens(_texts[size_t(r)], _texts[size_t(a)]);
				if (alignment.similarity < MinEditedEndSimilarity)
					return;
				pairs.push_back(MovedLinePair{ .removed = r, .added = a, .edited = true, .alignment = std::move(alignment) });
			}
			r += step;
			a += step;
		}
	}

	// The nearest anchor past (r, a) in the direction of `step`: the same content line on both sides, each
	// within the gap of its side and the lines up to it still the block's kind
	[[nodiscard]] std::optional<std::pair<int, int>> nextAnchor(int r, int a, int step) const
	{
		int removedReach = 0, addedReach = 0;
		while (removedReach < MaxMovedBlockGap && removedAt(r + step * (removedReach + 1)))
			++removedReach;
		while (addedReach < MaxMovedBlockGap && addedAt(a + step * (addedReach + 1)))
			++addedReach;

		for (int distance = 1; distance <= removedReach + addedReach; ++distance)
		{
			for (int dr = std::max(0, distance - addedReach); dr <= std::min(distance, removedReach); ++dr)
			{
				const int rr = r + step * dr, aa = a + step * (distance - dr);
				if (_alnum[size_t(rr)] > 0 && _trimmed[size_t(rr)] == _trimmed[size_t(aa)])
					return std::pair{ rr, aa };
			}
		}
		return std::nullopt;
	}

	// The lines between two anchors, paired by similarity: an edited pair each, the rest dropped or put in.
	// Pushed in the order walked.
	void pairGap(int removedFirst, int removedCount, int addedFirst, int addedCount, int step, std::vector<MovedLinePair>& pairs) const
	{
		const std::span<const QStringView> texts{ _texts };
		std::vector<LinePair> gapPairs = pairSimilarLines(texts.subspan(size_t(removedFirst), size_t(removedCount)),
			texts.subspan(size_t(addedFirst), size_t(addedCount)));
		if (step < 0)
			std::reverse(gapPairs.begin(), gapPairs.end());
		for (LinePair& pair : gapPairs)
		{
			pairs.push_back(MovedLinePair{ .removed = removedFirst + pair.left, .added = addedFirst + pair.right,
				.edited = true, .alignment = std::move(pair.alignment) });
		}
	}

private:
	const std::vector<ChangeLine>& _lines;
	const int _count;
	std::vector<QStringView> _texts;
	std::vector<QStringView> _trimmed;
	std::vector<int> _alnum;
	std::unordered_map<QStringView, std::vector<int>> _removedByText; // each line's copies, ascending
	std::vector<bool> _taken;
};

} // namespace

std::vector<MovedBlock> detectMovedBlocks(const std::vector<ChangeLine>& lines)
{
	BlockGrower grower{ lines };
	const int count = int(lines.size());

	std::vector<MovedBlock> blocks;
	std::vector<MovedBlock> candidates;
	for (int a = 0; a < count; )
	{
		const std::vector<int>* copies = grower.seedCopies(a);
		if (!copies)
		{
			++a;
			continue;
		}

		// The block grown from every copy that is enough of one. The block with the most anchors is the move;
		// the others covering the same added lines are its copies, and one covering less matched the seed line
		// by chance.
		candidates.clear();
		size_t best = 0;
		int mostAnchors = 0;
		for (const int r : *copies)
		{
			MovedBlock block = grower.grow(r, a);
			const BlockGrower::Anchors anchors = grower.anchors(block);
			if (anchors.lines < MinMovedBlockContentLines || anchors.chars < MinMovedBlockAlnumChars)
				continue;
			if (anchors.lines > mostAnchors)
			{
				mostAnchors = anchors.lines;
				best = candidates.size();
			}
			candidates.push_back(std::move(block));
		}

		if (candidates.empty())
		{
			++a;
			continue;
		}

		const int addedFirst = candidates[best].addedFirst, addedCount = candidates[best].addedCount;
		std::erase_if(candidates, [&](const MovedBlock& block) { return block.addedFirst != addedFirst || block.addedCount != addedCount; });

		// Periodic content grows blocks from copies inside one another's block; only the first of such a set is a copy
		std::sort(candidates.begin(), candidates.end(), [](const MovedBlock& l, const MovedBlock& r) { return l.removedFirst < r.removedFirst; });
		int previousEnd = -1;
		for (MovedBlock& block : candidates)
		{
			if (block.removedFirst < previousEnd)
				continue;
			previousEnd = block.removedFirst + block.removedCount;
			blocks.push_back(std::move(block));
		}
		grower.take(addedFirst, addedCount);
		a = addedFirst + addedCount;
	}

	const auto overlap = [](int first1, int count1, int first2, int count2) { return first1 < first2 + count2 && first2 < first1 + count1; };
	BlockGroups groups{ blocks.size() };
	for (size_t i = 0; i < blocks.size(); ++i)
	{
		for (size_t j = 0; j < i; ++j)
		{
			if (overlap(blocks[j].removedFirst, blocks[j].removedCount, blocks[i].removedFirst, blocks[i].removedCount)
				|| overlap(blocks[j].addedFirst, blocks[j].addedCount, blocks[i].addedFirst, blocks[i].addedCount))
			{
				groups.unite(int(j), int(i));
			}
		}
	}

	std::vector<int> groupNumbers(blocks.size(), -1); // by root block, in order of first appearance
	int nextGroup = 0;
	for (size_t i = 0; i < blocks.size(); ++i)
	{
		int& number = groupNumbers[size_t(groups.find(int(i)))];
		if (number < 0)
			number = nextGroup++;
		blocks[i].group = number;
	}

	return blocks;
}
