#include "theme.h"
#include "settings.h"
#include "stylesheet.h"

#include "assert/advanced_assert.h"
#include "settings/csettings.h"
#include "theme/cthemecontroller.h"
#include "theme/cthemeiconhandler.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {

Theme s_active;

QColor c(QRgb rgb) { return QColor::fromRgb(rgb); }

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

void applyTheme(QApplication& app)
{
	// Serves the tinted QSS glyphs; must exist before any stylesheet references themeicon:/ URLs
	// and for the application's whole lifetime.
	static const CThemeIconHandler iconHandler{ QStringLiteral(":/theme") };

	QObject::connect(&CThemeController::instance(), &CThemeController::themeChanged, &app, &applyActiveTheme);
	applyActiveTheme();
}

const Theme& activeTheme()
{
	assert_debug_only(!s_active.name.isEmpty()); // applyTheme() has not run yet
	return s_active;
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

QFont monospaceFont()
{
	const CSettings settings;
	const QString family = settings.value(Settings::MonospaceFontFamilyKey).toString();
	if (family.isEmpty()) // no override stored
		return QFontDatabase::systemFont(QFontDatabase::FixedFont);

	QFont font{ family };
	if (const int pointSize = settings.value(Settings::MonospaceFontPointSizeKey).toInt(); pointSize > 0)
		font.setPointSize(pointSize);
	return font;
}
