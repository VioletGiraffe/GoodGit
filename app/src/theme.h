#pragma once

#include <QColor>
#include <QFont>

#include <array>

class QApplication;

// The app's visual style, mirroring the CSS variables of doc/UI/mockup.html.
// To restyle: edit or swap the palettes in theme.cpp. The check mark / arrow glyphs are SVGs in
// res/ with their fill hardcoded to accentFg - touch them too if accentFg changes polarity.
// Light or dark is picked once at startup from the system theme.
struct Theme
{
	QColor winBg;      // window chrome: bars, gaps between panes
	QColor pane;       // content panes: file list, diff, editors
	QColor paneAlt;    // slightly offset pane: repo bar
	QColor border;     // pane and bar separators
	QColor borderSoft; // row separators
	QColor text;
	QColor dim;        // secondary text
	QColor accent;     // primary action, selection stripe, checked boxes
	QColor accentFg;   // text and glyphs on accent
	QColor sel;        // selected row background
	QColor btn;
	QColor btnBorder;
	QColor warnBg;     // blocked submodule rows, detached-HEAD strip
	QColor warnFg;     // text on warnBg
	QColor errBg;      // merge/cherry-pick/revert/rebase strip
	QColor errFg;

	QColor stModified, stAdded, stUntracked, stDeleted, stRenamed, stSubmodule;

	QColor diffAddBg, diffAddFg;
	QColor diffDelBg, diffDelFg;
	QColor diffHunk;
	QColor diffCtx; // context lines in the diff view

	// The commit graph's lines, cycled by line of history rather than by lane, so a branch and the one that
	// reuses its lane still differ. Chains are numbered as the walk meets them, which keeps the lines on
	// screen together on different colors.
	std::array<QColor, 6> graphLanes;

	QColor blockedRowTint() const; // warnBg, translucent so selection and the base show through
};

[[nodiscard]] const Theme& activeTheme();
[[nodiscard]] QFont monospaceFont();

// Palette + app-wide stylesheet; call once, right after constructing the QApplication
void applyTheme(QApplication& app);
