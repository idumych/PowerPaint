/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 */
#include "Raster.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <sstream>


namespace pp {

// #pragma mark - Rect


Rect
Rect::Union(const Rect& other) const
{
	if (IsEmpty())
		return other;
	if (other.IsEmpty())
		return *this;
	int left = std::min(x, other.x);
	int top = std::min(y, other.y);
	int right = std::max(Right(), other.Right());
	int bottom = std::max(Bottom(), other.Bottom());
	return Rect{left, top, right - left, bottom - top};
}


Rect
Rect::Intersect(const Rect& other) const
{
	int left = std::max(x, other.x);
	int top = std::max(y, other.y);
	int right = std::min(Right(), other.Right());
	int bottom = std::min(Bottom(), other.Bottom());
	if (right <= left || bottom <= top)
		return Rect();
	return Rect{left, top, right - left, bottom - top};
}


Rect
Rect::FromCorners(int x1, int y1, int x2, int y2)
{
	int left = std::min(x1, x2);
	int top = std::min(y1, y2);
	return Rect{left, top, std::abs(x2 - x1) + 1, std::abs(y2 - y1) + 1};
}


// #pragma mark - Raster


static size_t
CheckedSize(int width, int height)
{
	if ((int64_t)width * height > kMaxPixels)
		throw std::bad_alloc();
	return (size_t)width * height;
}


Raster::Raster(int width, int height, Pixel fill)
	:
	fWidth(std::max(width, 0)),
	fHeight(std::max(height, 0)),
	fPixels(CheckedSize(fWidth, fHeight), fill)
{
}


void
Raster::Fill(Pixel p)
{
	std::fill(fPixels.begin(), fPixels.end(), p);
}


void
Raster::FillRect(Rect r, Pixel p)
{
	r = r.Intersect(Bounds());
	for (int y = r.y; y < r.Bottom(); y++)
		std::fill(&At(r.x, y), &At(r.x, y) + r.w, p);
}


Raster
Raster::Copy(Rect r) const
{
	Raster copy(r.w, r.h, 0);
	copy.BlitRect(*this, r, 0, 0);
	return copy;
}


void
Raster::Blit(const Raster& src, int x, int y)
{
	BlitRect(src, src.Bounds(), x, y);
}


void
Raster::BlitRect(const Raster& src, Rect from, int x, int y)
{
	// Clip against the source, then against this raster.
	Rect s = from.Intersect(src.Bounds());
	x += s.x - from.x;
	y += s.y - from.y;
	Rect d = Rect{x, y, s.w, s.h}.Intersect(Bounds());
	if (d.IsEmpty())
		return;
	int sx = s.x + (d.x - x);
	int sy = s.y + (d.y - y);
	for (int row = 0; row < d.h; row++) {
		memcpy(&At(d.x, d.y + row),
			src.Bits() + (size_t)(sy + row) * src.Width() + sx,
			d.w * sizeof(Pixel));
	}
}


bool
Raster::HasTransparency() const
{
	for (Pixel p : fPixels) {
		if (Alpha(p) != 255)
			return true;
	}
	return false;
}


// #pragma mark - Brushes and lines


std::vector<Point>
BrushOffsets(BrushShape shape, int size)
{
	std::vector<Point> offsets;
	if (size < 1)
		size = 1;
	int left = -size / 2;
	int top = -size / 2;
	switch (shape) {
		case kBrushCircle:
		{
			double r = size / 2.0;
			for (int j = 0; j < size; j++) {
				for (int i = 0; i < size; i++) {
					double dx = i + 0.5 - r;
					double dy = j + 0.5 - r;
					if (dx * dx + dy * dy <= r * r)
						offsets.push_back(Point{left + i, top + j});
				}
			}
			break;
		}
		case kBrushSquare:
			for (int j = 0; j < size; j++) {
				for (int i = 0; i < size; i++)
					offsets.push_back(Point{left + i, top + j});
			}
			break;
		case kBrushDiagonal:
			for (int i = 0; i < size; i++)
				offsets.push_back(Point{left + i, top + i});
			break;
		case kBrushReverseDiagonal:
			for (int i = 0; i < size; i++)
				offsets.push_back(Point{left + i, top + size - 1 - i});
			break;
	}
	return offsets;
}


static void
Stamp(Raster& r, int x, int y, const std::vector<Point>& offsets,
	Pixel color)
{
	for (const Point& o : offsets)
		r.Set(x + o.x, y + o.y, color);
}


void
StampBrush(Raster& r, int x, int y, BrushShape shape, int size, Pixel color)
{
	Stamp(r, x, y, BrushOffsets(shape, size), color);
}


void
BrushLine(Raster& r, int x0, int y0, int x1, int y1, BrushShape shape,
	int size, Pixel color)
{
	std::vector<Point> offsets = BrushOffsets(shape, size);
	BresenhamLine(x0, y0, x1, y1, [&](int x, int y) {
		Stamp(r, x, y, offsets, color);
	});
}


void
DrawLine(Raster& r, int x0, int y0, int x1, int y1, Pixel color, int width)
{
	if (width <= 1) {
		BresenhamLine(x0, y0, x1, y1, [&](int x, int y) {
			r.Set(x, y, color);
		});
		return;
	}
	BrushLine(r, x0, y0, x1, y1, kBrushCircle, width, color);
}


// #pragma mark - Shapes


// Paints the pixels of `box` for which inside(px, py) holds.
template<typename Inside>
static void
PaintWhere(Raster& r, Rect box, Pixel color, Inside inside)
{
	Rect clip = box.Intersect(r.Bounds());
	for (int y = clip.y; y < clip.Bottom(); y++) {
		for (int x = clip.x; x < clip.Right(); x++) {
			if (inside(x, y))
				r.At(x, y) = color;
		}
	}
}


// Shapes are drawn as "inside the box" tests; the outline is the part of the
// shape that isn't inside the same shape inset by the outline's width.
template<typename Inside>
static void
DrawInsideShape(Raster& r, Rect box, FillStyle style, Pixel stroke,
	Pixel fill, int width, Inside inside)
{
	if (box.IsEmpty())
		return;
	if (width < 1)
		width = 1;
	Rect inner = box.Inset(width);
	if (style == kFillOnly) {
		PaintWhere(r, box, fill, [&](int x, int y) {
			return inside(box, x, y);
		});
		return;
	}
	if (style == kFillOutlineAndFill && !inner.IsEmpty()) {
		PaintWhere(r, inner, fill, [&](int x, int y) {
			return inside(inner, x, y);
		});
	}
	PaintWhere(r, box, stroke, [&](int x, int y) {
		return inside(box, x, y)
			&& (inner.IsEmpty() || !inside(inner, x, y));
	});
}


static bool
InsideRect(const Rect& box, int x, int y)
{
	return x >= box.x && y >= box.y && x < box.Right() && y < box.Bottom();
}


static bool
InsideEllipse(const Rect& box, int x, int y)
{
	double rx = box.w / 2.0;
	double ry = box.h / 2.0;
	double dx = (x + 0.5 - box.x - rx) / rx;
	double dy = (y + 0.5 - box.y - ry) / ry;
	return dx * dx + dy * dy <= 1.0;
}


static bool
InsideRounded(const Rect& box, double radius, int x, int y)
{
	if (!InsideRect(box, x, y))
		return false;
	if (radius <= 0)
		return true;
	double px = x + 0.5;
	double py = y + 0.5;
	// The nearest corner circle's centre, if the point is in a corner.
	double cx = std::clamp(px, box.x + radius, box.Right() - radius);
	double cy = std::clamp(py, box.y + radius, box.Bottom() - radius);
	double dx = px - cx;
	double dy = py - cy;
	return dx * dx + dy * dy <= radius * radius;
}


void
DrawRectangle(Raster& r, Rect box, FillStyle style, Pixel stroke, Pixel fill,
	int width)
{
	DrawInsideShape(r, box, style, stroke, fill, width, InsideRect);
}


void
DrawEllipse(Raster& r, Rect box, FillStyle style, Pixel stroke, Pixel fill,
	int width)
{
	DrawInsideShape(r, box, style, stroke, fill, width, InsideEllipse);
}


void
DrawRoundedRectangle(Raster& r, Rect box, FillStyle style, Pixel stroke,
	Pixel fill, int width)
{
	// JS Paint: a radius of 8, smaller for small boxes; the inner edge of
	// the outline follows with a radius smaller by the width.
	const double outer = std::min({8.0, box.w / 2.0, box.h / 2.0});
	DrawInsideShape(r, box, style, stroke, fill, width,
		[&](const Rect& b, int x, int y) {
			double radius = outer - (b.x - box.x);
			return InsideRounded(b, std::max(radius, 0.0), x, y);
		});
}


Rect
PointsBounds(const std::vector<Point>& points)
{
	if (points.empty())
		return Rect();
	int left = points[0].x, right = points[0].x;
	int top = points[0].y, bottom = points[0].y;
	for (const Point& p : points) {
		left = std::min(left, p.x);
		right = std::max(right, p.x);
		top = std::min(top, p.y);
		bottom = std::max(bottom, p.y);
	}
	return Rect{left, top, right - left + 1, bottom - top + 1};
}


// Even-odd scanline fill at pixel centres; fn(x, y) for each inside pixel.
template<typename Fn>
static void
ScanPolygon(const std::vector<Point>& points, Rect clip, Fn fn)
{
	size_t n = points.size();
	if (n < 3)
		return;
	std::vector<double> xs;
	for (int y = clip.y; y < clip.Bottom(); y++) {
		double cy = y + 0.5;
		xs.clear();
		for (size_t i = 0; i < n; i++) {
			// Edges between pixel centres of the vertices.
			double x0 = points[i].x + 0.5, y0 = points[i].y + 0.5;
			double x1 = points[(i + 1) % n].x + 0.5;
			double y1 = points[(i + 1) % n].y + 0.5;
			if ((y0 <= cy && y1 > cy) || (y1 <= cy && y0 > cy))
				xs.push_back(x0 + (cy - y0) * (x1 - x0) / (y1 - y0));
		}
		std::sort(xs.begin(), xs.end());
		for (size_t i = 0; i + 1 < xs.size(); i += 2) {
			int from = std::max((int)std::ceil(xs[i] - 0.5), clip.x);
			int to = std::min((int)std::ceil(xs[i + 1] - 0.5), clip.Right());
			for (int x = from; x < to; x++)
				fn(x, y);
		}
	}
}


void
DrawPolyline(Raster& r, const std::vector<Point>& points, Pixel color,
	int width)
{
	if (points.size() == 1)
		DrawLine(r, points[0].x, points[0].y, points[0].x, points[0].y, color,
			width);
	for (size_t i = 0; i + 1 < points.size(); i++) {
		DrawLine(r, points[i].x, points[i].y, points[i + 1].x,
			points[i + 1].y, color, width);
	}
}


void
DrawPolygon(Raster& r, const std::vector<Point>& points, FillStyle style,
	Pixel stroke, Pixel fill, int width)
{
	if (points.empty())
		return;
	if (style != kFillOutline) {
		ScanPolygon(points, r.Bounds(), [&](int x, int y) {
			r.At(x, y) = fill;
		});
	}
	if (style != kFillOnly) {
		std::vector<Point> closed = points;
		closed.push_back(points[0]);
		DrawPolyline(r, closed, stroke, width);
	}
}


Mask
PolygonMask(const std::vector<Point>& points, Rect bounds)
{
	Mask mask(bounds.w, bounds.h);
	std::vector<Point> local;
	for (const Point& p : points)
		local.push_back(Point{p.x - bounds.x, p.y - bounds.y});
	ScanPolygon(local, Rect{0, 0, bounds.w, bounds.h}, [&](int x, int y) {
		mask.Set(x, y);
	});
	// The outline belongs to the selection too.
	for (size_t i = 0; i < local.size(); i++) {
		const Point& a = local[i];
		const Point& b = local[(i + 1) % local.size()];
		BresenhamLine(a.x, a.y, b.x, b.y, [&](int x, int y) {
			mask.Set(x, y);
		});
	}
	return mask;
}


void
DrawBezier(Raster& r, Point p0, Point p1, Point p2, Point p3, Pixel color,
	int width)
{
	// Enough steps that consecutive points are about a pixel apart.
	double length = std::hypot(p1.x - p0.x, p1.y - p0.y)
		+ std::hypot(p2.x - p1.x, p2.y - p1.y)
		+ std::hypot(p3.x - p2.x, p3.y - p2.y);
	int steps = std::max(1, (int)std::ceil(length));
	std::vector<Point> points;
	for (int i = 0; i <= steps; i++) {
		double t = (double)i / steps;
		double u = 1 - t;
		double x = u * u * u * p0.x + 3 * u * u * t * p1.x
			+ 3 * u * t * t * p2.x + t * t * t * p3.x;
		double y = u * u * u * p0.y + 3 * u * u * t * p1.y
			+ 3 * u * t * t * p2.y + t * t * t * p3.y;
		Point p{(int)std::lround(x), (int)std::lround(y)};
		if (points.empty() || p.x != points.back().x || p.y != points.back().y)
			points.push_back(p);
	}
	DrawPolyline(r, points, color, width);
}


// #pragma mark - Fills and the color eraser


Rect
FloodFill(Raster& r, int x, int y, Pixel color)
{
	if (!r.Contains(x, y))
		return Rect();
	Pixel target = r.At(x, y);
	if (target == color)
		return Rect();

	int left = x, right = x, top = y, bottom = y;
	std::vector<Point> stack;
	stack.push_back(Point{x, y});
	while (!stack.empty()) {
		Point p = stack.back();
		stack.pop_back();
		if (r.At(p.x, p.y) != target)
			continue;
		// Extend the span both ways, paint it, and queue the rows around it.
		int x0 = p.x, x1 = p.x;
		while (x0 > 0 && r.At(x0 - 1, p.y) == target)
			x0--;
		while (x1 < r.Width() - 1 && r.At(x1 + 1, p.y) == target)
			x1++;
		for (int i = x0; i <= x1; i++)
			r.At(i, p.y) = color;
		left = std::min(left, x0);
		right = std::max(right, x1);
		top = std::min(top, p.y);
		bottom = std::max(bottom, p.y);
		for (int dy = -1; dy <= 1; dy += 2) {
			int ny = p.y + dy;
			if (ny < 0 || ny >= r.Height())
				continue;
			bool inSpan = false;
			for (int i = x0; i <= x1; i++) {
				bool match = r.At(i, ny) == target;
				if (match && !inSpan)
					stack.push_back(Point{i, ny});
				inSpan = match;
			}
		}
	}
	return Rect{left, top, right - left + 1, bottom - top + 1};
}


void
ReplaceColor(Raster& r, Rect box, Pixel from, Pixel to)
{
	box = box.Intersect(r.Bounds());
	for (int y = box.y; y < box.Bottom(); y++) {
		for (int x = box.x; x < box.Right(); x++) {
			if (r.At(x, y) == from)
				r.At(x, y) = to;
		}
	}
}


void
Spray(Raster& r, int x, int y, int size, Pixel color, std::mt19937& random)
{
	// JS Paint's airbrush: 6 + r/5 tries per burst, inside the circle only.
	double radius = size / 2.0;
	std::uniform_real_distribution<double> unit(-1.0, 1.0);
	int tries = 6 + (int)(radius / 5);
	for (int i = 0; i < tries; i++) {
		double rx = unit(random) * radius;
		double ry = unit(random) * radius;
		if (rx * rx + ry * ry <= radius * radius)
			r.Set(x + (int)rx, y + (int)ry, color);
	}
}


// #pragma mark - Transforms


void
FlipHorizontal(Raster& r)
{
	for (int y = 0; y < r.Height(); y++)
		std::reverse(&r.At(0, y), &r.At(0, y) + r.Width());
}


void
FlipVertical(Raster& r)
{
	for (int y = 0; y < r.Height() / 2; y++) {
		std::swap_ranges(&r.At(0, y), &r.At(0, y) + r.Width(),
			&r.At(0, r.Height() - 1 - y));
	}
}


Raster
Rotate90(const Raster& r, int quarterTurns)
{
	quarterTurns = ((quarterTurns % 4) + 4) % 4;
	int w = r.Width(), h = r.Height();
	if (quarterTurns == 0)
		return r;
	if (quarterTurns == 2) {
		Raster out(w, h);
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++)
				out.At(w - 1 - x, h - 1 - y) = r.At(x, y);
		}
		return out;
	}
	Raster out(h, w);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			if (quarterTurns == 1)	// clockwise
				out.At(h - 1 - y, x) = r.At(x, y);
			else
				out.At(y, w - 1 - x) = r.At(x, y);
		}
	}
	return out;
}


// Maps each output pixel back through the inverse of the 2x2 matrix
// (a b; c d) and samples the nearest source pixel.
static Raster
AffineNearest(const Raster& r, double a, double b, double c, double d,
	Pixel background)
{
	int w = r.Width(), h = r.Height();
	double xs[4] = {0, a * w, b * h, a * w + b * h};
	double ys[4] = {0, c * w, d * h, c * w + d * h};
	double minX = *std::min_element(xs, xs + 4);
	double maxX = *std::max_element(xs, xs + 4);
	double minY = *std::min_element(ys, ys + 4);
	double maxY = *std::max_element(ys, ys + 4);
	int outW = std::max(1, (int)std::lround(maxX - minX));
	int outH = std::max(1, (int)std::lround(maxY - minY));
	double det = a * d - b * c;
	Raster out(outW, outH, background);
	if (std::fabs(det) < 1e-9)
		return out;
	for (int y = 0; y < outH; y++) {
		for (int x = 0; x < outW; x++) {
			double px = x + 0.5 + minX;
			double py = y + 0.5 + minY;
			double sx = (d * px - b * py) / det;
			double sy = (-c * px + a * py) / det;
			int ix = (int)std::floor(sx);
			int iy = (int)std::floor(sy);
			if (r.Contains(ix, iy))
				out.At(x, y) = r.At(ix, iy);
		}
	}
	return out;
}


Raster
RotateDegrees(const Raster& r, double degrees, Pixel background)
{
	double turns = degrees / 90.0;
	if (std::fabs(turns - std::round(turns)) < 1e-9)
		return Rotate90(r, (int)std::lround(turns));
	double t = degrees * M_PI / 180.0;
	return AffineNearest(r, std::cos(t), -std::sin(t), std::sin(t),
		std::cos(t), background);
}


Raster
StretchSkew(const Raster& r, double xScale, double yScale,
	double hSkewDegrees, double vSkewDegrees, Pixel background)
{
	double th = std::tan(hSkewDegrees * M_PI / 180.0);
	double tv = std::tan(vSkewDegrees * M_PI / 180.0);
	if (th == 0 && tv == 0) {
		return ScaleNearest(r, std::max(1, (int)std::lround(r.Width() * xScale)),
			std::max(1, (int)std::lround(r.Height() * yScale)));
	}
	return AffineNearest(r, xScale, th * yScale, tv * xScale, yScale,
		background);
}


Raster
ScaleNearest(const Raster& r, int width, int height)
{
	width = std::max(width, 1);
	height = std::max(height, 1);
	Raster out(width, height);
	if (r.Width() == 0 || r.Height() == 0)
		return out;
	for (int y = 0; y < height; y++) {
		int sy = (int)((int64_t)y * r.Height() / height);
		for (int x = 0; x < width; x++) {
			int sx = (int)((int64_t)x * r.Width() / width);
			out.At(x, y) = r.At(sx, sy);
		}
	}
	return out;
}


void
Invert(Raster& r)
{
	Pixel* p = r.Bits();
	for (size_t i = 0; i < (size_t)r.Width() * r.Height(); i++)
		p[i] ^= 0x00ffffff;
}


Raster
ResizeCanvas(const Raster& r, int width, int height, Pixel background)
{
	Raster out(std::max(width, 1), std::max(height, 1), background);
	out.Blit(r, 0, 0);
	return out;
}


void
Composite(Raster& dst, const Raster& src, int x, int y,
	const Pixel* transparentColor)
{
	Rect d = Rect{x, y, src.Width(), src.Height()}.Intersect(dst.Bounds());
	for (int dy = d.y; dy < d.Bottom(); dy++) {
		for (int dx = d.x; dx < d.Right(); dx++) {
			int sx = dx - x, sy = dy - y;
			Pixel p = src.At(sx, sy);
			if (Alpha(p) == 0)
				continue;
			if (transparentColor != nullptr && p == *transparentColor)
				continue;
			dst.At(dx, dy) = p;
		}
	}
}


// #pragma mark - Palettes


static uint32_t
ReadLE32(const uint8_t* p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}


static void
WriteLE32(std::vector<uint8_t>& out, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		out.push_back((v >> (8 * i)) & 0xff);
}


bool
ReadPalette(const std::vector<uint8_t>& data, std::vector<Pixel>& colors)
{
	colors.clear();
	if (data.size() >= 24 && memcmp(data.data(), "RIFF", 4) == 0
		&& memcmp(data.data() + 8, "PAL ", 4) == 0) {
		// RIFF palette: chunks after the form type; "data" holds a
		// version (0x0300), a count, then r g b flags per entry.
		size_t pos = 12;
		while (pos + 8 <= data.size()) {
			uint32_t size = ReadLE32(data.data() + pos + 4);
			if (memcmp(data.data() + pos, "data", 4) == 0) {
				const uint8_t* chunk = data.data() + pos + 8;
				if (size < 4 || pos + 8 + size > data.size())
					return false;
				int count = chunk[2] | (chunk[3] << 8);
				if (4 + (size_t)count * 4 > size)
					return false;
				for (int i = 0; i < count; i++) {
					const uint8_t* e = chunk + 4 + i * 4;
					colors.push_back(MakePixel(e[0], e[1], e[2]));
				}
				return !colors.empty();
			}
			pos += 8 + size + (size & 1);
		}
		return false;
	}

	std::string text(data.begin(), data.end());
	std::istringstream in(text);
	std::string line;
	if (!std::getline(in, line))
		return false;
	if (!line.empty() && line.back() == '\r')
		line.pop_back();
	bool jasc = line == "JASC-PAL";
	if (!jasc && line != "GIMP Palette")
		return false;
	int skip = jasc ? 2 : 0;	// version and count
	while (std::getline(in, line)) {
		if (skip > 0) {
			skip--;
			continue;
		}
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty() || line[0] == '#' || line.compare(0, 5, "Name:") == 0
			|| line.compare(0, 8, "Columns:") == 0)
			continue;
		std::istringstream fields(line);
		int r, g, b;
		if (fields >> r >> g >> b)
			colors.push_back(MakePixel(r & 0xff, g & 0xff, b & 0xff));
	}
	return !colors.empty();
}


std::vector<uint8_t>
WritePalette(const std::vector<Pixel>& colors, const std::string& fileName)
{
	std::vector<uint8_t> out;
	std::string lower = fileName;
	for (char& c : lower)
		c = (char)tolower((unsigned char)c);
	if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".gpl") == 0) {
		std::ostringstream text;
		text << "GIMP Palette\nName: PowerPaint colors\nColumns: 14\n#\n";
		for (Pixel p : colors) {
			text << Red(p) << ' ' << Green(p) << ' ' << Blue(p) << '\t'
				<< "Untitled\n";
		}
		std::string s = text.str();
		return std::vector<uint8_t>(s.begin(), s.end());
	}

	uint32_t dataSize = 4 + 4 * (uint32_t)colors.size();
	out.insert(out.end(), {'R', 'I', 'F', 'F'});
	WriteLE32(out, 4 + 8 + dataSize);
	out.insert(out.end(), {'P', 'A', 'L', ' ', 'd', 'a', 't', 'a'});
	WriteLE32(out, dataSize);
	out.insert(out.end(), {0x00, 0x03});
	out.push_back(colors.size() & 0xff);
	out.push_back((colors.size() >> 8) & 0xff);
	for (Pixel p : colors) {
		out.push_back(Red(p));
		out.push_back(Green(p));
		out.push_back(Blue(p));
		out.push_back(0);
	}
	return out;
}

}	// namespace pp
