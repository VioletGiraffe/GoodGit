#pragma once

#include "vcstypes.h"

#include <QMetaType>

#include <vector>

// The lane diagram drawn beside a commit list. Built from the records' parent links and nothing else, so
// one implementation serves every backend.
//
// It relies on the listing being topologically ordered - every commit above all of its parents - which is
// what the backends' walks are asked to guarantee. A parent the listing does not hold, whether below the
// last row or pruned by a path limit, simply leaves its lane running off the bottom edge.

// One line crossing one row. A lane number is a horizontal position; -1 means the row's own node, where a
// line either begins or ends.
struct GraphSegment
{
	int fromLane = -1;   // where the line enters at the top edge
	int toLane = -1;     // where it leaves at the bottom edge
	int chain = 0;       // the line of history it belongs to, which is what colors it
	bool elided = false; // its two ends are not adjacent commits, so it stands for more than one step
};

// One row's slice of the diagram
struct GraphRow
{
	int lane = 0;  // where the node sits
	int chain = 0; // the line of history the node is on
	std::vector<GraphSegment> segments;
};

struct CommitGraph
{
	std::vector<GraphRow> rows; // parallel to the commits it was built from
	int laneCount = 0;          // enough for every row, so the column keeps one width down the whole list
};

// A chain is one first-parent path: two commits sharing one are therefore ancestor and descendant, and a
// chain holds a single lane for its whole length. Both are what filteredCommitGraph() draws from.
[[nodiscard]] CommitGraph buildCommitGraph(const std::vector<CommitRecord>& commits);

// The same diagram over a subset of those commits - a search hiding the rest. Only what stays true survives:
// each node keeps its lane, and two rows on one chain are joined, the join marked elided where commits
// between them are hidden. Merge lines are dropped, their far end being an arbitrary distance away.
// `visible` holds ascending indexes into the commits the graph was built from.
[[nodiscard]] CommitGraph filteredCommitGraph(const CommitGraph& full, const std::vector<int>& visible);

Q_DECLARE_METATYPE(GraphRow)
