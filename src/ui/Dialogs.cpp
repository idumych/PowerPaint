/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 */
#include "Dialogs.h"

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <ColorControl.h>
#include <GridView.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <RadioButton.h>
#include <StringView.h>
#include <TextControl.h>

#include <ctype.h>
#include <stdlib.h>

#include "Constants.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Dialogs"


enum {
	kMsgOK			= 'dgok',
	kMsgDefault		= 'dgdf',
	kMsgRadio		= 'dgrd',
	kMsgColor		= 'dgcl',
};


// A text field that takes only digits (and a minus sign or point when
// asked to).
static BTextControl*
NumberField(const char* name, const char* label, const char* text,
	bool allowMinus = false, bool allowPoint = false)
{
	BTextControl* field = new BTextControl(name, label, text, NULL);
	BTextView* view = field->TextView();
	for (uint32 c = 32; c < 256; c++) {
		if (isdigit(c) || (allowMinus && c == '-') || (allowPoint && c == '.'))
			continue;
		view->DisallowChar(c);
	}
	return field;
}


// #pragma mark - DialogWindow


DialogWindow::DialogWindow(BWindow* parent, const char* title)
	:
	BWindow(BRect(0, 0, 100, 100), title, B_TITLED_WINDOW_LOOK,
		B_MODAL_SUBSET_WINDOW_FEEL, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_NOT_MINIMIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fTarget(parent),
	fParent(parent)
{
	AddToSubset(parent);
	fOK = new BButton("ok", B_TRANSLATE("OK"), new BMessage(kMsgOK));
}


void
DialogWindow::Finish(BView* content, BView* extraButton)
{
	BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));
	BGroupLayout* buttons;
	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(content)
		.AddGroup(B_HORIZONTAL)
			.GetLayout(&buttons)
			.AddGlue()
			.Add(cancel)
			.Add(fOK)
		.End();
	if (extraButton != NULL)
		buttons->AddView(0, extraButton);
	fOK->MakeDefault(true);
	CenterIn(fParent->Frame());
	Show();
}


void
DialogWindow::Fail(BTextControl* field, const char* message)
{
	BAlert* alert = new BAlert(Title(), message, B_TRANSLATE("OK"), NULL,
		NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
	field->MakeFocus(true);
}


// #pragma mark - AttributesWindow


AttributesWindow::AttributesWindow(BWindow* parent, int32 width,
	int32 height)
	:
	DialogWindow(parent, B_TRANSLATE("Attributes"))
{
	BString text;
	text << width;
	fWidth = NumberField("width", B_TRANSLATE("Width:"), text);
	text = "";
	text << height;
	fHeight = NumberField("height", B_TRANSLATE("Height:"), text);

	BGridView* grid = new BGridView();
	BLayoutBuilder::Grid<>(grid)
		.AddTextControl(fWidth, 0, 0)
		.Add(new BStringView("unit", B_TRANSLATE("pixels")), 2, 0)
		.AddTextControl(fHeight, 0, 1)
		.Add(new BStringView("unit", B_TRANSLATE("pixels")), 2, 1);
	fWidth->TextView()->SetExplicitMinSize(BSize(be_plain_font->StringWidth(
		"0000000"), B_SIZE_UNSET));

	// Before Finish() shows the window: after that it needs the lock.
	fWidth->MakeFocus(true);
	Finish(grid, new BButton("default", B_TRANSLATE("Default"),
		new BMessage(kMsgDefault)));
}


void
AttributesWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgDefault:
			// JS Paint's new picture size.
			fWidth->SetText("683");
			fHeight->SetText("384");
			break;
		case kMsgOK:
		{
			int32 width = atoi(fWidth->Text());
			int32 height = atoi(fHeight->Text());
			if (width < 1 || width > 16384) {
				Fail(fWidth, B_TRANSLATE("Please enter a width from 1 to "
					"16384 pixels."));
				break;
			}
			if (height < 1 || height > 16384) {
				Fail(fHeight, B_TRANSLATE("Please enter a height from 1 to "
					"16384 pixels."));
				break;
			}
			BMessage reply(kMsgDoAttributes);
			reply.AddInt32("width", width);
			reply.AddInt32("height", height);
			fTarget.SendMessage(&reply);
			Quit();
			break;
		}
		default:
			DialogWindow::MessageReceived(message);
	}
}


// #pragma mark - FlipRotateWindow


FlipRotateWindow::FlipRotateWindow(BWindow* parent)
	:
	DialogWindow(parent, B_TRANSLATE("Flip and rotate"))
{
	fFlipHorizontal = new BRadioButton("fliph", B_TRANSLATE("Flip horizontal"),
		new BMessage(kMsgRadio));
	fFlipVertical = new BRadioButton("flipv", B_TRANSLATE("Flip vertical"),
		new BMessage(kMsgRadio));
	fRotate = new BRadioButton("rotate", B_TRANSLATE("Rotate by angle"),
		new BMessage(kMsgRadio));
	const char* labels[] = { "90°", "180°", "270°" };
	for (int32 i = 0; i < 3; i++) {
		fAngles[i] = new BRadioButton(labels[i], labels[i],
			new BMessage(kMsgRadio));
	}
	fAngles[3] = new BRadioButton("custom", B_TRANSLATE("Custom:"),
		new BMessage(kMsgRadio));
	fCustomAngle = NumberField("degrees", NULL, "45", true, true);
	fCustomAngle->SetModificationMessage(new BMessage(kMsgRadio));
	fFlipHorizontal->SetValue(B_CONTROL_ON);
	fAngles[0]->SetValue(B_CONTROL_ON);

	// The angles are a group of their own, so they need a view of their own.
	BGroupView* angles = new BGroupView(B_VERTICAL, 0);
	BLayoutBuilder::Group<>(angles)
		.SetInsets(B_USE_BIG_SPACING, 0, 0, 0)
		.Add(fAngles[0])
		.Add(fAngles[1])
		.Add(fAngles[2])
		.AddGroup(B_HORIZONTAL)
			.Add(fAngles[3])
			.Add(fCustomAngle)
			.Add(new BStringView("unit", B_TRANSLATE("degrees")))
		.End();

	BBox* box = new BBox("box");
	box->SetLabel(B_TRANSLATE("Flip or rotate"));
	BGroupView* group = new BGroupView(B_VERTICAL, 0);
	BLayoutBuilder::Group<>(group)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fFlipHorizontal)
		.Add(fFlipVertical)
		.Add(fRotate)
		.Add(angles);
	box->AddChild(group);

	_UpdateEnabled();
	Finish(box);
}


void
FlipRotateWindow::_UpdateEnabled()
{
	bool rotate = fRotate->Value() == B_CONTROL_ON;
	for (BRadioButton* angle : fAngles)
		angle->SetEnabled(rotate);
	fCustomAngle->SetEnabled(rotate);
}


void
FlipRotateWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRadio:
			if (message->HasPointer("source")) {
				void* source = NULL;
				message->FindPointer("source", &source);
				if (source == fCustomAngle)
					fAngles[3]->SetValue(B_CONTROL_ON);
			}
			_UpdateEnabled();
			break;
		case kMsgOK:
		{
			BMessage reply(kMsgDoFlipRotate);
			if (fFlipHorizontal->Value() == B_CONTROL_ON)
				reply.AddInt32("flip", 0);
			else if (fFlipVertical->Value() == B_CONTROL_ON)
				reply.AddInt32("flip", 1);
			else {
				double angle = 90;
				if (fAngles[1]->Value() == B_CONTROL_ON)
					angle = 180;
				else if (fAngles[2]->Value() == B_CONTROL_ON)
					angle = 270;
				else if (fAngles[3]->Value() == B_CONTROL_ON) {
					angle = atof(fCustomAngle->Text());
					if (fCustomAngle->Text()[0] == '\0') {
						Fail(fCustomAngle, B_TRANSLATE("Please enter an "
							"angle in degrees."));
						break;
					}
				}
				reply.AddDouble("angle", angle);
			}
			fTarget.SendMessage(&reply);
			Quit();
			break;
		}
		default:
			DialogWindow::MessageReceived(message);
	}
}


// #pragma mark - StretchSkewWindow


StretchSkewWindow::StretchSkewWindow(BWindow* parent)
	:
	DialogWindow(parent, B_TRANSLATE("Stretch and skew"))
{
	fStretchH = NumberField("stretchh", B_TRANSLATE("Horizontal:"), "100");
	fStretchV = NumberField("stretchv", B_TRANSLATE("Vertical:"), "100");
	fSkewH = NumberField("skewh", B_TRANSLATE("Horizontal:"), "0", true);
	fSkewV = NumberField("skewv", B_TRANSLATE("Vertical:"), "0", true);

	BBox* stretch = new BBox("stretch");
	stretch->SetLabel(B_TRANSLATE("Stretch"));
	BGridView* stretchGrid = new BGridView();
	BLayoutBuilder::Grid<>(stretchGrid)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddTextControl(fStretchH, 0, 0)
		.Add(new BStringView("unit", "%"), 2, 0)
		.AddTextControl(fStretchV, 0, 1)
		.Add(new BStringView("unit", "%"), 2, 1);
	stretch->AddChild(stretchGrid);

	BBox* skew = new BBox("skew");
	skew->SetLabel(B_TRANSLATE("Skew"));
	BGridView* skewGrid = new BGridView();
	BLayoutBuilder::Grid<>(skewGrid)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddTextControl(fSkewH, 0, 0)
		.Add(new BStringView("unit", B_TRANSLATE("degrees")), 2, 0)
		.AddTextControl(fSkewV, 0, 1)
		.Add(new BStringView("unit", B_TRANSLATE("degrees")), 2, 1);
	skew->AddChild(skewGrid);

	BGroupView* content = new BGroupView(B_VERTICAL);
	BLayoutBuilder::Group<>(content)
		.Add(stretch)
		.Add(skew);
	fStretchH->MakeFocus(true);
	Finish(content);
}


void
StretchSkewWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgOK:
		{
			// MS Paint's limits.
			double sx = atof(fStretchH->Text());
			double sy = atof(fStretchV->Text());
			double hskew = atof(fSkewH->Text());
			double vskew = atof(fSkewV->Text());
			if (sx < 1 || sx > 500) {
				Fail(fStretchH, B_TRANSLATE("Please enter a number from 1 to "
					"500."));
				break;
			}
			if (sy < 1 || sy > 500) {
				Fail(fStretchV, B_TRANSLATE("Please enter a number from 1 to "
					"500."));
				break;
			}
			if (hskew < -89 || hskew > 89) {
				Fail(fSkewH, B_TRANSLATE("Please enter a number from -89 to "
					"89."));
				break;
			}
			if (vskew < -89 || vskew > 89) {
				Fail(fSkewV, B_TRANSLATE("Please enter a number from -89 to "
					"89."));
				break;
			}
			BMessage reply(kMsgDoStretchSkew);
			reply.AddDouble("sx", sx / 100);
			reply.AddDouble("sy", sy / 100);
			reply.AddDouble("hskew", hskew);
			reply.AddDouble("vskew", vskew);
			fTarget.SendMessage(&reply);
			Quit();
			break;
		}
		default:
			DialogWindow::MessageReceived(message);
	}
}


// #pragma mark - EditColorsWindow


EditColorsWindow::EditColorsWindow(BWindow* parent, pp::Pixel color,
	int32 slot)
	:
	DialogWindow(parent, B_TRANSLATE("Edit colors")),
	fSlot(slot)
{
	fControl = new BColorControl(B_ORIGIN, B_CELLS_32x8, 8, "color",
		new BMessage(kMsgColor));
	rgb_color value = make_color(pp::Red(color), pp::Green(color),
		pp::Blue(color));
	fControl->SetValue(value);
	fPreview = new BView("preview", B_WILL_DRAW);
	fPreview->SetViewColor(value);
	fPreview->SetExplicitMinSize(BSize(48, 32));
	fPreview->SetExplicitMaxSize(BSize(48, B_SIZE_UNLIMITED));

	BGroupView* content = new BGroupView(B_HORIZONTAL);
	BLayoutBuilder::Group<>(content)
		.Add(fControl)
		.Add(fPreview);
	Finish(content);
}


void
EditColorsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgColor:
			fPreview->SetViewColor(fControl->ValueAsColor());
			fPreview->Invalidate();
			break;
		case kMsgOK:
		{
			rgb_color c = fControl->ValueAsColor();
			BMessage reply(kMsgDoEditColor);
			reply.AddInt32("color", (int32)pp::MakePixel(c.red, c.green,
				c.blue));
			reply.AddInt32("slot", fSlot);
			fTarget.SendMessage(&reply);
			Quit();
			break;
		}
		default:
			DialogWindow::MessageReceived(message);
	}
}
