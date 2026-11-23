/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 *
 * File and clipboard code adapted from EasyPaint for Haiku (MIT).
 */
#include "ImageIO.h"

#include <BitmapStream.h>
#include <Clipboard.h>
#include <DataIO.h>
#include <Directory.h>
#include <File.h>
#include <Message.h>
#include <Mime.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <TranslationUtils.h>
#include <TranslatorRoster.h>

#include <new>
#include <string.h>
#include <strings.h>



namespace ImageIO {

BBitmap*
ToBitmap(const pp::Raster& raster, uint32 flags)
{
	BBitmap* bitmap = new BBitmap(BRect(0, 0, raster.Width() - 1,
		raster.Height() - 1), flags, B_RGBA32);
	if (bitmap->InitCheck() != B_OK) {
		delete bitmap;
		return NULL;
	}
	uint8* bits = (uint8*)bitmap->Bits();
	for (int32 y = 0; y < raster.Height(); y++) {
		memcpy(bits + y * bitmap->BytesPerRow(),
			raster.Bits() + (size_t)y * raster.Width(),
			raster.Width() * sizeof(pp::Pixel));
	}
	return bitmap;
}


bool
FromBitmap(const BBitmap* source, pp::Raster& raster)
{
	if (source == NULL || !source->IsValid())
		return false;
	BRect bounds = source->Bounds();
	int32 width = bounds.IntegerWidth() + 1;
	int32 height = bounds.IntegerHeight() + 1;

	const BBitmap* rgba = source;
	BBitmap* converted = NULL;
	color_space space = source->ColorSpace();
	if (space != B_RGBA32) {
		converted = new BBitmap(BRect(0, 0, width - 1, height - 1), 0,
			B_RGBA32);
		if (converted->InitCheck() != B_OK
			|| converted->ImportBits(source) != B_OK) {
			delete converted;
			return false;
		}
		rgba = converted;
	}

	raster = pp::Raster(width, height);
	const uint8* bits = (const uint8*)rgba->Bits();
	for (int32 y = 0; y < height; y++) {
		memcpy(raster.Bits() + (size_t)y * width,
			bits + y * rgba->BytesPerRow(), width * sizeof(pp::Pixel));
	}
	// These carry no alpha, and the alpha byte of B_RGB32 is undefined.
	if (space == B_RGB32 || space == B_RGB24 || space == B_RGB16
		|| space == B_RGB15 || space == B_GRAY8 || space == B_GRAY1
		|| space == B_CMAP8) {
		pp::Pixel* p = raster.Bits();
		for (size_t i = 0; i < (size_t)width * height; i++)
			p[i] |= 0xff000000;
	}
	delete converted;
	return true;
}


bool
Load(const entry_ref& ref, pp::Raster& raster)
{
	BBitmap* translated = BTranslationUtils::GetBitmap(&ref);
	bool ok = false;
	try {
		ok = FromBitmap(translated, raster);
	} catch (const std::bad_alloc&) {
		// Over pp::kMaxPixels; this also runs in the app thread, where
		// nothing else would catch it.
	}
	delete translated;
	return ok;
}


BString
LeafName(const entry_ref& ref)
{
	char name[B_FILE_NAME_LENGTH];
	BEntry entry(&ref);
	if (entry.GetName(name) == B_OK)
		return BString(name);
	const char* slash = strrchr(ref.name, '/');
	return BString(slash != NULL ? slash + 1 : ref.name);
}


static const struct {
	const char*	extension;
	const char*	mimeType;
} kFormats[] = {
	{ "png", "image/png" },
	{ "jpg", "image/jpeg" },
	{ "jpeg", "image/jpeg" },
	{ "bmp", "image/bmp" },
	{ "gif", "image/gif" },
	{ "tif", "image/tiff" },
	{ "tiff", "image/tiff" },
	{ "tga", "image/x-targa" },
	{ "webp", "image/webp" },
	{ "ppm", "image/x-portable-pixmap" },
	{ "avif", "image/avif" },
	{ "jp2", "image/jp2" },
	{ "ico", "image/vnd.microsoft.icon" },
	{ "psd", "image/vnd.adobe.photoshop" },
};


// Finds a translator that writes bitmaps as mimeType.
static bool
FindWriter(const char* mimeType, translator_id& translator, uint32& type)
{
	BTranslatorRoster* roster = BTranslatorRoster::Default();
	translator_id* ids = NULL;
	int32 count = 0;
	if (roster->GetAllTranslators(&ids, &count) != B_OK)
		return false;

	bool found = false;
	for (int32 i = 0; i < count && !found; i++) {
		const translation_format* formats = NULL;
		int32 formatCount = 0;
		if (roster->GetOutputFormats(ids[i], &formats, &formatCount) != B_OK)
			continue;
		for (int32 j = 0; j < formatCount; j++) {
			if (formats[j].group == B_TRANSLATOR_BITMAP
				&& formats[j].type != B_TRANSLATOR_BITMAP
				&& strcasecmp(formats[j].MIME, mimeType) == 0) {
				translator = ids[i];
				type = formats[j].type;
				found = true;
				break;
			}
		}
	}
	delete[] ids;
	return found;
}


static const char*
MimeTypeFor(const entry_ref& directory, const char* name)
{
	// An existing image keeps its type, even without an extension.
	BEntry entry;
	BDirectory dir(&directory);
	if (entry.SetTo(&dir, name) == B_OK && entry.Exists()) {
		BNode node(&entry);
		BNodeInfo info(&node);
		static char type[B_MIME_TYPE_LENGTH];
		translator_id translator;
		uint32 format;
		if (info.GetType(type) == B_OK && strncmp(type, "image/", 6) == 0
			&& FindWriter(type, translator, format)) {
			return type;
		}
	}

	const char* dot = strrchr(name, '.');
	if (dot != NULL) {
		for (const auto& format : kFormats) {
			if (strcasecmp(dot + 1, format.extension) == 0)
				return format.mimeType;
		}
	}
	return "image/png";
}


status_t
Save(const pp::Raster& raster, const entry_ref& directory, const char* name,
	BString* error)
{
	const char* mimeType = MimeTypeFor(directory, name);
	translator_id translator;
	uint32 type;
	if (!FindWriter(mimeType, translator, type)) {
		// Like the original: an unknown format is saved as PNG.
		mimeType = "image/png";
		if (!FindWriter(mimeType, translator, type)) {
			if (error != NULL)
				error->SetTo("No translator can write PNG images.");
			return B_NOT_SUPPORTED;
		}
	}

	// Written next to the file and renamed over it at the end, so a failed
	// save leaves the existing picture as it was.
	BDirectory dir(&directory);
	BString tempName;
	tempName.SetToFormat(".%s.powerpaint-tmp", name);
	BFile file;
	status_t status = dir.CreateFile(tempName, &file, false);
	if (status == B_OK) {
		BBitmap* copy = ToBitmap(raster);
		if (copy == NULL)
			status = B_NO_MEMORY;
		else {
			// The stream deletes the copy.
			BBitmapStream stream(copy);
			status = BTranslatorRoster::Default()->Translate(translator,
				&stream, NULL, &file, type);
		}
	}
	if (status == B_OK) {
		file.SetSize(file.Position());
		BNodeInfo info(&file);
		info.SetType(mimeType);
		status = file.Sync();
	}
	file.Unset();
	BEntry temp(&dir, tempName);
	if (status == B_OK)
		status = temp.Rename(name, true);
	if (status != B_OK) {
		temp.Remove();
		if (error != NULL)
			error->SetTo(strerror(status));
	}
	return status;
}


status_t
CopyToClipboard(const pp::Raster& raster)
{
	BBitmap* bitmap = ToBitmap(raster);
	if (bitmap == NULL)
		return B_NO_MEMORY;
	if (!be_clipboard->Lock()) {
		delete bitmap;
		return B_ERROR;
	}

	be_clipboard->Clear();
	BMessage* clip = be_clipboard->Data();
	BMessage archive;
	status_t status = bitmap->Archive(&archive);
	if (status == B_OK) {
		// The same layout as ShowImage's "Copy".
		clip->AddMessage("image/bitmap", &archive);
		clip->AddRect("rect", bitmap->Bounds());
		status = be_clipboard->Commit();
	}
	be_clipboard->Unlock();
	delete bitmap;
	return status;
}


bool
PasteFromClipboard(pp::Raster& raster)
{
	if (!be_clipboard->Lock())
		return false;

	bool ok = false;
	BMessage* clip = be_clipboard->Data();
	BMessage archive;
	if (clip != NULL && clip->FindMessage("image/bitmap", &archive) == B_OK) {
		BBitmap* archived = new BBitmap(&archive);
		if (archived->InitCheck() == B_OK)
			ok = FromBitmap(archived, raster);
		delete archived;
	}

	// Raw image data under its MIME type, as some apps put it there.
	if (!ok && clip != NULL) {
		char* name;
		type_code type;
		for (int32 i = 0; clip->GetInfo(B_MIME_TYPE, i, &name, &type) == B_OK;
				i++) {
			if (strncmp(name, "image/", 6) != 0)
				continue;
			const void* data;
			ssize_t size;
			if (clip->FindData(name, B_MIME_TYPE, &data, &size) != B_OK)
				continue;
			BMemoryIO io(data, size);
			BBitmap* translated = BTranslationUtils::GetBitmap(&io);
			ok = FromBitmap(translated, raster);
			delete translated;
			if (ok)
				break;
		}
	}

	be_clipboard->Unlock();
	return ok;
}


}	// namespace ImageIO
