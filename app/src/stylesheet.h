#pragma once

#include "theme.h"

#include "theme/cstylesheettokens.h"
#include "theme/cthemeiconhandler.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QFontInfo>
#include <QString>
RESTORE_COMPILER_WARNINGS

// The application-wide stylesheet, built from a Theme. @token@ placeholders are named after the Theme /
// CBasePalette fields they substitute. Detail header of theme.cpp, not part of the theme API.

// Derived rather than stored, so they follow any base color change
inline QColor hoverShade(const QColor& base, bool dark) { return dark ? base.lighter(115) : base.darker(104); }
inline QColor pressedShade(const QColor& base, bool dark) { return dark ? base.lighter(130) : base.darker(110); }

inline QString buildStyleSheet(const Theme& t)
{
	QString qss = QStringLiteral(R"qss(
QMainWindow, QDialog, QWidget#welcomeWindow { background: @windowBg@; }
QLabel#appVersionLabel { color: @textDim@; }

/* ---------- bars ---------- */
QFrame#repoBar { background: @surfaceAlt@; border-bottom: 1px solid @border@; }
QLabel#branchChip { background: @surface@; color: @text@; border: 1px solid @border@; border-radius: @controlRadius@px; padding: 1px 7px; }
QLabel#aheadLabel { color: @accentText@; font-weight: 600; }
QFrame#counterBar { background: @windowBg@; border-bottom: 1px solid @border@; }
QFrame#diffHeader { background: @windowBg@; border-bottom: 1px solid @border@; }
QFrame#commitBodyPane { border-bottom: 1px solid @border@; }
QFrame#pushLogHeader { background: @windowBg@; border-top: 1px solid @border@; border-bottom: 1px solid @border@; }
QFrame#dockHeader { background: @surfaceAlt@; border-bottom: 1px solid @border@; }
QFrame#findBar { background: @windowBg@; border-top: 1px solid @border@; }
QLabel#diffTagLabel { color: @textDim@; }
QWidget#messageHeader QLabel, QFrame#pushLogHeader QLabel, QFrame#dockHeader QLabel { color: @textDim@; }

/* ---------- status strips ---------- */
QLabel#errorStrip { background: @errBg@; color: @errFg@; }
QFrame#warningStrip { background: @warnBg@; }
QFrame#warningStrip, QFrame#warningStrip QLabel { color: @warnFg@; }

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
QFrame#dockHeader QPushButton, QFrame#diffHeader QPushButton, QFrame#warningStrip QPushButton { padding: 3px 9px; }
QPushButton#commitButton, QPushButton#commitPushButton { padding: 8px 14px; }
QPushButton#commitButton { background: @accent@; color: @accentFg@; border-color: @accent@; font-weight: 600; }
QPushButton#commitButton:hover { background: @accentHover@; }
QPushButton#commitButton:pressed { background: @accentPressed@; }
QPushButton#commitButton:disabled { background: @surfaceAlt@; color: @textDim@; border-color: @buttonBorder@; font-weight: 400; }

/* ---------- editors ---------- */
QPlainTextEdit { background: @surface@; color: @text@; border: none; }
QPlainTextEdit#messageEdit { border: 1px solid @buttonBorder@; border-radius: @controlRadius@px; }
QPlainTextEdit#diffView { color: @diffCtx@; }

/* Input fields: windows11 fills them from the palette only where the brush is set explicitly, which a background rule does.
   -qt-style-features: the base style keeps drawing the field; without it the stylesheet engine draws the field itself.
   Editable combo boxes only: a non-editable one fills from Button, which the rule also sets. */
QLineEdit, QComboBox[editable="true"], QAbstractSpinBox { background: @surface@; -qt-style-features: background-color; }

QSplitter::handle { background: @border@; }

/* Completion and find-match popups */
QListView { background: @surface@; color: @text@; border: 1px solid @border@; outline: none; }

/* ---------- menus ---------- */
QMenuBar { background: @windowBg@; color: @text@; border-bottom: 1px solid @border@; }
QMenuBar::item { background: transparent; padding: 4px 10px; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: @selectionBg@; }
/* The welcome window's own bar, quieter than a repository window's.
   font-size belongs on the widget, not on ::item: the item rects are measured with the widget's font.
   The popup menus keep the full size: a QMenu is a window, and font propagation stops at one. */
QMenuBar#welcomeMenuBar { background: transparent; border-bottom: none; color: @textDim@; font-size: @menuBarFontPt@pt; }
/* The rule above dims the highlighted text as well, so the open menu's title needs its contrast back */
QMenuBar#welcomeMenuBar::item:selected, QMenuBar#welcomeMenuBar::item:pressed { color: @text@; }
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
	qss += t.qssFragment; // last, so a theme's rules win equal-specificity ties; tokens are substituted here too

	CStyleSheetTokens tokens;
	tokens.addPalette(t.palette);
	tokens.add(QStringLiteral("warnBg"), t.warnBg);
	tokens.add(QStringLiteral("warnFg"), t.warnFg);
	tokens.add(QStringLiteral("errBg"), t.errBg);
	tokens.add(QStringLiteral("errFg"), t.errFg);
	tokens.add(QStringLiteral("diffCtx"), t.diffCtx);
	tokens.add(QStringLiteral("buttonHover"), hoverShade(t.palette.button, t.dark));
	tokens.add(QStringLiteral("buttonPressed"), pressedShade(t.palette.button, t.dark));
	tokens.add(QStringLiteral("accentHover"), hoverShade(t.palette.accent, t.dark));
	tokens.add(QStringLiteral("accentPressed"), pressedShade(t.palette.accent, t.dark));
	tokens.add(QStringLiteral("controlRadius"), t.metrics.controlRadius);
	tokens.add(QStringLiteral("checkboxSize"), t.metrics.checkboxSize);
	tokens.add(QStringLiteral("checkboxRadius"), t.metrics.checkboxRadius);
	tokens.add(QStringLiteral("scrollBarThickness"), t.metrics.scrollBarThickness);
	tokens.add(QStringLiteral("scrollBarHandleRadius"), t.metrics.scrollBarHandleRadius);
	// QFontInfo resolves the size in points even where the UI font was set in pixels, which QSS cannot express
	tokens.add(QStringLiteral("menuBarFontPt"), QFontInfo{ QApplication::font() }.pointSize() - 1);
	// Monochrome SVGs tinted per theme and served by CThemeIconHandler, since QSS url() only takes a path
	tokens.add(QStringLiteral("checkIcon"), themeIconUrl(QStringLiteral("check"), t.palette.accentFg));
	tokens.add(QStringLiteral("dashIcon"), themeIconUrl(QStringLiteral("dash"), t.palette.accentFg));
	return tokens.expanded(qss);
}
