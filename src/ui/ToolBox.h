/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 *
 * The tool box: sixteen 25 x 25 tool buttons in two columns, and under them
 * the 41 x 66 box with the current tool's options (brush shapes, eraser and
 * airbrush sizes, line widths, fill styles, zoom, transparent selection).
 */
#ifndef TOOL_BOX_H
#define TOOL_BOX_H

#include <View.h>

#include <vector>

#include "App.h"

class BBitmap;


const char*	ToolName(int32 tool);
const char*	ToolDescription(int32 tool);


class ToolBox : public BView {
public:
							ToolBox();
	virtual					~ToolBox();

	virtual void			Draw(BRect updateRect);
	virtual void			MouseDown(BPoint where);
	virtual void			MouseMoved(BPoint where, uint32 transit,
								const BMessage* dragMessage);
	virtual BSize			MinSize();
	virtual BSize			MaxSize();
	virtual BSize			PreferredSize();

	void					StateChanged();
	// The zoom the magnifier's options show as chosen.
	void					SetZoom(int32 zoom);

private:
	BRect					_ToolRect(int32 tool) const;
	BRect					_OptionsFrame() const;
	void					_OptionRects(std::vector<BRect>& rects) const;
	int32					_SelectedOption() const;
	void					_ChooseOption(int32 index);
	void					_DrawOptions(BRect updateRect);
	void					_DrawOption(int32 index, BRect rect, bool selected);

	PaintState				fState;
	int32					fZoom;
	BBitmap*				fIcons;
	BBitmap*				fTransparency;
	int32					fHoverTool;
};

#endif	// TOOL_BOX_H
