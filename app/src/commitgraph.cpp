#include "commitgraph.h"

#include <algorithm>

namespace {

// A lane is occupied from the row that reserves it down to the row holding the commit it waits for
struct Lane
{
	QString waitingFor;
	int chain = 0;
};

int leftmostFreeLane(std::vector<Lane>& lanes)
{
	const auto free = std::ranges::find_if(lanes, [](const Lane& lane) { return lane.waitingFor.isEmpty(); });
	if (free != lanes.end())
		return int(free - lanes.begin());

	lanes.emplace_back();
	return int(lanes.size()) - 1;
}

int laneWaitingFor(const std::vector<Lane>& lanes, const QString& sha)
{
	const auto found = std::ranges::find(lanes, sha, &Lane::waitingFor);
	return found == lanes.end() ? -1 : int(found - lanes.begin());
}

} // namespace

CommitGraph buildCommitGraph(const std::vector<CommitRecord>& commits)
{
	CommitGraph graph;
	graph.rows.resize(commits.size());

	std::vector<Lane> lanes;
	std::vector<QString> shasWaitedForAbove; // the lanes as the row found them, reused row to row
	int nextChain = 0;

	for (size_t i = 0; i < commits.size(); ++i)
	{
		const CommitRecord& commit = commits[i];
		GraphRow& row = graph.rows[i];

		shasWaitedForAbove.clear();
		for (const Lane& lane : lanes)
			shasWaitedForAbove.push_back(lane.waitingFor);

		// The lane reserved for this commit is the one its node sits in, and the line it continues ends here.
		// Only ever one lane: the parents pass below joins the lane already holding a parent rather than
		// reserving a second for it.
		int nodeLane = laneWaitingFor(lanes, commit.sha);
		if (nodeLane >= 0)
		{
			row.chain = lanes[size_t(nodeLane)].chain;
			row.segments.push_back({ .fromLane = nodeLane, .chain = row.chain });
			lanes[size_t(nodeLane)] = {};
		}
		else
		{
			// Nothing reserved one: the newest row, or a commit whose children the listing does not reach
			nodeLane = leftmostFreeLane(lanes);
			row.chain = nextChain++;
		}
		row.lane = nodeLane;

		// The first parent carries the node's own line onwards, in the node's lane; the others start lines of
		// their own or rejoin one already running, which is what draws a merge. A parent two lines both reach
		// belongs to the leftmost of them, lane and chain alike - the same tie-break the node's own lane is
		// picked by, and what keeps a trunk on its lane and in its color past a branch point.
		for (qsizetype p = 0; p < commit.parents.size(); ++p)
		{
			const QString& parent = commit.parents[p];
			int target = laneWaitingFor(lanes, parent);
			if (target < 0)
			{
				target = p == 0 ? nodeLane : leftmostFreeLane(lanes);
				lanes[size_t(target)] = { parent, p == 0 ? row.chain : nextChain++ };
			}
			else if (p == 0 && nodeLane < target)
			{
				// The line already holding it gives it up and slants across to the node's lane
				row.segments.push_back({ .fromLane = target, .toLane = nodeLane, .chain = lanes[size_t(target)].chain });
				lanes[size_t(target)] = {};
				target = nodeLane;
				lanes[size_t(target)] = { parent, row.chain };
			}
			row.segments.push_back({ .toLane = target, .chain = lanes[size_t(target)].chain });
		}

		// A lane still waiting for what it waited for above this row was untouched by it, and draws straight
		// through. Every other occupied lane already has its segment from the two passes above.
		for (int l = 0; l < int(lanes.size()); ++l)
		{
			const QString& waitingFor = lanes[size_t(l)].waitingFor;
			if (!waitingFor.isEmpty() && l < int(shasWaitedForAbove.size()) && shasWaitedForAbove[size_t(l)] == waitingFor)
				row.segments.push_back({ .fromLane = l, .toLane = l, .chain = lanes[size_t(l)].chain });
		}
	}

	graph.laneCount = int(lanes.size()); // lanes are freed in place and never removed, so this is the widest row
	return graph;
}

CommitGraph filteredCommitGraph(const CommitGraph& full, const std::vector<int>& visible)
{
	CommitGraph graph;
	graph.rows.resize(visible.size());
	graph.laneCount = full.laneCount; // the width the same list has unfiltered, so typing does not resize it

	for (size_t r = 0; r < visible.size(); ++r)
	{
		const GraphRow& source = full.rows[size_t(visible[r])];
		GraphRow& row = graph.rows[r];
		row.lane = source.lane;
		row.chain = source.chain;

		// One chain is one lane, so a join is the straight line between two nodes at the same position
		if (r > 0 && full.rows[size_t(visible[r - 1])].chain == row.chain)
			row.segments.push_back({ .fromLane = row.lane, .chain = row.chain, .elided = visible[r - 1] != visible[r] - 1 });
		if (r + 1 < visible.size() && full.rows[size_t(visible[r + 1])].chain == row.chain)
			row.segments.push_back({ .toLane = row.lane, .chain = row.chain, .elided = visible[r + 1] != visible[r] + 1 });
	}
	return graph;
}
