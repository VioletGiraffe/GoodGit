#include "fileicons.h"

#ifdef Q_OS_MACOS
#include "fileicons_mac.h"
#endif

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHash>
#include <QPixmap>
RESTORE_COMPILER_WARNINGS

#include <array>

namespace {

// The sizes the Windows shell renders natively (small, large, extra large); other device pixel ratios scale the nearest
constexpr std::array IconPixelSizes{ 16, 32, 48 };

[[nodiscard]] QPixmap fileTypePixmap(const QFileInfo& file, int pixelSize)
{
#ifdef Q_OS_MACOS
	return macFileTypePixmap(file.suffix(), pixelSize);
#else
	// For a path that does not exist Qt queries by type: the Windows shell by extension with no overlays, the MIME
	// database by name elsewhere. The directory is never created.
	static const QDir lookupDirectory{ QDir::temp().filePath(QStringLiteral("goodgit-icon-lookup")) };
	const QIcon icon = QFileIconProvider{}.icon(QFileInfo{ lookupDirectory.filePath(file.fileName()) });
	return icon.pixmap(QSize{ pixelSize, pixelSize }, 1.0);
#endif
}

} // namespace

QIcon fileTypeIcon(const QString& path)
{
	const QFileInfo file{ path };
	const QString suffix = file.suffix();
	// A name without an extension is its own type ("Makefile"); the dot keeps the two key spaces apart
	const QString typeKey = suffix.isEmpty() ? file.fileName() : QLatin1Char('.') + suffix;

	static QHash<QString, QIcon> iconsByType;
	if (const auto it = iconsByType.constFind(typeKey); it != iconsByType.cend())
		return *it;

	// Rendered up front: Qt's file icon engines do not cache a path that does not exist, and would query the system per paint
	QIcon icon;
	for (const int pixelSize : IconPixelSizes)
	{
		if (const QPixmap pixmap = fileTypePixmap(file, pixelSize); !pixmap.isNull())
			icon.addPixmap(pixmap);
	}
	return *iconsByType.insert(typeKey, icon);
}
