#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QStringView>
RESTORE_COMPILER_WARNINGS

#include <span>
#include <stdint.h>
#include <vector>

// A range of characters within one line
struct TextRange
{
	int start = 0;
	int length = 0;
};

enum class SegmentKind : uint8_t
{
	Common,  // the same text in both lines
	Removed, // in the left line alone
	Added,   // in the right line alone
};

// One run of the two lines interleaved. `range` reads the right line for Added and the left line otherwise.
struct MergeSegment
{
	SegmentKind kind = SegmentKind::Common;
	TextRange range;
};

// Two lines aligned as sequences of tokens: a run of letters, digits and underscores, a run of whitespace,
// or one other character. Words rather than characters, or an edit inside a long identifier would come back
// as confetti.
struct TokenAlignment
{
	// 0 where the two share nothing, 1 for the same tokens in the same order. Counted in characters, not in
	// tokens, and whitespace counts for nothing: shared spacing and a few short words are not one line edited.
	double similarity = 0.0;
	// In order, so concatenating them - the right line's text for Added, the left line's otherwise - reads
	// as one line holding both
	std::vector<MergeSegment> segments;
};

// Answers nothing at all for a pair that costs more to align than the answer is worth: a line longer than
// MaxAlignedLineLength, or one differing in more than MaxAlignedTokens tokens beyond the ends the two
// share. Such lines are a rewrite rather than an edit, and marking one up would light the whole of it.
inline constexpr int MaxAlignedLineLength = 1000;
inline constexpr int MaxAlignedTokens = 64;

[[nodiscard]] TokenAlignment alignTokens(QStringView left, QStringView right);

// A left line and the right line it was most likely edited into, as indices into the sequences given
struct LinePair
{
	int left = 0;
	int right = 0;
	TokenAlignment alignment;
};

// Below this the two lines have nothing to do with each other. It is a floor and a way to rank candidates,
// not the test of whether one line was edited into the other: how many fragments the merge comes out in
// answers that far better, and does it after the two have been aligned.
inline constexpr double SimilarityThreshold = 0.3;
// Two ranges offering more pairings than this are a rewritten block, where no pairing is worth finding
inline constexpr int MaxLinePairings = 100;

// The pairing of the greatest total similarity, no pair below SimilarityThreshold. Pairs never cross: the
// lines are sequences, and a crossing pair would mark a line against one it does not answer. Ascending on
// both sides. Empty above MaxLinePairings.
[[nodiscard]] std::vector<LinePair> pairSimilarLines(std::span<const QStringView> left, std::span<const QStringView> right);
