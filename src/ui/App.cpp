/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 */
#include "App.h"

#include <Alert.h>
#include <Autolock.h>
#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Screen.h>
#include <Window.h>

#ifdef APP_VOS
// V\OS's own about window; the header is a copy of V\OS's private one (src/ui/vos)
#include <AboutWindow.h>
#endif

#include <stdio.h>
#include <string.h>

#include "Constants.h"
#include "PaintWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "App"


static const char* kSettingsName = "PowerPaint settings";


PaintState::PaintState()
	:
	tool(kToolPencil),
	previousTool(kToolPencil),
	foreground(pp::MakePixel(0, 0, 0)),
	background(pp::MakePixel(255, 255, 255)),
	palette(DefaultPalette()),
	brushShape(pp::kBrushCircle),
	brushSize(4),
	eraserSize(8),
	airbrushSize(9),
	strokeSize(1),
	fillStyle(pp::kFillOutline),
	transparentMode(false),
	magnifierZoom(4),
	fontSize(12),
	bold(false),
	italic(false),
	underline(false),
	showToolBox(true),
	showColorBox(true),
	showStatusBar(true),
	showTextToolbar(true)
{
}


std::vector<pp::Pixel>
PaintState::DefaultPalette()
{
	// JS Paint's default_palette: MS Paint's, the top row then the bottom.
	static const uint8 kColors[][3] = {
		{0, 0, 0}, {128, 128, 128}, {128, 0, 0}, {128, 128, 0}, {0, 128, 0},
		{0, 128, 128}, {0, 0, 128}, {128, 0, 128}, {128, 128, 64}, {0, 64, 64},
		{0, 128, 255}, {0, 64, 128}, {64, 0, 255}, {128, 64, 0},
		{255, 255, 255}, {192, 192, 192}, {255, 0, 0}, {255, 255, 0},
		{0, 255, 0}, {0, 255, 255}, {0, 0, 255}, {255, 0, 255},
		{255, 255, 128}, {0, 255, 128}, {128, 255, 255}, {128, 128, 255},
		{255, 0, 128}, {255, 128, 64},
	};
	std::vector<pp::Pixel> palette;
	for (const auto& c : kColors)
		palette.push_back(pp::MakePixel(c[0], c[1], c[2]));
	return palette;
}


PaintApp*
Paint()
{
	return static_cast<PaintApp*>(be_app);
}


PaintState
CurrentState()
{
	return Paint()->State();
}


PaintApp::PaintApp()
	:
	BApplication(APP_SIGNATURE),
	fStateLock("paint state"),
	fWindowFrame(100, 80, 100 + 640, 80 + 480),
	fWindowCount(0),
	fUntitledCount(0),
	fArgvOpened(false)
{
	_LoadSettings();
}


void
PaintApp::ReadyToRun()
{
	if (fWindowCount == 0 && !fArgvOpened)
		_NewWindow(NULL);
}


void
PaintApp::ArgvReceived(int32 argc, char** argv)
{
	BMessage refs(B_REFS_RECEIVED);
	const char* cwd = NULL;
	if (CurrentMessage() != NULL)
		CurrentMessage()->FindString("cwd", &cwd);
	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--do") == 0 && i + 1 < argc) {
			fScript = argv[++i];
			continue;
		}
		BPath path;
		if (argv[i][0] == '/' || cwd == NULL)
			path.SetTo(argv[i]);
		else
			path.SetTo(cwd, argv[i]);
		entry_ref ref;
		if (get_ref_for_path(path.Path(), &ref) == B_OK)
			refs.AddRef("refs", &ref);
	}
	if (refs.HasRef("refs")) {
		fArgvOpened = true;
		RefsReceived(&refs);
	}
	// A running app (single launch) gets --do here: the front window runs it.
	if (fScript.Length() > 0 && !IsLaunching()) {
		for (int32 i = 0; BWindow* window = WindowAt(i); i++) {
			if (dynamic_cast<PaintWindow*>(window) == NULL)
				continue;
			BMessage script(kMsgScript);
			script.AddString("script", fScript);
			window->PostMessage(&script);
			break;
		}
		fScript = "";
	}
}


void
PaintApp::RefsReceived(BMessage* message)
{
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		// An untouched empty window takes the first file, as MS Paint's
		// single window would.
		bool reused = false;
		for (int32 j = 0; BWindow* window = WindowAt(j); j++) {
			PaintWindow* paint = dynamic_cast<PaintWindow*>(window);
			if (paint == NULL || !paint->Lock())
				continue;
			if (paint->IsPristine()) {
				paint->Open(ref);
				reused = true;
			}
			paint->Unlock();
			if (reused)
				break;
		}
		if (!reused)
			_NewWindow(&ref);
	}
}


void
PaintApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgNewWindow:
			_NewWindow(NULL);
			break;
		case kMsgWindowClosed:
			if (--fWindowCount <= 0)
				PostMessage(B_QUIT_REQUESTED);
			break;
		default:
			BApplication::MessageReceived(message);
	}
}


bool
PaintApp::QuitRequested()
{
	// Each window asks about its own unsaved changes; Cancel stops the quit.
	for (;;) {
		BWindow* window = NULL;
		for (int32 i = 0; (window = WindowAt(i)) != NULL; i++) {
			if (dynamic_cast<PaintWindow*>(window) != NULL)
				break;
		}
		if (window == NULL || !window->Lock())
			break;
		if (!window->QuitRequested()) {
			window->Unlock();
			return false;
		}
		window->Quit();
	}
	_SaveSettings();
	return true;
}


void
PaintApp::AboutRequested()
{
#ifdef APP_VOS
	BAboutWindow* about = new BAboutWindow(B_TRANSLATE_SYSTEM_NAME("PowerPaint"),
		APP_SIGNATURE);
	about->AddDescription(B_TRANSLATE("A native Haiku port of JS Paint by "
		"Isaiah Odhner, a remake of MS Paint."));
	// AddCopyright() runs the first year up to the current one, so the
	// contributors come first and JS Paint's fixed 2022 line is an extra.
	const char* extraCopyrights[] = { "2022 Isaiah Odhner (JS Paint)", NULL };
	about->AddCopyright(2026, "the PowerPaint contributors", extraCopyrights);
	about->AddExtraInfo(B_TRANSLATE("Distributed under the terms of the MIT License."));
	about->Show();
#else
	BAlert* alert = new BAlert(B_TRANSLATE("About PowerPaint"),
		B_TRANSLATE("PowerPaint\n\n"
			"A native Haiku port of JS Paint by Isaiah Odhner, "
			"a remake of MS Paint.\n\n"
			"JS Paint: Copyright 2022 Isaiah Odhner\n"
			"Distributed under the terms of the MIT License."),
		B_TRANSLATE("OK"));
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go(NULL);
#endif
}


PaintState
PaintApp::State()
{
	BAutolock locker(fStateLock);
	return fState;
}


void
PaintApp::ChangeState(const std::function<void(PaintState&)>& change)
{
	{
		BAutolock locker(fStateLock);
		change(fState);
	}
	BMessage changed(kMsgStateChanged);
	for (int32 i = 0; BWindow* window = WindowAt(i); i++) {
		if (dynamic_cast<PaintWindow*>(window) != NULL)
			window->PostMessage(&changed);
	}
}


BRect
PaintApp::NextWindowFrame()
{
	BRect frame = fWindowFrame;
	// Each new window a tab's height further down and right.
	frame.OffsetBy(fWindowCount * 20, fWindowCount * 20);
	BRect screen = BScreen().Frame();
	if (!screen.Contains(frame.LeftTop() + BPoint(40, 40)))
		frame.OffsetTo(screen.left + 50, screen.top + 50);
	return frame;
}


void
PaintApp::RememberWindowFrame(BRect frame)
{
	fWindowFrame = frame;
}


void
PaintApp::_NewWindow(const entry_ref* ref)
{
	BRect frame = NextWindowFrame();
	int32 untitled = ref == NULL ? ++fUntitledCount : 0;
	PaintWindow* window = new PaintWindow(frame, untitled);
	fWindowCount++;
	if (ref != NULL) {
		window->Lock();
		window->Open(*ref);
		window->Unlock();
	}
	window->Show();
	if (fScript.Length() > 0) {
		BMessage script(kMsgScript);
		script.AddString("script", fScript);
		window->PostMessage(&script);
		fScript = "";
	}
}


// #pragma mark - Settings


void
PaintApp::_LoadSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append(kSettingsName);
	BFile file(path.Path(), B_READ_ONLY);
	BMessage settings;
	if (file.InitCheck() != B_OK || settings.Unflatten(&file) != B_OK)
		return;

	BRect frame;
	if (settings.FindRect("frame", &frame) == B_OK && frame.IsValid()
		&& frame.Width() > 200 && frame.Height() > 150) {
		fWindowFrame = frame;
	}

	PaintState& s = fState;
	settings.FindInt32("tool", &s.tool);
	if (s.tool < 0 || s.tool >= kToolCount || s.tool == kToolPickColor)
		s.tool = kToolPencil;
	s.previousTool = s.tool;
	int32 value;
	if (settings.FindInt32("foreground", &value) == B_OK)
		s.foreground = (pp::Pixel)value;
	if (settings.FindInt32("background", &value) == B_OK)
		s.background = (pp::Pixel)value;
	std::vector<pp::Pixel> palette;
	for (int32 i = 0; settings.FindInt32("palette", i, &value) == B_OK; i++)
		palette.push_back((pp::Pixel)value);
	if (!palette.empty())
		s.palette = palette;
	settings.FindInt32("brushShape", &s.brushShape);
	settings.FindInt32("brushSize", &s.brushSize);
	settings.FindInt32("eraserSize", &s.eraserSize);
	settings.FindInt32("airbrushSize", &s.airbrushSize);
	settings.FindInt32("strokeSize", &s.strokeSize);
	settings.FindInt32("fillStyle", &s.fillStyle);
	settings.FindBool("transparentMode", &s.transparentMode);
	settings.FindInt32("magnifierZoom", &s.magnifierZoom);
	settings.FindString("fontFamily", &s.fontFamily);
	settings.FindFloat("fontSize", &s.fontSize);
	settings.FindBool("bold", &s.bold);
	settings.FindBool("italic", &s.italic);
	settings.FindBool("underline", &s.underline);
	settings.FindBool("showToolBox", &s.showToolBox);
	settings.FindBool("showColorBox", &s.showColorBox);
	settings.FindBool("showStatusBar", &s.showStatusBar);
	settings.FindBool("showTextToolbar", &s.showTextToolbar);
}


void
PaintApp::_SaveSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append(kSettingsName);

	PaintState s = State();
	BMessage settings('pset');
	settings.AddRect("frame", fWindowFrame);
	settings.AddInt32("tool", s.tool == kToolPickColor ? s.previousTool
		: s.tool);
	settings.AddInt32("foreground", (int32)s.foreground);
	settings.AddInt32("background", (int32)s.background);
	for (pp::Pixel p : s.palette)
		settings.AddInt32("palette", (int32)p);
	settings.AddInt32("brushShape", s.brushShape);
	settings.AddInt32("brushSize", s.brushSize);
	settings.AddInt32("eraserSize", s.eraserSize);
	settings.AddInt32("airbrushSize", s.airbrushSize);
	settings.AddInt32("strokeSize", s.strokeSize);
	settings.AddInt32("fillStyle", s.fillStyle);
	settings.AddBool("transparentMode", s.transparentMode);
	settings.AddInt32("magnifierZoom", s.magnifierZoom);
	settings.AddString("fontFamily", s.fontFamily);
	settings.AddFloat("fontSize", s.fontSize);
	settings.AddBool("bold", s.bold);
	settings.AddBool("italic", s.italic);
	settings.AddBool("underline", s.underline);
	settings.AddBool("showToolBox", s.showToolBox);
	settings.AddBool("showColorBox", s.showColorBox);
	settings.AddBool("showStatusBar", s.showStatusBar);
	settings.AddBool("showTextToolbar", s.showTextToolbar);

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		settings.Flatten(&file);
}


int
main()
{
	PaintApp app;
	app.Run();
	return 0;
}
