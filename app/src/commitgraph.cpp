#include "commitgraph.h"

DISABLE_COMPILER_WARNINGS
#include <QHash>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {

// A lane is occupied from the row that reserves it down to the row holding the commit it waits for
struct Lane
{
	QString waitingFor;
	int chain = 0;
	bool ahead = false; // every commit whose edge runs down this lane is above the current one
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

// Marks every commit with a path of child links down to `currentSha`, which is not one of them.
// Relies on the topological order the diagram needs: a commit's parents sit below it, so one pass upwards
// decides each row from rows already decided. A path between two listed commits is listed whole, the cap
// cutting the bottom of the walk rather than the middle.
std::vector<bool> commitsAboveCurrent(const std::vector<CommitRecord>& commits, const QString& currentSha)
{
	std::vector<bool> ahead(commits.size(), false);
	if (currentSha.isEmpty())
		return ahead;

	QHash<QString, size_t> rowOfSha;
	rowOfSha.reserve(qsizetype(commits.size()));
	for (size_t i = 0; i < commits.size(); ++i)
		rowOfSha.insert(commits[i].sha, i);

	for (size_t i = commits.size(); i-- > 0;)
	{
		if (commits[i].sha == currentSha)
			continue;

		for (const QString& parent : commits[i].parents)
		{
			const auto parentRow = rowOfSha.constFind(parent);
			if (parent == currentSha || (parentRow != rowOfSha.constEnd() && ahead[*parentRow]))
			{
				ahead[i] = true;
				break;
			}
		}
	}
	return ahead;
}

} // namespace

CommitGraph buildCommitGraph(const std::vector<CommitRecord>& commits, const QString& currentSha)
{
	CommitGraph graph;
	graph.rows.resize(commits.size());
	const std::vector<bool> ahead = commitsAboveCurrent(commits, currentSha);

	std::vector<Lane> lanes;
	std::vector<QString> shasWaitedForAbove; // the lanes as the row found them
	int nextChain = 0;

	for (size_t i = 0; i < commits.size(); ++i)
	{
		const CommitRecord& commit = commits[i];
		GraphRow& row = graph.rows[i];
		row.current = !currentSha.isEmpty() && commit.sha == currentSha;
		row.ahead = ahead[i];

		shasWaitedForAbove.clear();
		for (const Lane& lane : lanes)
			shasWaitedForAbove.push_back(lane.waitingFor);

		// The node sits in the lane reserved for this commit, and the line reserving it ends here.
		// At most one lane is reserved: the parents pass below joins a lane already waiting for a parent.
		int nodeLane = laneWaitingFor(lanes, commit.sha);
		if (nodeLane >= 0)
		{
			row.chain = lanes[size_t(nodeLane)].chain;
			row.segments.push_back({ .fromLane = nodeLane, .chain = row.chain, .ahead = lanes[size_t(nodeLane)].ahead });
			lanes[size_t(nodeLane)] = {};
		}
		else
		{
			// The newest row, or a commit whose children the listing does not reach
			nodeLane = leftmostFreeLane(lanes);
			row.chain = nextChain++;
		}
		row.lane = nodeLane;

		// The first parent continues the node's line in the node's lane; the others start lines or join one
		// already running (a merge).
		// A parent two lines both reach belongs to the leftmost, lane and chain alike: a trunk keeps its lane
		// and color past a branch point.
		// A lane two lines reach is ahead only where both are: the checkout reaches whatever its own line does.
		for (qsizetype p = 0; p < commit.parents.size(); ++p)
		{
			const QString& parent = commit.parents[p];
			int target = laneWaitingFor(lanes, parent);
			if (target < 0)
			{
				target = p == 0 ? nodeLane : leftmostFreeLane(lanes);
				lanes[size_t(target)] = { parent, p == 0 ? row.chain : nextChain++, row.ahead };
			}
			else if (p == 0 && nodeLane < target)
			{
				// The line already waiting for it gives it up and slants across to the node's lane
				const Lane& given = lanes[size_t(target)];
				row.segments.push_back({ .fromLane = target, .toLane = nodeLane, .chain = given.chain, .ahead = given.ahead });
				const bool aheadBelow = given.ahead && row.ahead;
				lanes[size_t(target)] = {};
				target = nodeLane;
				lanes[size_t(target)] = { parent, row.chain, aheadBelow };
			}
			else
				lanes[size_t(target)].ahead = lanes[size_t(target)].ahead && row.ahead;

			// This row's own edge, whatever the lane below it carries
			row.segments.push_back({ .toLane = target, .chain = lanes[size_t(target)].chain, .ahead = row.ahead });
		}

		// A lane still waiting for the same commit as above this row was untouched by it and draws straight
		// through. Every other occupied lane already has its segment from the two passes above.
		for (int l = 0; l < int(lanes.size()); ++l)
		{
			const QString& waitingFor = lanes[size_t(l)].waitingFor;
			if (!waitingFor.isEmpty() && l < int(shasWaitedForAbove.size()) && shasWaitedForAbove[size_t(l)] == waitingFor)
				row.segments.push_back({ .fromLane = l, .toLane = l, .chain = lanes[size_t(l)].chain, .ahead = lanes[size_t(l)].ahead });
		}
	}

	graph.laneCount = int(lanes.size()); // lanes are freed in place and never removed, so this is the widest row
	return graph;
}

CommitGraph filteredCommitGraph(const CommitGraph& full, const std::vector<int>& visible)
{
	CommitGraph graph;
	graph.rows.resize(visible.size());
	graph.laneCount = full.laneCount; // the unfiltered width, so typing does not resize the column

	for (size_t r = 0; r < visible.size(); ++r)
	{
		const GraphRow& source = full.rows[size_t(visible[r])];
		GraphRow& row = graph.rows[r];
		row.lane = source.lane;
		row.chain = source.chain;
		row.current = source.current;
		row.ahead = source.ahead;

		// One chain is one lane, so a join is a straight vertical line.
		// Each half is above the current commit where the history it descends from is: the half above the
		// node also where the node is the current commit itself.
		if (r > 0 && full.rows[size_t(visible[r - 1])].chain == row.chain)
		{
			row.segments.push_back({ .fromLane = row.lane, .chain = row.chain,
				.elided = visible[r - 1] != visible[r] - 1, .ahead = row.ahead || row.current });
		}
		if (r + 1 < visible.size() && full.rows[size_t(visible[r + 1])].chain == row.chain)
		{
			row.segments.push_back({ .toLane = row.lane, .chain = row.chain,
				.elided = visible[r + 1] != visible[r] + 1, .ahead = row.ahead });
		}
	}
	return graph;
}
