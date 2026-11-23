/*
 * PowerPaint - host-side tests of the portable core (make test).
 * Distributed under the terms of the MIT License.
 */
#include "Raster.h"

#include <cstdio>
#include <cstdlib>
#include <new>

using namespace pp;

static int sFailures = 0;

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
				#condition); \
			sFailures++; \
		} \
	} while (0)

static const Pixel kWhite = MakePixel(255, 255, 255);
static const Pixel kBlack = MakePixel(0, 0, 0);
static const Pixel kRed = MakePixel(255, 0, 0);


static int
Count(const Raster& r, Pixel p)
{
	int n = 0;
	for (int y = 0; y < r.Height(); y++) {
		for (int x = 0; x < r.Width(); x++)
			n += r.At(x, y) == p;
	}
	return n;
}


static void
TestBrushes()
{
	CHECK(BrushOffsets(kBrushCircle, 1).size() == 1);
	CHECK(BrushOffsets(kBrushSquare, 5).size() == 25);
	CHECK(BrushOffsets(kBrushDiagonal, 8).size() == 8);
	// A 4-pixel round brush has its corners cut off.
	CHECK(BrushOffsets(kBrushCircle, 4).size() == 12);
	// A 7-pixel one is symmetric round its centre pixel.
	for (const Point& p : BrushOffsets(kBrushCircle, 7))
		CHECK(p.x >= -3 && p.x <= 3 && p.y >= -3 && p.y <= 3);
}


static void
TestLines()
{
	Raster r(20, 20, kWhite);
	DrawLine(r, 0, 0, 19, 19, kBlack);
	CHECK(Count(r, kBlack) == 20);
	CHECK(r.At(0, 0) == kBlack && r.At(19, 19) == kBlack);

	Raster h(20, 20, kWhite);
	DrawLine(h, 2, 10, 17, 10, kBlack, 3);
	// A 3-wide round brush along a horizontal line: three rows.
	CHECK(h.At(10, 9) == kBlack && h.At(10, 10) == kBlack
		&& h.At(10, 11) == kBlack && h.At(10, 12) == kWhite);

	// Lines off the canvas are clipped, not crashing.
	DrawLine(h, -50, -50, 100, 100, kRed, 5);
}


static void
TestShapes()
{
	Raster r(30, 30, kWhite);
	DrawRectangle(r, Rect{5, 5, 10, 8}, kFillOutline, kBlack, kRed, 1);
	CHECK(Count(r, kBlack) == 2 * 10 + 2 * 6);
	CHECK(r.At(5, 5) == kBlack && r.At(14, 12) == kBlack);
	CHECK(r.At(6, 6) == kWhite);

	Raster f(30, 30, kWhite);
	DrawRectangle(f, Rect{5, 5, 10, 8}, kFillOutlineAndFill, kBlack, kRed, 2);
	CHECK(Count(f, kRed) == 6 * 4);
	CHECK(Count(f, kBlack) == 80 - 24);

	Raster e(40, 40, kWhite);
	DrawEllipse(e, Rect{0, 0, 40, 40}, kFillOnly, kBlack, kRed, 1);
	CHECK(e.At(20, 20) == kRed && e.At(0, 0) == kWhite);
	CHECK(e.At(0, 20) == kRed && e.At(20, 0) == kRed);

	Raster o(40, 40, kWhite);
	DrawEllipse(o, Rect{0, 0, 40, 40}, kFillOutline, kBlack, kRed, 1);
	CHECK(o.At(20, 20) == kWhite && o.At(0, 20) == kBlack);

	Raster rr(40, 40, kWhite);
	DrawRoundedRectangle(rr, Rect{0, 0, 40, 40}, kFillOnly, kBlack, kRed, 1);
	CHECK(rr.At(0, 0) == kWhite && rr.At(20, 0) == kRed
		&& rr.At(0, 20) == kRed);

	// A box thinner than the outline is all outline.
	Raster thin(10, 10, kWhite);
	DrawRectangle(thin, Rect{0, 0, 3, 10}, kFillOutlineAndFill, kBlack, kRed, 2);
	CHECK(Count(thin, kRed) == 0 && Count(thin, kBlack) == 30);
}


static void
TestPolygon()
{
	Raster r(20, 20, kWhite);
	std::vector<Point> square = {{2, 2}, {11, 2}, {11, 11}, {2, 11}};
	DrawPolygon(r, square, kFillOnly, kBlack, kRed, 1);
	// Centres between the vertex centres: 9 x 9 pixels.
	CHECK(Count(r, kRed) == 81);

	Raster o(20, 20, kWhite);
	DrawPolygon(o, square, kFillOutline, kBlack, kRed, 1);
	CHECK(Count(o, kBlack) == 36);

	Mask m = PolygonMask(square, PointsBounds(square));
	CHECK(m.Width() == 10 && m.Height() == 10);
	CHECK(m.Get(0, 0) && m.Get(9, 9) && m.Get(5, 5));
}


static void
TestFill()
{
	Raster r(20, 20, kWhite);
	DrawRectangle(r, Rect{5, 5, 10, 10}, kFillOutline, kBlack, kBlack, 1);
	Rect changed = FloodFill(r, 10, 10, kRed);
	CHECK(Count(r, kRed) == 64);
	CHECK(changed.x == 6 && changed.y == 6 && changed.w == 8 && changed.h == 8);
	FloodFill(r, 0, 0, kRed);
	CHECK(Count(r, kRed) == 400 - 36);
	// Filling with the same color changes nothing.
	CHECK(FloodFill(r, 0, 0, kRed).IsEmpty());

	// A large area must not overflow anything.
	Raster big(2000, 2000, kWhite);
	FloodFill(big, 1000, 1000, kBlack);
	CHECK(Count(big, kBlack) == 2000 * 2000);
}


static void
TestTransforms()
{
	Raster r(3, 2, kWhite);
	r.At(0, 0) = kRed;
	Raster cw = Rotate90(r, 1);
	CHECK(cw.Width() == 2 && cw.Height() == 3 && cw.At(1, 0) == kRed);
	Raster ccw = Rotate90(r, 3);
	CHECK(ccw.At(0, 2) == kRed);
	Raster half = Rotate90(r, 2);
	CHECK(half.At(2, 1) == kRed);
	CHECK(RotateDegrees(r, 270, kBlack) == ccw);

	Raster flip = r;
	FlipHorizontal(flip);
	CHECK(flip.At(2, 0) == kRed);
	FlipVertical(flip);
	CHECK(flip.At(2, 1) == kRed);

	Raster big = StretchSkew(r, 2.0, 3.0, 0, 0, kBlack);
	CHECK(big.Width() == 6 && big.Height() == 6 && big.At(1, 2) == kRed
		&& big.At(2, 0) == kWhite);

	Raster skewed = StretchSkew(Raster(10, 10, kWhite), 1, 1, 45, 0, kBlack);
	CHECK(skewed.Width() == 20 && skewed.Height() == 10);
	CHECK(skewed.At(0, 0) == kWhite && skewed.At(0, 9) == kBlack);

	Raster rotated = RotateDegrees(Raster(10, 10, kWhite), 45, kBlack);
	CHECK(rotated.Width() == 14 && rotated.At(0, 0) == kBlack
		&& rotated.At(7, 7) == kWhite);

	Raster inv(1, 1, kRed);
	Invert(inv);
	CHECK(inv.At(0, 0) == MakePixel(0, 255, 255));

	bool threw = false;
	try {
		Raster huge(8192, 8193);
	} catch (const std::bad_alloc&) {
		threw = true;
	}
	CHECK(threw);

	Raster grown = ResizeCanvas(r, 5, 1, kBlack);
	CHECK(grown.Width() == 5 && grown.At(0, 0) == kRed
		&& grown.At(4, 0) == kBlack);
}


static void
TestComposite()
{
	Raster dst(4, 4, kBlack);
	Raster src(2, 2, kWhite);
	src.At(1, 1) = kRed;
	Composite(dst, src, 1, 1, &kWhite);
	CHECK(dst.At(1, 1) == kBlack && dst.At(2, 2) == kRed);

	Raster dst2(4, 4, kBlack);
	Composite(dst2, src, -1, -1);
	CHECK(dst2.At(0, 0) == kRed);
	// Clear pixels (outside a free-form selection) leave dst alone.
	Raster cut = src;
	cut.At(1, 1) = 0;
	Raster dst3(4, 4, kBlack);
	Composite(dst3, cut, 0, 0);
	CHECK(dst3.At(0, 0) == kWhite && dst3.At(1, 1) == kBlack);

	Raster c = src.Copy(Rect{1, 1, 1, 1});
	CHECK(c.Width() == 1 && c.At(0, 0) == kRed);
}


static void
TestPalettes()
{
	std::vector<Pixel> colors = {kRed, kBlack, MakePixel(1, 2, 3)};
	std::vector<Pixel> back;
	CHECK(ReadPalette(WritePalette(colors, "x.pal"), back) && back == colors);
	CHECK(ReadPalette(WritePalette(colors, "x.GPL"), back) && back == colors);
	std::string jasc = "JASC-PAL\r\n0100\r\n2\r\n255 0 0\r\n0 0 0\r\n";
	CHECK(ReadPalette(std::vector<uint8_t>(jasc.begin(), jasc.end()), back)
		&& back.size() == 2 && back[0] == kRed);
	std::string junk = "hello";
	CHECK(!ReadPalette(std::vector<uint8_t>(junk.begin(), junk.end()), back));
}


static void
TestSpray()
{
	std::mt19937 random(1);
	Raster r(30, 30, kWhite);
	for (int i = 0; i < 200; i++)
		Spray(r, 15, 15, 9, kBlack, random);
	// Every dot within the circle of diameter 9.
	for (int y = 0; y < 30; y++) {
		for (int x = 0; x < 30; x++) {
			if (r.At(x, y) == kBlack)
				CHECK(std::abs(x - 15) <= 4 && std::abs(y - 15) <= 4);
		}
	}
	CHECK(Count(r, kBlack) > 30);
}


int
main()
{
	TestBrushes();
	TestLines();
	TestShapes();
	TestPolygon();
	TestFill();
	TestTransforms();
	TestComposite();
	TestPalettes();
	TestSpray();
	if (sFailures == 0)
		printf("RasterTest: all passed\n");
	return sFailures == 0 ? 0 : 1;
}
