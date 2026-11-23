/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 *
 * MS Paint's dialogs: Attributes, Flip and rotate, Stretch and skew, Edit
 * colors. Each blocks only its own document window and sends its result
 * to it as a message.
 */
#ifndef DIALOGS_H
#define DIALOGS_H

#include <Messenger.h>
#include <Window.h>

#include "Raster.h"

class BButton;
class BColorControl;
class BRadioButton;
class BTextControl;


class DialogWindow : public BWindow {
public:
							DialogWindow(BWindow* parent, const char* title);

protected:
	// Lays out `content` above the Cancel and OK buttons and shows the
	// window centred on its parent.
	void					Finish(BView* content, BView* extraButton = NULL);
	void					Fail(BTextControl* field, const char* message);

	BMessenger				fTarget;
	BWindow*				fParent;
	BButton*				fOK;
};


class AttributesWindow : public DialogWindow {
public:
							AttributesWindow(BWindow* parent, int32 width,
								int32 height);
	virtual void			MessageReceived(BMessage* message);

private:
	BTextControl*			fWidth;
	BTextControl*			fHeight;
};


class FlipRotateWindow : public DialogWindow {
public:
							FlipRotateWindow(BWindow* parent);
	virtual void			MessageReceived(BMessage* message);

private:
	void					_UpdateEnabled();

	BRadioButton*			fFlipHorizontal;
	BRadioButton*			fFlipVertical;
	BRadioButton*			fRotate;
	BRadioButton*			fAngles[4];		// 90, 180, 270, custom
	BTextControl*			fCustomAngle;
};


class StretchSkewWindow : public DialogWindow {
public:
							StretchSkewWindow(BWindow* parent);
	virtual void			MessageReceived(BMessage* message);

private:
	BTextControl*			fStretchH;
	BTextControl*			fStretchV;
	BTextControl*			fSkewH;
	BTextControl*			fSkewV;
};


class EditColorsWindow : public DialogWindow {
public:
							EditColorsWindow(BWindow* parent,
								pp::Pixel color, int32 slot);
	virtual void			MessageReceived(BMessage* message);

private:
	BColorControl*			fControl;
	BView*					fPreview;
	int32					fSlot;
};

#endif	// DIALOGS_H
