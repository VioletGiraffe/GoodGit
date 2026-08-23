#pragma once

#include "vcstypes.h"

DISABLE_COMPILER_WARNINGS
#include <QMetaType>
RESTORE_COMPILER_WARNINGS

#include <vector>

// The lane diagram drawn beside a commit list. Built from the records' parent links alone, so one
// implementation serves every backend.
// Requires a topologically ordered listing (every commit above all its parents); the backends' walks guarantee it.
// A parent the listing does not hold (below the last row, or pruned by a path limit) leaves its lane running
// off the bottom edge.

// One line crossing one row. A lane number is a horizontal position; -1 means the row's own node, where a
// line begins or ends.
struct GraphSegment
{
	int fromLane = -1;   // where the line enters at the top edge
	int toLane = -1;     // where it leaves at the bottom edge
	int chain = 0;       // the line of history it belongs to, which decides its color
	bool elided = false; // the two ends are not adjacent commits
};

struct GraphRow
{
	int lane = 0;  // where the node sits
	int chain = 0; // the line of history the node is on
	std::vector<GraphSegment> segments;
};

struct CommitGraph
{
	std::vector<GraphRow> rows; // parallel to the commits it was built from
	int laneCount = 0;          // the widest row, so the column keeps one width down the whole list
};

// A chain is one first-parent path, so two commits sharing one are ancestor and descendant, and a chain
// holds a single lane for its whole length. filteredCommitGraph() relies on both.
[[nodiscard]] CommitGraph buildCommitGraph(const std::vector<CommitRecord>& commits);

// The same diagram over a subset of the commits (a search hiding the rest).
// Each node keeps its lane. Two rows on one chain are joined, marked elided where commits between them are hidden.
// Merge lines are dropped: their far end is an arbitrary distance away.
// `visible` holds ascending indexes into the commits the graph was built from.
[[nodiscard]] CommitGraph filteredCommitGraph(const CommitGraph& full, const std::vector<int>& visible);

Q_DECLARE_METATYPE(GraphRow)
