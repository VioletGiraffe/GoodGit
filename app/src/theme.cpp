#include "theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>

#include <utility>

namespace {

bool s_darkTheme = false;

QColor c(QRgb rgb) { return QColor::fromRgb(rgb); }

// Hover / pressed shades are derived rather than stored, so they follow any base color swap
QColor hoverShade(const QColor& base) { return s_darkTheme ? base.lighter(115) : base.darker(104); }
QColor pressedShade(const QColor& base) { return s_darkTheme ? base.lighter(130) : base.darker(110); }

QString buildStyleSheet(const Theme& t)
{
	QString qss = QStringLiteral(R"qss(
QMainWindow, QDialog { background: @winBg@; }

/* ---------- bars ---------- */
QFrame#repoBar { background: @paneAlt@; border-bottom: 1px solid @border@; }
QLabel#branchChip { background: @pane@; color: @text@; border: 1px solid @border@; border-radius: 4px; padding: 1px 7px; }
QLabel#aheadLabel { color: @accent@; font-weight: 600; }
QFrame#counterBar { background: @winBg@; border-bottom: 1px solid @border@; }
QFrame#diffHeader { background: @winBg@; border-bottom: 1px solid @border@; }
QFrame#pushLogHeader { background: @winBg@; border-top: 1px solid @border@; border-bottom: 1px solid @border@; }
QLabel#diffTagLabel { color: @dim@; }
QWidget#messageHeader QLabel, QFrame#pushLogHeader QLabel { color: @dim@; }

/* ---------- file list ---------- */
QTreeView { background: @pane@; border: none; outline: none; }
QTreeView::item { border-bottom: 1px solid @borderSoft@; padding: 2px 0; }
QTreeView::item:selected { background: @sel@; }

/* Check boxes: the counter-bar box and the file-row indicators share the look */
QCheckBox { color: @dim@; spacing: 8px; }
QCheckBox::indicator, QTreeView::indicator {
	width: 13px; height: 13px;
	border: 1px solid @btnBorder@; border-radius: 3px; background: @pane@;
}
QCheckBox::indicator:checked, QTreeView::indicator:checked {
	background: @accent@; border-color: @accent@; image: url(:/theme/check-@mode@.svg);
}
QCheckBox::indicator:indeterminate {
	background: @accent@; border-color: @accent@; image: url(:/theme/dash-@mode@.svg);
}

/* ---------- buttons ---------- */
QPushButton { background: @btn@; color: @text@; border: 1px solid @btnBorder@; border-radius: 4px; padding: 5px 14px; }
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
QPlainTextEdit#messageEdit { border: 1px solid @btnBorder@; border-radius: 4px; }
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
QScrollBar:vertical { width: 12px; }
QScrollBar:horizontal { height: 12px; }
QScrollBar::handle { background: @btnBorder@; border-radius: 4px; }
QScrollBar::handle:hover { background: @dim@; }
QScrollBar::handle:vertical { min-height: 30px; margin: 2px 3px; }
QScrollBar::handle:horizontal { min-width: 30px; margin: 3px 2px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)qss");

	const std::pair<QString, QColor> colors[] = {
		{ QStringLiteral("@winBg@"), t.winBg },
		{ QStringLiteral("@pane@"), t.pane },
		{ QStringLiteral("@paneAlt@"), t.paneAlt },
		{ QStringLiteral("@border@"), t.border },
		{ QStringLiteral("@borderSoft@"), t.borderSoft },
		{ QStringLiteral("@text@"), t.text },
		{ QStringLiteral("@dim@"), t.dim },
		{ QStringLiteral("@accent@"), t.accent },
		{ QStringLiteral("@accentFg@"), t.accentFg },
		{ QStringLiteral("@sel@"), t.sel },
		{ QStringLiteral("@btn@"), t.btn },
		{ QStringLiteral("@btnBorder@"), t.btnBorder },
		{ QStringLiteral("@diffCtx@"), t.diffCtx },
		{ QStringLiteral("@btnHover@"), hoverShade(t.btn) },
		{ QStringLiteral("@btnPressed@"), pressedShade(t.btn) },
		{ QStringLiteral("@accentHover@"), hoverShade(t.accent) },
		{ QStringLiteral("@accentPressed@"), pressedShade(t.accent) },
	};
	for (const auto& [token, color] : colors)
		qss.replace(token, color.name(QColor::HexRgb));
	qss.replace(QStringLiteral("@mode@"), s_darkTheme ? QStringLiteral("dark") : QStringLiteral("light"));
	return qss;
}

} // namespace

QColor Theme::blockedRowTint() const
{
	QColor tint = warnBg;
	tint.setAlpha(160);
	return tint;
}

const Theme& activeTheme()
{
	static const Theme light{
		.winBg = c(0xf3f3f3), .pane = c(0xffffff), .paneAlt = c(0xfafafa),
		.border = c(0xd0d3d8), .borderSoft = c(0xe3e5e9),
		.text = c(0x16181c), .dim = c(0x6b7280),
		.accent = c(0x0d6bc4), .accentFg = c(0xffffff),
		.sel = c(0xd6e8fb), .btn = c(0xfdfdfd), .btnBorder = c(0xc2c6cc),
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
		.winBg = c(0x1f2227), .pane = c(0x191c21), .paneAlt = c(0x1c1f24),
		.border = c(0x33383f), .borderSoft = c(0x282c32),
		.text = c(0xe6e8ec), .dim = c(0x98a0ab),
		.accent = c(0x3b8fe0), .accentFg = c(0x06121f),
		.sel = c(0x1e3c58), .btn = c(0x262a30), .btnBorder = c(0x3c424a),
		.warnBg = c(0x3a3013), .warnFg = c(0xd4b352),
		.errBg = c(0x3a1d1c), .errFg = c(0xf0a79c),
		.stModified = c(0x6cb0f0), .stAdded = c(0x62c98a), .stUntracked = c(0xd4b352),
		.stDeleted = c(0xef8c82), .stRenamed = c(0xb394ef), .stSubmodule = c(0xdda45c),
		.diffAddBg = c(0x16341f), .diffAddFg = c(0x8fdca8),
		.diffDelBg = c(0x3a1d1c), .diffDelFg = c(0xf0a79c),
		.diffHunk = c(0x9b8fe0), .diffCtx = c(0xc9ced6),
		.graphLanes = { c(0x6cb0f0), c(0x62c98a), c(0xdda45c), c(0xb394ef), c(0x5ec8c8), c(0xef8c82) },
	};
	return s_darkTheme ? dark : light;
}

QFont monospaceFont()
{
	return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}

void applyTheme(QApplication& app)
{
	// Decided from the pre-override system palette; everything after this reads activeTheme()
	s_darkTheme = app.palette().color(QPalette::Base).lightness() < 128;
	const Theme& t = activeTheme();

	QPalette p = app.palette();
	p.setColor(QPalette::Window, t.winBg);
	p.setColor(QPalette::WindowText, t.text);
	p.setColor(QPalette::Base, t.pane);
	p.setColor(QPalette::AlternateBase, t.paneAlt);
	p.setColor(QPalette::Text, t.text);
	p.setColor(QPalette::Button, t.btn);
	p.setColor(QPalette::ButtonText, t.text);
	p.setColor(QPalette::Highlight, t.sel);
	p.setColor(QPalette::HighlightedText, t.text);
	p.setColor(QPalette::PlaceholderText, t.dim);
	p.setColor(QPalette::Link, t.accent);
	p.setColor(QPalette::ToolTipBase, t.pane);
	p.setColor(QPalette::ToolTipText, t.text);
	p.setColor(QPalette::Disabled, QPalette::WindowText, t.dim);
	p.setColor(QPalette::Disabled, QPalette::Text, t.dim);
	p.setColor(QPalette::Disabled, QPalette::ButtonText, t.dim);
	app.setPalette(p);

	app.setStyleSheet(buildStyleSheet(t));
}
