/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 *
 * Images in and out: files through the Translation Kit and the system
 * clipboard, converted to and from pp::Raster.
 */
#ifndef IMAGE_IO_H
#define IMAGE_IO_H

#include <Bitmap.h>
#include <Entry.h>
#include <String.h>

#include "Raster.h"


namespace ImageIO {

// A B_RGBA32 bitmap of the raster, and back.
BBitmap*	ToBitmap(const pp::Raster& raster, uint32 flags = 0);
bool		FromBitmap(const BBitmap* bitmap, pp::Raster& raster);

bool		Load(const entry_ref& ref, pp::Raster& raster);
// Picks the format from the file's MIME type if it already exists and is
// an image, otherwise from the name's extension; PNG if neither is known.
status_t	Save(const pp::Raster& raster, const entry_ref& directory,
				const char* name, BString* error);
// The file's name. entry_ref::name is a full path on Cosmoe, so don't use it
// for display or to build paths.
BString		LeafName(const entry_ref& ref);

status_t	CopyToClipboard(const pp::Raster& raster);
bool		PasteFromClipboard(pp::Raster& raster);

}	// namespace ImageIO

#endif	// IMAGE_IO_H
