#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include "movedblocks.h"

namespace {

// "-text" is a removed line, "+text" an added one, anything else neither. Views into `marked`, which must outlive them.
std::vector<ChangeLine> changeLines(const QStringList& marked)
{
	std::vector<ChangeLine> lines;
	for (const QString& line : marked)
	{
		ChangeLine change;
		if (line.startsWith(QLatin1Char('-')))
			change = { QStringView{ line }.sliced(1), ChangeSide::Removed };
		else if (line.startsWith(QLatin1Char('+')))
			change = { QStringView{ line }.sliced(1), ChangeSide::Added };
		lines.push_back(change);
	}
	return lines;
}

} // namespace

TEST_CASE("A block removed in one place and added in another is one move", "[movedblocks]")
{
	const QStringList diff = {
		" context",
		"-int total = computeTotal();",
		"-if (total > limit)",
		"-	return false;",
		" context",
		" context",
		"+int total = computeTotal();",
		"+if (total > limit)",
		"+	return false;",
		" context",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 1);
	CHECK(blocks[0].removedFirst == 1);
	CHECK(blocks[0].addedFirst == 6);
	CHECK(blocks[0].removedCount == 3);
	CHECK(blocks[0].addedCount == 3);
	CHECK(blocks[0].group == 0);
}

TEST_CASE("A block moved upward is found from its added side", "[movedblocks]")
{
	const QStringList diff = {
		"+int total = computeTotal();",
		"+if (total > limit)",
		" context",
		"-int total = computeTotal();",
		"-if (total > limit)",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 1);
	CHECK(blocks[0].removedFirst == 3);
	CHECK(blocks[0].addedFirst == 0);
	CHECK(blocks[0].removedCount == 2);
	CHECK(blocks[0].addedCount == 2);
}

TEST_CASE("Whitespace at either end of a line counts for nothing", "[movedblocks]")
{
	const QStringList diff = {
		"-int total = computeTotal();  ",
		"-if (total > limit)",
		" context",
		"+		int total = computeTotal();",
		"+		if (total > limit)\t",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 1);
	CHECK(blocks[0].removedCount == 2);
	CHECK(blocks[0].addedCount == 2);
}

TEST_CASE("Whitespace inside a line still counts", "[movedblocks]")
{
	const QStringList diff = {
		"-int total = computeTotal();",
		"-if (total > limit)",
		" context",
		"+int total =  computeTotal();",
		"+if (total > limit)",
	};
	CHECK(detectMovedBlocks(changeLines(diff)).empty());
}

TEST_CASE("A block needs enough lines with something in them", "[movedblocks]")
{
	SECTION("one content line is not a block, however long")
	{
		const QStringList diff = {
			"-const auto value = computeTheAnswerToEverything(input);",
			" context",
			"+const auto value = computeTheAnswerToEverything(input);",
		};
		CHECK(detectMovedBlocks(changeLines(diff)).empty());
	}

	SECTION("two content lines one character short of the minimum are not a block")
	{
		const QStringList diff = {
			"-int abc = 12;", // 8 alphanumerics
			"-int ab = 3;",   // 14 in all
			" context",
			"+int abc = 12;",
			"+int ab = 3;",
		};
		CHECK(detectMovedBlocks(changeLines(diff)).empty());
	}

	SECTION("two content lines reaching the minimum are")
	{
		const QStringList diff = {
			"-int abc = 12;", // 8 alphanumerics
			"-int abc = 3;",  // 15 in all
			" context",
			"+int abc = 12;",
			"+int abc = 3;",
		};
		REQUIRE(detectMovedBlocks(changeLines(diff)).size() == 1);
	}

	SECTION("closing braces alone never make a block")
	{
		const QStringList diff = {
			"-	}",
			"-}",
			"-",
			" context",
			"+	}",
			"+}",
			"+",
		};
		CHECK(detectMovedBlocks(changeLines(diff)).empty());
	}
}

TEST_CASE("Lines without content sit inside a block without counting", "[movedblocks]")
{
	const QStringList diff = {
		"-int total = computeTotal();",
		"-",
		"-{",
		"-if (total > limit)",
		"-}",
		" context",
		"+int total = computeTotal();",
		"+",
		"+{",
		"+if (total > limit)",
		"+}",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 1);
	CHECK(blocks[0].removedCount == 5);
	CHECK(blocks[0].addedCount == 5);
}

TEST_CASE("A line without content joins a block at either end without seeding one", "[movedblocks]")
{
	const QStringList diff = {
		"-}",
		"-int total = computeTotal();",
		"-if (total > limit)",
		" context",
		"+}",
		"+int total = computeTotal();",
		"+if (total > limit)",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 1);
	CHECK(blocks[0].removedFirst == 0);
	CHECK(blocks[0].addedFirst == 4);
	CHECK(blocks[0].removedCount == 3);
	CHECK(blocks[0].addedCount == 3);
	CHECK(blocks[0].pairs.size() == 3);
}

TEST_CASE("Anything but a removed line ends the removed side of a block", "[movedblocks]")
{
	const QStringList diff = {
		"-int total = computeTotal();",
		"-if (total > limit)",
		" context",
		"-return false;",
		" context",
		"+int total = computeTotal();",
		"+if (total > limit)",
		"+return false;",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 1);
	CHECK(blocks[0].removedCount == 2);
	CHECK(blocks[0].addedCount == 2); // "return false;" alone is below the minimum
}

TEST_CASE("Independent moves are separate groups, in order of their added side", "[movedblocks]")
{
	const QStringList diff = {
		"-int total = computeTotal();",
		"-if (total > limit)",
		" context",
		"-const auto name = readName();",
		"-names.push_back(name);",
		" context",
		"+const auto name = readName();",
		"+names.push_back(name);",
		" context",
		"+int total = computeTotal();",
		"+if (total > limit)",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 2);
	CHECK(blocks[0].removedFirst == 3);
	CHECK(blocks[0].addedFirst == 6);
	CHECK(blocks[0].group == 0);
	CHECK(blocks[1].removedFirst == 0);
	CHECK(blocks[1].addedFirst == 9);
	CHECK(blocks[1].group == 1);
}

TEST_CASE("A block added in several places is a block per copy, in one group", "[movedblocks]")
{
	const QStringList diff = {
		"-int total = computeTotal();",
		"-if (total > limit)",
		" context",
		"+int total = computeTotal();",
		"+if (total > limit)",
		" context",
		"+int total = computeTotal();",
		"+if (total > limit)",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 2);
	CHECK(blocks[0].removedFirst == 0);
	CHECK(blocks[0].addedFirst == 3);
	CHECK(blocks[1].removedFirst == 0);
	CHECK(blocks[1].addedFirst == 6);
	CHECK(blocks[0].group == 0);
	CHECK(blocks[1].group == 0);
}

TEST_CASE("Several removed copies collapsed into one are a block per copy, in one group", "[movedblocks]")
{
	const QStringList diff = {
		"-int total = computeTotal();",
		"-if (total > limit)",
		" context",
		"-int total = computeTotal();",
		"-if (total > limit)",
		" context",
		"+int total = computeTotal();",
		"+if (total > limit)",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 2);
	CHECK(blocks[0].removedFirst == 0);
	CHECK(blocks[1].removedFirst == 3);
	CHECK(blocks[0].addedFirst == 6);
	CHECK(blocks[1].addedFirst == 6);
	CHECK(blocks[0].group == 0);
	CHECK(blocks[1].group == 0);
}

TEST_CASE("Periodic content is matched from the first of its overlapping starts alone", "[movedblocks]")
{
	const QStringList diff = {
		"-first line of pattern",
		"-second line of pattern",
		"-first line of pattern",
		"-second line of pattern",
		"-first line of pattern",
		"-second line of pattern",
		" context",
		"+first line of pattern",
		"+second line of pattern",
		"+first line of pattern",
		"+second line of pattern",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 1);
	CHECK(blocks[0].removedFirst == 0);
	CHECK(blocks[0].addedFirst == 7);
	CHECK(blocks[0].removedCount == 4);
	CHECK(blocks[0].addedCount == 4);
}

TEST_CASE("The longest matching removed run wins over a shorter one", "[movedblocks]")
{
	const QStringList diff = {
		"-int total = computeTotal();",
		"-if (total > limit)",
		" context",
		"-int total = computeTotal();",
		"-if (total > limit)",
		"-	return false;",
		" context",
		"+int total = computeTotal();",
		"+if (total > limit)",
		"+	return false;",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 1);
	CHECK(blocks[0].removedFirst == 3);
	CHECK(blocks[0].removedCount == 3);
	CHECK(blocks[0].addedCount == 3);
}

TEST_CASE("Nothing to find", "[movedblocks]")
{
	CHECK(detectMovedBlocks({}).empty());

	const QStringList unrelated = {
		"-int total = computeTotal();",
		"-if (total > limit)",
		"+const auto name = readName();",
		"+names.push_back(name);",
	};
	CHECK(detectMovedBlocks(changeLines(unrelated)).empty());
}

TEST_CASE("A line edited on the way sits inside the block as an edited pair", "[movedblocks]")
{
	const QStringList diff = {
		"-int total = computeTotal(items);",
		"-if (total > limit)",
		"-	return false;",
		" context",
		"+int total = computeTotal(items);",
		"+if (total >= limit)",
		"+	return false;",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 1);
	CHECK(blocks[0].removedCount == 3);
	CHECK(blocks[0].addedCount == 3);
	REQUIRE(blocks[0].pairs.size() == 3);
	CHECK(!blocks[0].pairs[0].edited);
	CHECK(blocks[0].pairs[1].edited);
	CHECK(blocks[0].pairs[1].removed == 1);
	CHECK(blocks[0].pairs[1].added == 5);
	CHECK(!blocks[0].pairs[1].alignment.segments.empty());
	CHECK(!blocks[0].pairs[2].edited);
}

TEST_CASE("Lines edited at either end join the block", "[movedblocks]")
{
	const QStringList diff = {
		"-int total = computeTotal(items);",
		"-if (total > limit)",
		"-	return false;",
		"-names.push_back(name);",
		" context",
		"+int total = computeTotal(entries);",
		"+if (total > limit)",
		"+	return false;",
		"+names.push_back(newName);",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 1);
	CHECK(blocks[0].removedFirst == 0);
	CHECK(blocks[0].addedFirst == 5);
	CHECK(blocks[0].removedCount == 4);
	CHECK(blocks[0].addedCount == 4);
	REQUIRE(blocks[0].pairs.size() == 4);
	CHECK(blocks[0].pairs[0].edited);
	CHECK(!blocks[0].pairs[1].edited);
	CHECK(!blocks[0].pairs[2].edited);
	CHECK(blocks[0].pairs[3].edited);
}

TEST_CASE("A line too unlike its counterpart stays outside the block", "[movedblocks]")
{
	const QStringList diff = {
		"-int total = computeTotal(items);",
		"-if (total > limit)",
		"-	return false;",
		"-unrelated trailing statement here;",
		" context",
		"+int total = computeTotal(items);",
		"+if (total > limit)",
		"+	return false;",
		"+something else entirely different;",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 1);
	CHECK(blocks[0].removedCount == 3);
	CHECK(blocks[0].addedCount == 3);
	CHECK(blocks[0].pairs.size() == 3);
}

TEST_CASE("Lines put into a block or dropped from it sit inside it unpaired", "[movedblocks]")
{
	SECTION("put in")
	{
		const QStringList diff = {
			"-int total = computeTotal(items);",
			"-if (total > limit)",
			"-	return false;",
			" context",
			"+int total = computeTotal(items);",
			"+logCall(total);",
			"+if (total > limit)",
			"+	return false;",
		};
		const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

		REQUIRE(blocks.size() == 1);
		CHECK(blocks[0].removedCount == 3);
		CHECK(blocks[0].addedCount == 4);
		REQUIRE(blocks[0].pairs.size() == 3);
		CHECK(!blocks[0].pairs[1].edited);
		CHECK(blocks[0].pairs[1].removed == 1);
		CHECK(blocks[0].pairs[1].added == 6);
	}

	SECTION("dropped")
	{
		const QStringList diff = {
			"-int total = computeTotal(items);",
			"-logCall(total);",
			"-if (total > limit)",
			"-	return false;",
			" context",
			"+int total = computeTotal(items);",
			"+if (total > limit)",
			"+	return false;",
		};
		const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

		REQUIRE(blocks.size() == 1);
		CHECK(blocks[0].removedCount == 4);
		CHECK(blocks[0].addedCount == 3);
		REQUIRE(blocks[0].pairs.size() == 3);
		CHECK(blocks[0].pairs[1].removed == 2);
		CHECK(blocks[0].pairs[1].added == 6);
	}
}

TEST_CASE("A gap wider than the allowance ends the block, and the rest is a block of its own", "[movedblocks]")
{
	QStringList diff = {
		"-int total = computeTotal(items);",
		"-if (total > limit)",
		"-	return computeFallback(items);",
		"-names.push_back(name);",
		" context",
		"+int total = computeTotal(items);",
		"+if (total > limit)",
	};
	const QStringList tail = {
		"+	return computeFallback(items);",
		"+names.push_back(name);",
	};

	SECTION("as many lines put in as allowed")
	{
		for (int i = 0; i < MaxMovedBlockGap; ++i)
			diff.push_back(QStringLiteral("+// filler line number %1").arg(i));
		diff.append(tail);
		const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

		REQUIRE(blocks.size() == 1);
		CHECK(blocks[0].removedCount == 4);
		CHECK(blocks[0].addedCount == 4 + MaxMovedBlockGap);
		CHECK(blocks[0].pairs.size() == 4);
	}

	SECTION("one more")
	{
		for (int i = 0; i <= MaxMovedBlockGap; ++i)
			diff.push_back(QStringLiteral("+// filler line number %1").arg(i));
		diff.append(tail);
		const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

		REQUIRE(blocks.size() == 2);
		CHECK(blocks[0].removedFirst == 0);
		CHECK(blocks[0].removedCount == 2);
		CHECK(blocks[0].addedCount == 2);
		CHECK(blocks[1].removedFirst == 2);
		CHECK(blocks[1].removedCount == 2);
		CHECK(blocks[1].addedFirst == 7 + MaxMovedBlockGap + 1);
		CHECK(blocks[1].addedCount == 2);
		CHECK(blocks[0].group != blocks[1].group);
	}
}

TEST_CASE("A gap never resumes on a line without content", "[movedblocks]")
{
	const QStringList diff = {
		"-int total = computeTotal(items);",
		"-if (total > limit)",
		"-}",
		"-names.push_back(name);",
		" context",
		"+int total = computeTotal(items);",
		"+if (total > limit)",
		"+other statement one;",
		"+other statement two;",
		"+}",
		"+colors.push_back(color);",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 1);
	CHECK(blocks[0].removedCount == 2);
	CHECK(blocks[0].addedCount == 2);
}

TEST_CASE("Only anchors count toward the minimum", "[movedblocks]")
{
	const QStringList diff = {
		"-int total = computeTotal(items);",
		"-if (total > limit)",
		" context",
		"+int total = computeTotal(items);",
		"+if (total >= limit)",
	};
	CHECK(detectMovedBlocks(changeLines(diff)).empty());
}

TEST_CASE("A block copied to several places, one of them edited, is one group", "[movedblocks]")
{
	const QStringList diff = {
		"-int total = computeTotal(items);",
		"-if (total > limit)",
		"-	return false;",
		" context",
		"+int total = computeTotal(items);",
		"+if (total > limit)",
		"+	return false;",
		" context",
		"+int total = computeTotal(items);",
		"+if (total >= limit)",
		"+	return false;",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 2);
	CHECK(blocks[0].addedFirst == 4);
	CHECK(blocks[1].addedFirst == 8);
	CHECK(blocks[0].group == blocks[1].group);
	REQUIRE(blocks[1].pairs.size() == 3);
	CHECK(!blocks[0].pairs[1].edited);
	CHECK(blocks[1].pairs[1].edited);
}

TEST_CASE("Several removed copies collapsed into one, one of them edited, are all copies", "[movedblocks]")
{
	const QStringList diff = {
		"-int total = computeTotal(items);",
		"-if (total >= limit)",
		"-	return false;",
		" context",
		"-int total = computeTotal(items);",
		"-if (total > limit)",
		"-	return false;",
		" context",
		"+int total = computeTotal(items);",
		"+if (total > limit)",
		"+	return false;",
	};
	const std::vector<MovedBlock> blocks = detectMovedBlocks(changeLines(diff));

	REQUIRE(blocks.size() == 2);
	CHECK(blocks[0].removedFirst == 0);
	CHECK(blocks[1].removedFirst == 4);
	CHECK(blocks[0].addedFirst == 8);
	CHECK(blocks[1].addedFirst == 8);
	CHECK(blocks[0].group == blocks[1].group);
	REQUIRE(blocks[0].pairs.size() == 3);
	CHECK(blocks[0].pairs[1].edited);
	CHECK(!blocks[1].pairs[1].edited);
}
