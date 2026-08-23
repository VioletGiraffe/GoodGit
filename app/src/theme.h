#pragma once

#include "theme/cbasepalette.h"

DISABLE_COMPILER_WARNINGS
#include <QFont>
#include <QIcon>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <array>
#include <vector>

class QApplication;

// Geometry the stylesheet and custom painting share. Per theme, so a theme can reshape and not only recolor.
struct ThemeMetrics
{
	int controlRadius = 4;        // buttons, branch chip, message editor
	int checkboxSize = 13;        // the counter-bar box and the file-row indicators
	int checkboxRadius = 3;
	int scrollBarThickness = 12;
	int scrollBarHandleRadius = 4;
	int selectionStripeWidth = 2; // the accent stripe FileListDelegate paints on selected rows
};

// One selectable look, mirroring the design vocabulary of doc/UI/mockup.html. Pure data; `name` is unique
// within its polarity. Themes are defined in theme.cpp.
struct Theme
{
	QString name;
	bool dark = false;

	CBasePalette palette;
	ThemeMetrics metrics;

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
	// reuses its lane still differ. Chains are numbered as the walk meets them, so lines near each other on
	// screen get different colors.
	std::array<QColor, 6> graphLanes;

	QString qssFragment; // optional per-theme QSS, appended after the app sheet so it wins ties

	QColor blockedRowTint() const; // warnBg, translucent so selection and the base show through

	// The band under the spans an edited line differs in, a step from the band toward its own text color.
	// Derived rather than authored: how far a theme's bands sit from its surface is the theme's own
	// decision, and the step keeps whatever it chose.
	QColor diffAddEmphasisBg() const;
	QColor diffDelEmphasisBg() const;
};

[[nodiscard]] QFont monospaceFont();

// The folder glyph in the submodule color. Tinted per render, so it follows a theme change.
[[nodiscard]] QIcon submoduleIcon();

// Installs the themeicon handler, applies the active theme, and reapplies whenever CThemeController
// announces a change. Call once, right after constructing the QApplication.
void applyTheme(QApplication& app);

// Valid once applyTheme() ran
[[nodiscard]] const Theme& activeTheme();

[[nodiscard]] const std::vector<Theme>& allThemes();
