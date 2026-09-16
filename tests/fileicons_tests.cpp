#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "fileicons.h"

DISABLE_COMPILER_WARNINGS
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <cstdlib>

namespace {

[[nodiscard]] QImage renderedImage(const QIcon& icon)
{
	constexpr int pixelSize = 48;
	return icon.pixmap(QSize{ pixelSize, pixelSize }, 1.0).toImage().convertToFormat(QImage::Format_ARGB32);
}

} // namespace

TEST_CASE("Files of one type share one icon, whatever their directory", "[fileicons]")
{
	CHECK(fileTypeIcon(QStringLiteral("src/main.cpp")).cacheKey() == fileTypeIcon(QStringLiteral("other/dir/widget.cpp")).cacheKey());
}

TEST_CASE("A file that does not exist still gets an icon", "[fileicons]")
{
	CHECK_FALSE(renderedImage(fileTypeIcon(QStringLiteral("no/such/directory/notes.txt"))).isNull());
}

// Linux is left out: what it shows depends on the installed icon theme, down to every type getting the generic icon
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)

namespace {

// Sum of the per-channel differences of two images of one size
[[nodiscard]] qint64 imageDistance(const QImage& left, const QImage& right)
{
	qint64 distance = 0;
	for (int y = 0; y < left.height(); ++y)
	{
		for (int x = 0; x < left.width(); ++x)
		{
			const QRgb a = left.pixel(x, y);
			const QRgb b = right.pixel(x, y);
			distance += std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b)) + std::abs(qAlpha(a) - qAlpha(b));
		}
	}
	return distance;
}

} // namespace

TEST_CASE("Two types get distinct icons, with neither file on disk", "[fileicons]")
{
	const QImage text = renderedImage(fileTypeIcon(QStringLiteral("no/such/directory/notes.txt")));
	const QImage archive = renderedImage(fileTypeIcon(QStringLiteral("no/such/directory/bundle.zip")));

	REQUIRE_FALSE(text.isNull());
	REQUIRE_FALSE(archive.isNull());
	CHECK(text != archive);
}

TEST_CASE("A type icon renders the way Qt renders that type for a file on disk, not mirrored", "[fileicons]")
{
	const QTemporaryDir directory;
	REQUIRE(directory.isValid());
	const QString referencePath = directory.filePath(QStringLiteral("reference.txt"));
	QFile referenceFile{ referencePath };
	REQUIRE(referenceFile.open(QIODevice::WriteOnly));
	referenceFile.close();

	// Qt's own lookup of an existing file draws it upright on every platform
	const QImage reference = renderedImage(QFileIconProvider{}.icon(QFileInfo{ referencePath }));
	const QImage rendered = renderedImage(fileTypeIcon(QStringLiteral("other.txt")));

	REQUIRE_FALSE(reference.isNull());
	REQUIRE(rendered.size() == reference.size());
	CHECK(imageDistance(rendered, reference) < imageDistance(rendered.flipped(Qt::Vertical), reference));
}

#endif
