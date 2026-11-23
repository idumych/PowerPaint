/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 *
 * MS Paint's Fonts toolbar: a small floating window, shown with the text
 * tool, to choose the font family, size and bold/italic/underline.
 */
#ifndef FONT_BAR_H
#define FONT_BAR_H

#include <Window.h>

class BButton;
class BMenuField;


class FontBar : public BWindow {
public:
							FontBar(BWindow* parent);

	virtual void			MessageReceived(BMessage* message);
	virtual bool			QuitRequested();

	void					StateChanged();

private:
	BWindow*				fParent;
	BMenuField*				fFamily;
	BMenuField*				fSize;
	BButton*				fBold;
	BButton*				fItalic;
	BButton*				fUnderline;
};

#endif	// FONT_BAR_H
