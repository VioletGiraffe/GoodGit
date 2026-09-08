#include "movedblocks.h"

DISABLE_COMPILER_WARNINGS
#include <QtCore/qhashfunctions.h> // std::hash<QStringView>
RESTORE_COMPILER_WARNINGS

#include <numeric>
#include <unordered_map>

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

} // namespace

std::vector<MovedBlock> detectMovedBlocks(const std::vector<ChangeLine>& lines)
{
	const int count = int(lines.size());

	std::vector<QStringView> trimmed(lines.size());
	std::vector<int> alnum(lines.size());
	std::unordered_map<QStringView, std::vector<int>> removedByText; // each line's copies, ascending
	for (int i = 0; i < count; ++i)
	{
		trimmed[size_t(i)] = lines[size_t(i)].text.trimmed();
		alnum[size_t(i)] = alnumCount(trimmed[size_t(i)]);
		if (lines[size_t(i)].side == ChangeSide::Removed)
			removedByText[trimmed[size_t(i)]].push_back(i);
	}

	// Whether the lines [first, first + length) are enough of a block to report
	const auto meetsMinimum = [&](int first, int length) {
		int contentLines = 0, chars = 0;
		for (int i = first; i < first + length; ++i)
		{
			if (alnum[size_t(i)] == 0)
				continue;
			++contentLines;
			chars += alnum[size_t(i)];
		}
		return contentLines >= MinMovedBlockContentLines && chars >= MinMovedBlockAlnumChars;
	};

	std::vector<MovedBlock> blocks;
	std::vector<int> removedStarts;
	for (int a = 0; a < count; )
	{
		if (lines[size_t(a)].side != ChangeSide::Added || alnum[size_t(a)] == 0)
		{
			++a;
			continue;
		}
		const auto copies = removedByText.find(trimmed[size_t(a)]);
		if (copies == removedByText.end() || copies->second.size() > size_t(MaxBlockStartCandidates))
		{
			++a;
			continue;
		}

		// The longest run matching from any copy; every copy reaching that length is a block of its own
		int bestLength = 0;
		removedStarts.clear();
		for (const int r : copies->second)
		{
			int length = 0;
			while (a + length < count && r + length < count
				&& lines[size_t(a + length)].side == ChangeSide::Added && lines[size_t(r + length)].side == ChangeSide::Removed
				&& trimmed[size_t(a + length)] == trimmed[size_t(r + length)])
			{
				++length;
			}

			if (length > bestLength)
			{
				bestLength = length;
				removedStarts.clear();
			}
			if (length == bestLength)
				removedStarts.push_back(r);
		}

		if (!meetsMinimum(a, bestLength))
		{
			++a;
			continue;
		}

		// Periodic content matches from starts inside one another's run; only the first of such a set is a copy
		int previousEnd = -1;
		for (const int r : removedStarts)
		{
			if (r < previousEnd)
				continue;
			blocks.push_back(MovedBlock{ .removedFirst = r, .addedFirst = a, .lineCount = bestLength });
			previousEnd = r + bestLength;
		}
		a += bestLength;
	}

	BlockGroups groups{ blocks.size() };
	for (size_t i = 0; i < blocks.size(); ++i)
	{
		for (size_t j = 0; j < i; ++j)
		{
			if (blocks[j].removedFirst == blocks[i].removedFirst || blocks[j].addedFirst == blocks[i].addedFirst)
				groups.unite(int(j), int(i));
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
