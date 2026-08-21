#include "theme.h"
#include "settings.h"
#include "stylesheet.h"

#include "assert/advanced_assert.h"
#include "settings/csettings.h"
#include "theme/cthemecontroller.h"
#include "theme/cthemeiconhandler.h"
#include "theme/ctintedsvgiconengine.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {

Theme s_active;

inline QColor c(QRgb rgb) { return QColor::fromRgb(rgb); }

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

QIcon submoduleIcon()
{
	// One cached instance suffices: the engine resolves the tint per render, so it follows theme switches
	static const QIcon icon = tintedSvgIcon(QStringLiteral(":/theme/folder.svg"), [] { return activeTheme().stSubmodule; });
	return icon;
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
	// The order matters: the first theme of each polarity is the default - what a fresh install
	// resolves to and what the settings combo shows for an unset selection.
	static const Theme classicLight{
		.name = "Classic",
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

	static const Theme classicDark{
		.name = "Classic",
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

	// Warm cream chrome, amber accent. The accent fails as text on light surfaces, hence the
	// authored accentText.
	static const Theme honey{
		.name = "Honey",
		.dark = false,
		.palette = {
			.windowBg = c(0xf7f0dd), .surface = c(0xfffdf4), .surfaceAlt = c(0xfbf6e7),
			.text = c(0x221c0c), .textDim = c(0x857a58),
			.button = c(0xfdf9ec),
			.accent = c(0xe8a013),
			.selectionBg = c(0xf5e6bb), .selectionFg = c(0x221c0c),
			.border = c(0xdfd3ae), .borderSubtle = c(0xefe7cd),
			.accentFg = c(0x241a00), .accentText = c(0x8a6600),
			.buttonBorder = c(0xd4c79b),
		},
		.warnBg = c(0xffe6c2), .warnFg = c(0x7a4a00),
		.errBg = c(0xfce3da), .errFg = c(0x96140c),
		.stModified = c(0x1668c4), .stAdded = c(0x0d9c3c), .stUntracked = c(0x0c7d84),
		.stDeleted = c(0xdd2418), .stRenamed = c(0x7345c0), .stSubmodule = c(0xa15c00),
		.diffAddBg = c(0xd1f2cf), .diffAddFg = c(0x07561f),
		.diffDelBg = c(0xfcd9d2), .diffDelFg = c(0x96140c),
		.diffHunk = c(0x6a5fb0), .diffCtx = c(0x322b18),
		.graphLanes = { c(0x1668c4), c(0x0d9c3c), c(0xa15c00), c(0x7345c0), c(0x0c7d84), c(0xdd2418) },
	};

	// Near-black violet chrome, golden yellow accent. The diff tints sit a step above the surface on
	// purpose: the brightness lives in the diff text, not the bands.
	static const Theme blackoutViolet{
		.name = "Blackout Violet",
		.dark = true,
		.palette = {
			.windowBg = c(0x100a17), .surface = c(0x09060c), .surfaceAlt = c(0x0d0814),
			.text = c(0xe9e3f2), .textDim = c(0x9488a8),
			.button = c(0x1a1128),
			.accent = c(0xffc226),
			.selectionBg = c(0x372d0d), .selectionFg = c(0xe9e3f2),
			.border = c(0x241a36), .borderSubtle = c(0x170f24),
			.accentFg = c(0x1f1800),
			.buttonBorder = c(0x322447),
		},
		.warnBg = c(0x46280e), .warnFg = c(0xe8a458),
		.errBg = c(0x401412), .errFg = c(0xff8d80),
		.stModified = c(0x6cb0f0), .stAdded = c(0x38e07c), .stUntracked = c(0x45c9c0),
		.stDeleted = c(0xff6a5c), .stRenamed = c(0xb394ef), .stSubmodule = c(0xdda45c),
		.diffAddBg = c(0x07150b), .diffAddFg = c(0x52ec92),
		.diffDelBg = c(0x1b0a09), .diffDelFg = c(0xff8d80),
		.diffHunk = c(0xa795e8), .diffCtx = c(0xcec7dc),
		.graphLanes = { c(0x6cb0f0), c(0x38e07c), c(0xdda45c), c(0xb394ef), c(0x45c9c0), c(0xff6a5c) },
	};

	// Warm-gray paper, taxicab-yellow accent - the same fill/text split as Honey, one step louder.
	static const Theme taxicabLight{
		.name = "Taxicab",
		.dark = false,
		.palette = {
			.windowBg = c(0xf4f2ec), .surface = c(0xfffef9), .surfaceAlt = c(0xfbf9f2),
			.text = c(0x1c1a12), .textDim = c(0x7c7866),
			.button = c(0xfbf9f2),
			.accent = c(0xf5c518),
			.selectionBg = c(0xf7ecc0), .selectionFg = c(0x1c1a12),
			.border = c(0xd9d4c4), .borderSubtle = c(0xece8da),
			.accentFg = c(0x221b00), .accentText = c(0x8a6d00),
			.buttonBorder = c(0xcfc9b6),
		},
		.warnBg = c(0xffe4c0), .warnFg = c(0x7a4a00),
		.errBg = c(0xfce3da), .errFg = c(0x96140c),
		.stModified = c(0x1668c4), .stAdded = c(0x0d9c3c), .stUntracked = c(0x0c7d84),
		.stDeleted = c(0xdd2418), .stRenamed = c(0x7345c0), .stSubmodule = c(0xa15c00),
		.diffAddBg = c(0xd1f2cf), .diffAddFg = c(0x07561f),
		.diffDelBg = c(0xfcd9d2), .diffDelFg = c(0x96140c),
		.diffHunk = c(0x6a5fb0), .diffCtx = c(0x2a2820),
		.graphLanes = { c(0x1668c4), c(0x0d9c3c), c(0xa15c00), c(0x7345c0), c(0x0c7d84), c(0xdd2418) },
	};

	// Neutral ink chrome, bright taxicab yellow.
	static const Theme taxicabDark{
		.name = "Taxicab",
		.dark = true,
		.palette = {
			.windowBg = c(0x1c1c1a), .surface = c(0x151514), .surfaceAlt = c(0x1a1a18),
			.text = c(0xe9e7e0), .textDim = c(0x9c9a8f),
			.button = c(0x26251f),
			.accent = c(0xf8ce1c),
			.selectionBg = c(0x3f3a10), .selectionFg = c(0xe9e7e0),
			.border = c(0x3a392f), .borderSubtle = c(0x262620),
			.accentFg = c(0x1f1800),
			.buttonBorder = c(0x3f3e33),
		},
		.warnBg = c(0x44290f), .warnFg = c(0xe8a458),
		.errBg = c(0x3d1815), .errFg = c(0xff8d80),
		.stModified = c(0x6cb0f0), .stAdded = c(0x38e07c), .stUntracked = c(0x45c9c0),
		.stDeleted = c(0xff6a5c), .stRenamed = c(0xb394ef), .stSubmodule = c(0xdda45c),
		.diffAddBg = c(0x122718), .diffAddFg = c(0x52ec92),
		.diffDelBg = c(0x2f1412), .diffDelFg = c(0xff8d80),
		.diffHunk = c(0x9b8fe0), .diffCtx = c(0xccc9c0),
		.graphLanes = { c(0x6cb0f0), c(0x38e07c), c(0xdda45c), c(0xb394ef), c(0x45c9c0), c(0xff6a5c) },
	};

	// Navy chrome, hot orange accent. Dark only: the light rendition reads as stock Ubuntu.
	static const Theme forge{
		.name = "Forge",
		.dark = true,
		.palette = {
			.windowBg = c(0x1a2233), .surface = c(0x141b29), .surfaceAlt = c(0x172030),
			.text = c(0xe4e8f0), .textDim = c(0x8e9ab2),
			.button = c(0x232d42),
			.accent = c(0xf2683f),
			.selectionBg = c(0x3d2a20), .selectionFg = c(0xe4e8f0),
			.border = c(0x303d56), .borderSubtle = c(0x222c40),
			.accentFg = c(0xffffff),
			.buttonBorder = c(0x3a4763),
		},
		.warnBg = c(0x3a3116), .warnFg = c(0xd8b45a),
		.errBg = c(0x3f1b1a), .errFg = c(0xf79a8d),
		.stModified = c(0x6cb0f0), .stAdded = c(0x38e07c), .stUntracked = c(0x45c9c0),
		.stDeleted = c(0xff6a5c), .stRenamed = c(0xb394ef), .stSubmodule = c(0xdda45c),
		.diffAddBg = c(0x1b3324), .diffAddFg = c(0x52ec92),
		.diffDelBg = c(0x3f2023), .diffDelFg = c(0xff8d80),
		.diffHunk = c(0x9f93e8), .diffCtx = c(0xcbd2e0),
		.graphLanes = { c(0x6cb0f0), c(0x38e07c), c(0xdda45c), c(0xb394ef), c(0x45c9c0), c(0xff6a5c) },
	};

	// Teal patina on warm sand / dark bronze. The teal accent frees amber for untracked here.
	static const Theme verdigrisLight{
		.name = "Verdigris",
		.dark = false,
		.palette = {
			.windowBg = c(0xf3ead9), .surface = c(0xfffcf5), .surfaceAlt = c(0xfaf4e6),
			.text = c(0x231e14), .textDim = c(0x8a7d63),
			.button = c(0xfaf4e6),
			.accent = c(0x0d8577),
			.selectionBg = c(0xd6ebde), .selectionFg = c(0x231e14),
			.border = c(0xddd0b8), .borderSubtle = c(0xeee5d2),
			.accentFg = c(0xffffff),
			.buttonBorder = c(0xd5c8a8),
		},
		.warnBg = c(0xffe3bd), .warnFg = c(0x7a4a00),
		.errBg = c(0xfbe2d9), .errFg = c(0x96140c),
		.stModified = c(0x1668c4), .stAdded = c(0x0d9c3c), .stUntracked = c(0x8a6a08),
		.stDeleted = c(0xdd2418), .stRenamed = c(0x7345c0), .stSubmodule = c(0xa15c00),
		.diffAddBg = c(0xdff0d5), .diffAddFg = c(0x07561f),
		.diffDelBg = c(0xf8e2da), .diffDelFg = c(0x96140c),
		.diffHunk = c(0x6a5fb0), .diffCtx = c(0x2f2a1e),
		.graphLanes = { c(0x1668c4), c(0x0d9c3c), c(0xa15c00), c(0x7345c0), c(0x0c7d84), c(0xdd2418) },
	};

	static const Theme verdigrisDark{
		.name = "Verdigris",
		.dark = true,
		.palette = {
			.windowBg = c(0x262019), .surface = c(0x1d1813), .surfaceAlt = c(0x221c15),
			.text = c(0xece6da), .textDim = c(0xa89c88),
			.button = c(0x322a20),
			.accent = c(0x2fbfa4),
			.selectionBg = c(0x14453a), .selectionFg = c(0xece6da),
			.border = c(0x453a2c), .borderSubtle = c(0x2e261d),
			.accentFg = c(0x03231c),
			.buttonBorder = c(0x4a3e2e),
		},
		.warnBg = c(0x46300f), .warnFg = c(0xe0ac55),
		.errBg = c(0x401a15), .errFg = c(0xff8d80),
		.stModified = c(0x6cb0f0), .stAdded = c(0x38e07c), .stUntracked = c(0xd4b352),
		.stDeleted = c(0xff6a5c), .stRenamed = c(0xb394ef), .stSubmodule = c(0xdda45c),
		.diffAddBg = c(0x14301d), .diffAddFg = c(0x52ec92),
		.diffDelBg = c(0x3a1c17), .diffDelFg = c(0xff8d80),
		.diffHunk = c(0x9b8fe0), .diffCtx = c(0xd6cfc2),
		.graphLanes = { c(0x6cb0f0), c(0x38e07c), c(0xdda45c), c(0xb394ef), c(0x45c9c0), c(0xff6a5c) },
	};

	// Chartreuse signal in green woods. In light mode the accent splits like Taxicab's:
	// bright fills, olive accentText.
	static const Theme fireflyLight{
		.name = "Firefly",
		.dark = false,
		.palette = {
			.windowBg = c(0xeff3e8), .surface = c(0xfdfff7), .surfaceAlt = c(0xf7faee),
			.text = c(0x181d10), .textDim = c(0x717d64),
			.button = c(0xf7faee),
			.accent = c(0xc8dc28),
			.selectionBg = c(0xeef2bd), .selectionFg = c(0x181d10),
			.border = c(0xd3dcc2), .borderSubtle = c(0xe5ecd6),
			.accentFg = c(0x1c2000), .accentText = c(0x5f7000),
			.buttonBorder = c(0xc8d3b2),
		},
		.warnBg = c(0xffe4c0), .warnFg = c(0x7a4a00),
		.errBg = c(0xfce3da), .errFg = c(0x96140c),
		.stModified = c(0x1668c4), .stAdded = c(0x0d9c3c), .stUntracked = c(0x0c7d84),
		.stDeleted = c(0xdd2418), .stRenamed = c(0x7345c0), .stSubmodule = c(0xa15c00),
		.diffAddBg = c(0xe3f0cc), .diffAddFg = c(0x07561f),
		.diffDelBg = c(0xf8e3da), .diffDelFg = c(0x96140c),
		.diffHunk = c(0x6a5fb0), .diffCtx = c(0x2a2f1e),
		.graphLanes = { c(0x1668c4), c(0x0d9c3c), c(0xa15c00), c(0x7345c0), c(0x0c7d84), c(0xdd2418) },
	};

	static const Theme fireflyDark{
		.name = "Firefly",
		.dark = true,
		.palette = {
			.windowBg = c(0x1c2418), .surface = c(0x161d13), .surfaceAlt = c(0x192015),
			.text = c(0xe6ebe1), .textDim = c(0x93a189),
			.button = c(0x242e1e),
			.accent = c(0xc8e224),
			.selectionBg = c(0x39420f), .selectionFg = c(0xe6ebe1),
			.border = c(0x33402c), .borderSubtle = c(0x232c1d),
			.accentFg = c(0x1c2000),
			.buttonBorder = c(0x44543a),
		},
		.warnBg = c(0x44290f), .warnFg = c(0xe8a458),
		.errBg = c(0x3d1815), .errFg = c(0xff8d80),
		.stModified = c(0x6cb0f0), .stAdded = c(0x38e07c), .stUntracked = c(0x45c9c0),
		.stDeleted = c(0xff6a5c), .stRenamed = c(0xb394ef), .stSubmodule = c(0xdda45c),
		.diffAddBg = c(0x1e3322), .diffAddFg = c(0x52ec92),
		.diffDelBg = c(0x3e2320), .diffDelFg = c(0xff8d80),
		.diffHunk = c(0x9b8fe0), .diffCtx = c(0xccd3c6),
		.graphLanes = { c(0x6cb0f0), c(0x38e07c), c(0xdda45c), c(0xb394ef), c(0x45c9c0), c(0xff6a5c) },
	};

	// Sunset orange on plum. Dark only, like Forge.
	static const Theme afterglow{
		.name = "Afterglow",
		.dark = true,
		.palette = {
			.windowBg = c(0x261b28), .surface = c(0x1e1420), .surfaceAlt = c(0x221824),
			.text = c(0xece4ee), .textDim = c(0xa794ab),
			.button = c(0x332338),
			.accent = c(0xf2683f),
			.selectionBg = c(0x472823), .selectionFg = c(0xece4ee),
			.border = c(0x443048), .borderSubtle = c(0x2e2031),
			.accentFg = c(0xffffff),
			.buttonBorder = c(0x4a3650),
		},
		.warnBg = c(0x3e2f12), .warnFg = c(0xd8b45a),
		.errBg = c(0x421a1c), .errFg = c(0xf79a8d),
		.stModified = c(0x6cb0f0), .stAdded = c(0x38e07c), .stUntracked = c(0x45c9c0),
		.stDeleted = c(0xff6a5c), .stRenamed = c(0xb394ef), .stSubmodule = c(0xdda45c),
		.diffAddBg = c(0x243325), .diffAddFg = c(0x52ec92),
		.diffDelBg = c(0x43211f), .diffDelFg = c(0xff8d80),
		.diffHunk = c(0xa795e8), .diffCtx = c(0xd5cdd8),
		.graphLanes = { c(0x6cb0f0), c(0x38e07c), c(0xdda45c), c(0xb394ef), c(0x45c9c0), c(0xff6a5c) },
	};

	static const std::vector<Theme> themes{ honey, blackoutViolet, classicLight, classicDark,
		taxicabLight, taxicabDark, forge, verdigrisLight, verdigrisDark, fireflyLight, fireflyDark, afterglow };
	return themes;
}
