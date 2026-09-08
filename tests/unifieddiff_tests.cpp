#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
#include <QFile>
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include "unifieddiff.h"

namespace {

QString fixture(const char* name)
{
	QFile file{ QString::fromLatin1(FIXTURES_DIR) + QLatin1String(name) };
	REQUIRE(file.open(QIODevice::ReadOnly));
	return QString::fromUtf8(file.readAll());
}

QString joined(const QStringList& lines)
{
	return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

int countOfKind(const ParsedDiff& parsed, DiffLineKind kind)
{
	int count = 0;
	for (const DiffLine& line : parsed.lines)
	{
		if (line.kind == kind)
			++count;
	}
	return count;
}

} // namespace

TEST_CASE("Lines are classified and numbered against both files", "[unifieddiff]")
{
	const ParsedDiff parsed = parseUnifiedDiff(joined({
		"diff --git a/f b/f",
		"--- a/f",
		"+++ b/f",
		"@@ -10,3 +20,3 @@",
		" context",
		"-completely different removed text",
		"+xyz",
		" context",
	}));

	REQUIRE(parsed.lines.size() == 8);
	CHECK(parsed.lines[0].kind == DiffLineKind::FileHeader);
	CHECK(parsed.lines[3].kind == DiffLineKind::HunkHeader);
	CHECK(parsed.lines[4].kind == DiffLineKind::Context);
	CHECK(parsed.lines[4].oldLine == 10);
	CHECK(parsed.lines[4].newLine == 20);
	CHECK(parsed.lines[5].kind == DiffLineKind::Removed); // too unalike to be one edit
	CHECK(parsed.lines[5].oldLine == 11);
	CHECK(parsed.lines[5].newLine == 0);
	CHECK(parsed.lines[6].kind == DiffLineKind::Added);
	CHECK(parsed.lines[6].oldLine == 0);
	CHECK(parsed.lines[6].newLine == 21);
	CHECK(parsed.lines[7].oldLine == 12);
	CHECK(parsed.lines[7].newLine == 22);
	CHECK(parsed.spans.empty());
	CHECK(parsed.moves.empty());
}

TEST_CASE("One edit becomes one line carrying both sides", "[unifieddiff]")
{
	const ParsedDiff parsed = parseUnifiedDiff(joined({
		"@@ -10,3 +10,3 @@",
		" context",
		"-old line to edit here",
		"+new line to edit here",
		" context",
	}));
	const QStringList shown = parsed.text.split(QLatin1Char('\n'));

	REQUIRE(parsed.lines.size() == 4);
	CHECK(parsed.lines[2].kind == DiffLineKind::Edited);
	CHECK(parsed.lines[2].oldLine == 11);
	CHECK(parsed.lines[2].newLine == 11);
	CHECK(shown[2] == "~oldnew line to edit here");

	REQUIRE(parsed.spans.size() == 2);
	CHECK(parsed.spans[0].line == 2);
	CHECK(parsed.spans[0].start == 1);
	CHECK(parsed.spans[0].length == 3);
	CHECK(parsed.spans[0].removed);
	CHECK(parsed.spans[1].start == 4);
	CHECK(parsed.spans[1].length == 3);
	CHECK(!parsed.spans[1].removed);
	CHECK(parsed.moves.empty());
}

TEST_CASE("A block moved within a file: the installer steps of CI.yml moved above the metrics", "[unifieddiff]")
{
	const ParsedDiff parsed = parseUnifiedDiff(fixture("block_moved_up.diff"));
	const QStringList shown = parsed.text.split(QLatin1Char('\n'));

	REQUIRE(shown.size() == 70);
	REQUIRE(parsed.moves.size() == 1);
	const MovedBlock& block = parsed.moves[0];
	CHECK(block.addedFirst == 8);
	CHECK(block.removedFirst == 41);
	CHECK(block.lineCount == 26);
	CHECK(block.group == 0);
	CHECK(shown[block.addedFirst] == "+    - name: Windows - create installer");
	CHECK(shown[block.addedFirst + block.lineCount - 1] == "+");
	CHECK(shown[block.removedFirst] == "-    - name: Windows - create installer");
	CHECK(shown[block.removedFirst + block.lineCount - 1] == "-");
	CHECK(parsed.lines[size_t(block.addedFirst)].kind == DiffLineKind::Added);
	CHECK(parsed.lines[size_t(block.removedFirst)].kind == DiffLineKind::Removed);
	CHECK(countOfKind(parsed, DiffLineKind::Added) == 26);
	CHECK(countOfKind(parsed, DiffLineKind::Removed) == 26);
	CHECK(countOfKind(parsed, DiffLineKind::Edited) == 0);
}

TEST_CASE("A moved block ends a run, and the rest of the run still pairs", "[unifieddiff]")
{
	const ParsedDiff parsed = parseUnifiedDiff(joined({
		"@@ -1,5 +1,3 @@",
		" context",
		"-int total = computeTotal();",
		"-if (total > limit)",
		"-old line to edit here",
		"+new line to edit here",
		" context",
		"@@ -20,2 +18,4 @@",
		" context",
		"+int total = computeTotal();",
		"+if (total > limit)",
		" context",
	}));

	REQUIRE(parsed.lines.size() == 11);
	CHECK(parsed.lines[2].kind == DiffLineKind::Removed);
	CHECK(parsed.lines[3].kind == DiffLineKind::Removed);
	CHECK(parsed.lines[4].kind == DiffLineKind::Edited); // the pair after the block, merged as it would be without it
	CHECK(parsed.lines[8].kind == DiffLineKind::Added);

	REQUIRE(parsed.moves.size() == 1);
	CHECK(parsed.moves[0].removedFirst == 2);
	CHECK(parsed.moves[0].addedFirst == 8);
	CHECK(parsed.moves[0].lineCount == 2);
}

// The accepted limit of the run rule: a block in the middle of a run cuts the removed lines before it off
// from the added lines after it, which stand unpaired rather than merged with the wrong line
TEST_CASE("A moved block in the middle of a run leaves the rest unpaired", "[unifieddiff]")
{
	const ParsedDiff parsed = parseUnifiedDiff(joined({
		"@@ -1,5 +1,3 @@",
		" context",
		"-old line to edit here",
		"-int total = computeTotal();",
		"-if (total > limit)",
		"+new line to edit here",
		" context",
		"@@ -20,2 +18,4 @@",
		" context",
		"+int total = computeTotal();",
		"+if (total > limit)",
		" context",
	}));

	REQUIRE(parsed.lines.size() == 12);
	CHECK(parsed.lines[2].kind == DiffLineKind::Removed);
	CHECK(parsed.lines[5].kind == DiffLineKind::Added);
	CHECK(countOfKind(parsed, DiffLineKind::Edited) == 0);
	REQUIRE(parsed.moves.size() == 1);
	CHECK(parsed.moves[0].removedFirst == 3);
	CHECK(parsed.moves[0].addedFirst == 9);
}

TEST_CASE("A move's lines are placed among the lines shown, not the diff's own", "[unifieddiff]")
{
	const ParsedDiff parsed = parseUnifiedDiff(joined({
		"@@ -1,7 +1,5 @@",
		" context",
		"-old line to edit here", // merged with the next into one shown line, shifting everything after by one
		"+new line to edit here",
		" context",
		"-int total = computeTotal();",
		"-if (total > limit)",
		" context",
		"@@ -20,2 +18,4 @@",
		" context",
		"+int total = computeTotal();",
		"+if (total > limit)",
		" context",
	}));
	const QStringList shown = parsed.text.split(QLatin1Char('\n'));

	REQUIRE(parsed.lines.size() == 12);
	CHECK(parsed.lines[2].kind == DiffLineKind::Edited);
	REQUIRE(parsed.moves.size() == 1);
	CHECK(parsed.moves[0].removedFirst == 4);
	CHECK(parsed.moves[0].addedFirst == 9);
	CHECK(shown[4] == "-int total = computeTotal();");
	CHECK(shown[9] == "+int total = computeTotal();");
}
