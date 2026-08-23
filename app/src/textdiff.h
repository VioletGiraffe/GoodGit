#pragma once

#include <QStringView>

#include <vector>

// A range of characters within one line
struct TextRange
{
	int start = 0;
	int length = 0;
};

// Two lines compared as sequences of tokens: a run of letters, digits and underscores, a run of whitespace,
// or one other character. Words rather than characters, or an edit inside a long identifier would come back
// as confetti.
struct TokenAlignment
{
	double similarity = 0.0; // 0 where no token is shared, 1 for the same tokens in the same order
	// Where the two differ, one list per side. Adjacent differing tokens are merged, so a range covers a
	// phrase rather than each token of it.
	std::vector<TextRange> leftChanges;
	std::vector<TextRange> rightChanges;
};

// Answers nothing at all for a pair that costs more to align than the answer is worth: a line longer than
// MaxAlignedLineLength, or one differing in more than MaxAlignedTokens tokens beyond the ends the two
// share. Such lines are a rewrite rather than an edit, and marking one up would light the whole of it.
inline constexpr int MaxAlignedLineLength = 1000;
inline constexpr int MaxAlignedTokens = 64;

[[nodiscard]] TokenAlignment alignTokens(QStringView left, QStringView right);
