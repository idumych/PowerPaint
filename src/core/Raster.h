/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 *
 * The portable core: a pixel buffer and the aliased drawing operations of
 * MS Paint as JS Paint implements them. No Haiku headers, so it builds and
 * is tested on any host (make test).
 *
 * Pixels are uint32 0xAARRGGBB, which is B_RGBA32's memory layout on a
 * little-endian machine.
 */
#ifndef PP_RASTER_H
#define PP_RASTER_H

#include <cstdint>
#include <random>
#include <string>
#include <vector>


namespace pp {

typedef uint32_t Pixel;

inline Pixel
MakePixel(int r, int g, int b, int a = 255)
{
	return ((Pixel)a << 24) | ((Pixel)r << 16) | ((Pixel)g << 8) | (Pixel)b;
}

inline int Red(Pixel p) { return (p >> 16) & 0xff; }
inline int Green(Pixel p) { return (p >> 8) & 0xff; }
inline int Blue(Pixel p) { return p & 0xff; }
inline int Alpha(Pixel p) { return p >> 24; }


struct Point {
	int x, y;
};


struct Rect {
	int x = 0, y = 0, w = 0, h = 0;

	bool	IsEmpty() const { return w <= 0 || h <= 0; }
	int		Right() const { return x + w; }
	int		Bottom() const { return y + h; }
	// The smallest rectangle containing both; an empty one adds nothing.
	Rect	Union(const Rect& other) const;
	Rect	Intersect(const Rect& other) const;
	Rect	Inset(int by) const { return Rect{x + by, y + by, w - 2 * by,
				h - 2 * by}; }
	static Rect	FromCorners(int x1, int y1, int x2, int y2);
};


// shortcut: a fixed cap instead of checking free memory; Haiku lets huge
// allocations succeed and then panics when memory runs out. Raise it when
// that is fixed or pictures need more than 256 MiB per copy.
const int64_t kMaxPixels = 8192 * 8192;


// Throws std::bad_alloc for more than kMaxPixels pixels.
class Raster {
public:
							Raster() {}
							Raster(int width, int height, Pixel fill = 0);

	int						Width() const { return fWidth; }
	int						Height() const { return fHeight; }
	Rect					Bounds() const { return Rect{0, 0, fWidth,
								fHeight}; }
	bool					Contains(int x, int y) const
								{ return x >= 0 && y >= 0 && x < fWidth
									&& y < fHeight; }

	Pixel*					Bits() { return fPixels.data(); }
	const Pixel*			Bits() const { return fPixels.data(); }
	Pixel&					At(int x, int y)
								{ return fPixels[(size_t)y * fWidth + x]; }
	Pixel					At(int x, int y) const
								{ return fPixels[(size_t)y * fWidth + x]; }
	// Bounds-checked; outside pixels are ignored or read as 0.
	void					Set(int x, int y, Pixel p)
								{ if (Contains(x, y)) At(x, y) = p; }
	Pixel					Get(int x, int y) const
								{ return Contains(x, y) ? At(x, y) : 0; }

	void					Fill(Pixel p);
	void					FillRect(Rect r, Pixel p);
	Raster					Copy(Rect r) const;
	// Copies src onto this raster at (x, y), clipped, replacing pixels.
	void					Blit(const Raster& src, int x, int y);
	void					BlitRect(const Raster& src, Rect from, int x,
								int y);
	bool					operator==(const Raster& other) const
								{ return fWidth == other.fWidth
									&& fHeight == other.fHeight
									&& fPixels == other.fPixels; }
	bool					HasTransparency() const;

private:
	int						fWidth = 0;
	int						fHeight = 0;
	std::vector<Pixel>		fPixels;
};


// One byte per pixel, nonzero = inside. Used for free-form selections and
// for collecting shapes before they are painted.
class Mask {
public:
							Mask() {}
							Mask(int width, int height, uint8_t value = 0)
								: fWidth(width), fHeight(height),
								  fBits((size_t)width * height, value) {}

	int						Width() const { return fWidth; }
	int						Height() const { return fHeight; }
	bool					Get(int x, int y) const
								{ return x >= 0 && y >= 0 && x < fWidth
									&& y < fHeight
									&& fBits[(size_t)y * fWidth + x] != 0; }
	void					Set(int x, int y, bool on = true)
								{ if (x >= 0 && y >= 0 && x < fWidth
									&& y < fHeight)
									fBits[(size_t)y * fWidth + x] = on; }

private:
	int						fWidth = 0;
	int						fHeight = 0;
	std::vector<uint8_t>	fBits;
};


enum BrushShape {
	kBrushCircle = 0,
	kBrushSquare,
	kBrushReverseDiagonal,	// "/"
	kBrushDiagonal			// "\"
};

enum FillStyle {
	kFillOutline = 0,
	kFillOutlineAndFill,
	kFillOnly
};


// The pixels a brush covers, as offsets from the pointer's pixel.
std::vector<Point>	BrushOffsets(BrushShape shape, int size);
void				StampBrush(Raster& r, int x, int y, BrushShape shape,
						int size, Pixel color);

// Calls fn(x, y) for each pixel of a Bresenham line, both ends included.
template<typename Fn>
void
BresenhamLine(int x0, int y0, int x1, int y1, Fn fn)
{
	int dx = x1 > x0 ? x1 - x0 : x0 - x1;
	int dy = y1 > y0 ? y0 - y1 : y1 - y0;
	int sx = x0 < x1 ? 1 : -1;
	int sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	for (;;) {
		fn(x0, y0);
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

// A line of the given width, as MS Paint draws one: a round brush of that
// size stamped along a Bresenham line (width 1 is the plain line).
void				DrawLine(Raster& r, int x0, int y0, int x1, int y1,
						Pixel color, int width = 1);
void				BrushLine(Raster& r, int x0, int y0, int x1, int y1,
						BrushShape shape, int size, Pixel color);

// Shapes inside the box (x, y, w, h). The outline is `width` pixels thick
// and drawn inside the box; the fill covers the inside of the outline.
void				DrawRectangle(Raster& r, Rect box, FillStyle style,
						Pixel stroke, Pixel fill, int width);
void				DrawEllipse(Raster& r, Rect box, FillStyle style,
						Pixel stroke, Pixel fill, int width);
void				DrawRoundedRectangle(Raster& r, Rect box,
						FillStyle style, Pixel stroke, Pixel fill, int width);
// A closed polygon; the fill uses the even-odd rule at pixel centres.
void				DrawPolygon(Raster& r, const std::vector<Point>& points,
						FillStyle style, Pixel stroke, Pixel fill, int width);
// Open polyline (the polygon tool before it's closed).
void				DrawPolyline(Raster& r, const std::vector<Point>& points,
						Pixel color, int width);
// Cubic Bézier from p0 to p3 with control points p1 and p2.
void				DrawBezier(Raster& r, Point p0, Point p1, Point p2,
						Point p3, Pixel color, int width);
// The pixels inside a polygon (free-form selection), in a mask the size
// of `bounds`, with (bounds.x, bounds.y) as its origin.
Mask				PolygonMask(const std::vector<Point>& points,
						Rect bounds);
Rect				PointsBounds(const std::vector<Point>& points);

// Contiguous fill of the pixels exactly equal to the one at (x, y).
// Returns the area changed.
Rect				FloodFill(Raster& r, int x, int y, Pixel color);
// The color eraser: within `box`, pixels equal to `from` become `to`.
void				ReplaceColor(Raster& r, Rect box, Pixel from, Pixel to);
// Airbrush: one burst of random dots within a circle of `size`.
void				Spray(Raster& r, int x, int y, int size, Pixel color,
						std::mt19937& random);

// Transforms. Those that change the size return a new raster; uncovered
// pixels get `background`.
void				FlipHorizontal(Raster& r);
void				FlipVertical(Raster& r);
Raster				Rotate90(const Raster& r, int quarterTurns);
Raster				RotateDegrees(const Raster& r, double degrees,
						Pixel background);
Raster				StretchSkew(const Raster& r, double xScale,
						double yScale, double hSkewDegrees,
						double vSkewDegrees, Pixel background);
Raster				ScaleNearest(const Raster& r, int width, int height);
void				Invert(Raster& r);
Raster				ResizeCanvas(const Raster& r, int width, int height,
						Pixel background);

// Composites src onto dst at (x, y). Fully transparent source pixels (what
// lies outside a free-form selection) are skipped; with `transparentColor`
// set, pixels of that color are skipped too (MS Paint's transparent
// selection mode).
void				Composite(Raster& dst, const Raster& src, int x, int y,
						const Pixel* transparentColor = nullptr);

// Palettes. Reads GIMP (.gpl), RIFF (.pal) and JASC (.pal) files; writes
// the format the name's extension asks for (.gpl, else RIFF .pal).
bool				ReadPalette(const std::vector<uint8_t>& data,
						std::vector<Pixel>& colors);
std::vector<uint8_t>	WritePalette(const std::vector<Pixel>& colors,
						const std::string& fileName);

}	// namespace pp

#endif	// PP_RASTER_H
