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
	const DiffMove& block = parsed.moves[0];
	CHECK(block.addedFirst == 8);
	CHECK(block.removedFirst == 41);
	CHECK(block.removedCount == 26);
	CHECK(block.addedCount == 26);
	CHECK(block.group == 0);
	CHECK(shown[block.addedFirst] == "+    - name: Windows - create installer");
	CHECK(shown[block.addedFirst + block.addedCount - 1] == "+");
	CHECK(shown[block.removedFirst] == "-    - name: Windows - create installer");
	CHECK(shown[block.removedFirst + block.removedCount - 1] == "-");
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
	CHECK(parsed.moves[0].removedCount == 2);
	CHECK(parsed.moves[0].addedCount == 2);
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

TEST_CASE("An edit within a moved block is marked on the added line alone", "[unifieddiff]")
{
	const ParsedDiff parsed = parseUnifiedDiff(joined({
		"@@ -1,5 +1,3 @@",
		" context",
		"-int total = computeTotal(items);",
		"-if (total > limit)",
		"-	return false;",
		" context",
		"@@ -20,2 +18,5 @@",
		" context",
		"+int total = computeTotal(items);",
		"+if (total >= limit)",
		"+	return false;",
		" context",
	}));
	const QStringList shown = parsed.text.split(QLatin1Char('\n'));

	REQUIRE(parsed.moves.size() == 1);
	CHECK(parsed.moves[0].removedFirst == 2);
	CHECK(parsed.moves[0].addedFirst == 8);
	CHECK(parsed.moves[0].removedCount == 3);
	CHECK(parsed.moves[0].addedCount == 3);
	CHECK(!parsed.moves[0].foreign);
	CHECK(countOfKind(parsed, DiffLineKind::Edited) == 0);
	for (size_t line = 0; line < parsed.lines.size(); ++line)
		CHECK(parsed.lines[line].moved == ((line >= 2 && line < 5) || (line >= 8 && line < 11)));

	REQUIRE(parsed.spans.size() == 1);
	CHECK(parsed.spans[0].line == 9);
	CHECK(!parsed.spans[0].removed);
	CHECK(shown[9].mid(parsed.spans[0].start, parsed.spans[0].length) == "="); // by token: '>' is shared, '=' is new
}

TEST_CASE("Spans stay in line order with a move marked below a merged line", "[unifieddiff]")
{
	const ParsedDiff parsed = parseUnifiedDiff(joined({
		"@@ -1,5 +1,3 @@",
		" context",
		"+int total = computeTotal(items);",
		"+if (total >= limit)",
		"+	return false;",
		"-old line to edit here",
		"+new line to edit here",
		" context",
		"@@ -20,2 +18,5 @@",
		" context",
		"-int total = computeTotal(items);",
		"-if (total > limit)",
		"-	return false;",
		" context",
	}));

	REQUIRE(parsed.moves.size() == 1);
	CHECK(parsed.moves[0].addedFirst == 2);
	CHECK(parsed.lines[5].kind == DiffLineKind::Edited);
	REQUIRE(parsed.spans.size() == 3);
	CHECK(parsed.spans[0].line == 3); // the move's, before the merged line's
	CHECK(parsed.spans[1].line == 5);
	CHECK(parsed.spans[2].line == 5);
}

TEST_CASE("A block copied to several places keeps every copy out of the pairing", "[unifieddiff]")
{
	const ParsedDiff parsed = parseUnifiedDiff(joined({
		"@@ -1,5 +1,8 @@",
		" context",
		"-int total = computeTotal(items);",
		"-if (total > limit)",
		"-	return false;",
		"+int total = computeTotal(items);",
		"+if (total > limit)",
		"+	return false;",
		"+int total = computeTotal(items);",
		"+if (total >= limit)",
		"+	return false;",
		" context",
	}));

	REQUIRE(parsed.moves.size() == 2);
	CHECK(parsed.moves[0].removedFirst == 2);
	CHECK(parsed.moves[0].addedFirst == 5);
	CHECK(parsed.moves[1].removedFirst == 2);
	CHECK(parsed.moves[1].addedFirst == 8);
	CHECK(parsed.moves[0].group == parsed.moves[1].group);
	CHECK(countOfKind(parsed, DiffLineKind::Edited) == 0);
	REQUIRE(parsed.spans.size() == 1);
	CHECK(parsed.spans[0].line == 9);
}

TEST_CASE("A change set is cut into its files' sections, a rename keyed by both paths", "[unifieddiff]")
{
	const ChangeSetDiff set{ fixture("moved_between_files.diff") };

	REQUIRE(set.fileCount() == 3);
	CHECK(set.fileIndex("src/alpha.cpp") == 0);
	CHECK(set.fileIndex("src/beta.cpp") == 1);
	CHECK(set.fileIndex("docs/new name.md") == 2);
	CHECK(set.fileIndex("docs/old name.md") == 2);
	CHECK(!set.fileIndex("src/gamma.cpp"));
	CHECK(set.filePath(2) == "docs/new name.md");
	CHECK(set.fileDiff(0).startsWith(QLatin1String("diff --git a/src/alpha.cpp")));
	CHECK(set.fileDiff(0).endsWith(QLatin1String(" }\n")));
	CHECK(!set.fileDiff(0).contains(QLatin1String("beta")));
	CHECK(set.fileDiff(2).startsWith(QLatin1String("diff --git a/docs/old name.md")));
	CHECK(set.fileDiff(2).endsWith(QLatin1String(" tail\n")));
}

TEST_CASE("A quoted path in a header is keyed by the name it stands for", "[unifieddiff]")
{
	// git quotes a path holding a quote, a backslash or a control character, each side on its own, and
	// writes a byte outside ASCII as \NNN where core.quotepath is on
	const ChangeSetDiff set{ joined({
		R"(diff --git "a/src/we\"ird.cpp" "b/src/we\"ird.cpp")",
		"@@ -1,1 +1,1 @@",
		"-old",
		"+new",
		R"(diff --git "a/back\\slash.txt" "b/back\\slash.txt")",
		"@@ -1,1 +1,1 @@",
		"-old",
		"+new",
		R"(diff --git "a/caf\303\251.md" "b/caf\303\251.md")",
		"@@ -1,1 +1,1 @@",
		"-old",
		"+new",
		// Quoted for the quote, the emoji left as the bytes it is: a character of two UTF-16 units either side of an escape
		R"(diff --git "a/emoji 🙂\"x.txt" "b/emoji 🙂\"x.txt")",
		"@@ -1,1 +1,1 @@",
		"-old",
		"+new",
	}) };

	REQUIRE(set.fileCount() == 4);
	CHECK(set.fileIndex(R"(src/we"ird.cpp)") == 0);
	CHECK(set.fileIndex(R"(back\slash.txt)") == 1);
	CHECK(set.fileIndex(QString::fromUtf8("café.md")) == 2);
	CHECK(set.fileIndex(QString::fromUtf8(R"(emoji 🙂"x.txt)")) == 3);
	CHECK(set.filePath(0) == R"(src/we"ird.cpp)");
}

TEST_CASE("A quote in a path that was not quoted for it is still read, as hg leaves it", "[unifieddiff]")
{
	const ChangeSetDiff set{ joined({
		R"(diff --git a/src/we"ird.cpp b/src/we"ird.cpp)",
		"@@ -1,1 +1,1 @@",
		"-old",
		"+new",
	}) };

	REQUIRE(set.fileCount() == 1);
	CHECK(set.fileIndex(R"(src/we"ird.cpp)") == 0);
}

TEST_CASE("A rename with one side quoted is keyed by both paths from the header alone", "[unifieddiff]")
{
	// Only the side needing quotes carries them, and the rename lines quote the same way
	const ChangeSetDiff set{ joined({
		R"(diff --git a/plain name.txt "b/quoted \"name\".txt")",
		"similarity index 90%",
		"rename from plain name.txt",
		R"(rename to "quoted \"name\".txt")",
		"@@ -1,1 +1,1 @@",
		"-old",
		"+new",
	}) };

	REQUIRE(set.fileCount() == 1);
	CHECK(set.fileIndex("plain name.txt") == 0);
	CHECK(set.fileIndex(R"(quoted "name".txt)") == 0);
	CHECK(set.filePath(0) == R"(quoted "name".txt)");
}

TEST_CASE("A block moved between two files is a move in either, its far end named", "[unifieddiff]")
{
	const ChangeSetDiff set{ fixture("moved_between_files.diff") };

	SECTION("the file it left")
	{
		const ParsedDiff parsed = parseUnifiedDiff(set, 0);

		REQUIRE(parsed.moves.size() == 1);
		const DiffMove& move = parsed.moves[0];
		CHECK(move.removedFirst == 7);
		CHECK(move.removedCount == 4);
		CHECK(move.addedCount == 0);
		REQUIRE(move.foreign);
		CHECK(move.foreign->path == "src/beta.cpp");
		CHECK(move.foreign->diffLine == 7);
		CHECK(move.foreign->below);
		for (size_t line = 0; line < parsed.lines.size(); ++line)
			CHECK(parsed.lines[line].moved == (line >= 7 && line < 11));
		CHECK(parsed.spans.empty()); // the edit is marked where the block went
	}

	SECTION("the file it went to")
	{
		const ParsedDiff parsed = parseUnifiedDiff(set, 1);

		REQUIRE(parsed.moves.size() == 1);
		const DiffMove& move = parsed.moves[0];
		CHECK(move.addedFirst == 7);
		CHECK(move.addedCount == 4);
		CHECK(move.removedCount == 0);
		REQUIRE(move.foreign);
		CHECK(move.foreign->path == "src/alpha.cpp");
		CHECK(move.foreign->diffLine == 7);
		CHECK(!move.foreign->below);
		CHECK(parsed.shownLine[7] == 7);
		REQUIRE(parsed.spans.size() == 1);
		CHECK(parsed.spans[0].line == 8);
	}

	SECTION("a file it does not touch")
	{
		const ParsedDiff parsed = parseUnifiedDiff(set, 2);

		CHECK(parsed.moves.empty());
		CHECK(parsed.lines[9].kind == DiffLineKind::Edited);
		CHECK(parsed.shownLine[10] == -1); // merged into the line before it
		CHECK(parsed.shownLine[11] == 10);
	}
}

TEST_CASE("A diff has content where it holds a hunk or a binary notice", "[unifieddiff]")
{
	CHECK(diffHasContent(joined({ "diff --git a/f b/f", "--- a/f", "+++ b/f", "@@ -1 +1 @@", "-x", "+y" })));
	CHECK(diffHasContent(joined({ "diff --git a/f b/f", "index 1..2 100644", "Binary files a/f and b/f differ" })));
	CHECK(diffHasContent(joined({ "diff --git a/f b/f", "index 1..2 100644", "GIT binary patch", "literal 3" })));
	CHECK(diffHasContent(joined({ "@@ -1 +1 @@", "-x", "+y" })));
	CHECK(!diffHasContent(joined({ "diff --git a/f b/f", "old mode 100644", "new mode 100755" })));
	CHECK(!diffHasContent(QString{}));
}

TEST_CASE("A section parsed through its set shows as it does alone, its moves aside", "[unifieddiff]")
{
	const ChangeSetDiff set{ fixture("moved_between_files.diff") };
	const ParsedDiff inSet = parseUnifiedDiff(set, 1);
	const ParsedDiff alone = parseUnifiedDiff(set.fileDiff(1));

	CHECK(inSet.text == alone.text);
	REQUIRE(inSet.lines.size() == alone.lines.size());
	for (size_t line = 0; line < alone.lines.size(); ++line)
	{
		CHECK(inSet.lines[line].kind == alone.lines[line].kind);
		CHECK(inSet.lines[line].newLine == alone.lines[line].newLine);
	}
	CHECK(alone.moves.empty()); // the block's other end is in another file
	CHECK(alone.spans.empty());
}
