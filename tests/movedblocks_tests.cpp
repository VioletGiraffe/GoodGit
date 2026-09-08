#include "compiler/compiler_warnings_control.h"

#define CATCH_CONFIG_MAIN // Catch provides main() in this one file
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
	CHECK(blocks[0].lineCount == 3);
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
	CHECK(blocks[0].lineCount == 2);
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
	CHECK(blocks[0].lineCount == 2);
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
	CHECK(blocks[0].lineCount == 5);
}

TEST_CASE("A block never starts on a line without content", "[movedblocks]")
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
	CHECK(blocks[0].removedFirst == 1);
	CHECK(blocks[0].addedFirst == 5);
	CHECK(blocks[0].lineCount == 2);
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
	CHECK(blocks[0].lineCount == 2); // "return false;" alone is below the minimum
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
	CHECK(blocks[0].lineCount == 4);
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
	CHECK(blocks[0].lineCount == 3);
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
