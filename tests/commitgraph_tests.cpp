#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "commitgraph.h"

#include <algorithm>

namespace {

[[nodiscard]] CommitRecord commit(const char* sha, QStringList parentShas = {})
{
	return { .sha = QString::fromLatin1(sha), .parents = std::move(parentShas) };
}

[[nodiscard]] QStringList parents(std::initializer_list<const char*> shas)
{
	QStringList list;
	for (const char* sha : shas)
		list << QString::fromLatin1(sha);
	return list;
}

// Newest first: a merge of a side branch, both branched from one base
[[nodiscard]] std::vector<CommitRecord> mergeHistory()
{
	return { commit("merge", parents({ "trunk", "side" })), commit("side", parents({ "base" })), commit("trunk", parents({ "base" })), commit("base") };
}

[[nodiscard]] qsizetype ownEdgeCount(const GraphRow& row)
{
	return std::ranges::count_if(row.segments, [](const GraphSegment& segment) { return segment.fromLane < 0; });
}

} // namespace

TEST_CASE("Linear history runs down one lane on one chain", "[commitgraph]")
{
	const CommitGraph graph = buildCommitGraph({ commit("c", parents({ "b" })), commit("b", parents({ "a" })), commit("a") }, {});

	CHECK(graph.laneCount == 1);
	for (const GraphRow& row : graph.rows)
	{
		CHECK(row.lane == 0);
		CHECK(row.chain == graph.rows[0].chain);
	}
}

TEST_CASE("A merge: the second parent opens a lane on a chain of its own, and the trunk keeps its lane past the branch point", "[commitgraph]")
{
	const CommitGraph graph = buildCommitGraph(mergeHistory(), {});
	const GraphRow& merge = graph.rows[0];
	const GraphRow& side = graph.rows[1];
	const GraphRow& trunk = graph.rows[2];
	const GraphRow& base = graph.rows[3];

	CHECK(graph.laneCount == 2);
	CHECK(ownEdgeCount(merge) == 2);
	CHECK(side.lane == 1);
	CHECK(side.chain != merge.chain);
	CHECK(trunk.lane == 0);
	CHECK(trunk.chain == merge.chain);
	CHECK(base.lane == 0);
	CHECK(base.chain == merge.chain);
	// The side line gives up its wait for the base and slants over to the trunk's lane
	CHECK(std::ranges::any_of(trunk.segments, [](const GraphSegment& segment) { return segment.fromLane == 1 && segment.toLane == 0; }));
}

TEST_CASE("The current commit is marked, and only the commits descending from it are ahead", "[commitgraph]")
{
	SECTION("on the side branch")
	{
		const CommitGraph graph = buildCommitGraph(mergeHistory(), QStringLiteral("side"));

		CHECK(graph.rows[1].current);
		CHECK(graph.rows[0].ahead);
		CHECK_FALSE(graph.rows[1].ahead);
		CHECK_FALSE(graph.rows[2].ahead);
		CHECK_FALSE(graph.rows[3].ahead);
	}

	SECTION("a commit the listing does not hold")
	{
		const CommitGraph graph = buildCommitGraph(mergeHistory(), QStringLiteral("elsewhere"));
		CHECK(std::ranges::none_of(graph.rows, [](const GraphRow& row) { return row.current || row.ahead; }));
	}
}

TEST_CASE("A filtered graph joins the rows of one chain, marked elided across hidden commits", "[commitgraph]")
{
	const CommitGraph full = buildCommitGraph({ commit("c", parents({ "b" })), commit("b", parents({ "a" })), commit("a") }, {});

	SECTION("a hidden commit between")
	{
		const CommitGraph filtered = filteredCommitGraph(full, { 0, 2 });

		REQUIRE(filtered.rows.size() == 2);
		REQUIRE(filtered.rows[0].segments.size() == 1);
		REQUIRE(filtered.rows[1].segments.size() == 1);
		CHECK(filtered.rows[0].segments[0].elided);
		CHECK(filtered.rows[1].segments[0].elided);
	}

	SECTION("adjacent commits")
	{
		const CommitGraph filtered = filteredCommitGraph(full, { 0, 1 });

		REQUIRE(filtered.rows[0].segments.size() == 1);
		CHECK_FALSE(filtered.rows[0].segments[0].elided);
	}
}

TEST_CASE("A filtered graph drops merge lines and keeps the full width", "[commitgraph]")
{
	const CommitGraph full = buildCommitGraph(mergeHistory(), {});
	const CommitGraph filtered = filteredCommitGraph(full, { 0, 1 }); // the merge and the side commit, on two chains

	CHECK(filtered.laneCount == full.laneCount);
	CHECK(filtered.rows[0].segments.empty());
	CHECK(filtered.rows[1].segments.empty());
	CHECK(filtered.rows[1].lane == full.rows[1].lane);
}
