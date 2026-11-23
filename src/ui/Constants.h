/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 */
#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <SupportDefs.h>


#define APP_NAME		"PowerPaint"
#define APP_SIGNATURE	"application/x-vnd.idumych-PowerPaint"

// Tools, in tool box order (two columns, as in MS Paint), which is also the
// order of their icons in images/tools.png.
enum {
	kToolFreeFormSelect = 0,
	kToolSelect,
	kToolEraser,
	kToolFill,
	kToolPickColor,
	kToolMagnifier,
	kToolPencil,
	kToolBrush,
	kToolAirbrush,
	kToolText,
	kToolLine,
	kToolCurve,
	kToolRectangle,
	kToolPolygon,
	kToolEllipse,
	kToolRoundedRectangle,
	kToolCount
};

// Application messages
enum {
	kMsgStateChanged		= 'stch',	// broadcast to every window
	kMsgNewWindow			= 'nwin',
	kMsgOpen				= 'open',
	kMsgWindowClosed		= 'wcls',
	kMsgScript				= 'Grun',	// "script": commands for testing

	// File
	kMsgSave				= 'save',
	kMsgSaveAs				= 'svas',
	kMsgSaveRequested		= 'svrq',	// save panel reply

	// Edit
	kMsgUndo				= 'undo',
	kMsgRedo				= 'redo',
	kMsgClearSelection		= 'clrs',
	kMsgCopyTo				= 'cpto',
	kMsgCopyToRequested		= 'cptr',
	kMsgPasteFrom			= 'pfrm',
	kMsgPasteFromRequested	= 'pfrr',

	// View
	kMsgToggleToolBox		= 'vtlb',
	kMsgToggleColorBox		= 'vclb',
	kMsgToggleStatusBar		= 'vstb',
	kMsgToggleTextToolbar	= 'vtxt',
	kMsgZoom				= 'zoom',	// "zoom" int32
	kMsgToggleGrid			= 'grid',
	kMsgViewBitmap			= 'vbmp',

	// Image
	kMsgFlipRotate			= 'flrt',
	kMsgStretchSkew			= 'stsk',
	kMsgInvertColors		= 'invt',
	kMsgAttributes			= 'attr',
	kMsgClearImage			= 'clri',
	kMsgToggleDrawOpaque	= 'opaq',

	// Dialog replies
	kMsgDoFlipRotate		= 'dflr',	// "flip" int32 (0 h, 1 v) or "angle" double
	kMsgDoStretchSkew		= 'dstk',	// "sx" "sy" "hskew" "vskew" double
	kMsgDoAttributes		= 'datt',	// "width" "height" int32
	kMsgDoEditColor			= 'dedc',	// "color" int32, "slot" int32

	// Colors
	kMsgEditColors			= 'edcl',
	kMsgGetColors			= 'gtcl',
	kMsgGetColorsRequested	= 'gtcr',
	kMsgSaveColors			= 'svcl',
	kMsgSaveColorsRequested	= 'svcr',

	// Help
	kMsgAbout				= 'abut',

	// Views to window
	kMsgEditSlot			= 'edsl',	// "slot" int32
	kMsgSprayPulse			= 'spry',
};

// The colour box's current-colour slot when editing the foreground colour
// rather than a palette swatch.
const int32 kSlotForeground = -1;

#endif	// CONSTANTS_H
