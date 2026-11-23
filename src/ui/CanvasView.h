/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 *
 * The picture: the drawing surface inside the scroll view, with the tools'
 * mouse handling, the floating selection, the text box, undo and zoom.
 * The image sits at a small inset in a grey area, with MS Paint's resize
 * handles at its right and bottom edges.
 */
#ifndef CANVAS_VIEW_H
#define CANVAS_VIEW_H

#include <View.h>

#include <random>
#include <vector>

#include "App.h"
#include "Raster.h"

class BBitmap;
class BMessageRunner;


class CanvasView : public BView {
public:
							CanvasView();
	virtual					~CanvasView();

	virtual void			AttachedToWindow();
	virtual void			Draw(BRect updateRect);
	virtual void			FrameResized(float width, float height);
	virtual void			MouseDown(BPoint where);
	virtual void			MouseMoved(BPoint where, uint32 transit,
								const BMessage* dragMessage);
	virtual void			MouseUp(BPoint where);
	virtual void			KeyDown(const char* bytes, int32 numBytes);
	virtual void			MessageReceived(BMessage* message);

	// The document
	void					SetImage(const pp::Raster& image);
	const pp::Raster&		Image() const { return fImage; }
	// The picture as it would be saved: with the floating selection and
	// the text box painted in.
	pp::Raster				Flattened() const;
	bool					IsModified() const { return fModified; }
	void					SetModified(bool modified);

	bool					CanUndo() const;
	bool					CanRedo() const { return !fRedo.empty(); }
	void					Undo();
	void					Redo();

	// View
	int32					Zoom() const { return fZoom; }
	// Keeps `center` (image coordinates) in the middle when given.
	void					SetZoom(int32 zoom, const BPoint* center = NULL);
	bool					ShowGrid() const { return fShowGrid; }
	void					SetShowGrid(bool show);

	// Selection
	bool					HasSelection() const { return fSelection.active; }
	void					SelectAll();
	void					DeleteSelection();
	bool					SelectionRaster(pp::Raster& raster) const;
	// Places a picture as a new selection at the top left of the view.
	void					PasteRaster(const pp::Raster& raster);
	bool					HasText() const { return fText.active; }
	void					InsertText(const char* text);

	// Image menu: these work on the selection if there is one.
	void					FlipHorizontal();
	void					FlipVertical();
	void					Rotate(double degrees);
	void					StretchSkew(double xScale, double yScale,
								double hSkew, double vSkew);
	void					InvertColors();
	void					ResizeImage(int32 width, int32 height);
	void					ClearImage();

	// Ends whatever is in progress (polygon, curve, text, selection).
	void					FinishPending();
	void					CancelOperation();
	void					StateChanged(const PaintState& oldState);

	// Tool input in image coordinates; the mouse handlers and the test
	// script both come through here.
	void					ToolDown(pp::Point p, bool secondary,
								uint32 modifiers, int32 clicks);
	void					ToolMove(pp::Point p, uint32 modifiers);
	void					ToolUp(pp::Point p, uint32 modifiers);

private:
	// A free-form selection's pixels outside its outline are clear (0).
	struct Selection {
		bool				active = false;
		pp::Raster			source;
		pp::Rect			rect;			// where, at what size
		pp::Raster			scaled;			// source at rect's size
	};
	struct TextBox {
		bool				active = false;
		pp::Rect			rect;
		BString				text;
		pp::Raster			rendered;		// alpha 0 = transparent
	};
	enum DragMode {
		kDragNone = 0,
		kDragTool,
		kDragSelectionMove,
		kDragSelectionResize,
		kDragCanvasResize,
		kDragNewSelection,
		kDragNewTextBox
	};

	pp::Point				_ImagePoint(BPoint where) const;
	BRect					_ViewRect(pp::Rect r) const;
	BRect					_ImageFrame() const;
	void					_SyncBitmap(pp::Rect r);
	void					_SyncAll();
	void					_InvalidateImageRect(pp::Rect r);
	void					_UpdateScrollBars();
	void					_UpdateCursor(BPoint where);
	void					_Status(pp::Point p);
	void					_StatusSize(int32 width, int32 height);

	void					_PushUndo();
	void					_PushUndoState(pp::Raster&& state);
	// Updates the window; `modified` marks the document as changed.
	void					_Changed(bool modified = true);

	// Shape tools redraw from the image as it was at the mouse down.
	void					_RestoreBefore();
	void					_DrawShape(pp::Point p, uint32 modifiers);
	void					_DrawPolygonInProgress();
	void					_FinishPolygon();
	void					_FinishCurve();
	pp::Point				_Constrain(pp::Point p, uint32 modifiers,
								bool square) const;
	void					_ShapeColors(pp::Pixel& stroke,
								pp::Pixel& fill) const;

	void					_PaintAt(pp::Point from, pp::Point to);
	void					_Spray();

	// Selection
	void					_CreateSelection(pp::Rect rect,
								const std::vector<pp::Point>* outline);
	void					_CommitSelection();
	void					_DiscardSelection();
	void					_UpdateSelectionScale();
	void					_RebuildSelectionBitmap();
	void					_StampSelection();
	void					_TransformSelection(pp::Raster&& raster);
	void					_TransformImage(pp::Raster&& raster);
	int32					_SelectionHandleAt(BPoint where) const;
	int32					_CanvasHandleAt(BPoint where) const;
	BRect					_HandleRect(BRect frame, int32 handle) const;
	void					_DrawHandles(BRect frame, bool all);
	void					_DrawDashedRect(BRect rect);

	// Text box
	void					_RenderText();
	void					_CommitText();

	pp::Raster				fImage;
	BBitmap*				fBitmap;
	bool					fHasAlpha;
	bool					fModified;
	std::vector<pp::Raster>	fUndo;
	std::vector<pp::Raster>	fRedo;

	int32					fZoom;
	bool					fShowGrid;

	PaintState				fState;			// copy, refreshed on change
	Selection				fSelection;
	BBitmap*				fSelectionBitmap;
	TextBox					fText;
	BBitmap*				fTextBitmap;

	// The current drag
	DragMode				fDrag;
	bool					fSecondary;
	pp::Point				fStart;
	pp::Point				fLast;
	pp::Raster				fBefore;
	pp::Rect				fDirty;			// what the last preview touched
	int32					fHandle;
	pp::Point				fGrabOffset;
	pp::Rect				fGrabRect;
	pp::Point				fResizeTo;		// canvas resize preview
	std::vector<pp::Point>	fOutline;		// free-form selection

	// Polygon and curve stay in progress between drags.
	std::vector<pp::Point>	fPolygon;
	int32					fCurveStage;	// 0 none, 1 line, 2 one bend
	pp::Point				fCurve[4];

	BMessageRunner*			fSprayRunner;
	std::mt19937			fRandom;

	bool					fMouseInside;
	pp::Point				fHover;
};

#endif	// CANVAS_VIEW_H
