#pragma once

#include "theme/cbasepalette.h"

DISABLE_COMPILER_WARNINGS
#include <QFont>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <array>
#include <vector>

class QApplication;

// Geometry the stylesheet and custom painting share. Per theme, so a theme can reshape and not only
// recolour; the defaults are the house values.
struct ThemeMetrics
{
	int controlRadius = 4;        // buttons, branch chip, message editor
	int checkboxSize = 13;        // the counter-bar box and the file-row indicators
	int checkboxRadius = 3;
	int scrollBarThickness = 12;
	int scrollBarHandleRadius = 4;
	int selectionStripeWidth = 2; // the accent stripe FileListDelegate paints on selected rows
};

// One selectable look, mirroring the design vocabulary of doc/UI/mockup.html. Pure data; the
// identity is `name` (unique within its polarity). To restyle: edit or add themes in theme.cpp.
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
	// reuses its lane still differ. Chains are numbered as the walk meets them, which keeps the lines on
	// screen together on different colors.
	std::array<QColor, 6> graphLanes;

	QString qssFragment; // optional per-theme QSS, appended after the app sheet so it wins ties

	QColor blockedRowTint() const; // warnBg, translucent so selection and the base show through
};

[[nodiscard]] QFont monospaceFont();

// Installs the themeicon handler, applies the active theme, and reapplies whenever
// CThemeController announces a change. Call once, right after constructing the QApplication.
void applyTheme(QApplication& app);

// The theme in effect - a copy independent of allThemes() storage. Valid once applyTheme() ran.
[[nodiscard]] const Theme& activeTheme();

// Every selectable theme, both polarities.
[[nodiscard]] const std::vector<Theme>& allThemes();
