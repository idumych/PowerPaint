/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 *
 * The colour box along the bottom: the current foreground and background
 * colours, then the palette in two rows of 16 x 16 swatches. The primary
 * button picks the foreground colour, the secondary the background, and a
 * double click edits the swatch.
 */
#ifndef COLOR_BOX_H
#define COLOR_BOX_H

#include <View.h>

#include "App.h"


class ColorBox : public BView {
public:
							ColorBox();

	virtual void			Draw(BRect updateRect);
	virtual void			MouseDown(BPoint where);
	virtual BSize			MinSize();
	virtual BSize			MaxSize();

	void					StateChanged();

private:
	BRect					_CurrentColorsFrame() const;
	BRect					_SwatchRect(int32 index) const;
	void					_DrawSwatch(BRect rect, pp::Pixel color);

	PaintState				fState;
};

#endif	// COLOR_BOX_H
