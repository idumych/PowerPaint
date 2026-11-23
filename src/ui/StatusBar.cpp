/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 */
#include "StatusBar.h"

#include <algorithm>


// JS Paint's .status-coordinates width; the size field matches it.
static const float kFieldWidth = 114;


StatusBar::StatusBar()
	:
	BView("statusbar", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


BSize
StatusBar::MinSize()
{
	font_height fh;
	GetFontHeight(&fh);
	float height = std::max(21.0f, ceilf(fh.ascent + fh.descent) + 8);
	return BSize(3 * kFieldWidth, height - 1);
}


BSize
StatusBar::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, MinSize().height);
}


BRect
StatusBar::_Field(int32 index) const
{
	BRect bounds = Bounds().InsetByCopy(2, 2);
	// Room for the window's resize corner on the right.
	float right = bounds.right - 12;
	BRect size(right - kFieldWidth + 1, bounds.top, right, bounds.bottom);
	BRect coordinates = size.OffsetByCopy(-kFieldWidth - 2, 0);
	if (index == 2)
		return size;
	if (index == 1)
		return coordinates;
	return BRect(bounds.left, bounds.top, coordinates.left - 3, bounds.bottom);
}


void
StatusBar::_DrawField(BRect frame, const BString& text)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	SetHighColor(tint_color(base, B_DARKEN_2_TINT));
	StrokeLine(frame.LeftBottom(), frame.LeftTop());
	StrokeLine(frame.RightTop());
	SetHighColor(tint_color(base, B_LIGHTEN_MAX_TINT));
	StrokeLine(frame.RightBottom());
	StrokeLine(frame.LeftBottom());

	BString truncated = text;
	TruncateString(&truncated, B_TRUNCATE_END, frame.Width() - 6);
	font_height fh;
	GetFontHeight(&fh);
	SetHighUIColor(B_PANEL_TEXT_COLOR);
	SetLowColor(base);
	float baseline = floorf((frame.top + frame.bottom + fh.ascent
		- fh.descent) / 2) + 1;
	DrawString(truncated, BPoint(frame.left + 3, baseline));
}


void
StatusBar::Draw(BRect updateRect)
{
	SetHighUIColor(B_PANEL_BACKGROUND_COLOR);
	FillRect(updateRect);
	_DrawField(_Field(0), fText);
	_DrawField(_Field(1), fCoordinates);
	_DrawField(_Field(2), fSize);
}


void
StatusBar::SetText(const char* text)
{
	if (fText == text)
		return;
	fText = text;
	Invalidate(_Field(0));
}


void
StatusBar::SetCoordinates(const char* text)
{
	if (fCoordinates == text)
		return;
	fCoordinates = text;
	Invalidate(_Field(1));
}


void
StatusBar::SetSize(const char* text)
{
	if (fSize == text)
		return;
	fSize = text;
	Invalidate(_Field(2));
}
