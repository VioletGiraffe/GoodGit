#pragma once

#include "textdiff.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QStringView>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>
#include <vector>

// Finds the blocks a diff removed in one place and added in another, edited on the way or not. A line the
// same on both sides, whitespace at either end counting for nothing, is an anchor; between two anchors a few
// lines may differ, and a block may begin or end with lines edited into each other. Similarity is textdiff's.
// Blocks are many-to-many: one removed block copied to several places, or several removed copies collapsed
// into one, are all reported, each pairing as a block of its own.
// Index-free: works on whatever sequence it is given, so the caller decides which lines take part.

enum class ChangeSide : uint8_t
{
	None, // context, a header, anything else: ends a block on either side
	Removed,
	Added,
};

struct ChangeLine
{
	QStringView text; // the line's content, without the diff's marker column
	ChangeSide side = ChangeSide::None;
};

// A removed line and the added line it stands as, as indices into the sequence given
struct MovedLinePair
{
	int removed = 0;
	int added = 0;
	bool edited = false;      // the two differ
	TokenAlignment alignment; // of an edited pair: the removed line against the added one, in `text` offsets
};

// One block as the pairing of a removed range with an added range, as indices into the sequence given. A
// line of either range in no pair was dropped from the block or put into it on the way.
struct MovedBlock
{
	int removedFirst = 0;
	int removedCount = 0;
	int addedFirst = 0;
	int addedCount = 0;
	std::vector<MovedLinePair> pairs; // ascending on both sides, the first and the last bounding the ranges
	// Blocks whose removed ranges overlap, or whose added ranges do, share a group: the copies of one block, or
	// the copies collapsed into one. Numbered from 0 in order of first appearance.
	int group = 0;
};

// A block's anchors alone must hold a few lines with something in them, or every run of closing braces reads
// as a move. A line without a letter or digit counts toward neither: it may sit inside a block or at its ends,
// but never seeds one, and never anchors one after a gap.
inline constexpr int MinMovedBlockContentLines = 2;
inline constexpr int MinMovedBlockAlnumChars = 15;
// Between two anchors, at most this many lines per side may differ
inline constexpr int MaxMovedBlockGap = 4;
// A line with no anchor within the gap past it joins the block where it is at least this alike its
// counterpart. Stricter than SimilarityThreshold: no anchor vouches for the pairing.
inline constexpr double MinEditedEndSimilarity = 0.5;
// A line with more removed copies than this seeds no block: growing from every copy is quadratic, and a
// line that common is no anchor. A block starting on it is found from its next line.
inline constexpr int MaxBlockStartCandidates = 64;

// Ascending by addedFirst, the copies of one block by removedFirst; each added line in at most one block.
// Greedy: from each added line not yet in a block that some removed line equals, the block with the most
// anchors wins, with every copy covering the same added lines, and a block seeded a line later is never considered.
[[nodiscard]] std::vector<MovedBlock> detectMovedBlocks(const std::vector<ChangeLine>& lines);
