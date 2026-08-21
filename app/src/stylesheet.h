#pragma once

#include "theme.h"

#include "theme/cthemeiconhandler.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <utility>

// The application-wide stylesheet, built from a Theme. @token@ placeholders are named after the
// Theme / CBasePalette fields they substitute. Detail header of theme.cpp, not part of the theme API.

// Hover / pressed shades are derived rather than stored, so they follow any base color swap
inline QColor hoverShade(const QColor& base, bool dark) { return dark ? base.lighter(115) : base.darker(104); }
inline QColor pressedShade(const QColor& base, bool dark) { return dark ? base.lighter(130) : base.darker(110); }

inline QString buildStyleSheet(const Theme& t)
{
	QString qss = QStringLiteral(R"qss(
QMainWindow, QDialog { background: @windowBg@; }

/* ---------- bars ---------- */
QFrame#repoBar { background: @surfaceAlt@; border-bottom: 1px solid @border@; }
QLabel#branchChip { background: @surface@; color: @text@; border: 1px solid @border@; border-radius: @controlRadius@px; padding: 1px 7px; }
QLabel#aheadLabel { color: @accentText@; font-weight: 600; }
QFrame#counterBar { background: @windowBg@; border-bottom: 1px solid @border@; }
QFrame#diffHeader { background: @windowBg@; border-bottom: 1px solid @border@; }
QFrame#pushLogHeader { background: @windowBg@; border-top: 1px solid @border@; border-bottom: 1px solid @border@; }
QFrame#dockHeader { background: @surfaceAlt@; border-bottom: 1px solid @border@; }
QLabel#diffTagLabel { color: @textDim@; }
QWidget#messageHeader QLabel, QFrame#pushLogHeader QLabel, QFrame#dockHeader QLabel { color: @textDim@; }

/* ---------- status strips ---------- */
QLabel#errorStrip { background: @errBg@; color: @errFg@; }
QLabel#warningStrip { background: @warnBg@; color: @warnFg@; }

/* ---------- file list ---------- */
QTreeView { background: @surface@; border: none; outline: none; }
QTreeView::item { border-bottom: 1px solid @borderSubtle@; padding: 2px 0; }
QTreeView::item:selected { background: @selectionBg@; }

/* Check boxes: the counter-bar box and the file-row indicators share the look */
QCheckBox { color: @textDim@; spacing: 8px; }
QCheckBox::indicator, QTreeView::indicator {
	width: @checkboxSize@px; height: @checkboxSize@px;
	border: 1px solid @buttonBorder@; border-radius: @checkboxRadius@px; background: @surface@;
}
QCheckBox::indicator:checked, QTreeView::indicator:checked {
	background: @accent@; border-color: @accent@; image: url(@checkIcon@);
}
QCheckBox::indicator:indeterminate {
	background: @accent@; border-color: @accent@; image: url(@dashIcon@);
}

/* ---------- buttons ---------- */
QPushButton { background: @button@; color: @text@; border: 1px solid @buttonBorder@; border-radius: @controlRadius@px; padding: 5px 14px; }
QPushButton:hover { background: @buttonHover@; }
QPushButton:pressed { background: @buttonPressed@; }
QPushButton:disabled { background: @surfaceAlt@; color: @textDim@; }
QFrame#repoBar QPushButton, QFrame#counterBar QPushButton, QFrame#pushLogHeader QPushButton,
QFrame#dockHeader QPushButton { padding: 3px 9px; }
QPushButton#commitButton, QPushButton#commitPushButton { padding: 8px 14px; }
QPushButton#commitButton { background: @accent@; color: @accentFg@; border-color: @accent@; font-weight: 600; }
QPushButton#commitButton:hover { background: @accentHover@; }
QPushButton#commitButton:pressed { background: @accentPressed@; }
QPushButton#commitButton:disabled { background: @surfaceAlt@; color: @textDim@; border-color: @buttonBorder@; font-weight: 400; }

/* ---------- editors ---------- */
QPlainTextEdit { background: @surface@; color: @text@; border: none; }
QPlainTextEdit#messageEdit { border: 1px solid @buttonBorder@; border-radius: @controlRadius@px; }
QPlainTextEdit#diffView { color: @diffCtx@; }

QSplitter::handle { background: @border@; }

/* Completion popup */
QListView { background: @surface@; color: @text@; border: 1px solid @border@; outline: none; }

/* ---------- menus ---------- */
QMenuBar { background: @windowBg@; color: @text@; border-bottom: 1px solid @border@; }
QMenuBar::item { background: transparent; padding: 4px 10px; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: @selectionBg@; }
QMenu { background: @surface@; color: @text@; border: 1px solid @border@; padding: 4px 0; }
QMenu::item { padding: 4px 24px 4px 12px; }
QMenu::item:selected { background: @selectionBg@; }
QMenu::item:disabled { color: @textDim@; }
QMenu::separator { height: 1px; background: @borderSubtle@; margin: 4px 0; }

QToolTip { background: @surface@; color: @text@; border: 1px solid @border@; }

/* ---------- scroll bars: flat, no step buttons ---------- */
QScrollBar { background: transparent; }
QScrollBar:vertical { width: @scrollBarThickness@px; }
QScrollBar:horizontal { height: @scrollBarThickness@px; }
QScrollBar::handle { background: @buttonBorder@; border-radius: @scrollBarHandleRadius@px; }
QScrollBar::handle:hover { background: @textDim@; }
QScrollBar::handle:vertical { min-height: 30px; margin: 2px 3px; }
QScrollBar::handle:horizontal { min-width: 30px; margin: 3px 2px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)qss");
	qss += t.qssFragment; // last, so a theme's rules win equal-specificity ties; tokens work here too

	const std::pair<QString, QString> tokens[] = {
		{ QStringLiteral("@windowBg@"), t.palette.windowBg.name() },
		{ QStringLiteral("@surface@"), t.palette.surface.name() },
		{ QStringLiteral("@surfaceAlt@"), t.palette.surfaceAlt.name() },
		{ QStringLiteral("@border@"), t.palette.border.name() },
		{ QStringLiteral("@borderSubtle@"), t.palette.borderSubtle.name() },
		{ QStringLiteral("@text@"), t.palette.text.name() },
		{ QStringLiteral("@textDim@"), t.palette.textDim.name() },
		{ QStringLiteral("@accent@"), t.palette.accent.name() },
		{ QStringLiteral("@accentFg@"), t.palette.accentFg.name() },
		{ QStringLiteral("@accentText@"), t.palette.accentText.name() },
		{ QStringLiteral("@selectionBg@"), t.palette.selectionBg.name() },
		{ QStringLiteral("@button@"), t.palette.button.name() },
		{ QStringLiteral("@buttonBorder@"), t.palette.buttonBorder.name() },
		{ QStringLiteral("@warnBg@"), t.warnBg.name() },
		{ QStringLiteral("@warnFg@"), t.warnFg.name() },
		{ QStringLiteral("@errBg@"), t.errBg.name() },
		{ QStringLiteral("@errFg@"), t.errFg.name() },
		{ QStringLiteral("@diffCtx@"), t.diffCtx.name() },
		{ QStringLiteral("@buttonHover@"), hoverShade(t.palette.button, t.dark).name() },
		{ QStringLiteral("@buttonPressed@"), pressedShade(t.palette.button, t.dark).name() },
		{ QStringLiteral("@accentHover@"), hoverShade(t.palette.accent, t.dark).name() },
		{ QStringLiteral("@accentPressed@"), pressedShade(t.palette.accent, t.dark).name() },
		{ QStringLiteral("@controlRadius@"), QString::number(t.metrics.controlRadius) },
		{ QStringLiteral("@checkboxSize@"), QString::number(t.metrics.checkboxSize) },
		{ QStringLiteral("@checkboxRadius@"), QString::number(t.metrics.checkboxRadius) },
		{ QStringLiteral("@scrollBarThickness@"), QString::number(t.metrics.scrollBarThickness) },
		{ QStringLiteral("@scrollBarHandleRadius@"), QString::number(t.metrics.scrollBarHandleRadius) },
		// Monochrome sources tinted per theme and served by CThemeIconHandler - QSS url() only takes a path
		{ QStringLiteral("@checkIcon@"), themeIconUrl(QStringLiteral("check"), t.palette.accentFg) },
		{ QStringLiteral("@dashIcon@"), themeIconUrl(QStringLiteral("dash"), t.palette.accentFg) },
	};
	for (const auto& [token, value] : tokens)
		qss.replace(token, value);
	return qss;
}
