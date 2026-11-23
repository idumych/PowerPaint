/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 */
#include "CanvasView.h"

#include <Bitmap.h>
#include <Cursor.h>
#include <MessageRunner.h>
#include <Region.h>
#include <ScrollBar.h>
#include <Window.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <new>
#include <stdio.h>
#include <string.h>

#include "Constants.h"
#include "Cursors.h"
#include "ImageIO.h"
#include "PaintWindow.h"
#include "TextRender.h"


// Where the image starts in the view, leaving room for the handles.
static const float kInset = 4;
static const float kHandleSize = 3;
// Undo keeps whole images, up to about this much memory.
static const size_t kUndoBytes = 256 * 1024 * 1024;
static const bigtime_t kSprayInterval = 20000;

static const pattern kDashes = { { 0xf0, 0xf0, 0xf0, 0xf0, 0x0f, 0x0f, 0x0f,
	0x0f } };

enum {
	kHandleTopLeft = 0,
	kHandleTop,
	kHandleTopRight,
	kHandleRight,
	kHandleBottomRight,
	kHandleBottom,
	kHandleBottomLeft,
	kHandleLeft,
	kHandleCount
};


static bool
IsSelectionTool(int32 tool)
{
	return tool == kToolSelect || tool == kToolFreeFormSelect;
}


static bool
IsShapeTool(int32 tool)
{
	return tool == kToolRectangle || tool == kToolEllipse
		|| tool == kToolRoundedRectangle || tool == kToolPolygon;
}


static bool
Inside(const pp::Rect& r, pp::Point p)
{
	return p.x >= r.x && p.y >= r.y && p.x < r.Right() && p.y < r.Bottom();
}


static pp::Rect
Inflate(pp::Rect r, int by)
{
	return pp::Rect{r.x - by, r.y - by, r.w + 2 * by, r.h + 2 * by};
}


CanvasView::CanvasView()
	:
	BView("canvas", B_WILL_DRAW | B_FRAME_EVENTS),
	fBitmap(NULL),
	fHasAlpha(false),
	fModified(false),
	fZoom(1),
	fShowGrid(false),
	fState(CurrentState()),
	fSelectionBitmap(NULL),
	fTextBitmap(NULL),
	fDrag(kDragNone),
	fSecondary(false),
	fHandle(-1),
	fCurveStage(0),
	fSprayRunner(NULL),
	fRandom(std::random_device()()),
	fMouseInside(false)
{
	// JS Paint's default canvas.
	SetImage(pp::Raster(683, 384, fState.background));
}


CanvasView::~CanvasView()
{
	delete fSprayRunner;
	delete fBitmap;
	delete fSelectionBitmap;
	delete fTextBitmap;
}


void
CanvasView::AttachedToWindow()
{
	SetViewColor(B_TRANSPARENT_COLOR);
	_UpdateScrollBars();
}


// #pragma mark - Drawing


void
CanvasView::Draw(BRect updateRect)
{
	BRect frame = _ImageFrame();

	BRegion outside(updateRect);
	outside.Exclude(frame);
	SetHighColor(128, 128, 128);
	FillRegion(&outside);

	// Only the image pixels in the update rect are scaled and drawn.
	BRect visible = updateRect & frame;
	if (visible.IsValid() && fBitmap != NULL) {
		int32 x0 = (int32)floorf((visible.left - kInset) / fZoom);
		int32 y0 = (int32)floorf((visible.top - kInset) / fZoom);
		int32 x1 = (int32)floorf((visible.right - kInset) / fZoom);
		int32 y1 = (int32)floorf((visible.bottom - kInset) / fZoom);
		x0 = std::max<int32>(x0, 0);
		y0 = std::max<int32>(y0, 0);
		x1 = std::min<int32>(x1, fImage.Width() - 1);
		y1 = std::min<int32>(y1, fImage.Height() - 1);
		BRect source(x0, y0, x1, y1);
		BRect dest = _ViewRect(pp::Rect{x0, y0, x1 - x0 + 1, y1 - y0 + 1});
		if (fHasAlpha) {
			// A checkerboard under transparent pixels.
			SetHighColor(255, 255, 255);
			SetLowColor(204, 204, 204);
			pattern checker = { { 0xf0, 0xf0, 0xf0, 0xf0, 0x0f, 0x0f,
				0x0f, 0x0f } };
			FillRect(dest, checker);
			SetDrawingMode(B_OP_ALPHA);
			SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		}
		DrawBitmap(fBitmap, source, dest);
		SetDrawingMode(B_OP_COPY);
	}

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	if (fSelection.active && fSelectionBitmap != NULL) {
		DrawBitmap(fSelectionBitmap, fSelectionBitmap->Bounds(),
			_ViewRect(fSelection.rect));
	}
	if (fText.active && fTextBitmap != NULL)
		DrawBitmap(fTextBitmap, fTextBitmap->Bounds(), _ViewRect(fText.rect));
	SetDrawingMode(B_OP_COPY);

	if (fShowGrid && fZoom >= 4) {
		SetHighColor(128, 128, 128);
		BRect grid = updateRect & frame;
		if (grid.IsValid()) {
			int32 first = (int32)ceilf((grid.left - kInset) / fZoom);
			int32 last = (int32)floorf((grid.right - kInset) / fZoom);
			for (int32 i = first; i <= last; i++) {
				float x = kInset + i * fZoom;
				StrokeLine(BPoint(x, grid.top), BPoint(x, grid.bottom));
			}
			first = (int32)ceilf((grid.top - kInset) / fZoom);
			last = (int32)floorf((grid.bottom - kInset) / fZoom);
			for (int32 i = first; i <= last; i++) {
				float y = kInset + i * fZoom;
				StrokeLine(BPoint(grid.left, y), BPoint(grid.right, y));
			}
		}
	}

	// The eraser shows where it will erase.
	if (fState.tool == kToolEraser && fMouseInside && fDrag != kDragCanvasResize) {
		int32 size = fState.eraserSize;
		BRect r = _ViewRect(pp::Rect{fHover.x - size / 2, fHover.y - size / 2,
			size, size});
		rgb_color bg = { (uint8)pp::Red(fState.background),
			(uint8)pp::Green(fState.background),
			(uint8)pp::Blue(fState.background), 255 };
		SetHighColor(bg);
		FillRect(r);
		SetHighColor(0, 0, 0);
		StrokeRect(r);
	}

	if (fSelection.active) {
		BRect r = _ViewRect(fSelection.rect).InsetByCopy(-1, -1);
		_DrawDashedRect(r);
		_DrawHandles(r, true);
	}
	if (fText.active)
		_DrawDashedRect(_ViewRect(fText.rect).InsetByCopy(-1, -1));

	if (fDrag == kDragNewSelection || fDrag == kDragNewTextBox) {
		if (fState.tool == kToolFreeFormSelect) {
			SetHighColor(0, 0, 0);
			for (size_t i = 0; i + 1 < fOutline.size(); i++) {
				BPoint a(kInset + (fOutline[i].x + 0.5f) * fZoom,
					kInset + (fOutline[i].y + 0.5f) * fZoom);
				BPoint b(kInset + (fOutline[i + 1].x + 0.5f) * fZoom,
					kInset + (fOutline[i + 1].y + 0.5f) * fZoom);
				StrokeLine(a, b);
			}
		} else {
			_DrawDashedRect(_ViewRect(pp::Rect::FromCorners(fStart.x, fStart.y,
				fLast.x, fLast.y)));
		}
	}

	if (fDrag == kDragCanvasResize) {
		_DrawDashedRect(_ViewRect(pp::Rect{0, 0, fResizeTo.x, fResizeTo.y})
			.InsetByCopy(-1, -1));
	}
	_DrawHandles(frame.InsetByCopy(-1, -1), false);
}


void
CanvasView::_DrawDashedRect(BRect rect)
{
	SetHighColor(0, 0, 0);
	SetLowColor(255, 255, 255);
	StrokeRect(rect, kDashes);
}


BRect
CanvasView::_HandleRect(BRect frame, int32 handle) const
{
	float x, y;
	float midX = floorf((frame.left + frame.right) / 2) - 1;
	float midY = floorf((frame.top + frame.bottom) / 2) - 1;
	switch (handle) {
		case kHandleTopLeft:
		case kHandleLeft:
		case kHandleBottomLeft:
			x = frame.left - kHandleSize + 1;
			break;
		case kHandleTop:
		case kHandleBottom:
			x = midX;
			break;
		default:
			x = frame.right;
	}
	switch (handle) {
		case kHandleTopLeft:
		case kHandleTop:
		case kHandleTopRight:
			y = frame.top - kHandleSize + 1;
			break;
		case kHandleLeft:
		case kHandleRight:
			y = midY;
			break;
		default:
			y = frame.bottom;
	}
	return BRect(x, y, x + kHandleSize - 1, y + kHandleSize - 1);
}


// The selection has eight handles; the canvas has three that work (right,
// bottom right, bottom) and five drawn hollow, as in MS Paint.
void
CanvasView::_DrawHandles(BRect frame, bool all)
{
	rgb_color handle = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
	for (int32 i = 0; i < kHandleCount; i++) {
		BRect r = _HandleRect(frame, i);
		bool active = all || i == kHandleRight || i == kHandleBottomRight
			|| i == kHandleBottom;
		SetHighColor(handle);
		if (active)
			FillRect(r);
		else
			StrokeRect(r);
	}
}


// #pragma mark - Geometry


pp::Point
CanvasView::_ImagePoint(BPoint where) const
{
	return pp::Point{(int)floorf((where.x - kInset) / fZoom),
		(int)floorf((where.y - kInset) / fZoom)};
}


BRect
CanvasView::_ViewRect(pp::Rect r) const
{
	return BRect(kInset + r.x * fZoom, kInset + r.y * fZoom,
		kInset + r.Right() * fZoom - 1, kInset + r.Bottom() * fZoom - 1);
}


BRect
CanvasView::_ImageFrame() const
{
	return _ViewRect(fImage.Bounds());
}


int32
CanvasView::_SelectionHandleAt(BPoint where) const
{
	if (!fSelection.active)
		return -1;
	BRect frame = _ViewRect(fSelection.rect).InsetByCopy(-1, -1);
	for (int32 i = 0; i < kHandleCount; i++) {
		if (_HandleRect(frame, i).InsetByCopy(-2, -2).Contains(where))
			return i;
	}
	return -1;
}


int32
CanvasView::_CanvasHandleAt(BPoint where) const
{
	BRect frame = _ImageFrame().InsetByCopy(-1, -1);
	const int32 handles[] = { kHandleRight, kHandleBottomRight, kHandleBottom };
	for (int32 handle : handles) {
		if (_HandleRect(frame, handle).InsetByCopy(-2, -2).Contains(where))
			return handle;
	}
	return -1;
}


void
CanvasView::_UpdateScrollBars()
{
	float dataWidth = kInset * 2 + fImage.Width() * fZoom + kHandleSize;
	float dataHeight = kInset * 2 + fImage.Height() * fZoom + kHandleSize;
	BRect bounds = Bounds();
	float width = bounds.Width() + 1;
	float height = bounds.Height() + 1;
	if (BScrollBar* bar = ScrollBar(B_HORIZONTAL)) {
		bar->SetRange(0, std::max(0.0f, dataWidth - width));
		bar->SetProportion(std::min(1.0f, width / dataWidth));
		bar->SetSteps(std::max(8.0f, (float)fZoom * 8), width * 0.9f);
	}
	if (BScrollBar* bar = ScrollBar(B_VERTICAL)) {
		bar->SetRange(0, std::max(0.0f, dataHeight - height));
		bar->SetProportion(std::min(1.0f, height / dataHeight));
		bar->SetSteps(std::max(8.0f, (float)fZoom * 8), height * 0.9f);
	}
}


void
CanvasView::FrameResized(float width, float height)
{
	_UpdateScrollBars();
}


// #pragma mark - The bitmap on screen


void
CanvasView::_SyncBitmap(pp::Rect r)
{
	r = r.Intersect(fImage.Bounds());
	if (r.IsEmpty() || fBitmap == NULL)
		return;
	uint8* bits = (uint8*)fBitmap->Bits();
	int32 bpr = fBitmap->BytesPerRow();
	for (int y = r.y; y < r.Bottom(); y++) {
		memcpy(bits + y * bpr + r.x * 4,
			fImage.Bits() + (size_t)y * fImage.Width() + r.x,
			r.w * sizeof(pp::Pixel));
	}
	_InvalidateImageRect(r);
}


void
CanvasView::_SyncAll()
{
	if (fBitmap == NULL
		|| fBitmap->Bounds().IntegerWidth() + 1 != fImage.Width()
		|| fBitmap->Bounds().IntegerHeight() + 1 != fImage.Height()) {
		delete fBitmap;
		fBitmap = ImageIO::ToBitmap(fImage);
		_UpdateScrollBars();
		Invalidate();
	} else
		_SyncBitmap(fImage.Bounds());
	fHasAlpha = fImage.HasTransparency();
}


void
CanvasView::_InvalidateImageRect(pp::Rect r)
{
	// Grow by the handles and dashes drawn round selections.
	Invalidate(_ViewRect(r).InsetByCopy(-kHandleSize - 3, -kHandleSize - 3));
}


// #pragma mark - Document


void
CanvasView::SetImage(const pp::Raster& image)
{
	_DiscardSelection();
	fText = TextBox();
	delete fTextBitmap;
	fTextBitmap = NULL;
	fPolygon.clear();
	fCurveStage = 0;
	fUndo.clear();
	fRedo.clear();
	fImage = image;
	fModified = false;
	_SyncAll();
	Invalidate();
}


pp::Raster
CanvasView::Flattened() const
{
	pp::Raster flat = fImage;
	if (fSelection.active) {
		pp::Composite(flat, fSelection.scaled, fSelection.rect.x,
			fSelection.rect.y,
			fState.transparentMode ? &fState.background : nullptr);
	}
	if (fText.active)
		pp::Composite(flat, fText.rendered, fText.rect.x, fText.rect.y);
	return flat;
}


void
CanvasView::SetModified(bool modified)
{
	fModified = modified;
}


void
CanvasView::_Changed(bool modified)
{
	if (modified)
		fModified = true;
	if (PaintWindow* window = dynamic_cast<PaintWindow*>(Window()))
		window->DocumentChanged();
}


void
CanvasView::_PushUndoState(pp::Raster&& state)
{
	fUndo.push_back(std::move(state));
	fRedo.clear();
	size_t bytes = 0;
	for (const pp::Raster& r : fUndo)
		bytes += (size_t)r.Width() * r.Height() * 4;
	while (fUndo.size() > 1 && bytes > kUndoBytes) {
		bytes -= (size_t)fUndo.front().Width() * fUndo.front().Height() * 4;
		fUndo.erase(fUndo.begin());
	}
}


void
CanvasView::_PushUndo()
{
	_PushUndoState(pp::Raster(fImage));
}


bool
CanvasView::CanUndo() const
{
	return !fUndo.empty() || fText.active || !fPolygon.empty()
		|| fCurveStage > 0;
}


void
CanvasView::Undo()
{
	if (fDrag != kDragNone)
		return;
	// Something still in progress is cancelled first.
	if (!fPolygon.empty() || fCurveStage > 0) {
		fImage = fBefore;
		fBefore = pp::Raster();
		fPolygon.clear();
		fCurveStage = 0;
		_SyncAll();
		_Changed(false);
		return;
	}
	if (fText.active) {
		_InvalidateImageRect(fText.rect);
		fText = TextBox();
		delete fTextBitmap;
		fTextBitmap = NULL;
		_Changed(false);
		return;
	}
	if (fUndo.empty())
		return;
	pp::Raster redo = Flattened();
	_DiscardSelection();
	fImage = std::move(fUndo.back());
	fUndo.pop_back();
	fRedo.push_back(std::move(redo));
	_SyncAll();
	_Changed();
}


void
CanvasView::Redo()
{
	if (fDrag != kDragNone)
		return;
	// Finishing a text box or polygon saves an undo step, which empties the
	// redo list, so check it afterwards.
	FinishPending();
	if (fRedo.empty())
		return;
	fUndo.push_back(fImage);
	fImage = std::move(fRedo.back());
	fRedo.pop_back();
	_SyncAll();
	_Changed();
}


// #pragma mark - View


void
CanvasView::SetZoom(int32 zoom, const BPoint* center)
{
	zoom = std::clamp<int32>(zoom, 1, 10);
	if (zoom == fZoom)
		return;
	BRect bounds = Bounds();
	BPoint c;
	if (center != NULL)
		c = *center;
	else {
		c.x = (bounds.left + bounds.Width() / 2 - kInset) / fZoom;
		c.y = (bounds.top + bounds.Height() / 2 - kInset) / fZoom;
	}
	fZoom = zoom;
	_UpdateScrollBars();
	float x = kInset + c.x * fZoom - bounds.Width() / 2;
	float y = kInset + c.y * fZoom - bounds.Height() / 2;
	float maxX = 0, maxY = 0, minimum;
	if (BScrollBar* bar = ScrollBar(B_HORIZONTAL))
		bar->GetRange(&minimum, &maxX);
	if (BScrollBar* bar = ScrollBar(B_VERTICAL))
		bar->GetRange(&minimum, &maxY);
	ScrollTo(std::clamp(x, 0.0f, maxX), std::clamp(y, 0.0f, maxY));
	Invalidate();
	if (PaintWindow* window = dynamic_cast<PaintWindow*>(Window()))
		window->ZoomChanged();
}


void
CanvasView::SetShowGrid(bool show)
{
	fShowGrid = show;
	Invalidate();
}


// #pragma mark - Mouse


void
CanvasView::MouseDown(BPoint where)
{
	MakeFocus(true);
	int32 buttons = B_PRIMARY_MOUSE_BUTTON;
	int32 clicks = 1;
	if (BMessage* message = Window()->CurrentMessage()) {
		message->FindInt32("buttons", &buttons);
		message->FindInt32("clicks", &clicks);
	}
	// A second button during a drag cancels it, as in MS Paint.
	if (fDrag != kDragNone) {
		CancelOperation();
		return;
	}
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);

	int32 handle = _SelectionHandleAt(where);
	if (handle >= 0) {
		fDrag = kDragSelectionResize;
		fHandle = handle;
		fGrabRect = fSelection.rect;
		fStart = fLast = _ImagePoint(where);
		return;
	}
	handle = _CanvasHandleAt(where);
	if (handle >= 0) {
		FinishPending();
		fDrag = kDragCanvasResize;
		fHandle = handle;
		fResizeTo = pp::Point{fImage.Width(), fImage.Height()};
		return;
	}
	ToolDown(_ImagePoint(where), (buttons & B_SECONDARY_MOUSE_BUTTON) != 0,
		modifiers(), clicks);
}


void
CanvasView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
	bool inside = transit != B_EXITED_VIEW && transit != B_OUTSIDE_VIEW;
	if (fState.tool == kToolEraser && (inside != fMouseInside
			|| _ImagePoint(where).x != fHover.x
			|| _ImagePoint(where).y != fHover.y)) {
		// The eraser's outline moves with the pointer.
		int32 size = fState.eraserSize + 2;
		_InvalidateImageRect(pp::Rect{fHover.x - size / 2, fHover.y - size / 2,
			size, size});
		pp::Point p = _ImagePoint(where);
		_InvalidateImageRect(pp::Rect{p.x - size / 2, p.y - size / 2, size,
			size});
	}
	fMouseInside = inside;
	if (inside && fDrag == kDragNone)
		_UpdateCursor(where);
	ToolMove(_ImagePoint(where), modifiers());
}


void
CanvasView::MouseUp(BPoint where)
{
	ToolUp(_ImagePoint(where), modifiers());
	_UpdateCursor(where);
}


void
CanvasView::_UpdateCursor(BPoint where)
{
	int32 handle = _SelectionHandleAt(where);
	if (handle < 0)
		handle = _CanvasHandleAt(where);
	BCursorID id = B_CURSOR_ID_SYSTEM_DEFAULT;
	switch (handle) {
		case kHandleTopLeft:
		case kHandleBottomRight:
			id = B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST;
			break;
		case kHandleTopRight:
		case kHandleBottomLeft:
			id = B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST;
			break;
		case kHandleTop:
		case kHandleBottom:
			id = B_CURSOR_ID_RESIZE_NORTH_SOUTH;
			break;
		case kHandleLeft:
		case kHandleRight:
			id = B_CURSOR_ID_RESIZE_EAST_WEST;
			break;
		default:
			if (fSelection.active && IsSelectionTool(fState.tool)
				&& Inside(fSelection.rect, _ImagePoint(where))) {
				id = B_CURSOR_ID_MOVE;
			} else if (!_ImageFrame().Contains(where)) {
				id = B_CURSOR_ID_SYSTEM_DEFAULT;
			} else {
				SetViewCursor(Cursors::ForTool(fState.tool));
				return;
			}
	}
	BCursor cursor(id);
	SetViewCursor(&cursor);
}


// #pragma mark - Tools


void
CanvasView::_ShapeColors(pp::Pixel& stroke, pp::Pixel& fill) const
{
	// JS Paint: shapes outline in the foreground and fill with the
	// background; the secondary button swaps them, and so does "fill only".
	bool reverse = fSecondary;
	if (IsShapeTool(fState.tool) && fState.fillStyle == pp::kFillOnly)
		reverse = !reverse;
	if (!IsShapeTool(fState.tool)) {
		stroke = fill = fSecondary ? fState.background : fState.foreground;
		return;
	}
	stroke = reverse ? fState.background : fState.foreground;
	fill = reverse ? fState.foreground : fState.background;
}


pp::Point
CanvasView::_Constrain(pp::Point p, uint32 modifiers, bool square) const
{
	if ((modifiers & B_SHIFT_KEY) == 0)
		return p;
	int dx = p.x - fStart.x;
	int dy = p.y - fStart.y;
	int adx = std::abs(dx), ady = std::abs(dy);
	int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
	if (!square) {
		// Lines snap to the nearest multiple of 45 degrees.
		if (adx > 2 * ady)
			return pp::Point{p.x, fStart.y};
		if (ady > 2 * adx)
			return pp::Point{fStart.x, p.y};
	}
	int size = std::max(adx, ady);
	return pp::Point{fStart.x + sx * size, fStart.y + sy * size};
}


void
CanvasView::ToolDown(pp::Point p, bool secondary, uint32 modifiers,
	int32 clicks)
{
	int32 tool = fState.tool;
	// A polygon or curve keeps the colours of its first drag.
	bool continuing = (tool == kToolPolygon && !fPolygon.empty())
		|| (tool == kToolCurve && fCurveStage > 1);
	if (!continuing)
		fSecondary = secondary;
	fStart = fLast = p;
	fHover = p;

	if (IsSelectionTool(tool)) {
		_CommitText();
		if (fSelection.active && Inside(fSelection.rect, p)) {
			fDrag = kDragSelectionMove;
			fGrabOffset = pp::Point{p.x - fSelection.rect.x,
				p.y - fSelection.rect.y};
			// Control-drag leaves a copy behind.
			if ((modifiers & B_CONTROL_KEY) != 0) {
				_PushUndo();
				_StampSelection();
			}
			return;
		}
		_CommitSelection();
		fDrag = kDragNewSelection;
		fOutline.clear();
		fOutline.push_back(p);
		return;
	}
	if (tool == kToolText) {
		_CommitSelection();
		if (fText.active && Inside(fText.rect, p))
			return;
		_CommitText();
		fDrag = kDragNewTextBox;
		return;
	}

	_CommitText();
	_CommitSelection();
	fDrag = kDragTool;
	pp::Pixel color = fSecondary ? fState.background : fState.foreground;
	switch (tool) {
		case kToolPickColor:
		{
			if (!fImage.Contains(p.x, p.y))
				break;
			pp::Pixel picked = fImage.At(p.x, p.y) | 0xff000000;
			bool toBackground = fSecondary;
			if (toBackground)
				fState.background = picked;
			else
				fState.foreground = picked;
			Paint()->ChangeState([=](PaintState& s) {
				if (toBackground)
					s.background = picked;
				else
					s.foreground = picked;
			});
			break;
		}
		case kToolFill:
		{
			if (!fImage.Contains(p.x, p.y) || fImage.At(p.x, p.y) == color)
				break;
			_PushUndo();
			pp::Rect changed = pp::FloodFill(fImage, p.x, p.y, color);
			_SyncBitmap(changed);
			_Changed();
			break;
		}
		case kToolMagnifier:
			break;
		case kToolPencil:
		case kToolBrush:
		case kToolEraser:
		case kToolAirbrush:
			_PushUndo();
			_PaintAt(p, p);
			if (tool == kToolAirbrush) {
				BMessage pulse(kMsgSprayPulse);
				fSprayRunner = new BMessageRunner(BMessenger(this), &pulse,
					kSprayInterval);
			}
			break;
		case kToolPolygon:
			if (fPolygon.empty()) {
				fBefore = fImage;
				fDirty = pp::Rect();
				fPolygon.push_back(p);
				fPolygon.push_back(p);
			} else if (clicks >= 2 || (std::abs(p.x - fPolygon[0].x) <= 2
					&& std::abs(p.y - fPolygon[0].y) <= 2
					&& fPolygon.size() > 2)) {
				// A double click, or a click on the first point, closes it.
				_FinishPolygon();
				break;
			} else
				fPolygon.push_back(p);
			_DrawPolygonInProgress();
			break;
		case kToolCurve:
			if (fCurveStage == 0) {
				fBefore = fImage;
				fDirty = pp::Rect();
				fCurveStage = 1;
				for (pp::Point& c : fCurve)
					c = p;
			}
			_DrawShape(p, modifiers);
			break;
		default:
			// Line, rectangle, ellipse, rounded rectangle
			fBefore = fImage;
			fDirty = pp::Rect();
			_DrawShape(p, modifiers);
	}
}


void
CanvasView::ToolMove(pp::Point p, uint32 modifiers)
{
	fHover = p;
	_Status(p);
	switch (fDrag) {
		case kDragNone:
			return;

		case kDragCanvasResize:
			if (fHandle != kHandleBottom)
				fResizeTo.x = std::max(1, p.x);
			if (fHandle != kHandleRight)
				fResizeTo.y = std::max(1, p.y);
			_StatusSize(fResizeTo.x, fResizeTo.y);
			Invalidate();
			break;

		case kDragSelectionResize:
		{
			pp::Rect old = fSelection.rect;
			int left = fGrabRect.x, top = fGrabRect.y;
			int right = fGrabRect.Right(), bottom = fGrabRect.Bottom();
			int h = fHandle;
			if (h == kHandleTopLeft || h == kHandleLeft || h == kHandleBottomLeft)
				left = std::min(p.x, right - 1);
			if (h == kHandleTopRight || h == kHandleRight
				|| h == kHandleBottomRight)
				right = std::max(p.x + 1, left + 1);
			if (h == kHandleTopLeft || h == kHandleTop || h == kHandleTopRight)
				top = std::min(p.y, bottom - 1);
			if (h == kHandleBottomLeft || h == kHandleBottom
				|| h == kHandleBottomRight)
				bottom = std::max(p.y + 1, top + 1);
			fSelection.rect = pp::Rect{left, top, right - left, bottom - top};
			_UpdateSelectionScale();
			_InvalidateImageRect(old.Union(fSelection.rect));
			_StatusSize(fSelection.rect.w, fSelection.rect.h);
			break;
		}

		case kDragSelectionMove:
		{
			pp::Rect old = fSelection.rect;
			fSelection.rect.x = p.x - fGrabOffset.x;
			fSelection.rect.y = p.y - fGrabOffset.y;
			// Shift-drag smears the selection across the picture.
			if ((modifiers & B_SHIFT_KEY) != 0)
				_StampSelection();
			_InvalidateImageRect(old.Union(fSelection.rect));
			break;
		}

		case kDragNewSelection:
		case kDragNewTextBox:
		{
			pp::Rect old = pp::Rect::FromCorners(fStart.x, fStart.y, fLast.x,
				fLast.y);
			if (fState.tool == kToolFreeFormSelect) {
				if (p.x != fOutline.back().x || p.y != fOutline.back().y) {
					old = pp::PointsBounds(fOutline);
					fOutline.push_back(p);
					_InvalidateImageRect(old.Union(pp::PointsBounds(fOutline)));
				}
			} else {
				fLast = p;
				_InvalidateImageRect(old.Union(pp::Rect::FromCorners(fStart.x,
					fStart.y, p.x, p.y)));
				_StatusSize(std::abs(p.x - fStart.x) + 1,
					std::abs(p.y - fStart.y) + 1);
			}
			break;
		}

		case kDragTool:
			switch (fState.tool) {
				case kToolPencil:
				case kToolBrush:
				case kToolEraser:
				case kToolAirbrush:
					_PaintAt(fLast, p);
					break;
				case kToolPolygon:
					if (fPolygon.empty())
						break;
					fPolygon.back() = p;
					_DrawPolygonInProgress();
					break;
				case kToolLine:
				case kToolCurve:
				case kToolRectangle:
				case kToolEllipse:
				case kToolRoundedRectangle:
					_DrawShape(p, modifiers);
					break;
			}
			break;
	}
	fLast = p;
}


void
CanvasView::ToolUp(pp::Point p, uint32 modifiers)
{
	DragMode drag = fDrag;
	fDrag = kDragNone;
	switch (drag) {
		case kDragNone:
		case kDragSelectionMove:
		case kDragSelectionResize:
			break;

		case kDragCanvasResize:
			if (fResizeTo.x != fImage.Width() || fResizeTo.y != fImage.Height())
				ResizeImage(fResizeTo.x, fResizeTo.y);
			Invalidate();
			break;

		case kDragNewSelection:
			if (fState.tool == kToolFreeFormSelect) {
				pp::Rect bounds = pp::PointsBounds(fOutline);
				if (fOutline.size() >= 3)
					_CreateSelection(bounds, &fOutline);
				_InvalidateImageRect(bounds);
				fOutline.clear();
			} else {
				pp::Rect rect = pp::Rect::FromCorners(fStart.x, fStart.y, p.x,
					p.y);
				if (p.x != fStart.x || p.y != fStart.y)
					_CreateSelection(rect, NULL);
				_InvalidateImageRect(rect);
			}
			break;

		case kDragNewTextBox:
		{
			pp::Rect rect = pp::Rect::FromCorners(fStart.x, fStart.y, p.x, p.y);
			_InvalidateImageRect(rect);
			// A click without a drag still makes a box to type in.
			if (rect.w < 4 && rect.h < 4)
				rect = pp::Rect{fStart.x, fStart.y, 150, 0};
			fText = TextBox();
			fText.active = true;
			fText.rect = rect;
			_RenderText();
			_Changed(false);
			break;
		}

		case kDragTool:
			switch (fState.tool) {
				case kToolPickColor:
				{
					// Back to the tool used before, as MS Paint does.
					int32 previous = fState.previousTool;
					fState.tool = previous;
					Paint()->ChangeState([=](PaintState& s) {
						s.tool = previous;
					});
					break;
				}
				case kToolMagnifier:
				{
					BPoint center(p.x, p.y);
					SetZoom(fZoom == 1 ? fState.magnifierZoom : 1, &center);
					break;
				}
				case kToolPencil:
				case kToolBrush:
				case kToolEraser:
				case kToolAirbrush:
					delete fSprayRunner;
					fSprayRunner = NULL;
					_Changed();
					break;
				case kToolLine:
				case kToolRectangle:
				case kToolEllipse:
				case kToolRoundedRectangle:
					_DrawShape(p, modifiers);
					_PushUndoState(std::move(fBefore));
					fBefore = pp::Raster();
					fDirty = pp::Rect();
					_Changed();
					break;
				case kToolCurve:
					// Line, first bend, second bend.
					if (++fCurveStage > 3)
						_FinishCurve();
					break;
			}
			break;
	}
	_StatusSize(-1, -1);
}


void
CanvasView::_PaintAt(pp::Point from, pp::Point to)
{
	pp::Pixel color = fSecondary ? fState.background : fState.foreground;
	pp::Rect touched = pp::Rect::FromCorners(from.x, from.y, to.x, to.y);
	switch (fState.tool) {
		case kToolPencil:
			pp::DrawLine(fImage, from.x, from.y, to.x, to.y, color, 1);
			break;
		case kToolBrush:
			pp::BrushLine(fImage, from.x, from.y, to.x, to.y,
				(pp::BrushShape)fState.brushShape, fState.brushSize, color);
			touched = Inflate(touched, fState.brushSize);
			break;
		case kToolEraser:
		{
			// Primary: erase to the background colour. Secondary: the color
			// eraser, which only replaces the foreground colour.
			int size = fState.eraserSize;
			pp::BresenhamLine(from.x, from.y, to.x, to.y, [&](int x, int y) {
				pp::Rect r{x - size / 2, y - size / 2, size, size};
				if (fSecondary)
					pp::ReplaceColor(fImage, r, fState.foreground,
						fState.background);
				else
					fImage.FillRect(r, fState.background);
			});
			touched = Inflate(touched, size);
			break;
		}
		case kToolAirbrush:
			fLast = to;
			_Spray();
			return;
	}
	_SyncBitmap(touched);
}


void
CanvasView::_Spray()
{
	pp::Pixel color = fSecondary ? fState.background : fState.foreground;
	for (int i = 0; i < 4; i++) {
		pp::Spray(fImage, fLast.x, fLast.y, fState.airbrushSize, color,
			fRandom);
	}
	int size = fState.airbrushSize;
	_SyncBitmap(pp::Rect{fLast.x - size / 2 - 1, fLast.y - size / 2 - 1,
		size + 2, size + 2});
}


void
CanvasView::_RestoreBefore()
{
	if (!fDirty.IsEmpty())
		fImage.BlitRect(fBefore, fDirty, fDirty.x, fDirty.y);
}


void
CanvasView::_DrawShape(pp::Point p, uint32 modifiers)
{
	pp::Rect old = fDirty;
	_RestoreBefore();
	pp::Pixel stroke, fill;
	_ShapeColors(stroke, fill);
	int width = fState.strokeSize;
	pp::FillStyle style = (pp::FillStyle)fState.fillStyle;
	pp::Rect touched;
	switch (fState.tool) {
		case kToolLine:
		{
			pp::Point q = _Constrain(p, modifiers, false);
			pp::DrawLine(fImage, fStart.x, fStart.y, q.x, q.y, stroke, width);
			touched = Inflate(pp::Rect::FromCorners(fStart.x, fStart.y, q.x,
				q.y), width);
			break;
		}
		case kToolCurve:
		{
			if (fCurveStage == 1) {
				fCurve[3] = p;
				fCurve[1] = fCurve[0];
				fCurve[2] = p;
			} else if (fCurveStage == 2) {
				fCurve[1] = fCurve[2] = p;
			} else
				fCurve[2] = p;
			pp::DrawBezier(fImage, fCurve[0], fCurve[1], fCurve[2], fCurve[3],
				stroke, width);
			std::vector<pp::Point> points(fCurve, fCurve + 4);
			touched = Inflate(pp::PointsBounds(points), width);
			break;
		}
		default:
		{
			pp::Point q = _Constrain(p, modifiers, true);
			pp::Rect box = pp::Rect::FromCorners(fStart.x, fStart.y, q.x, q.y);
			if (fState.tool == kToolRectangle)
				pp::DrawRectangle(fImage, box, style, stroke, fill, width);
			else if (fState.tool == kToolEllipse)
				pp::DrawEllipse(fImage, box, style, stroke, fill, width);
			else
				pp::DrawRoundedRectangle(fImage, box, style, stroke, fill,
					width);
			touched = box;
			_StatusSize(box.w, box.h);
		}
	}
	fDirty = touched.Intersect(fImage.Bounds());
	_SyncBitmap(old.Union(fDirty));
}


void
CanvasView::_DrawPolygonInProgress()
{
	pp::Rect old = fDirty;
	_RestoreBefore();
	pp::Pixel stroke, fill;
	_ShapeColors(stroke, fill);
	pp::DrawPolyline(fImage, fPolygon, stroke, fState.strokeSize);
	fDirty = Inflate(pp::PointsBounds(fPolygon), fState.strokeSize)
		.Intersect(fImage.Bounds());
	_SyncBitmap(old.Union(fDirty));
}


void
CanvasView::_FinishPolygon()
{
	if (fPolygon.empty())
		return;
	pp::Rect old = fDirty;
	_RestoreBefore();
	pp::Pixel stroke, fill;
	_ShapeColors(stroke, fill);
	pp::DrawPolygon(fImage, fPolygon, (pp::FillStyle)fState.fillStyle, stroke,
		fill, fState.strokeSize);
	pp::Rect touched = Inflate(pp::PointsBounds(fPolygon), fState.strokeSize);
	_PushUndoState(std::move(fBefore));
	fBefore = pp::Raster();
	fPolygon.clear();
	fDirty = pp::Rect();
	_SyncBitmap(old.Union(touched));
	_Changed();
}


void
CanvasView::_FinishCurve()
{
	if (fCurveStage == 0)
		return;
	_PushUndoState(std::move(fBefore));
	fBefore = pp::Raster();
	fCurveStage = 0;
	fDirty = pp::Rect();
	_Changed();
}


void
CanvasView::FinishPending()
{
	if (fDrag != kDragNone)
		return;
	_CommitText();
	_CommitSelection();
	_FinishPolygon();
	_FinishCurve();
}


void
CanvasView::CancelOperation()
{
	DragMode drag = fDrag;
	fDrag = kDragNone;
	delete fSprayRunner;
	fSprayRunner = NULL;
	switch (drag) {
		case kDragTool:
			if (!fBefore.Bounds().IsEmpty()) {
				// Shapes, polygons and curves: back to before they began.
				fImage = fBefore;
				fBefore = pp::Raster();
				fPolygon.clear();
				fCurveStage = 0;
				fDirty = pp::Rect();
				_SyncAll();
			} else if (!fUndo.empty() && (fState.tool == kToolPencil
					|| fState.tool == kToolBrush || fState.tool == kToolEraser
					|| fState.tool == kToolAirbrush)) {
				fImage = std::move(fUndo.back());
				fUndo.pop_back();
				_SyncAll();
			}
			break;
		case kDragNone:
			// Escape with nothing in progress puts a selection down.
			if (!fPolygon.empty() || fCurveStage > 0) {
				fImage = fBefore;
				fBefore = pp::Raster();
				fPolygon.clear();
				fCurveStage = 0;
				_SyncAll();
			} else
				FinishPending();
			break;
		default:
			fOutline.clear();
			Invalidate();
	}
	_Changed(false);
}


void
CanvasView::StateChanged(const PaintState& oldState)
{
	PaintState state = CurrentState();
	// The tool changed while the user was dragging: let the drag finish
	// with the tool it started with.
	if (fDrag != kDragNone && state.tool != fState.tool)
		state.tool = fState.tool;
	fState = state;

	if (state.tool != oldState.tool) {
		if (!IsSelectionTool(state.tool))
			_CommitSelection();
		if (state.tool != kToolText)
			_CommitText();
		if (oldState.tool == kToolPolygon)
			_FinishPolygon();
		if (oldState.tool == kToolCurve)
			_FinishCurve();
	}
	if (fSelection.active && (state.background != oldState.background
			|| state.transparentMode != oldState.transparentMode)) {
		_RebuildSelectionBitmap();
		_InvalidateImageRect(fSelection.rect);
	}
	if (fText.active)
		_RenderText();
	if (state.tool == kToolEraser || oldState.tool == kToolEraser)
		Invalidate();
}


void
CanvasView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSprayPulse:
			if (fDrag == kDragTool && fState.tool == kToolAirbrush)
				_Spray();
			break;
		default:
			BView::MessageReceived(message);
	}
}


void
CanvasView::KeyDown(const char* bytes, int32 numBytes)
{
	if (fText.active) {
		switch (bytes[0]) {
			case B_BACKSPACE:
			{
				// Remove the last UTF-8 character.
				int32 length = fText.text.Length();
				const char* s = fText.text.String();
				int32 i = length - 1;
				while (i > 0 && (s[i] & 0xc0) == 0x80)
					i--;
				if (i >= 0)
					fText.text.Truncate(i);
				_RenderText();
				return;
			}
			case B_ENTER:
				InsertText("\n");
				return;
			case B_ESCAPE:
				_CommitText();
				return;
			default:
				if ((uint8)bytes[0] >= 0x20 && bytes[0] != B_DELETE) {
					BString typed(bytes, numBytes);
					InsertText(typed.String());
					return;
				}
		}
	}

	switch (bytes[0]) {
		case B_ESCAPE:
			CancelOperation();
			break;
		case B_DELETE:
			DeleteSelection();
			break;
		case B_ENTER:
			FinishPending();
			break;
		case B_LEFT_ARROW:
		case B_RIGHT_ARROW:
		case B_UP_ARROW:
		case B_DOWN_ARROW:
			if (fSelection.active) {
				pp::Rect old = fSelection.rect;
				fSelection.rect.x += bytes[0] == B_LEFT_ARROW ? -1
					: bytes[0] == B_RIGHT_ARROW ? 1 : 0;
				fSelection.rect.y += bytes[0] == B_UP_ARROW ? -1
					: bytes[0] == B_DOWN_ARROW ? 1 : 0;
				_InvalidateImageRect(old.Union(fSelection.rect));
				break;
			}
			BView::KeyDown(bytes, numBytes);
			break;
		default:
			BView::KeyDown(bytes, numBytes);
	}
}


// #pragma mark - Status bar


void
CanvasView::_Status(pp::Point p)
{
	PaintWindow* window = dynamic_cast<PaintWindow*>(Window());
	if (window == NULL)
		return;
	BString text;
	if (fImage.Contains(p.x, p.y))
		text.SetToFormat("%d,%d", p.x, p.y);
	window->SetCoordinates(text);
}


void
CanvasView::_StatusSize(int32 width, int32 height)
{
	PaintWindow* window = dynamic_cast<PaintWindow*>(Window());
	if (window == NULL)
		return;
	BString text;
	if (width >= 0)
		text.SetToFormat("%" B_PRId32 "x%" B_PRId32, width, height);
	else if (fSelection.active)
		text.SetToFormat("%dx%d", fSelection.rect.w, fSelection.rect.h);
	window->SetSizeText(text);
}


// #pragma mark - Selection


void
CanvasView::_CreateSelection(pp::Rect rect,
	const std::vector<pp::Point>* outline)
{
	rect = rect.Intersect(fImage.Bounds());
	if (rect.IsEmpty())
		return;
	_PushUndo();
	fSelection.source = fImage.Copy(rect);
	if (outline != NULL) {
		pp::Mask mask = pp::PolygonMask(*outline, rect);
		for (int y = 0; y < rect.h; y++) {
			for (int x = 0; x < rect.w; x++) {
				if (!mask.Get(x, y))
					fSelection.source.At(x, y) = 0;
			}
		}
	}
	// The selection is lifted off the picture, leaving the background
	// colour behind.
	for (int y = 0; y < rect.h; y++) {
		for (int x = 0; x < rect.w; x++) {
			if (pp::Alpha(fSelection.source.At(x, y)) != 0)
				fImage.At(rect.x + x, rect.y + y) = fState.background;
		}
	}
	fSelection.active = true;
	fSelection.rect = rect;
	_UpdateSelectionScale();
	_SyncBitmap(rect);
	_StatusSize(-1, -1);
	_Changed(false);
}


void
CanvasView::_UpdateSelectionScale()
{
	if (fSelection.rect.w == fSelection.source.Width()
		&& fSelection.rect.h == fSelection.source.Height()) {
		fSelection.scaled = fSelection.source;
	} else {
		fSelection.scaled = pp::ScaleNearest(fSelection.source,
			fSelection.rect.w, fSelection.rect.h);
	}
	_RebuildSelectionBitmap();
}


void
CanvasView::_RebuildSelectionBitmap()
{
	delete fSelectionBitmap;
	fSelectionBitmap = NULL;
	if (!fSelection.active)
		return;
	pp::Raster shown = fSelection.scaled;
	if (fState.transparentMode) {
		pp::Pixel* p = shown.Bits();
		for (size_t i = 0; i < (size_t)shown.Width() * shown.Height(); i++) {
			if (p[i] == fState.background)
				p[i] = 0;
		}
	}
	fSelectionBitmap = ImageIO::ToBitmap(shown);
}


void
CanvasView::_StampSelection()
{
	pp::Composite(fImage, fSelection.scaled, fSelection.rect.x,
		fSelection.rect.y,
		fState.transparentMode ? &fState.background : nullptr);
	_SyncBitmap(fSelection.rect);
	_Changed();
}


void
CanvasView::_CommitSelection()
{
	if (!fSelection.active)
		return;
	_StampSelection();
	_DiscardSelection();
}


void
CanvasView::_DiscardSelection()
{
	if (fSelection.active)
		_InvalidateImageRect(fSelection.rect);
	fSelection = Selection();
	delete fSelectionBitmap;
	fSelectionBitmap = NULL;
}


void
CanvasView::SelectAll()
{
	FinishPending();
	if (!IsSelectionTool(fState.tool)) {
		fState.tool = kToolSelect;
		Paint()->ChangeState([](PaintState& s) {
			s.previousTool = s.tool;
			s.tool = kToolSelect;
		});
	}
	_CreateSelection(fImage.Bounds(), NULL);
}


void
CanvasView::DeleteSelection()
{
	if (!fSelection.active)
		return;
	_DiscardSelection();
	_StatusSize(-1, -1);
	_Changed();
}


bool
CanvasView::SelectionRaster(pp::Raster& raster) const
{
	if (!fSelection.active)
		return false;
	raster = fSelection.scaled;
	return true;
}


void
CanvasView::PasteRaster(const pp::Raster& raster)
{
	FinishPending();
	if (!IsSelectionTool(fState.tool)) {
		fState.tool = kToolSelect;
		Paint()->ChangeState([](PaintState& s) {
			s.previousTool = s.tool;
			s.tool = kToolSelect;
		});
	}
	_PushUndo();
	// At the top left of what's in view.
	pp::Point at = _ImagePoint(Bounds().LeftTop() + BPoint(kInset, kInset));
	at.x = std::clamp(at.x, 0, std::max(0, fImage.Width() - 1));
	at.y = std::clamp(at.y, 0, std::max(0, fImage.Height() - 1));
	fSelection = Selection();
	fSelection.active = true;
	fSelection.source = raster;
	fSelection.rect = pp::Rect{at.x, at.y, raster.Width(), raster.Height()};
	_UpdateSelectionScale();
	_InvalidateImageRect(fSelection.rect);
	_StatusSize(-1, -1);
	_Changed();
}


// #pragma mark - Image menu


void
CanvasView::_TransformSelection(pp::Raster&& raster)
{
	pp::Rect old = fSelection.rect;
	fSelection.source = std::move(raster);
	fSelection.rect.w = fSelection.source.Width();
	fSelection.rect.h = fSelection.source.Height();
	_UpdateSelectionScale();
	_InvalidateImageRect(old.Union(fSelection.rect));
	_StatusSize(-1, -1);
	_Changed();
}


void
CanvasView::_TransformImage(pp::Raster&& raster)
{
	FinishPending();
	// Everything that can run out of memory comes before the picture
	// changes; PaintWindow::DispatchMessage tells the user.
	std::unique_ptr<BBitmap> bitmap(ImageIO::ToBitmap(raster));
	if (!bitmap)
		throw std::bad_alloc();
	_PushUndo();
	fImage = std::move(raster);
	delete fBitmap;
	fBitmap = bitmap.release();
	fHasAlpha = fImage.HasTransparency();
	_UpdateScrollBars();
	Invalidate();
	_Changed();
}


void
CanvasView::FlipHorizontal()
{
	_CommitText();
	if (fSelection.active) {
		pp::Raster r = fSelection.scaled;
		pp::FlipHorizontal(r);
		_TransformSelection(std::move(r));
		return;
	}
	pp::Raster r = Flattened();
	pp::FlipHorizontal(r);
	_TransformImage(std::move(r));
}


void
CanvasView::FlipVertical()
{
	_CommitText();
	if (fSelection.active) {
		pp::Raster r = fSelection.scaled;
		pp::FlipVertical(r);
		_TransformSelection(std::move(r));
		return;
	}
	pp::Raster r = Flattened();
	pp::FlipVertical(r);
	_TransformImage(std::move(r));
}


void
CanvasView::Rotate(double degrees)
{
	_CommitText();
	// Corners a rotation uncovers are clear in a selection, and the
	// background colour in the picture.
	if (fSelection.active) {
		_TransformSelection(pp::RotateDegrees(fSelection.scaled, degrees, 0));
		return;
	}
	_TransformImage(pp::RotateDegrees(Flattened(), degrees,
		fState.background));
}


void
CanvasView::StretchSkew(double xScale, double yScale, double hSkew,
	double vSkew)
{
	_CommitText();
	if (fSelection.active) {
		_TransformSelection(pp::StretchSkew(fSelection.scaled, xScale, yScale,
			hSkew, vSkew, 0));
		return;
	}
	_TransformImage(pp::StretchSkew(Flattened(), xScale, yScale, hSkew, vSkew,
		fState.background));
}


void
CanvasView::InvertColors()
{
	_CommitText();
	if (fSelection.active) {
		pp::Raster r = fSelection.scaled;
		pp::Invert(r);
		_TransformSelection(std::move(r));
		return;
	}
	pp::Raster r = Flattened();
	pp::Invert(r);
	_TransformImage(std::move(r));
}


void
CanvasView::ResizeImage(int32 width, int32 height)
{
	if (width < 1 || height < 1)
		return;
	_TransformImage(pp::ResizeCanvas(Flattened(), width, height,
		fState.background));
}


void
CanvasView::ClearImage()
{
	pp::Raster r(fImage.Width(), fImage.Height(), fState.background);
	_TransformImage(std::move(r));
}


// #pragma mark - Text


void
CanvasView::InsertText(const char* text)
{
	if (!fText.active)
		return;
	fText.text << text;
	_RenderText();
}


void
CanvasView::_RenderText()
{
	if (!fText.active)
		return;
	pp::Rect old = fText.rect;
	int32 height = fText.rect.h;
	fText.rendered = TextRender::Render(fText.text, fText.rect.w, fState,
		&height);
	// The box grows downwards as lines are added.
	fText.rect.h = height;
	delete fTextBitmap;
	fTextBitmap = ImageIO::ToBitmap(fText.rendered);
	_InvalidateImageRect(old.Union(fText.rect));
}


void
CanvasView::_CommitText()
{
	if (!fText.active)
		return;
	if (fText.text.Length() > 0) {
		_PushUndo();
		pp::Composite(fImage, fText.rendered, fText.rect.x, fText.rect.y);
		_SyncBitmap(fText.rect);
		fModified = true;
	}
	_InvalidateImageRect(fText.rect);
	fText = TextBox();
	delete fTextBitmap;
	fTextBitmap = NULL;
	_Changed(false);
}
