/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 */
#include "TextRender.h"

#include <Bitmap.h>
#include <View.h>

#include <algorithm>
#include <math.h>
#include <string.h>
#include <vector>


namespace TextRender {

static const int32 kPadding = 2;


BFont
FontFor(const PaintState& state)
{
	BFont font(be_plain_font);
	if (state.fontFamily.Length() > 0)
		font.SetFamilyAndStyle(state.fontFamily.String(), NULL);
	uint16 face = 0;
	if (state.bold)
		face |= B_BOLD_FACE;
	if (state.italic)
		face |= B_ITALIC_FACE;
	if (state.underline)
		face |= B_UNDERSCORE_FACE;
	font.SetFace(face != 0 ? face : B_REGULAR_FACE);
	font.SetSize(state.fontSize);
	font.SetFlags(B_DISABLE_ANTIALIASING);
	return font;
}


// Splits the text into lines no wider than `width`: at newlines, between
// words, and inside words too long for a line on their own.
static std::vector<BString>
WrapLines(const BString& text, const BFont& font, float width)
{
	std::vector<BString> lines;
	int32 start = 0;
	while (start <= text.Length()) {
		int32 end = text.FindFirst('\n', start);
		if (end < 0)
			end = text.Length();
		BString paragraph;
		text.CopyInto(paragraph, start, end - start);

		BString line;
		int32 i = 0;
		while (i < paragraph.Length()) {
			// The next word with the spaces before it.
			int32 wordEnd = i;
			while (wordEnd < paragraph.Length() && paragraph[wordEnd] == ' ')
				wordEnd++;
			while (wordEnd < paragraph.Length() && paragraph[wordEnd] != ' ')
				wordEnd++;
			BString word;
			paragraph.CopyInto(word, i, wordEnd - i);
			i = wordEnd;

			BString candidate = line;
			candidate << word;
			if (line.Length() == 0
				|| font.StringWidth(candidate.String()) <= width) {
				line = candidate;
			} else {
				lines.push_back(line);
				int32 spaces = 0;
				while (spaces < word.Length() && word[spaces] == ' ')
					spaces++;
				word.Remove(0, spaces);
				line = word;
			}

			// A word wider than the box on its own is broken.
			while (font.StringWidth(line.String()) > width
				&& line.CountChars() > 1) {
				int32 fit = line.CountChars() - 1;
				BString head;
				for (; fit > 1; fit--) {
					head.SetTo(line);
					head.TruncateChars(fit);
					if (font.StringWidth(head.String()) <= width)
						break;
				}
				head.SetTo(line);
				head.TruncateChars(fit);
				lines.push_back(head);
				line.RemoveChars(0, fit);
			}
		}
		lines.push_back(line);
		start = end + 1;
	}
	return lines;
}


pp::Raster
Render(const BString& text, int32 width, const PaintState& state,
	int32* height)
{
	width = std::max<int32>(width, 8);
	BFont font = FontFor(state);
	font_height fh;
	font.GetHeight(&fh);
	float lineHeight = ceilf(fh.ascent + fh.descent + fh.leading);
	std::vector<BString> lines = WrapLines(text, font,
		width - 2 * kPadding);
	int32 needed = (int32)(lines.size() * lineHeight) + 2 * kPadding;
	*height = std::max(*height, needed);

	// Black text on white, then mapped to the colours.
	BBitmap bitmap(BRect(0, 0, width - 1, *height - 1),
		B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
	pp::Raster out(width, *height, 0);
	if (bitmap.InitCheck() != B_OK)
		return out;
	BView* view = new BView(bitmap.Bounds(), "text", B_FOLLOW_NONE, 0);
	bitmap.AddChild(view);
	if (bitmap.Lock()) {
		view->SetHighColor(255, 255, 255);
		view->FillRect(bitmap.Bounds());
		view->SetFont(&font);
		view->SetHighColor(0, 0, 0);
		view->SetLowColor(255, 255, 255);
		view->SetDrawingMode(B_OP_OVER);
		for (size_t i = 0; i < lines.size(); i++) {
			view->DrawString(lines[i].String(),
				BPoint(kPadding, kPadding + fh.ascent + i * lineHeight));
		}
		view->Sync();
		bitmap.Unlock();
	}

	const uint8* bits = (const uint8*)bitmap.Bits();
	pp::Pixel background = state.transparentMode ? 0 : state.background;
	for (int32 y = 0; y < *height; y++) {
		const uint8* row = bits + y * bitmap.BytesPerRow();
		for (int32 x = 0; x < width; x++) {
			// B_RGBA32 is B G R A in memory; any dark pixel is text.
			bool ink = row[x * 4] < 128 || row[x * 4 + 1] < 128
				|| row[x * 4 + 2] < 128;
			out.At(x, y) = ink ? state.foreground : background;
		}
	}
	return out;
}

}	// namespace TextRender
