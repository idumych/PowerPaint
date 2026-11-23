/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 */
#include "Cursors.h"

#include <Autolock.h>
#include <Bitmap.h>
#include <Locker.h>
#include <TranslationUtils.h>

#include "Constants.h"


namespace Cursors {

static BLocker sLock("cursors");
static BCursor* sCursors[kToolCount];
static BCursor* sCrossHair;


// Resource name and hot spot, as JS Paint gives them.
static const struct {
	const char*	name;
	float		x, y;
} kToolCursors[kToolCount] = {
	{ "cursor-precise", 16, 16 },			// free-form select
	{ "cursor-precise", 16, 16 },			// select
	{ NULL, 0, 0 },							// eraser: its outline shows
	{ "cursor-fill-bucket", 8, 22 },
	{ "cursor-eye-dropper", 9, 22 },
	{ "cursor-magnifier", 16, 16 },
	{ "cursor-pencil", 13, 23 },
	{ "cursor-precise-dotted", 16, 16 },	// brush
	{ "cursor-airbrush", 7, 22 },
	{ "cursor-precise", 16, 16 },			// text
	{ "cursor-precise", 16, 16 },			// line
	{ "cursor-precise", 16, 16 },			// curve
	{ "cursor-precise", 16, 16 },			// rectangle
	{ "cursor-precise", 16, 16 },			// polygon
	{ "cursor-precise", 16, 16 },			// ellipse
	{ "cursor-precise", 16, 16 },			// rounded rectangle
};


const BCursor*
ForTool(int32 tool)
{
	BAutolock locker(sLock);
	if (sCrossHair == NULL)
		sCrossHair = new BCursor(B_CURSOR_ID_CROSS_HAIR);
	if (tool < 0 || tool >= kToolCount || kToolCursors[tool].name == NULL)
		return sCrossHair;
	if (sCursors[tool] == NULL) {
		BBitmap* image = BTranslationUtils::GetBitmap('PNG ',
			kToolCursors[tool].name);
		if (image == NULL)
			return sCrossHair;
		// BCursor wants B_RGBA32.
		BBitmap rgba(image->Bounds(), B_RGBA32);
		rgba.ImportBits(image);
		delete image;
		sCursors[tool] = new BCursor(&rgba,
			BPoint(kToolCursors[tool].x, kToolCursors[tool].y));
	}
	return sCursors[tool];
}

}	// namespace Cursors
