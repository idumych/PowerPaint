/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 */
#include "ColorBox.h"

#include <Window.h>

#include "Constants.h"


static const float kHeight = 47;
static const float kLeft = 4;
static const float kSwatch = 16;


static rgb_color
ToColor(pp::Pixel p)
{
	return make_color(pp::Red(p), pp::Green(p), pp::Blue(p), 255);
}


ColorBox::ColorBox()
	:
	BView("colorbox", B_WILL_DRAW),
	fState(CurrentState())
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


BSize
ColorBox::MinSize()
{
	// MS Paint's 14 columns; swatches of a longer palette past the right
	// edge are cut off rather than widening the window past the screen.
	return BSize(kLeft + 30 + 14 * kSwatch + 4, kHeight - 1);
}


BSize
ColorBox::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, kHeight - 1);
}


BRect
ColorBox::_CurrentColorsFrame() const
{
	float top = floorf((kHeight - 32) / 2);
	return BRect(kLeft, top, kLeft + 29, top + 30);
}


BRect
ColorBox::_SwatchRect(int32 index) const
{
	int32 columns = (fState.palette.size() + 1) / 2;
	float left = _CurrentColorsFrame().right + 1;
	float top = floorf((kHeight - 32) / 2);
	float x = left + (index % columns) * kSwatch;
	float y = top + (index / columns) * kSwatch;
	return BRect(x, y, x + kSwatch - 1, y + kSwatch - 1);
}


// A sunken 16 x 16 well, as in MS Paint's classic look.
void
ColorBox::_DrawSwatch(BRect rect, pp::Pixel color)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	SetHighColor(tint_color(base, B_DARKEN_2_TINT));
	StrokeLine(rect.LeftBottom(), rect.LeftTop());
	StrokeLine(rect.RightTop());
	SetHighColor(tint_color(base, B_LIGHTEN_MAX_TINT));
	StrokeLine(rect.RightBottom());
	StrokeLine(rect.LeftBottom());
	rect.InsetBy(1, 1);
	SetHighColor(tint_color(base, B_DARKEN_4_TINT));
	StrokeLine(rect.LeftBottom(), rect.LeftTop());
	StrokeLine(rect.RightTop());
	SetHighColor(base);
	StrokeLine(rect.RightBottom());
	StrokeLine(rect.LeftBottom());
	rect.InsetBy(1, 1);
	SetHighColor(ToColor(color));
	FillRect(rect);
}


void
ColorBox::Draw(BRect updateRect)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);

	// The current colours: a sunken frame with a light dither, the
	// background colour behind and below the foreground colour.
	BRect frame = _CurrentColorsFrame();
	SetHighColor(tint_color(base, B_DARKEN_2_TINT));
	StrokeLine(frame.LeftBottom(), frame.LeftTop());
	StrokeLine(frame.RightTop());
	SetHighColor(tint_color(base, B_LIGHTEN_MAX_TINT));
	StrokeLine(frame.RightBottom());
	StrokeLine(frame.LeftBottom());
	SetHighColor(tint_color(base, B_LIGHTEN_MAX_TINT));
	SetLowColor(base);
	FillRect(frame.InsetByCopy(1, 1), B_MIXED_COLORS);

	BRect background(frame.left + 12, frame.top + 13, frame.left + 26,
		frame.top + 27);
	BRect foreground(frame.left + 4, frame.top + 4, frame.left + 18,
		frame.top + 18);
	for (BRect r : { background, foreground }) {
		SetHighColor(tint_color(base, B_LIGHTEN_MAX_TINT));
		StrokeRect(r);
		SetHighColor(tint_color(base, B_DARKEN_2_TINT));
		StrokeLine(r.LeftBottom(), r.LeftTop());
		StrokeLine(r.RightTop());
		SetHighColor(ToColor(r == foreground ? fState.foreground
			: fState.background));
		FillRect(r.InsetByCopy(1, 1));
	}

	for (size_t i = 0; i < fState.palette.size(); i++) {
		BRect r = _SwatchRect(i);
		if (r.Intersects(updateRect))
			_DrawSwatch(r, fState.palette[i]);
	}
}


void
ColorBox::MouseDown(BPoint where)
{
	int32 buttons = B_PRIMARY_MOUSE_BUTTON;
	int32 clicks = 1;
	if (BMessage* message = Window()->CurrentMessage()) {
		message->FindInt32("buttons", &buttons);
		message->FindInt32("clicks", &clicks);
	}
	bool secondary = (buttons & B_SECONDARY_MOUSE_BUTTON) != 0;

	if (_CurrentColorsFrame().Contains(where) && clicks >= 2) {
		BMessage edit(kMsgEditSlot);
		edit.AddInt32("slot", kSlotForeground);
		Window()->PostMessage(&edit);
		return;
	}
	for (size_t i = 0; i < fState.palette.size(); i++) {
		if (!_SwatchRect(i).Contains(where))
			continue;
		if (clicks >= 2 && !secondary) {
			BMessage edit(kMsgEditSlot);
			edit.AddInt32("slot", (int32)i);
			Window()->PostMessage(&edit);
		}
		pp::Pixel color = fState.palette[i];
		if (secondary)
			fState.background = color;
		else
			fState.foreground = color;
		Invalidate(_CurrentColorsFrame());
		Paint()->ChangeState([=](PaintState& s) {
			if (secondary)
				s.background = color;
			else
				s.foreground = color;
		});
		return;
	}
}


void
ColorBox::StateChanged()
{
	fState = CurrentState();
	Invalidate();
}
