/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 */
#include "FontBar.h"

#include <Button.h>
#include <Catalog.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>

#include <stdlib.h>

#include "App.h"
#include "Constants.h"
#include "TextRender.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FontBar"


enum {
	kMsgFamily		= 'fbfm',
	kMsgSize		= 'fbsz',
	kMsgBold		= 'fbbd',
	kMsgItalic		= 'fbit',
	kMsgUnderline	= 'fbul',
};

static const int32 kSizes[] = { 8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26,
	28, 36, 48, 72 };


FontBar::FontBar(BWindow* parent)
	:
	BWindow(BRect(0, 0, 100, 30), B_TRANSLATE("Fonts"),
		B_FLOATING_WINDOW_LOOK, B_FLOATING_SUBSET_WINDOW_FEEL,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_WILL_ACCEPT_FIRST_CLICK | B_AVOID_FOCUS),
	fParent(parent)
{
	AddToSubset(parent);

	BPopUpMenu* families = new BPopUpMenu("family");
	int32 count = count_font_families();
	for (int32 i = 0; i < count; i++) {
		font_family family;
		if (get_font_family(i, &family) != B_OK)
			continue;
		BMessage* message = new BMessage(kMsgFamily);
		message->AddString("family", family);
		families->AddItem(new BMenuItem(family, message));
	}
	fFamily = new BMenuField("family", NULL, families);

	BPopUpMenu* sizes = new BPopUpMenu("size");
	for (int32 size : kSizes) {
		BMessage* message = new BMessage(kMsgSize);
		message->AddFloat("size", size);
		BString label;
		label << size;
		sizes->AddItem(new BMenuItem(label, message));
	}
	fSize = new BMenuField("size", NULL, sizes);

	fBold = new BButton("bold", B_TRANSLATE("Bold"), new BMessage(kMsgBold));
	fItalic = new BButton("italic", B_TRANSLATE("Italic"),
		new BMessage(kMsgItalic));
	fUnderline = new BButton("underline", B_TRANSLATE("Underline"),
		new BMessage(kMsgUnderline));
	for (BButton* button : { fBold, fItalic, fUnderline })
		button->SetBehavior(BButton::B_TOGGLE_BEHAVIOR);

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_SMALL_SPACING)
		.Add(fFamily)
		.Add(fSize)
		.Add(fBold)
		.Add(fItalic)
		.Add(fUnderline);

	StateChanged();
	BRect frame = parent->Frame();
	MoveTo(frame.left + 70, frame.top + 70);
}


bool
FontBar::QuitRequested()
{
	// Closing the bar turns off View > Text toolbar.
	Paint()->ChangeState([](PaintState& s) { s.showTextToolbar = false; });
	return false;
}


void
FontBar::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgFamily:
		{
			BString family = message->GetString("family", "");
			Paint()->ChangeState([=](PaintState& s) { s.fontFamily = family; });
			break;
		}
		case kMsgSize:
		{
			float size = message->GetFloat("size", 12);
			Paint()->ChangeState([=](PaintState& s) { s.fontSize = size; });
			break;
		}
		case kMsgBold:
		case kMsgItalic:
		case kMsgUnderline:
		{
			bool bold = fBold->Value() == B_CONTROL_ON;
			bool italic = fItalic->Value() == B_CONTROL_ON;
			bool underline = fUnderline->Value() == B_CONTROL_ON;
			Paint()->ChangeState([=](PaintState& s) {
				s.bold = bold;
				s.italic = italic;
				s.underline = underline;
			});
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}


void
FontBar::StateChanged()
{
	PaintState state = CurrentState();
	font_family family;
	font_style style;
	TextRender::FontFor(state).GetFamilyAndStyle(&family, &style);
	if (BMenuItem* item = fFamily->Menu()->FindItem(family))
		item->SetMarked(true);
	BString size;
	size << (int32)state.fontSize;
	if (BMenuItem* item = fSize->Menu()->FindItem(size))
		item->SetMarked(true);
	fBold->SetValue(state.bold ? B_CONTROL_ON : B_CONTROL_OFF);
	fItalic->SetValue(state.italic ? B_CONTROL_ON : B_CONTROL_OFF);
	fUnderline->SetValue(state.underline ? B_CONTROL_ON : B_CONTROL_OFF);
}
