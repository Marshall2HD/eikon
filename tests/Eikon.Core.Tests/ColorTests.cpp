#include "pch.h"
#include "TestFramework.h"

#include "Core/Bitmap.h"
#include "Core/Color.h"

#include <cmath>
#include <numbers>

using namespace Eikon::Core;

// Expected values: the format table and examples (#FFC200, white, black,
// #FF0C01), Björn Ottosson's Oklab reference (https://bottosson.github.io/posts/oklab/: the XYZ
// table and linear_srgb_to_oklab), and the sampling rules of /.

namespace
{
    void SetPixel(Bitmap& image, int x, int y, Color c)
    {
        auto* p = image.pixels.data() + (static_cast<std::size_t>(y) * image.width + x) * 4;
        p[0] = c.b;
        p[1] = c.g;
        p[2] = c.r;
        p[3] = 255;
    }

    // Ottosson's reference linear_srgb_to_oklab (2021-01-25 matrices), in double.
    Oklab OttossonOklab(Color c)
    {
        double const r = SrgbToLinear(c.r / 255.0), g = SrgbToLinear(c.g / 255.0), b = SrgbToLinear(c.b / 255.0);
        double const l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
        double const m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
        double const s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
        return { 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s };
    }

    constexpr Color kAmber{ 0xFF, 0xC2, 0x00, 255 };
}

TEST(ColorFormats_TableOrderKeysTitlesAndWidths)
{
    auto const formats = ColorFormats();
    CHECK_EQ(formats.size(), std::size_t{ 8 });
    std::wstring_view const keys[] = { L"HEX", L"HEXno", L"RGBA", L"RGB1", L"Android", L"UIColor", L"CSharp", L"OKLCH" };
    std::wstring_view const titles[] = { L"HEX #FFC200", L"HEX FFC200", L"CSS rgba(255, 194, 0, 1)", L"RGB (1.00, 0.76, 0.00)",
                                         L"Android 0xFFFFC200", L"UIColor (red: 1.00, gre...", L"C# Color.fromArgb()", L"OKLCH" };
    double const widths[] = { 102, 102, 112, 120, 112, 120, 112, 112 };
    for (std::size_t i = 0; i < 8; ++i)
    {
        CHECK(formats[i].key == keys[i]);
        CHECK(formats[i].menuTitle == titles[i]);
        CHECK_EQ(formats[i].labelWidth, widths[i]);
    }
    CHECK_EQ(ColorFormatIndex(L"OKLCH"), std::size_t{ 7 });
    CHECK_EQ(ColorFormatIndex(L"CSharp"), std::size_t{ 6 });
    CHECK_EQ(ColorFormatIndex(L"hex"), std::size_t{ 0 });     // keys are case-sensitive: unknown -> HEX
    CHECK_EQ(ColorFormatIndex(L""), std::size_t{ 0 });
}

TEST(ColorFormats_SpecExamplesForFFC200)
{
    struct Case { std::wstring_view key, display, copy; };
    Case const cases[] = {
        { L"HEX", L"#FFC200", L"#FFC200" },
        { L"HEXno", L"FFC200", L"FFC200" },
        { L"RGBA", L"255, 194, 0", L"rgba(255, 194, 0, 1)" },
        { L"RGB1", L"1.00, 0.76, 0.00", L"(1.00, 0.76, 0.00)" },
        { L"Android", L"0xFFFFC200", L"0xFFFFC200" },
        { L"UIColor", L"1.00, 0.76, 0.00", L"UIColor(red: 1.00, green: 0.76, blue: 0.00, alpha: 1.00)" },
        { L"CSharp", L"255, 194, 0", L"Color.fromArgb(255, 194, 0);" },
        { L"OKLCH", L"L85 C17 H86", L"oklch(84.60% 0.17 85.57)" },
    };
    for (auto const& c : cases)
    {
        FormattedColor const f = FormatColor(kAmber, c.key);
        CHECK_EQ(f.display, std::wstring{ c.display });
        CHECK_EQ(f.copy, std::wstring{ c.copy });
    }
    // An unknown stored format falls back to HEX.
    CHECK_EQ(FormatColor(kAmber, L"Pantone").copy, std::wstring{ L"#FFC200" });
}

TEST(ColorFormats_HexIsUpperCaseAndPadded_DecimalsAreNot)
{
    Color const c{ 0x0A, 0x05, 0xAB, 255 };
    CHECK_EQ(FormatColor(c, L"HEX").copy, std::wstring{ L"#0A05AB" });
    CHECK_EQ(FormatColor(c, L"Android").copy, std::wstring{ L"0xFF0A05AB" });
    CHECK_EQ(FormatColor(c, L"RGBA").copy, std::wstring{ L"rgba(10, 5, 171, 1)" });
    // 10/255 = 0.0392 -> 0.04, 5/255 = 0.0196 -> 0.02, 171/255 = 0.6706 -> 0.67.
    CHECK_EQ(FormatColor(c, L"RGB1").copy, std::wstring{ L"(0.04, 0.02, 0.67)" });
}

TEST(ColorFormats_OklchSpecChecks)
{
    CHECK_EQ(FormatColor({ 255, 255, 255, 255 }, L"OKLCH").display, std::wstring{ L"L100 C0 H0" });
    CHECK_EQ(FormatColor({ 255, 255, 255, 255 }, L"OKLCH").copy, std::wstring{ L"oklch(100.00% 0.00 0.00)" });
    CHECK_EQ(FormatColor({ 0, 0, 0, 255 }, L"OKLCH").display, std::wstring{ L"L0 C0 H0" });
    CHECK_EQ(FormatColor({ 0, 0, 0, 255 }, L"OKLCH").copy, std::wstring{ L"oklch(0.00% 0.00 0.00)" });
    CHECK_EQ(FormatColor({ 0xFF, 0x0C, 0x01, 255 }, L"OKLCH").display, std::wstring{ L"L63 C26 H30" });
    CHECK_EQ(FormatColor({ 0xFF, 0x0C, 0x01, 255 }, L"OKLCH").copy, std::wstring{ L"oklch(63.01% 0.26 29.55)" });
}

TEST(Oklch_MatchesOttossonXyzTable)
{
    // Table "example XYZ and Oklab pairs", rounded to three decimals.
    struct Row { double x, y, z, l, a, b; };
    Row const rows[] = {
        { 0.950, 1.000, 1.089, 1.000, 0.000, 0.000 },
        { 1.000, 0.000, 0.000, 0.450, 1.236, -0.019 },
        { 0.000, 1.000, 0.000, 0.922, -0.671, 0.263 },
        { 0.000, 0.000, 1.000, 0.153, -1.415, -0.449 },
    };
    for (auto const& r : rows)
    {
        Oklab const lab = XyzToOklab(r.x, r.y, r.z);
        CHECK_NEAR(lab.l, r.l, 0.0006);
        CHECK_NEAR(lab.a, r.a, 0.0006);
        CHECK_NEAR(lab.b, r.b, 0.0006);
    }
}

TEST(Oklch_MatchesOttossonReferenceImplementation)
{
    Color const colors[] = { kAmber, { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 }, { 0x77, 0x58, 0xEF, 255 },
                             { 12, 200, 99, 255 }, { 3, 3, 3, 255 }, { 250, 251, 252, 255 } };
    for (Color const c : colors)
    {
        Oklab const ref = OttossonOklab(c);
        Oklch const lch = ToOklch(c);
        double const refC = std::sqrt(ref.a * ref.a + ref.b * ref.b);
        CHECK_NEAR(lch.l, ref.l, 1e-6);
        CHECK_NEAR(lch.c, refC, 1e-6);
        if (refC > 0.001)
        {
            double const refH = std::fmod(std::atan2(ref.b, ref.a) * 180.0 / std::numbers::pi + 360.0, 360.0);
            CHECK_NEAR(lch.h, refH, 1e-3);
        }
    }
    // CSS Color 4 / Ottosson: sRGB red is oklch(62.796% 0.25768 29.2339).
    Oklch const red = ToOklch({ 255, 0, 0, 255 });
    CHECK_NEAR(red.l, 0.62796, 0.00001);
    CHECK_NEAR(red.c, 0.25768, 0.00001);
    CHECK_NEAR(red.h, 29.2339, 0.0001);
}

TEST(Oklch_HueIsZeroForNeutralsAndWrapsIntoRange)
{
    // Greys have |a|, |b| below 0.0002, so H is 0 rather than atan2 noise.
    CHECK_EQ(ToOklch({ 128, 128, 128, 255 }).h, 0.0);
    CHECK_EQ(FormatColor({ 128, 128, 128, 255 }, L"OKLCH").display, std::wstring{ L"L60 C0 H0" });
    // Blue has a negative atan2; fmod(… + 360, 360) brings it to about 264°.
    double const blue = ToOklch({ 0, 0, 255, 255 }).h;
    CHECK(blue > 263.0 && blue < 265.0);
}

TEST(SrgbToLinear_PiecewiseBoundaryAndSign)
{
    CHECK_NEAR(SrgbToLinear(0.04045), 0.04045 / 12.92, 1e-15);   // <= 0.04045 is the linear part
    CHECK_NEAR(SrgbToLinear(0.5), std::pow((0.5 + 0.055) / 1.055, 2.4), 1e-15);
    CHECK_NEAR(SrgbToLinear(-0.5), -std::pow((0.5 + 0.055) / 1.055, 2.4), 1e-15);
    CHECK_EQ(SrgbToLinear(1.0), 1.0);
}

TEST(PickPixel_ReadsRowHeightMinusYAndOutsideIsBlack)
{
    // 3 × 4 image, each top-down row its own colour.
    Bitmap image = Bitmap::Create(3, 4);
    Color const rows[] = { { 10, 0, 0, 255 }, { 20, 0, 0, 255 }, { 30, 0, 0, 255 }, { 40, 0, 0, 255 } };
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            SetPixel(image, x, y, rows[y]);
        }
    }
    SetPixel(image, 2, 1, { 99, 1, 2, 255 });
    Rect const crop{ 0, 0, 3, 4 };
    // y-up 4.0 is the top edge: row 4 − 4 = 0. y-up 3.9 truncates to 3: row 1 (the ruler would read row 0).
    CHECK((PickPixel(image, crop, { 0, 4.0 }) == rows[0]));
    CHECK((PickPixel(image, crop, { 0, 3.9 }) == rows[1]));
    CHECK((PickPixel(image, crop, { 2.7, 3.0 }) == Color{ 99, 1, 2, 255 }));
    CHECK((PickPixel(image, crop, { 0, 1.0 }) == rows[3]));
    // y-up below 1 gives row 4, outside: black, like the bottom row.
    CHECK((PickPixel(image, crop, { 0, 0.5 }) == Color{ 0, 0, 0, 255 }));
    CHECK((PickPixel(image, crop, { 3.0, 2.0 }) == Color{ 0, 0, 0, 255 }));
    CHECK((PickPixel(image, crop, { -0.5, 2.0 }) == rows[2]));   // trunc(-0.5) = 0 is inside
    CHECK((PickPixel(image, crop, { -1.0, 2.0 }) == Color{ 0, 0, 0, 255 }));

    // A crop (1, 1, 2, 3): crop-relative (1, 3.0) is buffer (2, 1).
    CHECK((PickPixel(image, Rect{ 1, 1, 2, 3 }, { 1, 3.0 }) == Color{ 99, 1, 2, 255 }));
    CHECK((PickPixel(image, Rect{ 1, 1, 2, 3 }, { 2, 3.0 }) == Color{ 0, 0, 0, 255 }));
}

TEST(SampleArea_AverageTruncatesAndDarkestKeepsFirstOfTies)
{
    Bitmap image = Bitmap::Create(4, 4);
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            SetPixel(image, x, y, { 200, 200, 200, 255 });
        }
    }
    // Two equally dark pixels (sum 30): top-down (1, 0) and (1, 3). Visiting goes x outer and y-up
    // inner, so the bottom row (top-down 3) comes first and wins.
    SetPixel(image, 1, 0, { 10, 10, 10, 255 });
    SetPixel(image, 1, 3, { 0, 0, 30, 255 });
    Rect const crop{ 0, 0, 4, 4 };
    CHECK((SampleArea(image, crop, { 0, 0, 4, 4 }, SampleMode::Darkest) == Color{ 0, 0, 30, 255 }));
    // Average of 14 × 200 + (10,10,10) + (0,0,30) over 16: R = 2810/16 = 175.6 -> 175, B = 2840/16 = 177.5 -> 177.
    CHECK((SampleArea(image, crop, { 0, 0, 4, 4 }, SampleMode::Average) == Color{ 175, 175, 177, 255 }));
    // Negative sizes are normalised; parts outside the crop are clamped away.
    CHECK((SampleArea(image, crop, { 4, 4, -4, -4 }, SampleMode::Average) == Color{ 175, 175, 177, 255 }));
    CHECK((SampleArea(image, crop, { -10, -10, 20, 20 }, SampleMode::Darkest) == Color{ 0, 0, 30, 255 }));
    // y-up rect (0, 3, 4, 1) is the top row only.
    CHECK((SampleArea(image, crop, { 0, 3, 4, 1 }, SampleMode::Darkest) == Color{ 10, 10, 10, 255 }));
    // Empty and fully outside rects give black.
    CHECK((SampleArea(image, crop, { 0, 0, 0.9, 4 }, SampleMode::Average) == Color{ 0, 0, 0, 255 }));
    CHECK((SampleArea(image, crop, { 50, 50, 5, 5 }, SampleMode::Darkest) == Color{ 0, 0, 0, 255 }));
}

TEST(DarkestAround_UsesTwentyPixelSquare)
{
    Bitmap image = Bitmap::Create(40, 40);
    for (int y = 0; y < 40; ++y)
    {
        for (int x = 0; x < 40; ++x)
        {
            SetPixel(image, x, y, { 255, 255, 255, 255 });
        }
    }
    Rect const crop{ 0, 0, 40, 40 };
    // Cursor at y-up (20, 20): the square covers x 10..29 and y-up 10..29 = top-down rows 10..29.
    SetPixel(image, 29, 10, { 50, 50, 50, 255 });   // inside (last column, top row of the square)
    SetPixel(image, 30, 20, { 0, 0, 0, 255 });      // just outside to the right
    CHECK((DarkestAround(image, crop, { 20.5, 20.5 }) == Color{ 50, 50, 50, 255 }));
    SetPixel(image, 15, 9, { 1, 1, 1, 255 });       // top-down row 9 = y-up 30: outside
    CHECK((DarkestAround(image, crop, { 20.5, 20.5 }) == Color{ 50, 50, 50, 255 }));
}

// ColorMagic: expected lists worked out by
// hand from those rules.

TEST(DominantColors_ThresholdMergeMaxColorsAndMinFrequency)
{
    Color const red{ 255, 0, 0, 255 }, nearRed{ 250, 5, 5, 255 }, blue{ 0, 0, 255, 255 }, darkBlue{ 0, 0, 200, 255 };
    Color const green{ 0, 255, 0, 255 }, green2{ 0, 254, 0, 255 };
    // totalPixels 100000 -> keep counts > 10. nearRed is 15/255 = 0.059 from red: merged (+1).
    // darkBlue is 55/255 = 0.216 from blue: kept. green (10) is dropped by the threshold.
    std::vector<ColorCount> counts{ { green, 10 }, { darkBlue, 30 }, { nearRed, 50 }, { red, 100 }, { blue, 80 }, { green2, 11 } };
    auto all = DominantColors(counts, 100000, 8, 0);
    CHECK_EQ(all.size(), std::size_t{ 4 });
    CHECK((all[0] == red));
    CHECK((all[1] == blue));
    CHECK((all[2] == darkBlue));
    CHECK((all[3] == green2));
    // maxColors 3 cuts green2; minFrequency 30 then drops darkBlue (30 is not > 30).
    auto top = DominantColors(counts, 100000, 3, 30);
    CHECK_EQ(top.size(), std::size_t{ 2 });
    CHECK((top[0] == red));
    CHECK((top[1] == blue));
}

TEST(DominantColors_FirstClosestWinsAndMergesResort)
{
    // x and y are 52/255 = 0.204 apart, so both stay; z is exactly 26/255 from each (the same two
    // terms) and joins the first kept one, y (sorted first by its larger count): y 50 + 1, x 40.
    Color const x{ 26, 0, 0, 255 }, y{ 0, 26, 0, 255 }, z{ 13, 13, 0, 255 };
    auto colors = DominantColors({ { x, 40 }, { y, 50 }, { z, 30 } }, 0, 8, 50);
    CHECK_EQ(colors.size(), std::size_t{ 1 });   // only y reaches 51 > 50
    CHECK((colors[0] == y));

    // b starts below a, but two near-b colours merge into it: a 100, b 99 + 2 = 101 -> b first.
    Color const a{ 0, 0, 255, 255 }, b{ 255, 0, 0, 255 }, b1{ 254, 0, 0, 255 }, b2{ 253, 0, 0, 255 };
    auto sorted = DominantColors({ { a, 100 }, { b, 99 }, { b1, 20 }, { b2, 19 } }, 0, 8, 0);
    CHECK_EQ(sorted.size(), std::size_t{ 2 });
    CHECK((sorted[0] == b));
    CHECK((sorted[1] == a));
}

TEST(DominantColors_OnlyTheFirst500CandidatesCount)
{
    // a (count 1000) and 599 near-identical colours of count 1: the 499 that make the top 500 each add
    // 1 to a, the other 100 are cut before the merge. a ends at 1499.
    Color const a{ 128, 128, 128, 255 };
    std::vector<ColorCount> counts{ { a, 1000 } };
    for (int i = 0; i < 600; ++i)
    {
        Color const c{ static_cast<std::uint8_t>(118 + i % 20), static_cast<std::uint8_t>(113 + i / 20), 128, 255 };
        if (!(c == a))
        {
            counts.push_back({ c, 1 });
        }
    }
    CHECK_EQ(counts.size(), std::size_t{ 600 });
    CHECK_EQ(DominantColors(counts, 0, 8, 1498).size(), std::size_t{ 1 });
    CHECK_EQ(DominantColors(counts, 0, 8, 1499).size(), std::size_t{ 0 });
}

TEST(TopColors_PixelFilterOnTheFifthScaleImage)
{
    // 50 x 50 -> 10 x 10 after the 0.2 scale; every 5 x 5 block is one colour, so the scaled pixels are
    // exact. 100 scaled pixels: the frequency threshold is trunc(0.01) = 0.
    Bitmap image = Bitmap::Create(50, 50);
    auto block = [&](int bx, int by, Color c, std::uint8_t alpha = 255) {
        for (int y = by * 5; y < by * 5 + 5; ++y)
        {
            for (int x = bx * 5; x < bx * 5 + 5; ++x)
            {
                auto* p = image.pixels.data() + (static_cast<std::size_t>(y) * 50 + x) * 4;
                p[0] = c.b;
                p[1] = c.g;
                p[2] = c.r;
                p[3] = alpha;
            }
        }
    };
    for (int by = 0; by < 10; ++by)
    {
        for (int bx = 0; bx < 10; ++bx)
        {
            Color c{ 200, 30, 30, 255 };   // rows 0-4: 50 x red
            std::uint8_t alpha = 255;
            if (by == 5) c = bx < 5 ? Color{ 128, 128, 128, 255 } : Color{ 100, 100, 110, 255 };   // max - min <= 10: out
            if (by == 6) c = bx < 5 ? Color{ 40, 5, 3, 255 } : Color{ 40, 6, 3, 255 };             // sum 48 out, 49 in
            if (by == 7) c = Color{ 255, 255, 240, 255 };                                         // sum 750: out
            if (by == 8) c = Color{ 255, 255, 239, 255 };                                         // sum 749: in
            if (by == 9) { c = bx < 5 ? Color{ 118, 18, 18, 255 } : Color{ 119, 18, 18, 255 }; alpha = bx < 5 ? 150 : 151; }   // alpha > 150
            block(bx, by, c, alpha);
        }
    }
    auto colors = TopColors(image, 8, 0);
    CHECK_EQ(colors.size(), std::size_t{ 4 });
    CHECK((colors[0] == Color{ 200, 30, 30, 255 }));    // 50
    CHECK((colors[1] == Color{ 255, 255, 239, 255 }));  // 10
    CHECK((colors[2] == Color{ 40, 6, 3, 255 }));       // 5
    CHECK((colors[3] == Color{ 119, 18, 18, 255 }));    // 5 (0.41 from red: kept apart; ties keep raster order)
    // A minFrequency of 400 leaves nothing here: the default string.
    auto const strings = TopColorStrings(image);
    CHECK_EQ(strings.size(), std::size_t{ 1 });
    CHECK(strings[0] == L"#000000");
}

TEST(TopColors_StringsAndScaledSize)
{
    Bitmap image = Bitmap::Create(500, 500);
    for (int y = 0; y < 500; ++y)
    {
        for (int x = 0; x < 500; ++x)
        {
            SetPixel(image, x, y, kAmber);
        }
    }
    auto const strings = TopColorStrings(image);   // 100 x 100 = 10000 pixels > 400
    CHECK_EQ(strings.size(), std::size_t{ 1 });
    CHECK(strings[0] == L"#FFC200");

    CHECK(ScaleImage(Bitmap::Create(7, 3), 0.2).IsEmpty());   // 1 x 0: context fails
    Bitmap const scaled = ScaleImage(Bitmap::Create(10, 5), 0.2);
    CHECK_EQ(scaled.width, 2);
    CHECK_EQ(scaled.height, 1);
}
