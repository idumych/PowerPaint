/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 *
 * The status bar: help text, the pointer's position and the size of what is
 * being drawn or selected, each in a sunken field.
 */
#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include <String.h>
#include <View.h>


class StatusBar : public BView {
public:
							StatusBar();

	virtual void			Draw(BRect updateRect);
	virtual BSize			MinSize();
	virtual BSize			MaxSize();

	void					SetText(const char* text);
	void					SetCoordinates(const char* text);
	void					SetSize(const char* text);

private:
	BRect					_Field(int32 index) const;
	void					_DrawField(BRect frame, const BString& text);

	BString					fText;
	BString					fCoordinates;
	BString					fSize;
};

#endif	// STATUS_BAR_H
