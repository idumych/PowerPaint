/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 *
 * One window per picture: MS Paint's menus, the tool box on the left, the
 * picture in the middle, the colour box and status bar below.
 */
#ifndef PAINT_WINDOW_H
#define PAINT_WINDOW_H

#include <Entry.h>
#include <String.h>
#include <Window.h>

#include "App.h"
#include "Raster.h"

class BFilePanel;
class BMenu;
class BMenuBar;
class BMenuItem;
class BScrollView;
class CanvasView;
class ColorBox;
class FontBar;
class StatusBar;
class ToolBox;


class PaintWindow : public BWindow {
public:
							PaintWindow(BRect frame, int32 untitledNumber);
	virtual					~PaintWindow();

	virtual void			DispatchMessage(BMessage* message,
								BHandler* target);
	virtual void			MessageReceived(BMessage* message);
	virtual bool			QuitRequested();
	virtual void			MenusBeginning();

	void					Open(const entry_ref& ref);
	// Untitled, unchanged and never drawn on: an open file can replace it.
	bool					IsPristine() const;

	// From the views
	void					DocumentChanged();
	void					ZoomChanged();
	void					SetHelpText(const char* text);
	void					SetCoordinates(const BString& text);
	void					SetSizeText(const BString& text);

private:
	void					_BuildMenus();
	BMenuItem*				_AddItem(BMenu* menu, const char* label,
								uint32 what, char shortcut = 0,
								uint32 modifiers = 0);
	void					_UpdateTitle();
	void					_StateChanged();
	void					_UpdateFontBar();

	bool					_Save();
	void					_ShowSavePanel(uint32 what, const char* title,
								const char* name);
	void					_ShowOpenPanel(uint32 what, const char* title,
								bool toApp);
	bool					_SaveTo(const entry_ref& directory,
								const char* name);
	void					_Paste(pp::Raster& raster);
	void					_SetColor(pp::Pixel color, int32 slot);
	void					_ShowError(const char* text);
	void					_ShowOpenError(const entry_ref& ref);
	void					_ShowSaveError(const char* name,
								const BString& error);

	void					_RunScript(const BString& script);
	bool					_RunCommand(const BString& command);

	BMenuBar*				fMenuBar;
	ToolBox*				fToolBox;
	CanvasView*				fCanvas;
	BScrollView*			fScrollView;
	ColorBox*				fColorBox;
	StatusBar*				fStatusBar;
	FontBar*				fFontBar;
	bool					fFontBarShown;

	BFilePanel*				fOpenPanel;
	BFilePanel*				fSavePanel;

	entry_ref				fRef;
	bool					fHasRef;
	int32					fUntitledNumber;
	bool					fQuitAfterSave;
	PaintState				fState;

	BMenuItem*				fUndoItem;
	BMenuItem*				fRedoItem;
	BMenuItem*				fCutItem;
	BMenuItem*				fCopyItem;
	BMenuItem*				fClearSelectionItem;
	BMenuItem*				fCopyToItem;
	BMenuItem*				fToolBoxItem;
	BMenuItem*				fColorBoxItem;
	BMenuItem*				fStatusBarItem;
	BMenuItem*				fTextToolbarItem;
	BMenuItem*				fGridItem;
	BMenu*					fZoomMenu;
	BMenuItem*				fClearImageItem;
	BMenuItem*				fDrawOpaqueItem;
};

#endif	// PAINT_WINDOW_H
