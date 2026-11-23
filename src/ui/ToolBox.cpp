/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 */
#include "ToolBox.h"

#include <Bitmap.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <TranslationUtils.h>
#include <Window.h>

#include <random>

#include "Constants.h"
#include "PaintWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ToolBox"


static const float kButtonSize = 25;
static const float kLeft = 4;
static const float kTop = 2;
static const float kWidth = 56;
static const float kOptionsWidth = 41;
static const float kOptionsHeight = 66;

static const int32 kMagnifications[] = { 1, 2, 6, 8, 10 };
static const int32 kEraserSizes[] = { 4, 6, 8, 10 };
static const int32 kAirbrushSizes[] = { 9, 16, 24 };
static const int32 kCircleSizes[] = { 7, 4, 1 };
static const int32 kBrushSizes[] = { 8, 5, 2 };
static const int32 kBrushShapes[] = { pp::kBrushCircle, pp::kBrushSquare,
	pp::kBrushReverseDiagonal, pp::kBrushDiagonal };


// MS Paint's names and status bar help, in sentence case.
static const char* kToolNames[kToolCount] = {
	B_TRANSLATE_MARK("Free-form select"),
	B_TRANSLATE_MARK("Select"),
	B_TRANSLATE_MARK("Eraser/color eraser"),
	B_TRANSLATE_MARK("Fill with color"),
	B_TRANSLATE_MARK("Pick color"),
	B_TRANSLATE_MARK("Magnifier"),
	B_TRANSLATE_MARK("Pencil"),
	B_TRANSLATE_MARK("Brush"),
	B_TRANSLATE_MARK("Airbrush"),
	B_TRANSLATE_MARK("Text"),
	B_TRANSLATE_MARK("Line"),
	B_TRANSLATE_MARK("Curve"),
	B_TRANSLATE_MARK("Rectangle"),
	B_TRANSLATE_MARK("Polygon"),
	B_TRANSLATE_MARK("Ellipse"),
	B_TRANSLATE_MARK("Rounded rectangle"),
};

static const char* kToolDescriptions[kToolCount] = {
	B_TRANSLATE_MARK("Selects a free-form part of the picture to move, copy, "
		"or edit."),
	B_TRANSLATE_MARK("Selects a rectangular part of the picture to move, "
		"copy, or edit."),
	B_TRANSLATE_MARK("Erases a portion of the picture, using the selected "
		"eraser shape."),
	B_TRANSLATE_MARK("Fills an area with the current drawing color."),
	B_TRANSLATE_MARK("Picks up a color from the picture for drawing."),
	B_TRANSLATE_MARK("Changes the magnification."),
	B_TRANSLATE_MARK("Draws a free-form line one pixel wide."),
	B_TRANSLATE_MARK("Draws using a brush with the selected shape and size."),
	B_TRANSLATE_MARK("Draws using an airbrush of the selected size."),
	B_TRANSLATE_MARK("Inserts text into the picture."),
	B_TRANSLATE_MARK("Draws a straight line with the selected line width."),
	B_TRANSLATE_MARK("Draws a curved line with the selected line width."),
	B_TRANSLATE_MARK("Draws a rectangle with the selected fill style."),
	B_TRANSLATE_MARK("Draws a polygon with the selected fill style."),
	B_TRANSLATE_MARK("Draws an ellipse with the selected fill style."),
	B_TRANSLATE_MARK("Draws a rounded rectangle with the selected fill "
		"style."),
};


const char*
ToolName(int32 tool)
{
	if (tool < 0 || tool >= kToolCount)
		return "";
	return B_TRANSLATE_NOCOLLECT(kToolNames[tool]);
}


const char*
ToolDescription(int32 tool)
{
	if (tool < 0 || tool >= kToolCount)
		return "";
	return B_TRANSLATE_NOCOLLECT(kToolDescriptions[tool]);
}


static BBitmap*
LoadImage(const char* name)
{
	BBitmap* image = BTranslationUtils::GetBitmap('PNG ', name);
	if (image == NULL || image->ColorSpace() == B_RGBA32)
		return image;
	BBitmap* rgba = new BBitmap(image->Bounds(), B_RGBA32);
	rgba->ImportBits(image);
	delete image;
	return rgba;
}


ToolBox::ToolBox()
	:
	BView("toolbox", B_WILL_DRAW),
	fState(CurrentState()),
	fZoom(1),
	fIcons(LoadImage("tools")),
	fTransparency(LoadImage("options-transparency")),
	fHoverTool(-1)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


ToolBox::~ToolBox()
{
	delete fIcons;
	delete fTransparency;
}


BSize
ToolBox::MinSize()
{
	return BSize(kWidth - 1, kTop + 8 * kButtonSize + 3 + kOptionsHeight + 2);
}


BSize
ToolBox::MaxSize()
{
	return BSize(kWidth - 1, B_SIZE_UNLIMITED);
}


BSize
ToolBox::PreferredSize()
{
	return MinSize();
}


BRect
ToolBox::_ToolRect(int32 tool) const
{
	float x = kLeft + (tool % 2) * kButtonSize;
	float y = kTop + (tool / 2) * kButtonSize;
	return BRect(x, y, x + kButtonSize - 1, y + kButtonSize - 1);
}


BRect
ToolBox::_OptionsFrame() const
{
	float x = floorf((kWidth - kOptionsWidth) / 2) + 1;
	float y = kTop + 8 * kButtonSize + 3;
	return BRect(x, y, x + kOptionsWidth - 1, y + kOptionsHeight - 1);
}


void
ToolBox::Draw(BRect updateRect)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	for (int32 tool = 0; tool < kToolCount; tool++) {
		BRect r = _ToolRect(tool);
		if (!r.Intersects(updateRect))
			continue;
		bool selected = tool == fState.tool;
		uint32 flags = selected ? BControlLook::B_ACTIVATED : 0;
		BRect frame = r;
		be_control_look->DrawButtonFrame(this, frame, updateRect, base, base,
			flags);
		be_control_look->DrawButtonBackground(this, frame, updateRect, base,
			flags);
		if (fIcons != NULL) {
			// Pressed buttons show their icon a pixel down and right.
			float shift = selected ? 1 : 0;
			BRect source(tool * 16, 0, tool * 16 + 15, 15);
			BRect dest(r.left + 4 + shift, r.top + 4 + shift,
				r.left + 19 + shift, r.top + 19 + shift);
			SetDrawingMode(B_OP_ALPHA);
			SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			DrawBitmap(fIcons, source, dest);
			SetDrawingMode(B_OP_COPY);
		}
	}
	_DrawOptions(updateRect);
}


void
ToolBox::_DrawOptions(BRect updateRect)
{
	BRect frame = _OptionsFrame();
	if (!frame.Intersects(updateRect))
		return;
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	SetHighColor(base);
	FillRect(frame);
	// Sunken, as in MS Paint.
	SetHighColor(tint_color(base, B_DARKEN_2_TINT));
	StrokeLine(frame.LeftBottom(), frame.LeftTop());
	StrokeLine(frame.RightTop());
	SetHighColor(tint_color(base, B_LIGHTEN_MAX_TINT));
	StrokeLine(frame.RightBottom());
	StrokeLine(frame.LeftBottom());

	std::vector<BRect> rects;
	_OptionRects(rects);
	int32 selected = _SelectedOption();
	for (size_t i = 0; i < rects.size(); i++)
		_DrawOption(i, rects[i], (int32)i == selected);
}


// The options box's items for the current tool, in view coordinates.
void
ToolBox::_OptionRects(std::vector<BRect>& rects) const
{
	BRect inner = _OptionsFrame().InsetByCopy(1, 1);
	float x = inner.left, y = inner.top;
	auto rows = [&](int32 count, float height) {
		float top = y + floorf((inner.Height() + 1 - count * height) / 2);
		for (int32 i = 0; i < count; i++) {
			rects.push_back(BRect(x + 1, top + i * height,
				inner.right - 1, top + (i + 1) * height - 1));
		}
	};
	switch (fState.tool) {
		case kToolFreeFormSelect:
		case kToolSelect:
		case kToolText:
			rows(2, 26);
			break;
		case kToolEraser:
			rows(4, 15);
			break;
		case kToolMagnifier:
			rows(5, 12);
			break;
		case kToolLine:
		case kToolCurve:
			rows(5, 12);
			break;
		case kToolRectangle:
		case kToolPolygon:
		case kToolEllipse:
		case kToolRoundedRectangle:
			rows(3, 20);
			break;
		case kToolBrush:
			for (int32 row = 0; row < 4; row++) {
				for (int32 col = 0; col < 3; col++) {
					float left = x + 1 + col * 12;
					float top = y + 4 + row * 14;
					rects.push_back(BRect(left, top, left + 11, top + 13));
				}
			}
			break;
		case kToolAirbrush:
			rects.push_back(BRect(x + 1, y + 2, x + 18, y + 27));
			rects.push_back(BRect(x + 19, y + 2, x + 37, y + 27));
			rects.push_back(BRect(x + 1, y + 30, x + 37, y + 61));
			break;
	}
}


int32
ToolBox::_SelectedOption() const
{
	switch (fState.tool) {
		case kToolFreeFormSelect:
		case kToolSelect:
		case kToolText:
			return fState.transparentMode ? 1 : 0;
		case kToolEraser:
			for (int32 i = 0; i < 4; i++) {
				if (kEraserSizes[i] == fState.eraserSize)
					return i;
			}
			break;
		case kToolMagnifier:
			for (int32 i = 0; i < 5; i++) {
				if (kMagnifications[i] == fZoom)
					return i;
			}
			break;
		case kToolLine:
		case kToolCurve:
			return fState.strokeSize - 1;
		case kToolRectangle:
		case kToolPolygon:
		case kToolEllipse:
		case kToolRoundedRectangle:
			return fState.fillStyle;
		case kToolBrush:
			for (int32 row = 0; row < 4; row++) {
				if (kBrushShapes[row] != fState.brushShape)
					continue;
				const int32* sizes = row == 0 ? kCircleSizes : kBrushSizes;
				for (int32 col = 0; col < 3; col++) {
					if (sizes[col] == fState.brushSize)
						return row * 3 + col;
				}
			}
			break;
		case kToolAirbrush:
			for (int32 i = 0; i < 3; i++) {
				if (kAirbrushSizes[i] == fState.airbrushSize)
					return i;
			}
			break;
	}
	return -1;
}


void
ToolBox::_ChooseOption(int32 index)
{
	int32 tool = fState.tool;
	if (tool == kToolMagnifier) {
		BMessage zoom(kMsgZoom);
		zoom.AddInt32("zoom", kMagnifications[index]);
		Window()->PostMessage(&zoom);
		if (kMagnifications[index] > 1) {
			int32 value = kMagnifications[index];
			Paint()->ChangeState([=](PaintState& s) {
				s.magnifierZoom = value;
			});
		}
		return;
	}
	Paint()->ChangeState([=](PaintState& s) {
		switch (tool) {
			case kToolFreeFormSelect:
			case kToolSelect:
			case kToolText:
				s.transparentMode = index == 1;
				break;
			case kToolEraser:
				s.eraserSize = kEraserSizes[index];
				break;
			case kToolLine:
			case kToolCurve:
				s.strokeSize = index + 1;
				break;
			case kToolRectangle:
			case kToolPolygon:
			case kToolEllipse:
			case kToolRoundedRectangle:
				s.fillStyle = index;
				break;
			case kToolBrush:
				s.brushShape = kBrushShapes[index / 3];
				s.brushSize = (index / 3 == 0 ? kCircleSizes
					: kBrushSizes)[index % 3];
				break;
			case kToolAirbrush:
				s.airbrushSize = kAirbrushSizes[index];
				break;
		}
	});
}


void
ToolBox::_DrawOption(int32 index, BRect rect, bool selected)
{
	rgb_color highlight = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
	rgb_color ink = selected ? make_color(255, 255, 255)
		: ui_color(B_PANEL_TEXT_COLOR);
	if (selected && fState.tool != kToolBrush && fState.tool != kToolAirbrush) {
		SetHighColor(highlight);
		FillRect(rect);
	}
	SetHighColor(ink);
	BPoint center(floorf((rect.left + rect.right) / 2),
		floorf((rect.top + rect.bottom) / 2));

	switch (fState.tool) {
		case kToolFreeFormSelect:
		case kToolSelect:
		case kToolText:
			if (fTransparency != NULL) {
				// The image holds both: opaque on top, transparent below.
				BRect source(0, index * 23, 34, index * 23 + 21);
				BRect dest(center.x - 17, center.y - 11, center.x + 17,
					center.y + 10);
				SetDrawingMode(B_OP_ALPHA);
				SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
				DrawBitmap(fTransparency, source, dest);
				SetDrawingMode(B_OP_COPY);
			}
			break;
		case kToolEraser:
		{
			float size = kEraserSizes[index];
			BRect square(center.x - size / 2, center.y - size / 2,
				center.x + size / 2 - 1, center.y + size / 2 - 1);
			FillRect(square);
			break;
		}
		case kToolMagnifier:
		{
			BFont font(be_plain_font);
			font.SetSize(9);
			SetFont(&font);
			BString label;
			label.SetToFormat("%" B_PRId32 "x", kMagnifications[index]);
			SetLowColor(selected ? highlight
				: ui_color(B_PANEL_BACKGROUND_COLOR));
			DrawString(label, BPoint(center.x - StringWidth(label) / 2,
				rect.bottom - 2));
			break;
		}
		case kToolLine:
		case kToolCurve:
		{
			float width = index + 1;
			FillRect(BRect(rect.left + 4, center.y - floorf(width / 2),
				rect.right - 4, center.y - floorf(width / 2) + width - 1));
			break;
		}
		case kToolRectangle:
		case kToolPolygon:
		case kToolEllipse:
		case kToolRoundedRectangle:
		{
			BRect box(rect.left + 6, rect.top + 4, rect.right - 6,
				rect.bottom - 4);
			rgb_color gray = make_color(128, 128, 128);
			if (index == pp::kFillOutline)
				StrokeRect(box);
			else if (index == pp::kFillOutlineAndFill) {
				SetHighColor(gray);
				FillRect(box);
				SetHighColor(ink);
				StrokeRect(box);
			} else {
				SetHighColor(gray);
				FillRect(box);
			}
			break;
		}
		case kToolBrush:
		{
			int32 row = index / 3;
			int32 size = (row == 0 ? kCircleSizes : kBrushSizes)[index % 3];
			if (selected) {
				SetHighColor(highlight);
				FillRect(rect);
			}
			SetHighColor(ink);
			for (const pp::Point& p : pp::BrushOffsets(
					(pp::BrushShape)kBrushShapes[row], size)) {
				FillRect(BRect(center.x + p.x, center.y + p.y,
					center.x + p.x, center.y + p.y));
			}
			break;
		}
		case kToolAirbrush:
		{
			if (selected) {
				SetHighColor(highlight);
				FillRect(rect);
			}
			SetHighColor(ink);
			// A fixed pattern of dots the size of the spray.
			std::mt19937 random(index + 1);
			std::uniform_real_distribution<float> unit(-1, 1);
			float radius = kAirbrushSizes[index] / 2.0f;
			for (int32 i = 0; i < kAirbrushSizes[index] * 3; i++) {
				float dx = unit(random) * radius;
				float dy = unit(random) * radius;
				if (dx * dx + dy * dy > radius * radius)
					continue;
				BPoint dot(center.x + (int32)dx, center.y + (int32)dy);
				FillRect(BRect(dot, dot));
			}
			break;
		}
	}
}


void
ToolBox::MouseDown(BPoint where)
{
	for (int32 tool = 0; tool < kToolCount; tool++) {
		if (!_ToolRect(tool).Contains(where))
			continue;
		if (tool != fState.tool) {
			fState.previousTool = fState.tool;
			fState.tool = tool;
			Invalidate();
			Paint()->ChangeState([=](PaintState& s) {
				s.previousTool = s.tool;
				s.tool = tool;
			});
		}
		return;
	}
	std::vector<BRect> rects;
	_OptionRects(rects);
	for (size_t i = 0; i < rects.size(); i++) {
		if (rects[i].Contains(where)) {
			_ChooseOption(i);
			return;
		}
	}
}


void
ToolBox::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
	int32 hover = -1;
	if (transit != B_EXITED_VIEW && transit != B_OUTSIDE_VIEW) {
		for (int32 tool = 0; tool < kToolCount; tool++) {
			if (_ToolRect(tool).Contains(where))
				hover = tool;
		}
	}
	if (hover == fHoverTool)
		return;
	fHoverTool = hover;
	SetToolTip(hover >= 0 ? ToolName(hover) : NULL);
	if (PaintWindow* window = dynamic_cast<PaintWindow*>(Window()))
		window->SetHelpText(hover >= 0 ? ToolDescription(hover) : NULL);
}


void
ToolBox::StateChanged()
{
	fState = CurrentState();
	Invalidate();
}


void
ToolBox::SetZoom(int32 zoom)
{
	fZoom = zoom;
	if (fState.tool == kToolMagnifier)
		Invalidate(_OptionsFrame());
}
