#include "fileicons_mac.h"

DISABLE_COMPILER_WARNINGS
#include <QImage>
RESTORE_COMPILER_WARNINGS

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <utility>

QPixmap macFileTypePixmap(const QString& extension, int pixelSize)
{
	@autoreleasepool
	{
		// No extension, or one no installed application declares: the generic document icon
		UTType* type = extension.isEmpty() ? nil : [UTType typeWithFilenameExtension:extension.toNSString()];
		NSImage* icon = [NSWorkspace.sharedWorkspace iconForContentType:type ? type : UTTypeData];

		QImage image{ pixelSize, pixelSize, QImage::Format_ARGB32_Premultiplied };
		image.fill(Qt::transparent);

		// The memory layout of Format_ARGB32_Premultiplied. Unflipped: a bitmap context's first row is the image's top.
		const CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		const CGContextRef context = CGBitmapContextCreate(image.bits(), size_t(pixelSize), size_t(pixelSize), 8, size_t(image.bytesPerLine()),
			colorSpace, CGBitmapInfo(uint32_t(kCGImageAlphaPremultipliedFirst) | uint32_t(kCGBitmapByteOrder32Host)));
		CGColorSpaceRelease(colorSpace);
		if (!context)
			return {};

		[NSGraphicsContext saveGraphicsState];
		NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithCGContext:context flipped:NO];
		[icon drawInRect:NSMakeRect(0, 0, pixelSize, pixelSize) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
		[NSGraphicsContext restoreGraphicsState];
		CGContextRelease(context);

		return QPixmap::fromImage(std::move(image));
	}
}
