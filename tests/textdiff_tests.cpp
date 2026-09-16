#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "textdiff.h"

DISABLE_COMPILER_WARNINGS
#include <QStringList>
RESTORE_COMPILER_WARNINGS

namespace {

// A segment's text: an Added range reads the right line, every other the left
[[nodiscard]] QString segmentText(const MergeSegment& segment, QStringView left, QStringView right)
{
	return (segment.kind == SegmentKind::Added ? right : left).sliced(segment.range.start, segment.range.length).toString();
}

// One side's line, read back from the segments: Common and Removed for the left, Common and Added for the right
[[nodiscard]] QString sideText(const TokenAlignment& alignment, QStringView left, QStringView right, SegmentKind sideOnly)
{
	QString text;
	for (const MergeSegment& segment : alignment.segments)
	{
		if (segment.kind == SegmentKind::Common || segment.kind == sideOnly)
			text += segmentText(segment, left, right);
	}
	return text;
}

[[nodiscard]] QStringList segmentTexts(const TokenAlignment& alignment, QStringView left, QStringView right)
{
	QStringList texts;
	for (const MergeSegment& segment : alignment.segments)
		texts << segmentText(segment, left, right);
	return texts;
}

[[nodiscard]] QString wordsLine(QChar prefix, int count)
{
	QStringList words;
	for (int i = 0; i < count; ++i)
		words << prefix + QString::number(i);
	return words.join(QLatin1Char(' '));
}

} // namespace

TEST_CASE("Identical lines align as one common run of full similarity", "[textdiff]")
{
	const QString line = QStringLiteral("int total = computeTotal(items);");
	const TokenAlignment alignment = alignTokens(line, line);

	CHECK(alignment.similarity == 1.0);
	REQUIRE(alignment.segments.size() == 1);
	CHECK(alignment.segments[0].kind == SegmentKind::Common);
	CHECK(alignment.segments[0].range.length == line.size());
}

TEST_CASE("An edit inside an identifier replaces the whole word, and each side reads back whole", "[textdiff]")
{
	const QString left = QStringLiteral("value = computeTotal(items);");
	const QString right = QStringLiteral("value = computeSubtotal(items);");
	const TokenAlignment alignment = alignTokens(left, right);

	CHECK(segmentTexts(alignment, left, right)
		== QStringList{ QStringLiteral("value = "), QStringLiteral("computeTotal"), QStringLiteral("computeSubtotal"), QStringLiteral("(items);") });
	CHECK(alignment.segments[1].kind == SegmentKind::Removed);
	CHECK(alignment.segments[2].kind == SegmentKind::Added);
	// Characters outside whitespace: 26 on the left, 29 on the right, 14 of them shared
	CHECK(alignment.similarity == Approx(2.0 * 14 / (26 + 29)));

	CHECK(sideText(alignment, left, right, SegmentKind::Removed) == left);
	CHECK(sideText(alignment, left, right, SegmentKind::Added) == right);
}

TEST_CASE("Both sides read back whole from an alignment with edits scattered through it", "[textdiff]")
{
	const QString left = QStringLiteral("if (a && b) return first(x);");
	const QString right = QStringLiteral("if (a || c) { return second(x, y); }");
	const TokenAlignment alignment = alignTokens(left, right);

	CHECK(sideText(alignment, left, right, SegmentKind::Removed) == left);
	CHECK(sideText(alignment, left, right, SegmentKind::Added) == right);
}

TEST_CASE("Shared whitespace is no evidence of an edit, while indentation alone is one", "[textdiff]")
{
	CHECK(alignTokens(QStringLiteral("the cat sat"), QStringLiteral("a dog ran")).similarity == 0.0);
	CHECK(alignTokens(QStringLiteral("    "), QStringLiteral("\t")).similarity == 1.0);
}

TEST_CASE("A pair too costly to align gets no alignment, but long shared ends are no cost", "[textdiff]")
{
	SECTION("a line past the length limit")
	{
		const QString longLine(MaxAlignedLineLength + 1, QLatin1Char('x'));
		const TokenAlignment alignment = alignTokens(longLine, longLine);
		CHECK(alignment.segments.empty());
		CHECK(alignment.similarity == 0.0);
	}

	SECTION("more differing tokens than the limit")
	{
		const TokenAlignment alignment = alignTokens(wordsLine(QLatin1Char('a'), MaxAlignedTokens), wordsLine(QLatin1Char('b'), MaxAlignedTokens));
		CHECK(alignment.segments.empty());
	}

	SECTION("many tokens, only one of them differing")
	{
		const QString shared = wordsLine(QLatin1Char('w'), MaxAlignedTokens);
		const TokenAlignment alignment = alignTokens(shared + QStringLiteral(" old ") + shared, shared + QStringLiteral(" new ") + shared);
		CHECK(alignment.segments.size() == 4);
	}
}

TEST_CASE("Line pairing: similar lines pair in order, unrelated ones do not, and pairs never cross", "[textdiff]")
{
	const auto views = [](const QStringList& lines) {
		std::vector<QStringView> result;
		for (const QString& line : lines)
			result.emplace_back(line);
		return result;
	};

	SECTION("each line edited in place")
	{
		const QStringList left{ QStringLiteral("int total = 0;"), QStringLiteral("return total;") };
		const QStringList right{ QStringLiteral("int sum = 0;"), QStringLiteral("return sum;") };
		const std::vector<LinePair> pairs = pairSimilarLines(views(left), views(right));

		REQUIRE(pairs.size() == 2);
		CHECK((pairs[0].left == 0 && pairs[0].right == 0));
		CHECK((pairs[1].left == 1 && pairs[1].right == 1));
	}

	SECTION("lines with nothing in common")
	{
		const QStringList left{ QStringLiteral("int x = 1;") };
		const QStringList right{ QStringLiteral("return foo(bar);") };
		CHECK(pairSimilarLines(views(left), views(right)).empty());
	}

	SECTION("two lines swapped: only one of them pairs")
	{
		const QStringList left{ QStringLiteral("alpha beta gamma"), QStringLiteral("delta epsilon zeta") };
		const QStringList right{ QStringLiteral("delta epsilon zeta"), QStringLiteral("alpha beta gamma") };
		const std::vector<LinePair> pairs = pairSimilarLines(views(left), views(right));

		REQUIRE(pairs.size() == 1);
		CHECK(left[pairs[0].left] == right[pairs[0].right]);
	}

	SECTION("more pairings than the limit")
	{
		const QStringList left(11, QStringLiteral("same line"));
		const QStringList right(10, QStringLiteral("same line"));
		REQUIRE(left.size() * right.size() > MaxLinePairings);
		CHECK(pairSimilarLines(views(left), views(right)).empty());
	}
}
