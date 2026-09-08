# Diff rendering

## `unifieddiff`

A unified diff read into **the lines to show for it**, which are not the diff's own: a removed line and the added line one edit turned it into become a single line carrying both, the old text beside the new. Pairing is by similarity and never crosses, which is also what lets a run render in one order. How many fragments the merge comes out in is the test of whether one line really became the other - the allowance grows with the line's length, and a pair failing it stands as the diff's own two lines, unmarked. Similarity is only a floor and a way to rank candidates, counted in characters with whitespace weighing nothing: two comments share their spacing and a few short words without being one line edited. A block removed in one place and added in another is reported as a move, found before the pairing so that a moved line is never merged with what replaced it. Backend- and UI-free, so it can be tested directly

## `textdiff`

Two lines aligned as sequences of tokens, answering how alike they are and the interleaving that holds both. Declines a pair too long or too unalike to be one edit

## `movedblocks`

The blocks a diff removed in one place and added in another: the same lines in the same order, whitespace at a line's ends ignored so a block moved into another nesting level still matches. A block needs a few lines with content in them, or every run of closing braces would be a move. Many-to-many: a block copied to several places, or several copies collapsed into one, are all reported, grouped. Greedy from the added side

## `difftextview`

The read-only view under the pane's header: added and removed lines banded to the full width, a merged line unbanded and carrying the only marks any line carries - what the edit took out struck through, what it put in beside it - headers dimmed, and a gutter of old and new line numbers, both of which a merged line has. A moved block has both its places bracketed in the gutter and joined by a line with arrowheads the way it went, one color per move (the graph's lane colors), in a lane of its own where moves overlap; a click on either bracket brings the other end to the top. Diff colors come from the theme, the rest of its look from the stylesheet

## `diffpane`

The pane both windows show one file's text in: path and tag header over the view, with the hunk the view sits at named beside a step either way through the rest, within the one file shown. Three content kinds, since only the first is a diff: a diff, a file's own contents (numbered, undecorated), and a message - a placeholder, a failure, a commit message. Neither reads nor caps the text; the size limit travels into the read, so an oversize diff or file arrives as an ordinary failure and shows like any other

