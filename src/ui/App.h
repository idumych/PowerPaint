/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 *
 * The application keeps what all windows share, as MS Paint does between
 * sessions: the current tool and its options, the colours and the palette,
 * the text tool's font. Windows read a copy with State() and change it
 * with ChangeState(), which tells every window.
 */
#ifndef APP_H
#define APP_H

#include <Application.h>
#include <Locker.h>
#include <Rect.h>
#include <String.h>

#include <functional>
#include <vector>

#include "Raster.h"


struct PaintState {
	int32				tool;
	int32				previousTool;	// for Pick Color's return
	pp::Pixel			foreground;
	pp::Pixel			background;
	std::vector<pp::Pixel> palette;

	int32				brushShape;		// pp::BrushShape
	int32				brushSize;
	int32				eraserSize;
	int32				airbrushSize;
	int32				strokeSize;		// lines, curves, shape outlines
	int32				fillStyle;		// pp::FillStyle
	bool				transparentMode;	// selections and text
	int32				magnifierZoom;

	BString				fontFamily;
	float				fontSize;
	bool				bold;
	bool				italic;
	bool				underline;

	bool				showToolBox;
	bool				showColorBox;
	bool				showStatusBar;
	bool				showTextToolbar;

						PaintState();
	static std::vector<pp::Pixel> DefaultPalette();
};


class PaintApp : public BApplication {
public:
							PaintApp();

	virtual void			ReadyToRun();
	virtual void			ArgvReceived(int32 argc, char** argv);
	virtual void			RefsReceived(BMessage* message);
	virtual void			MessageReceived(BMessage* message);
	virtual bool			QuitRequested();
	virtual void			AboutRequested();

	PaintState				State();
	void					ChangeState(
								const std::function<void(PaintState&)>& change);

	// Where the next window goes; remembered across runs.
	BRect					NextWindowFrame();
	void					RememberWindowFrame(BRect frame);

private:
	void					_NewWindow(const entry_ref* ref);
	void					_LoadSettings();
	void					_SaveSettings();

	BLocker					fStateLock;
	PaintState				fState;
	BRect					fWindowFrame;
	int32					fWindowCount;
	int32					fUntitledCount;
	BString					fScript;
	bool					fArgvOpened;
};


PaintApp*	Paint();
PaintState	CurrentState();

#endif	// APP_H
