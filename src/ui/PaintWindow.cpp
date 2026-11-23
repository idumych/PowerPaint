/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 */
#include "PaintWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <Clipboard.h>
#include <Directory.h>
#include <File.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Path.h>
#include <RecentItems.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollView.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>

#include "CanvasView.h"
#include "ColorBox.h"
#include "Constants.h"
#include "Dialogs.h"
#include "FontBar.h"
#include "ImageIO.h"
#include "StatusBar.h"
#include "ToolBox.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PaintWindow"


static const int32 kZoomLevels[] = { 1, 2, 4, 6, 8, 10 };


// View > View bitmap: the picture alone, full screen, until a click or key.
class BitmapViewWindow : public BWindow {
public:
	BitmapViewWindow(const pp::Raster& raster)
		:
		BWindow(BScreen().Frame(), "", B_NO_BORDER_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL, B_NOT_MOVABLE | B_NOT_RESIZABLE
				| B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE)
	{
		AddChild(new Viewer(Bounds(), ImageIO::ToBitmap(raster)));
	}

private:
	class Viewer : public BView {
	public:
		Viewer(BRect frame, BBitmap* bitmap)
			:
			BView(frame, "viewer", B_FOLLOW_ALL, B_WILL_DRAW),
			fBitmap(bitmap)
		{
			SetViewColor(BScreen().DesktopColor());
		}

		~Viewer()
		{
			delete fBitmap;
		}

		void AttachedToWindow()
		{
			MakeFocus(true);
		}

		void Draw(BRect updateRect)
		{
			if (fBitmap == NULL)
				return;
			BRect bounds = Bounds();
			BRect image = fBitmap->Bounds();
			BPoint at(floorf((bounds.Width() - image.Width()) / 2),
				floorf((bounds.Height() - image.Height()) / 2));
			SetDrawingMode(B_OP_ALPHA);
			DrawBitmap(fBitmap, at);
		}

		void MouseDown(BPoint where)
		{
			Window()->PostMessage(B_QUIT_REQUESTED);
		}

		void KeyDown(const char* bytes, int32 numBytes)
		{
			Window()->PostMessage(B_QUIT_REQUESTED);
		}

	private:
		BBitmap*	fBitmap;
	};
};


PaintWindow::PaintWindow(BRect frame, int32 untitledNumber)
	:
	BWindow(frame, B_TRANSLATE("Untitled"), B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fFontBar(NULL),
	fFontBarShown(false),
	fOpenPanel(NULL),
	fSavePanel(NULL),
	fHasRef(false),
	fUntitledNumber(untitledNumber),
	fQuitAfterSave(false),
	fState(CurrentState())
{
	_BuildMenus();
	fToolBox = new ToolBox();
	fCanvas = new CanvasView();
	fScrollView = new BScrollView("scroller", fCanvas, 0, true, true,
		B_NO_BORDER);
	fColorBox = new ColorBox();
	fStatusBar = new StatusBar();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.AddGroup(B_HORIZONTAL, 0)
			.Add(fToolBox)
			.Add(fScrollView)
		.End()
		.Add(fColorBox)
		.Add(fStatusBar);

	if (!fState.showToolBox)
		fToolBox->Hide();
	if (!fState.showColorBox)
		fColorBox->Hide();
	if (!fState.showStatusBar)
		fStatusBar->Hide();
	SetHelpText(NULL);
	_UpdateTitle();
	fCanvas->MakeFocus(true);
	_UpdateFontBar();
}


PaintWindow::~PaintWindow()
{
	delete fOpenPanel;
	delete fSavePanel;
}


BMenuItem*
PaintWindow::_AddItem(BMenu* menu, const char* label, uint32 what,
	char shortcut, uint32 modifiers)
{
	BMenuItem* item = new BMenuItem(label, new BMessage(what), shortcut,
		modifiers);
	menu->AddItem(item);
	return item;
}


void
PaintWindow::_BuildMenus()
{
	fMenuBar = new BMenuBar("menubar");

	BMenu* file = new BMenu(B_TRANSLATE("File"));
	_AddItem(file, B_TRANSLATE("New"), kMsgNewWindow, 'N');
	_AddItem(file, B_TRANSLATE("Open" B_UTF8_ELLIPSIS), kMsgOpen, 'O');
	BMenu* recent = BRecentFilesList::NewFileListMenu(
		B_TRANSLATE("Open recent"), NULL, NULL, be_app, 10, false, "image",
		APP_SIGNATURE);
	file->AddItem(new BMenuItem(recent, new BMessage(kMsgOpen)));
	file->AddSeparatorItem();
	_AddItem(file, B_TRANSLATE("Save"), kMsgSave, 'S');
	_AddItem(file, B_TRANSLATE("Save as" B_UTF8_ELLIPSIS), kMsgSaveAs, 'S',
		B_SHIFT_KEY);
	file->AddSeparatorItem();
	_AddItem(file, B_TRANSLATE("Close"), B_QUIT_REQUESTED, 'W');
	BMenuItem* quit = _AddItem(file, B_TRANSLATE("Quit"), B_QUIT_REQUESTED,
		'Q');
	quit->SetTarget(be_app);
	fMenuBar->AddItem(file);

	BMenu* edit = new BMenu(B_TRANSLATE("Edit"));
	fUndoItem = _AddItem(edit, B_TRANSLATE("Undo"), kMsgUndo, 'Z');
	fRedoItem = _AddItem(edit, B_TRANSLATE("Redo"), kMsgRedo, 'Z',
		B_SHIFT_KEY);
	edit->AddSeparatorItem();
	fCutItem = _AddItem(edit, B_TRANSLATE("Cut"), B_CUT, 'X');
	fCopyItem = _AddItem(edit, B_TRANSLATE("Copy"), B_COPY, 'C');
	_AddItem(edit, B_TRANSLATE("Paste"), B_PASTE, 'V');
	fClearSelectionItem = _AddItem(edit, B_TRANSLATE("Clear selection"),
		kMsgClearSelection);
	_AddItem(edit, B_TRANSLATE("Select all"), B_SELECT_ALL, 'A');
	edit->AddSeparatorItem();
	fCopyToItem = _AddItem(edit, B_TRANSLATE("Copy to" B_UTF8_ELLIPSIS),
		kMsgCopyTo);
	_AddItem(edit, B_TRANSLATE("Paste from" B_UTF8_ELLIPSIS), kMsgPasteFrom);
	fMenuBar->AddItem(edit);

	BMenu* view = new BMenu(B_TRANSLATE("View"));
	fToolBoxItem = _AddItem(view, B_TRANSLATE("Tool box"), kMsgToggleToolBox,
		'T');
	fColorBoxItem = _AddItem(view, B_TRANSLATE("Color box"),
		kMsgToggleColorBox, 'L');
	fStatusBarItem = _AddItem(view, B_TRANSLATE("Status bar"),
		kMsgToggleStatusBar);
	fTextToolbarItem = _AddItem(view, B_TRANSLATE("Text toolbar"),
		kMsgToggleTextToolbar);
	view->AddSeparatorItem();
	fZoomMenu = new BMenu(B_TRANSLATE("Zoom"));
	BMessage* normal = new BMessage(kMsgZoom);
	normal->AddInt32("zoom", 1);
	fZoomMenu->AddItem(new BMenuItem(B_TRANSLATE("Normal size"), normal,
		B_PAGE_UP));
	BMessage* large = new BMessage(kMsgZoom);
	large->AddInt32("zoom", 4);
	fZoomMenu->AddItem(new BMenuItem(B_TRANSLATE("Large size"), large,
		B_PAGE_DOWN));
	fZoomMenu->AddSeparatorItem();
	for (int32 zoom : kZoomLevels) {
		BMessage* message = new BMessage(kMsgZoom);
		message->AddInt32("zoom", zoom);
		BString label;
		label << zoom * 100 << "%";
		fZoomMenu->AddItem(new BMenuItem(label, message));
	}
	fZoomMenu->AddSeparatorItem();
	fGridItem = _AddItem(fZoomMenu, B_TRANSLATE("Show grid"), kMsgToggleGrid,
		'G', B_SHIFT_KEY);
	view->AddItem(fZoomMenu);
	_AddItem(view, B_TRANSLATE("View bitmap"), kMsgViewBitmap, 'F',
		B_SHIFT_KEY);
	fMenuBar->AddItem(view);

	BMenu* image = new BMenu(B_TRANSLATE("Image"));
	_AddItem(image, B_TRANSLATE("Flip/rotate" B_UTF8_ELLIPSIS),
		kMsgFlipRotate, 'R');
	_AddItem(image, B_TRANSLATE("Stretch/skew" B_UTF8_ELLIPSIS),
		kMsgStretchSkew);
	_AddItem(image, B_TRANSLATE("Invert colors"), kMsgInvertColors, 'I');
	_AddItem(image, B_TRANSLATE("Attributes" B_UTF8_ELLIPSIS), kMsgAttributes,
		'E');
	fClearImageItem = _AddItem(image, B_TRANSLATE("Clear image"),
		kMsgClearImage, 'N', B_SHIFT_KEY);
	image->AddSeparatorItem();
	fDrawOpaqueItem = _AddItem(image, B_TRANSLATE("Draw opaque"),
		kMsgToggleDrawOpaque);
	fMenuBar->AddItem(image);

	BMenu* colors = new BMenu(B_TRANSLATE("Colors"));
	_AddItem(colors, B_TRANSLATE("Edit colors" B_UTF8_ELLIPSIS),
		kMsgEditColors);
	_AddItem(colors, B_TRANSLATE("Get colors" B_UTF8_ELLIPSIS),
		kMsgGetColors);
	_AddItem(colors, B_TRANSLATE("Save colors" B_UTF8_ELLIPSIS),
		kMsgSaveColors);
	fMenuBar->AddItem(colors);

	BMenu* help = new BMenu(B_TRANSLATE("Help"));
	_AddItem(help, B_TRANSLATE("About PowerPaint" B_UTF8_ELLIPSIS), kMsgAbout);
	fMenuBar->AddItem(help);
}


void
PaintWindow::MenusBeginning()
{
	bool selection = fCanvas->HasSelection();
	fUndoItem->SetEnabled(fCanvas->CanUndo());
	fRedoItem->SetEnabled(fCanvas->CanRedo());
	fCutItem->SetEnabled(selection);
	fCopyItem->SetEnabled(selection);
	fClearSelectionItem->SetEnabled(selection);
	fCopyToItem->SetEnabled(selection);
	fClearImageItem->SetEnabled(!selection);

	PaintState state = CurrentState();
	fToolBoxItem->SetMarked(state.showToolBox);
	fColorBoxItem->SetMarked(state.showColorBox);
	fStatusBarItem->SetMarked(state.showStatusBar);
	fTextToolbarItem->SetMarked(state.showTextToolbar);
	fDrawOpaqueItem->SetMarked(!state.transparentMode);
	fGridItem->SetMarked(fCanvas->ShowGrid());
	fGridItem->SetEnabled(fCanvas->Zoom() >= 4);
	for (int32 i = 0; BMenuItem* item = fZoomMenu->ItemAt(i); i++) {
		int32 zoom;
		if (i > 1 && item->Message() != NULL
			&& item->Message()->FindInt32("zoom", &zoom) == B_OK) {
			item->SetMarked(zoom == fCanvas->Zoom());
		}
	}
}


void
PaintWindow::_UpdateTitle()
{
	if (fHasRef) {
		SetTitle(ImageIO::LeafName(fRef));
		return;
	}
	BString title = B_TRANSLATE("Untitled");
	if (fUntitledNumber > 1)
		title << " " << fUntitledNumber;
	SetTitle(title);
}


bool
PaintWindow::IsPristine() const
{
	return !fHasRef && !fCanvas->IsModified() && !fCanvas->CanUndo();
}


void
PaintWindow::Open(const entry_ref& ref)
{
	pp::Raster raster;
	if (!ImageIO::Load(ref, raster)) {
		_ShowOpenError(ref);
		return;
	}
	fCanvas->SetImage(raster);
	fRef = ref;
	fHasRef = true;
	_UpdateTitle();
	be_roster->AddToRecentDocuments(&ref, APP_SIGNATURE);
}


// #pragma mark - From the views


void
PaintWindow::DocumentChanged()
{
	fUndoItem->SetEnabled(fCanvas->CanUndo());
	fRedoItem->SetEnabled(fCanvas->CanRedo());
}


void
PaintWindow::ZoomChanged()
{
	fToolBox->SetZoom(fCanvas->Zoom());
}


void
PaintWindow::SetHelpText(const char* text)
{
	// Without a tool under the pointer, the current tool's help.
	fStatusBar->SetText(text != NULL ? text
		: ToolDescription(CurrentState().tool));
}


void
PaintWindow::SetCoordinates(const BString& text)
{
	fStatusBar->SetCoordinates(text);
}


void
PaintWindow::SetSizeText(const BString& text)
{
	fStatusBar->SetSize(text);
}


// #pragma mark - Shared state


void
PaintWindow::_StateChanged()
{
	PaintState old = fState;
	fState = CurrentState();
	fCanvas->StateChanged(old);
	fToolBox->StateChanged();
	fColorBox->StateChanged();
	if (fState.tool != old.tool)
		SetHelpText(NULL);

	struct { BView* view; bool show, was; } panes[] = {
		{ fToolBox, fState.showToolBox, old.showToolBox },
		{ fColorBox, fState.showColorBox, old.showColorBox },
		{ fStatusBar, fState.showStatusBar, old.showStatusBar },
	};
	for (auto& pane : panes) {
		if (pane.show && !pane.was)
			pane.view->Show();
		else if (!pane.show && pane.was)
			pane.view->Hide();
	}
	_UpdateFontBar();
	if (fFontBar != NULL && fFontBar->Lock()) {
		fFontBar->StateChanged();
		fFontBar->Unlock();
	}
}


void
PaintWindow::_UpdateFontBar()
{
	bool show = fState.tool == kToolText && fState.showTextToolbar;
	if (show && fFontBar == NULL) {
		fFontBar = new FontBar(this);
	}
	if (fFontBar == NULL || show == fFontBarShown)
		return;
	fFontBarShown = show;
	if (show)
		fFontBar->Show();
	else
		fFontBar->Hide();
}


// #pragma mark - Files


void
PaintWindow::_ShowError(const char* text)
{
	BAlert* alert = new BAlert(B_TRANSLATE("PowerPaint"), text,
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go(NULL);
}


void
PaintWindow::_ShowOpenError(const entry_ref& ref)
{
	BString text(B_TRANSLATE("PowerPaint can't open \"%name%\". It isn't a "
		"picture any installed translator can read."));
	text.ReplaceFirst("%name%", ImageIO::LeafName(ref));
	_ShowError(text);
}


void
PaintWindow::_ShowSaveError(const char* name, const BString& error)
{
	BString text(B_TRANSLATE("PowerPaint couldn't save \"%name%\": %error%"));
	text.ReplaceFirst("%name%", name);
	text.ReplaceFirst("%error%", error);
	_ShowError(text);
}


void
PaintWindow::_ShowSavePanel(uint32 what, const char* title, const char* name)
{
	if (fSavePanel == NULL) {
		fSavePanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), NULL,
			0, false);
	}
	fSavePanel->SetMessage(new BMessage(what));
	fSavePanel->Window()->SetTitle(title);
	fSavePanel->SetSaveText(name);
	if (fHasRef && what == kMsgSaveRequested) {
		BEntry entry(&fRef);
		BEntry parent;
		if (entry.GetParent(&parent) == B_OK)
			fSavePanel->SetPanelDirectory(&parent);
	}
	fSavePanel->Show();
}


void
PaintWindow::_ShowOpenPanel(uint32 what, const char* title, bool toApp)
{
	if (fOpenPanel == NULL)
		fOpenPanel = new BFilePanel(B_OPEN_PANEL, NULL, NULL, 0, false);
	fOpenPanel->SetTarget(toApp ? BMessenger(be_app) : BMessenger(this));
	fOpenPanel->SetMessage(new BMessage(what));
	fOpenPanel->Window()->SetTitle(title);
	fOpenPanel->Show();
}


bool
PaintWindow::_Save()
{
	if (!fHasRef) {
		BString name = Title();
		name << ".png";
		_ShowSavePanel(kMsgSaveRequested, B_TRANSLATE("Save as"), name);
		return false;
	}
	BEntry entry(&fRef);
	BEntry parent;
	entry_ref directory;
	if (entry.GetParent(&parent) != B_OK || parent.GetRef(&directory) != B_OK)
		return false;
	return _SaveTo(directory, ImageIO::LeafName(fRef));
}


bool
PaintWindow::_SaveTo(const entry_ref& directory, const char* name)
{
	BString error;
	if (ImageIO::Save(fCanvas->Flattened(), directory, name, &error)
			!= B_OK) {
		_ShowSaveError(name, error);
		return false;
	}
	BDirectory dir(&directory);
	BEntry entry(&dir, name);
	if (entry.GetRef(&fRef) == B_OK) {
		fHasRef = true;
		be_roster->AddToRecentDocuments(&fRef, APP_SIGNATURE);
	}
	fCanvas->SetModified(false);
	_UpdateTitle();
	return true;
}


bool
PaintWindow::QuitRequested()
{
	if (fCanvas->IsModified()) {
		BString text(B_TRANSLATE("Save changes to \"%name%\"?"));
		text.ReplaceFirst("%name%", Title());
		BAlert* alert = new BAlert(B_TRANSLATE("PowerPaint"), text,
			B_TRANSLATE("Cancel"), B_TRANSLATE("Don't save"),
			B_TRANSLATE("Save"), B_WIDTH_AS_USUAL, B_OFFSET_SPACING,
			B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		int32 choice = alert->Go();
		if (choice == 0)
			return false;
		if (choice == 2) {
			fQuitAfterSave = true;
			if (!_Save())
				return false;
		}
	}
	Paint()->RememberWindowFrame(Frame());
	if (fFontBar != NULL && fFontBar->Lock())
		fFontBar->Quit();
	fFontBar = NULL;
	be_app->PostMessage(kMsgWindowClosed);
	return true;
}


void
PaintWindow::_Paste(pp::Raster& raster)
{
	const pp::Raster& image = fCanvas->Image();
	if (raster.Width() > image.Width() || raster.Height() > image.Height()) {
		BAlert* alert = new BAlert(B_TRANSLATE("PowerPaint"),
			B_TRANSLATE("The image in the clipboard is larger than the "
				"bitmap.\nWould you like the bitmap enlarged?"),
			B_TRANSLATE("Cancel"), B_TRANSLATE("Don't enlarge"),
			B_TRANSLATE("Enlarge"), B_WIDTH_AS_USUAL, B_OFFSET_SPACING,
			B_IDEA_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		int32 choice = alert->Go();
		if (choice == 0)
			return;
		if (choice == 2) {
			fCanvas->FinishPending();
			fCanvas->ResizeImage(std::max(raster.Width(), image.Width()),
				std::max(raster.Height(), image.Height()));
		}
	}
	fCanvas->PasteRaster(raster);
}


void
PaintWindow::_SetColor(pp::Pixel color, int32 slot)
{
	Paint()->ChangeState([=](PaintState& s) {
		s.foreground = color;
		if (slot >= 0 && slot < (int32)s.palette.size())
			s.palette[slot] = color;
	});
}


// #pragma mark - Messages


void
PaintWindow::DispatchMessage(BMessage* message, BHandler* target)
{
	// Too large a picture (pp::kMaxPixels) or out of memory. Every change
	// allocates its result before replacing the picture, so it stays as it was.
	try {
		BWindow::DispatchMessage(message, target);
	} catch (const std::bad_alloc&) {
		_ShowError(B_TRANSLATE("The picture would be too large. PowerPaint "
			"handles up to 67 million pixels, such as 8192 × 8192. The "
			"picture wasn't changed."));
	}
}


void
PaintWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgStateChanged:
			_StateChanged();
			break;
		case kMsgScript:
			_RunScript(message->GetString("script", ""));
			break;

		case B_SIMPLE_DATA:
			if (message->HasRef("refs")) {
				BMessage refs(*message);
				refs.what = B_REFS_RECEIVED;
				be_app->PostMessage(&refs);
			} else
				BWindow::MessageReceived(message);
			break;

		// File
		case kMsgNewWindow:
			be_app->PostMessage(kMsgNewWindow);
			break;
		case kMsgOpen:
			_ShowOpenPanel(B_REFS_RECEIVED, B_TRANSLATE("Open"), true);
			break;
		case kMsgSave:
			_Save();
			break;
		case kMsgSaveAs:
		{
			BString name = fHasRef ? ImageIO::LeafName(fRef)
				: BString(Title()) << ".png";
			_ShowSavePanel(kMsgSaveRequested, B_TRANSLATE("Save as"), name);
			break;
		}
		case kMsgSaveRequested:
		{
			entry_ref directory;
			const char* name;
			if (message->FindRef("directory", &directory) == B_OK
				&& message->FindString("name", &name) == B_OK
				&& _SaveTo(directory, name) && fQuitAfterSave) {
				PostMessage(B_QUIT_REQUESTED);
			}
			fQuitAfterSave = false;
			break;
		}
		case B_CANCEL:
			// The save panel closed without saving.
			fQuitAfterSave = false;
			break;

		// Edit
		case kMsgUndo:
			fCanvas->Undo();
			break;
		case kMsgRedo:
			fCanvas->Redo();
			break;
		case B_CUT:
		case B_COPY:
		{
			pp::Raster raster;
			if (!fCanvas->SelectionRaster(raster))
				break;
			ImageIO::CopyToClipboard(raster);
			if (message->what == B_CUT)
				fCanvas->DeleteSelection();
			break;
		}
		case B_PASTE:
		{
			// Into the text box, text; anywhere else, a picture.
			if (fCanvas->HasText() && be_clipboard->Lock()) {
				const char* text;
				ssize_t length;
				BString pasted;
				if (be_clipboard->Data()->FindData("text/plain", B_MIME_TYPE,
						(const void**)&text, &length) == B_OK) {
					pasted.SetTo(text, length);
				}
				be_clipboard->Unlock();
				fCanvas->InsertText(pasted);
				break;
			}
			pp::Raster raster;
			if (ImageIO::PasteFromClipboard(raster))
				_Paste(raster);
			break;
		}
		case kMsgClearSelection:
			fCanvas->DeleteSelection();
			break;
		case B_SELECT_ALL:
			fCanvas->SelectAll();
			break;
		case kMsgCopyTo:
			if (fCanvas->HasSelection()) {
				_ShowSavePanel(kMsgCopyToRequested, B_TRANSLATE("Copy to"),
					"Untitled.png");
			}
			break;
		case kMsgCopyToRequested:
		{
			entry_ref directory;
			const char* name;
			pp::Raster raster;
			BString error;
			if (message->FindRef("directory", &directory) == B_OK
				&& message->FindString("name", &name) == B_OK
				&& fCanvas->SelectionRaster(raster)
				&& ImageIO::Save(raster, directory, name, &error) != B_OK) {
				_ShowSaveError(name, error);
			}
			break;
		}
		case kMsgPasteFrom:
			_ShowOpenPanel(kMsgPasteFromRequested, B_TRANSLATE("Paste from"),
				false);
			break;
		case kMsgPasteFromRequested:
		{
			entry_ref ref;
			pp::Raster raster;
			if (message->FindRef("refs", &ref) != B_OK)
				break;
			if (ImageIO::Load(ref, raster))
				_Paste(raster);
			else {
				_ShowOpenError(ref);
			}
			break;
		}

		// View
		case kMsgToggleToolBox:
			Paint()->ChangeState([](PaintState& s) {
				s.showToolBox = !s.showToolBox;
			});
			break;
		case kMsgToggleColorBox:
			Paint()->ChangeState([](PaintState& s) {
				s.showColorBox = !s.showColorBox;
			});
			break;
		case kMsgToggleStatusBar:
			Paint()->ChangeState([](PaintState& s) {
				s.showStatusBar = !s.showStatusBar;
			});
			break;
		case kMsgToggleTextToolbar:
			Paint()->ChangeState([](PaintState& s) {
				s.showTextToolbar = !s.showTextToolbar;
			});
			break;
		case kMsgZoom:
			fCanvas->SetZoom(message->GetInt32("zoom", 1));
			break;
		case kMsgToggleGrid:
			if (fCanvas->Zoom() >= 4)
				fCanvas->SetShowGrid(!fCanvas->ShowGrid());
			break;
		case kMsgViewBitmap:
			(new BitmapViewWindow(fCanvas->Flattened()))->Show();
			break;

		// Image
		case kMsgFlipRotate:
			new FlipRotateWindow(this);
			break;
		case kMsgDoFlipRotate:
		{
			int32 flip;
			double angle;
			if (message->FindInt32("flip", &flip) == B_OK) {
				if (flip == 0)
					fCanvas->FlipHorizontal();
				else
					fCanvas->FlipVertical();
			} else if (message->FindDouble("angle", &angle) == B_OK)
				fCanvas->Rotate(angle);
			break;
		}
		case kMsgStretchSkew:
			new StretchSkewWindow(this);
			break;
		case kMsgDoStretchSkew:
			fCanvas->StretchSkew(message->GetDouble("sx", 1),
				message->GetDouble("sy", 1), message->GetDouble("hskew", 0),
				message->GetDouble("vskew", 0));
			break;
		case kMsgInvertColors:
			fCanvas->InvertColors();
			break;
		case kMsgAttributes:
			fCanvas->FinishPending();
			new AttributesWindow(this, fCanvas->Image().Width(),
				fCanvas->Image().Height());
			break;
		case kMsgDoAttributes:
			fCanvas->ResizeImage(message->GetInt32("width", 1),
				message->GetInt32("height", 1));
			break;
		case kMsgClearImage:
			if (!fCanvas->HasSelection())
				fCanvas->ClearImage();
			break;
		case kMsgToggleDrawOpaque:
			Paint()->ChangeState([](PaintState& s) {
				s.transparentMode = !s.transparentMode;
			});
			break;

		// Colors
		case kMsgEditColors:
		{
			// The foreground colour, and its swatch if it has one.
			PaintState state = CurrentState();
			int32 slot = kSlotForeground;
			for (size_t i = 0; i < state.palette.size(); i++) {
				if (state.palette[i] == state.foreground) {
					slot = i;
					break;
				}
			}
			new EditColorsWindow(this, state.foreground, slot);
			break;
		}
		case kMsgEditSlot:
		{
			PaintState state = CurrentState();
			int32 slot = message->GetInt32("slot", kSlotForeground);
			pp::Pixel color = slot >= 0 && slot < (int32)state.palette.size()
				? state.palette[slot] : state.foreground;
			new EditColorsWindow(this, color, slot);
			break;
		}
		case kMsgDoEditColor:
			_SetColor((pp::Pixel)message->GetInt32("color", 0),
				message->GetInt32("slot", kSlotForeground));
			break;
		case kMsgGetColors:
			_ShowOpenPanel(kMsgGetColorsRequested, B_TRANSLATE("Get colors"),
				false);
			break;
		case kMsgGetColorsRequested:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) != B_OK)
				break;
			BFile file(&ref, B_READ_ONLY);
			off_t size = 0;
			std::vector<uint8_t> data;
			if (file.GetSize(&size) == B_OK && size > 0 && size < 1 << 20) {
				data.resize(static_cast<size_t>(size));
				if (file.Read(data.data(), size) != size)
					data.clear();
			}
			std::vector<pp::Pixel> palette;
			if (!pp::ReadPalette(data, palette)) {
				BString text(B_TRANSLATE("PowerPaint can't read colors from "
					"\"%name%\". It reads GIMP (.gpl) and RIFF or JASC (.pal) "
					"palettes."));
				text.ReplaceFirst("%name%", ImageIO::LeafName(ref));
				_ShowError(text);
				break;
			}
			Paint()->ChangeState([=](PaintState& s) { s.palette = palette; });
			break;
		}
		case kMsgSaveColors:
			_ShowSavePanel(kMsgSaveColorsRequested, B_TRANSLATE("Save colors"),
				"Untitled.pal");
			break;
		case kMsgSaveColorsRequested:
		{
			entry_ref directory;
			const char* name;
			if (message->FindRef("directory", &directory) != B_OK
				|| message->FindString("name", &name) != B_OK)
				break;
			std::vector<uint8_t> data = pp::WritePalette(
				CurrentState().palette, name);
			BDirectory dir(&directory);
			BFile file(&dir, name, B_WRITE_ONLY | B_CREATE_FILE
				| B_ERASE_FILE);
			if (file.InitCheck() != B_OK
				|| file.Write(data.data(), data.size())
					!= (ssize_t)data.size()) {
				BString text(B_TRANSLATE("PowerPaint couldn't save the colors "
					"to \"%name%\"."));
				text.ReplaceFirst("%name%", name);
				_ShowError(text);
			}
			break;
		}

		case kMsgAbout:
			be_app->PostMessage(B_ABOUT_REQUESTED);
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


// #pragma mark - Test script


// Runs commands separated by ";" (see README, "Testing"); each one is
// echoed to standard output, with any result, so a test can read the log.
void
PaintWindow::_RunScript(const BString& script)
{
	int32 start = 0;
	while (start < script.Length()) {
		int32 end = script.FindFirst(';', start);
		if (end < 0)
			end = script.Length();
		BString command;
		script.CopyInto(command, start, end - start);
		command.Trim();
		start = end + 1;
		if (command.Length() == 0)
			continue;
		printf("script: %s\n", command.String());
		if (!_RunCommand(command))
			printf("script: error: %s\n", command.String());
		fflush(stdout);
	}
}


static bool
ParseTool(const BString& word, int32& tool)
{
	char* end;
	long number = strtol(word.String(), &end, 10);
	if (*end == '\0' && number >= 0 && number < kToolCount) {
		tool = number;
		return true;
	}
	for (int32 i = 0; i < kToolCount; i++) {
		BString name = ToolName(i);
		name.ReplaceAll(' ', '-');
		if (name.ICompare(word) == 0) {
			tool = i;
			return true;
		}
	}
	return false;
}


bool
PaintWindow::_RunCommand(const BString& command)
{
	// Split into words.
	std::vector<BString> words;
	int32 i = 0;
	while (i < command.Length()) {
		while (i < command.Length() && command[i] == ' ')
			i++;
		int32 j = i;
		while (j < command.Length() && command[j] != ' ')
			j++;
		if (j > i) {
			BString word;
			command.CopyInto(word, i, j - i);
			words.push_back(word);
		}
		i = j;
	}
	if (words.empty())
		return true;
	const BString& verb = words[0];
	auto number = [&](size_t index, int32 fallback = 0) {
		return index < words.size() ? atoi(words[index].String()) : fallback;
	};
	auto has = [&](const char* flag) {
		for (size_t k = 1; k < words.size(); k++) {
			if (words[k] == flag)
				return true;
		}
		return false;
	};
	uint32 modifiers = (has("shift") ? B_SHIFT_KEY : 0)
		| (has("ctrl") ? B_CONTROL_KEY : 0);

	if (verb == "tool" && words.size() >= 2) {
		int32 tool;
		if (!ParseTool(words[1], tool))
			return false;
		Paint()->ChangeState([=](PaintState& s) {
			if (s.tool != tool)
				s.previousTool = s.tool;
			s.tool = tool;
		});
		_StateChanged();
	} else if ((verb == "fg" || verb == "bg") && words.size() >= 4) {
		pp::Pixel color = pp::MakePixel(number(1), number(2), number(3));
		bool foreground = verb == "fg";
		Paint()->ChangeState([=](PaintState& s) {
			(foreground ? s.foreground : s.background) = color;
		});
		_StateChanged();
	} else if (verb == "set" && words.size() >= 3) {
		BString key = words[1];
		int32 value = number(2);
		bool ok = true;
		Paint()->ChangeState([&](PaintState& s) {
			if (key == "brushShape")
				s.brushShape = value;
			else if (key == "brushSize")
				s.brushSize = value;
			else if (key == "eraserSize")
				s.eraserSize = value;
			else if (key == "airbrushSize")
				s.airbrushSize = value;
			else if (key == "strokeSize")
				s.strokeSize = value;
			else if (key == "fillStyle")
				s.fillStyle = value;
			else if (key == "transparent")
				s.transparentMode = value != 0;
			else if (key == "fontSize")
				s.fontSize = value;
			else
				ok = false;
		});
		_StateChanged();
		return ok;
	} else if (verb == "down" || verb == "move" || verb == "up"
		|| verb == "click" || verb == "dclick") {
		pp::Point p{number(1), number(2)};
		bool secondary = has("right");
		if (verb == "down")
			fCanvas->ToolDown(p, secondary, modifiers, 1);
		else if (verb == "move")
			fCanvas->ToolMove(p, modifiers);
		else if (verb == "up")
			fCanvas->ToolUp(p, modifiers);
		else {
			fCanvas->ToolDown(p, secondary, modifiers, 1);
			fCanvas->ToolUp(p, modifiers);
			if (verb == "dclick") {
				fCanvas->ToolDown(p, secondary, modifiers, 2);
				fCanvas->ToolUp(p, modifiers);
			}
		}
	} else if (verb == "drag" && words.size() >= 5) {
		pp::Point from{number(1), number(2)};
		pp::Point to{number(3), number(4)};
		fCanvas->ToolDown(from, has("right"), modifiers, 1);
		const int steps = 8;
		for (int s = 1; s <= steps; s++) {
			fCanvas->ToolMove(pp::Point{from.x + (to.x - from.x) * s / steps,
				from.y + (to.y - from.y) * s / steps}, modifiers);
		}
		fCanvas->ToolUp(to, modifiers);
	} else if (verb == "type") {
		BString text = command;
		text.Remove(0, 4);
		text.Trim();
		fCanvas->InsertText(text);
	} else if (verb == "key" && words.size() >= 2) {
		char key = 0;
		if (words[1] == "esc")
			key = B_ESCAPE;
		else if (words[1] == "del")
			key = B_DELETE;
		else if (words[1] == "enter")
			key = B_ENTER;
		else if (words[1] == "backspace")
			key = B_BACKSPACE;
		else
			return false;
		fCanvas->KeyDown(&key, 1);
	} else if (verb == "zoom") {
		fCanvas->SetZoom(number(1, 1));
	} else if (verb == "grid") {
		fCanvas->SetShowGrid(has("on"));
	} else if (verb == "send" && words.size() >= 2 && words[1].Length() == 4) {
		// send ABCD field=value ...: a message, handled at once.
		const char* code = words[1].String();
		BMessage message((uint32)code[0] << 24 | (uint32)code[1] << 16
			| (uint32)code[2] << 8 | (uint32)code[3]);
		for (size_t k = 2; k < words.size(); k++) {
			int32 equals = words[k].FindFirst('=');
			if (equals < 0)
				continue;
			BString field, value;
			words[k].CopyInto(field, 0, equals);
			words[k].CopyInto(value, equals + 1, words[k].Length());
			if (value.FindFirst('.') >= 0)
				message.AddDouble(field, atof(value));
			else
				message.AddInt32(field, atoi(value));
		}
		MessageReceived(&message);
	} else if (verb == "save" && words.size() >= 2) {
		BPath path(words[1]);
		BPath parent;
		entry_ref directory;
		if (path.GetParent(&parent) != B_OK
			|| get_ref_for_path(parent.Path(), &directory) != B_OK)
			return false;
		return _SaveTo(directory, path.Leaf());
	} else if (verb == "open" && words.size() >= 2) {
		entry_ref ref;
		if (get_ref_for_path(words[1], &ref) != B_OK)
			return false;
		Open(ref);
	} else if (verb == "dump") {
		pp::Raster flat = fCanvas->Flattened();
		uint32 sum = 0;
		for (int y = 0; y < flat.Height(); y++) {
			for (int x = 0; x < flat.Width(); x++)
				sum = sum * 31 + flat.At(x, y);
		}
		PaintState s = CurrentState();
		printf("dump: size %dx%d zoom %" B_PRId32 " tool %s selection %s "
			"text %s modified %s undo %s hash %08" B_PRIx32 "\n",
			flat.Width(), flat.Height(), fCanvas->Zoom(), ToolName(s.tool),
			fCanvas->HasSelection() ? "yes" : "no",
			fCanvas->HasText() ? "yes" : "no",
			fCanvas->IsModified() ? "yes" : "no",
			fCanvas->CanUndo() ? "yes" : "no", sum);
	} else if (verb == "pixel" && words.size() >= 3) {
		pp::Raster flat = fCanvas->Flattened();
		pp::Pixel p = flat.Get(number(1), number(2));
		printf("pixel: %d,%d = %d %d %d %d\n", number(1), number(2),
			pp::Red(p), pp::Green(p), pp::Blue(p), pp::Alpha(p));
	} else if (verb == "colors") {
		PaintState s = CurrentState();
		printf("colors: fg %d %d %d bg %d %d %d palette %d\n",
			pp::Red(s.foreground), pp::Green(s.foreground),
			pp::Blue(s.foreground), pp::Red(s.background),
			pp::Green(s.background), pp::Blue(s.background),
			(int)s.palette.size());
	} else if (verb == "discard") {
		// Lets a test close the window without the save question.
		fCanvas->SetModified(false);
	} else
		return false;
	return true;
}
