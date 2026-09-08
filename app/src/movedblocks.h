#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QStringView>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>
#include <vector>

// Finds the blocks a diff removed in one place and added in another: the same lines in the same order on
// both sides, whitespace at either end of a line counting for nothing, so a block moved into another nesting
// level still matches. A block edited on the way is not found.
// Blocks are many-to-many: one removed block copied to several places, or several removed copies collapsed
// into one added block, are all reported, each pairing as a block of its own.
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

// One block as the pairing of a removed range with an added range, as indices into the sequence given
struct MovedBlock
{
	int removedFirst = 0;
	int addedFirst = 0;
	int lineCount = 0;
	// Blocks starting on the same removed line, or on the same added line, share a group: the copies of one
	// block, or the copies collapsed into one. Numbered from 0 in order of first appearance.
	int group = 0;
};

// A block must hold a few lines with something in them, or every run of closing braces reads as a move. A
// line without a letter or digit counts toward neither: it may sit inside a block, but never starts one.
inline constexpr int MinMovedBlockContentLines = 2;
inline constexpr int MinMovedBlockAlnumChars = 15;
// A line with more removed copies than this starts no block: matching from every copy is quadratic, and a
// line that common is no anchor. A block starting on it is found from its next line, one line shorter.
inline constexpr int MaxBlockStartCandidates = 64;

// Ascending by addedFirst, each added line in at most one block. Greedy: from each added line not yet in a
// block, the longest matching removed runs win, and a shorter one starting a line later is never considered.
[[nodiscard]] std::vector<MovedBlock> detectMovedBlocks(const std::vector<ChangeLine>& lines);
