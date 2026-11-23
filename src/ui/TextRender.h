/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 *
 * The text tool's text, drawn without anti-aliasing as MS Paint draws it,
 * word-wrapped to the box.
 */
#ifndef TEXT_RENDER_H
#define TEXT_RENDER_H

#include <Font.h>
#include <String.h>

#include "App.h"
#include "Raster.h"


namespace TextRender {

BFont		FontFor(const PaintState& state);
// `height` is the box's height on input; it comes back grown to fit the
// text. Text pixels are the foreground colour, the rest the background
// colour, or clear in transparent mode.
pp::Raster	Render(const BString& text, int32 width, const PaintState& state,
				int32* height);

}	// namespace TextRender

#endif	// TEXT_RENDER_H
