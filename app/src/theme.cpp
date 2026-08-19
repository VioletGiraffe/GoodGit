#include "theme.h"

#include "assert/advanced_assert.h"
#include "theme/cthemecontroller.h"
#include "theme/cthemeiconhandler.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <utility>

namespace {

Theme s_active;

QColor c(QRgb rgb) { return QColor::fromRgb(rgb); }

// Hover / pressed shades are derived rather than stored, so they follow any base color swap
QColor hoverShade(const QColor& base, bool dark) { return dark ? base.lighter(115) : base.darker(104); }
QColor pressedShade(const QColor& base, bool dark) { return dark ? base.lighter(130) : base.darker(110); }

QString buildStyleSheet(const Theme& t)
{
	QString qss = QStringLiteral(R"qss(
QMainWindow, QDialog { background: @winBg@; }

/* ---------- bars ---------- */
QFrame#repoBar { background: @paneAlt@; border-bottom: 1px solid @border@; }
QLabel#branchChip { background: @pane@; color: @text@; border: 1px solid @border@; border-radius: @controlRadius@px; padding: 1px 7px; }
QLabel#aheadLabel { color: @accent@; font-weight: 600; }
QFrame#counterBar { background: @winBg@; border-bottom: 1px solid @border@; }
QFrame#diffHeader { background: @winBg@; border-bottom: 1px solid @border@; }
QFrame#pushLogHeader { background: @winBg@; border-top: 1px solid @border@; border-bottom: 1px solid @border@; }
QLabel#diffTagLabel { color: @dim@; }
QWidget#messageHeader QLabel, QFrame#pushLogHeader QLabel { color: @dim@; }

/* ---------- status strips ---------- */
QLabel#errorStrip { background: @errBg@; color: @errFg@; }
QLabel#warningStrip { background: @warnBg@; color: @warnFg@; }

/* ---------- file list ---------- */
QTreeView { background: @pane@; border: none; outline: none; }
QTreeView::item { border-bottom: 1px solid @borderSoft@; padding: 2px 0; }
QTreeView::item:selected { background: @sel@; }

/* Check boxes: the counter-bar box and the file-row indicators share the look */
QCheckBox { color: @dim@; spacing: 8px; }
QCheckBox::indicator, QTreeView::indicator {
	width: @checkboxSize@px; height: @checkboxSize@px;
	border: 1px solid @btnBorder@; border-radius: @checkboxRadius@px; background: @pane@;
}
QCheckBox::indicator:checked, QTreeView::indicator:checked {
	background: @accent@; border-color: @accent@; image: url(@checkIcon@);
}
QCheckBox::indicator:indeterminate {
	background: @accent@; border-color: @accent@; image: url(@dashIcon@);
}

/* ---------- buttons ---------- */
QPushButton { background: @btn@; color: @text@; border: 1px solid @btnBorder@; border-radius: @controlRadius@px; padding: 5px 14px; }
QPushButton:hover { background: @btnHover@; }
QPushButton:pressed { background: @btnPressed@; }
QPushButton:disabled { background: @paneAlt@; color: @dim@; }
QFrame#repoBar QPushButton, QFrame#counterBar QPushButton, QFrame#pushLogHeader QPushButton { padding: 3px 9px; }
QPushButton#commitButton, QPushButton#commitPushButton { padding: 8px 14px; }
QPushButton#commitButton { background: @accent@; color: @accentFg@; border-color: @accent@; font-weight: 600; }
QPushButton#commitButton:hover { background: @accentHover@; }
QPushButton#commitButton:pressed { background: @accentPressed@; }
QPushButton#commitButton:disabled { background: @paneAlt@; color: @dim@; border-color: @btnBorder@; font-weight: 400; }

/* ---------- editors ---------- */
QPlainTextEdit { background: @pane@; color: @text@; border: none; }
QPlainTextEdit#messageEdit { border: 1px solid @btnBorder@; border-radius: @controlRadius@px; }
QPlainTextEdit#diffView { color: @diffCtx@; }

QSplitter::handle { background: @border@; }

/* Completion popup */
QListView { background: @pane@; color: @text@; border: 1px solid @border@; outline: none; }

/* ---------- menus ---------- */
QMenu { background: @pane@; color: @text@; border: 1px solid @border@; padding: 4px 0; }
QMenu::item { padding: 4px 24px 4px 12px; }
QMenu::item:selected { background: @sel@; }
QMenu::item:disabled { color: @dim@; }
QMenu::separator { height: 1px; background: @borderSoft@; margin: 4px 0; }

QToolTip { background: @pane@; color: @text@; border: 1px solid @border@; }

/* ---------- scroll bars: flat, no step buttons ---------- */
QScrollBar { background: transparent; }
QScrollBar:vertical { width: @scrollBarThickness@px; }
QScrollBar:horizontal { height: @scrollBarThickness@px; }
QScrollBar::handle { background: @btnBorder@; border-radius: @scrollBarHandleRadius@px; }
QScrollBar::handle:hover { background: @dim@; }
QScrollBar::handle:vertical { min-height: 30px; margin: 2px 3px; }
QScrollBar::handle:horizontal { min-width: 30px; margin: 3px 2px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)qss");
	qss += t.qssFragment; // last, so a theme's rules win equal-specificity ties; tokens work here too

	const std::pair<QString, QString> tokens[] = {
		{ QStringLiteral("@winBg@"), t.palette.windowBg.name() },
		{ QStringLiteral("@pane@"), t.palette.surface.name() },
		{ QStringLiteral("@paneAlt@"), t.palette.surfaceAlt.name() },
		{ QStringLiteral("@border@"), t.palette.border.name() },
		{ QStringLiteral("@borderSoft@"), t.palette.borderSubtle.name() },
		{ QStringLiteral("@text@"), t.palette.text.name() },
		{ QStringLiteral("@dim@"), t.palette.textDim.name() },
		{ QStringLiteral("@accent@"), t.palette.accent.name() },
		{ QStringLiteral("@accentFg@"), t.palette.accentFg.name() },
		{ QStringLiteral("@sel@"), t.palette.selectionBg.name() },
		{ QStringLiteral("@btn@"), t.palette.button.name() },
		{ QStringLiteral("@btnBorder@"), t.palette.buttonBorder.name() },
		{ QStringLiteral("@warnBg@"), t.warnBg.name() },
		{ QStringLiteral("@warnFg@"), t.warnFg.name() },
		{ QStringLiteral("@errBg@"), t.errBg.name() },
		{ QStringLiteral("@errFg@"), t.errFg.name() },
		{ QStringLiteral("@diffCtx@"), t.diffCtx.name() },
		{ QStringLiteral("@btnHover@"), hoverShade(t.palette.button, t.dark).name() },
		{ QStringLiteral("@btnPressed@"), pressedShade(t.palette.button, t.dark).name() },
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

void selectActiveTheme()
{
	const CThemeController& controller = CThemeController::instance();
	const bool dark = controller.darkActive();
	const QString storedName = controller.themeName(dark);

	const std::vector<Theme>& themes = allThemes();
	const auto matchesStored = [&](const Theme& t) { return t.dark == dark && t.name == storedName; };
	auto it = std::find_if(themes.begin(), themes.end(), matchesStored);
	if (it == themes.end()) // the stored name can outlive its theme in the settings
		it = std::find_if(themes.begin(), themes.end(), [dark](const Theme& t) { return t.dark == dark; });
	assert_r(it != themes.end()); // no theme of this polarity at all - a defective theme table

	s_active = *it; // a copy, so nothing dangles if the table is ever rebuilt
	s_active.palette = resolvedPalette(s_active.palette);
}

void applyActiveTheme()
{
	selectActiveTheme();
	qApp->setPalette(qtPaletteFor(s_active.palette));
	qApp->setStyleSheet(buildStyleSheet(s_active));
}

} // namespace

QColor Theme::blockedRowTint() const
{
	QColor tint = warnBg;
	tint.setAlpha(160);
	return tint;
}

const std::vector<Theme>& allThemes()
{
	static const Theme light{
		.name = QStringLiteral("Default"),
		.dark = false,
		.palette = {
			.windowBg = c(0xf3f3f3), .surface = c(0xffffff), .surfaceAlt = c(0xfafafa),
			.text = c(0x16181c), .textDim = c(0x6b7280),
			.button = c(0xfdfdfd),
			.accent = c(0x0d6bc4),
			.selectionBg = c(0xd6e8fb), .selectionFg = c(0x16181c),
			.border = c(0xd0d3d8), .borderSubtle = c(0xe3e5e9),
			.accentFg = c(0xffffff),
			.buttonBorder = c(0xc2c6cc),
		},
		.warnBg = c(0xfff4d6), .warnFg = c(0x6b4e00),
		.errBg = c(0xffe9e6), .errFg = c(0x8f2318),
		.stModified = c(0x1668c4), .stAdded = c(0x12783c), .stUntracked = c(0x8a6a08),
		.stDeleted = c(0xb8302a), .stRenamed = c(0x7345c0), .stSubmodule = c(0xa15c00),
		.diffAddBg = c(0xe3f7e8), .diffAddFg = c(0x0f5f2e),
		.diffDelBg = c(0xfdeaea), .diffDelFg = c(0x8f2318),
		.diffHunk = c(0x6a5fb0), .diffCtx = c(0x2b2f36),
		.graphLanes = { c(0x1668c4), c(0x12783c), c(0xa15c00), c(0x7345c0), c(0x0f8a8a), c(0xb8302a) },
	};
	static const Theme dark{
		.name = QStringLiteral("Default"),
		.dark = true,
		.palette = {
			.windowBg = c(0x1f2227), .surface = c(0x191c21), .surfaceAlt = c(0x1c1f24),
			.text = c(0xe6e8ec), .textDim = c(0x98a0ab),
			.button = c(0x262a30),
			.accent = c(0x3b8fe0),
			.selectionBg = c(0x1e3c58), .selectionFg = c(0xe6e8ec),
			.border = c(0x33383f), .borderSubtle = c(0x282c32),
			.accentFg = c(0x06121f),
			.buttonBorder = c(0x3c424a),
		},
		.warnBg = c(0x3a3013), .warnFg = c(0xd4b352),
		.errBg = c(0x3a1d1c), .errFg = c(0xf0a79c),
		.stModified = c(0x6cb0f0), .stAdded = c(0x62c98a), .stUntracked = c(0xd4b352),
		.stDeleted = c(0xef8c82), .stRenamed = c(0xb394ef), .stSubmodule = c(0xdda45c),
		.diffAddBg = c(0x16341f), .diffAddFg = c(0x8fdca8),
		.diffDelBg = c(0x3a1d1c), .diffDelFg = c(0xf0a79c),
		.diffHunk = c(0x9b8fe0), .diffCtx = c(0xc9ced6),
		.graphLanes = { c(0x6cb0f0), c(0x62c98a), c(0xdda45c), c(0xb394ef), c(0x5ec8c8), c(0xef8c82) },
	};

	static const std::vector<Theme> themes{ light, dark };
	return themes;
}

const Theme& activeTheme()
{
	assert_debug_only(!s_active.name.isEmpty()); // applyTheme() has not run yet
	return s_active;
}

QFont monospaceFont()
{
	return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}

void applyTheme(QApplication& app)
{
	// Serves the tinted QSS glyphs; must exist before any stylesheet references themeicon:/ URLs
	// and for the application's whole lifetime.
	static const CThemeIconHandler iconHandler{ QStringLiteral(":/theme") };

	QObject::connect(&CThemeController::instance(), &CThemeController::themeChanged, &app, &applyActiveTheme);
	applyActiveTheme();
}
