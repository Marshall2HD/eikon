#include "pch.h"
#include "TestFramework.h"

#include "Core/Bitmap.h"
#include "Core/Ruler.h"

using namespace Eikon::Core;
using Eikon::Core::Ruler::Run;

// Expected values follow (scout pseudo-code), (threshold table)
// (line, caps and label geometry) and (label text examples).

namespace
{
    constexpr std::uint8_t W = 255;

    struct Rgb { std::uint8_t r, g, b; };

    // A one-row image with the given colours.
    Bitmap Row(std::initializer_list<Rgb> colours)
    {
        Bitmap image = Bitmap::Create(static_cast<std::int32_t>(colours.size()), 1);
        std::size_t i = 0;
        for (Rgb const c : colours)
        {
            image.pixels[i * 4 + 0] = c.b;
            image.pixels[i * 4 + 1] = c.g;
            image.pixels[i * 4 + 2] = c.r;
            ++i;
        }
        return image;
    }

    Run ScoutRow(Bitmap const& image, std::int64_t x, std::int64_t persistence = 0, double sensitivity = Ruler::kDefaultSensitivity)
    {
        return Ruler::Scout(image, Rect{ 0, 0, static_cast<double>(image.width), 1 }, x, 0, false, persistence, sensitivity);
    }

    constexpr Rgb kWhite{ W, W, W };
    constexpr Rgb kBlack{ 0, 0, 0 };
    constexpr Rgb kGrey{ 230, 230, 230 };   // an anti-aliased edge pixel: 75 from white, 690 from black
}

TEST(Ruler_ThresholdTable)
{
    CHECK_EQ(Ruler::Threshold(0), std::int64_t{ 0 });
    CHECK_EQ(Ruler::Threshold(3.54), std::int64_t{ 25 });
    CHECK_EQ(Ruler::Threshold(5), std::int64_t{ 50 });
    CHECK_EQ(Ruler::Threshold(10), std::int64_t{ 200 });
    CHECK_EQ(Ruler::Threshold(20), std::int64_t{ 800 });
}

TEST(Ruler_SensitivityClampsToZeroAndTwenty)
{
    CHECK_NEAR(Ruler::AdjustSensitivity(3.54, 1), 4.54, 1e-12);
    CHECK_NEAR(Ruler::AdjustSensitivity(3.54, -0.25), 3.29, 1e-12);
    CHECK_EQ(Ruler::AdjustSensitivity(0.5, -1), 0.0);
    CHECK_EQ(Ruler::AdjustSensitivity(19.5, 1), 20.0);
    CHECK_EQ(Ruler::AdjustSensitivity(20, 3), 20.0);
}

TEST(RulerScout_StopsAtFirstEdgeOnEachSide)
{
    Bitmap const image = Row({ kWhite, kWhite, kWhite, kWhite, kWhite, kBlack, kBlack, kBlack, kBlack, kBlack });
    // Left reaches the image edge (0), right stops on the first black pixel (end is exclusive).
    CHECK_EQ(ScoutRow(image, 2), (Run{ 0, 5 }));
    // From inside the black run: start is one past the last white pixel, end the image width.
    CHECK_EQ(ScoutRow(image, 7), (Run{ 5, 10 }));
}

TEST(RulerScout_ThinLineIsAnEdge)
{
    Bitmap const image = Row({ kWhite, kWhite, kBlack, kWhite, kWhite, kWhite });
    // The pixel after the line jumps back by the same amount (765 < 2·765): a real edge.
    CHECK_EQ(ScoutRow(image, 0), (Run{ 0, 2 }));
    CHECK_EQ(ScoutRow(image, 4), (Run{ 3, 6 }));
}

TEST(RulerScout_SpikeRuleSkipsAntialiasedEdgePixel)
{
    // Right side: white -> grey (75) -> black (690 >= 2·75): the grey pixel is a spike, the edge is black.
    Bitmap const right = Row({ kWhite, kWhite, kWhite, kWhite, kGrey, kBlack, kBlack, kBlack });
    CHECK_EQ(ScoutRow(right, 1), (Run{ 0, 5 }));
    // Left side looks ahead in its own direction (i − 1): black, black, grey, white... -> start 2.
    Bitmap const left = Row({ kBlack, kBlack, kGrey, kWhite, kWhite, kWhite });
    CHECK_EQ(ScoutRow(left, 4), (Run{ 2, 6 }));
}

TEST(RulerScout_ThresholdIsStrict)
{
    // A step of exactly T = 25 is not an edge; 26 is.
    Bitmap const step25 = Row({ kWhite, kWhite, { 230, W, W }, { 230, W, W } });
    Bitmap const step26 = Row({ kWhite, kWhite, { 229, W, W }, { 229, W, W } });
    CHECK_EQ(ScoutRow(step25, 1), (Run{ 0, 4 }));
    CHECK_EQ(ScoutRow(step26, 1), (Run{ 0, 2 }));
    // Sensitivity 0 stops at any change; 20 (T = 800) never stops.
    Bitmap const tiny = Row({ kWhite, { 254, W, W }, kWhite, kWhite });
    CHECK_EQ(ScoutRow(tiny, 3, 0, 0.0), (Run{ 2, 4 }));
    Bitmap const strong = Row({ kBlack, kWhite, kWhite, kBlack });
    CHECK_EQ(ScoutRow(strong, 1, 0, 20.0), (Run{ 0, 4 }));
}

TEST(RulerScout_PersistenceSkipsOneEdgePerSide)
{
    Bitmap const image = Row({ kBlack, kWhite, kWhite, kWhite, kBlack, kWhite });
    CHECK_EQ(ScoutRow(image, 2, 0), (Run{ 1, 4 }));
    // Shift: each side skips its first edge with its own budget. Left: the edge at 0 is skipped and
    // the walk reaches the boundary; right: the black edge is skipped, the next one (back to white,
    // the last pixel, no look-ahead) stops it.
    CHECK_EQ(ScoutRow(image, 2, 1), (Run{ 0, 5 }));
}

TEST(RulerScout_VerticalWalksTopDownRows)
{
    // A one-column image: rows 0..5 top-down = W W B W W W.
    Bitmap image = Bitmap::Create(1, 6);
    for (int y = 0; y < 6; ++y)
    {
        std::uint8_t const v = y == 2 ? 0 : W;
        image.pixels[y * 4 + 0] = v;
        image.pixels[y * 4 + 1] = v;
        image.pixels[y * 4 + 2] = v;
    }
    Rect const crop{ 0, 0, 1, 6 };
    CHECK_EQ(Ruler::Scout(image, crop, 0, 4, true, 0, Ruler::kDefaultSensitivity), (Run{ 3, 6 }));
    // Measure takes a y-up cursor: y-up 1.5 is top-down row 6 − 1 − 1 = 4. The rect comes back in
    // y-up: rows [3, 6) -> y-up [0, 3).
    auto const m = Ruler::Measure(image, crop, { 0.2, 1.5 }, true, 0, false, Ruler::kDefaultSensitivity);
    CHECK(m.has_value());
    CHECK((m->rect == Rect{ 0, 0, 0, 3 }));
    CHECK_EQ(m->value, 3.0);
    // y-up 5.0 is row 0: rows [0, 2) -> y-up [4, 6).
    auto const top = Ruler::Measure(image, crop, { 0, 5.0 }, true, 0, false, Ruler::kDefaultSensitivity);
    CHECK((top->rect == Rect{ 0, 4, 0, 2 }));
}

TEST(RulerMeasure_HorizontalScansTheRowAtHeightMinusOneMinusY)
{
    // 6 × 2 image: the top row has an edge at x = 3, the bottom row is plain white.
    Bitmap image = Bitmap::Create(6, 2);
    for (int i = 0; i < 12; ++i)
    {
        bool const black = i >= 3 && i < 6;
        image.pixels[i * 4 + 0] = image.pixels[i * 4 + 1] = image.pixels[i * 4 + 2] = black ? 0 : W;
    }
    Rect const crop{ 0, 0, 6, 2 };
    auto const top = Ruler::Measure(image, crop, { 1.9, 1.99 }, false, 0, false, Ruler::kDefaultSensitivity);   // y-up 1 -> row 0
    CHECK((top->rect == Rect{ 0, 1, 3, 0 }));
    auto const bottom = Ruler::Measure(image, crop, { 1.9, 0.99 }, false, 0, false, Ruler::kDefaultSensitivity); // y-up 0 -> row 1
    CHECK((bottom->rect == Rect{ 0, 0, 6, 0 }));
    // trunc: -0.5 is still inside; -1, x = 6 and y-up 2 are outside.
    CHECK(Ruler::Measure(image, crop, { -0.5, 0.5 }, false, 0, false, 3.54).has_value());
    CHECK(!Ruler::Measure(image, crop, { -1.0, 0.5 }, false, 0, false, 3.54).has_value());
    CHECK(!Ruler::Measure(image, crop, { 6.0, 0.5 }, false, 0, false, 3.54).has_value());
    CHECK(!Ruler::Measure(image, crop, { 1.0, 2.0 }, false, 0, false, 3.54).has_value());
    // A guide is the full line with no scout.
    auto const guide = Ruler::Measure(image, crop, { 4.5, 1.2 }, false, 0, true, 3.54);
    CHECK((guide->rect == Rect{ 0, 1, 6, 0 }));
    auto const vguide = Ruler::Measure(image, crop, { 4.5, 1.2 }, true, 0, true, 3.54);
    CHECK((vguide->rect == Rect{ 4, 0, 0, 2 }));
}

TEST(RulerScout_RespectsCropOffset)
{
    // 8 × 1 image, crop (2, 0, 4, 1): pixels 2..5 are the crop; an edge outside the crop is never seen.
    Bitmap const image = Row({ kBlack, kBlack, kWhite, kWhite, kWhite, kWhite, kBlack, kBlack });
    CHECK_EQ(Ruler::Scout(image, Rect{ 2, 0, 4, 1 }, 1, 0, false, 0, 3.54), (Run{ 0, 4 }));
}

TEST(RulerLabel_TextUnits)
{
    CHECK_EQ(Ruler::LabelText(28, 2, true), std::wstring{ L"28px" });
    CHECK_EQ(Ruler::LabelText(28, 2, false), std::wstring{ L"14pt" });
    CHECK_EQ(Ruler::LabelText(27, 2, false), std::wstring{ L"13.5pt" });
    CHECK_EQ(Ruler::LabelText(7, 1.5, false), std::wstring{ L"4.666666666666667pt" });
    CHECK_EQ(Ruler::LabelText(3, 1.25, false), std::wstring{ L"2.4pt" });
    CHECK_EQ(Ruler::LabelText(5, 1, false), std::wstring{ L"5pt" });
    CHECK_EQ(Ruler::ObjectLabelText(28, 1), std::wstring{ L"28px" });
    CHECK_EQ(Ruler::ObjectLabelText(27, 2), std::wstring{ L"13.5pt" });
}

TEST(Ruler_ShortestDoubleDescription)
{
    CHECK_EQ(Ruler::ShortestDescription(21), std::wstring{ L"21.0" });
    CHECK_EQ(Ruler::ShortestDescription(106), std::wstring{ L"106.0" });
    CHECK_EQ(Ruler::ShortestDescription(4.5), std::wstring{ L"4.5" });
    CHECK_EQ(Ruler::ShortestDescription(1.04), std::wstring{ L"1.04" });
    CHECK_EQ(Ruler::ShortestDescription(0), std::wstring{ L"0.0" });
    CHECK_EQ(Ruler::ShortestDescription(-2.5), std::wstring{ L"-2.5" });
    CHECK_EQ(Ruler::ShortestDescription(0.1 + 0.2), std::wstring{ L"0.30000000000000004" });
    CHECK_EQ(Ruler::ShortestDescription(0.0001), std::wstring{ L"0.0001" });
    CHECK_EQ(Ruler::ShortestDescription(0.00001), std::wstring{ L"1e-05" });
    CHECK_EQ(Ruler::ShortestDescription(1e16), std::wstring{ L"1e+16" });
}

TEST(RulerLayout_HorizontalHighDpi)
{
    Ruler::ViewMap map{ 1.0, { 0, 0 }, 2.0, { 1000, 800 } };
    auto const layout = Ruler::LayoutLive({ 10, 20, 30, 0 }, false, false, { 22.7, 20.2 }, map, { 30, 13 });
    // Y = trunc(20 + 1·0.5)/2 − 0.5 = 9.5; X = 10/2; W = 30/2; H = max(1, 0).
    CHECK_EQ(layout.bars.size(), std::size_t{ 3 });
    CHECK((layout.bars[0] == Rect{ 5, 9.5, 15, 1 }));
    CHECK((layout.bars[1] == Rect{ 5, 6.5, 1, 7 }));
    CHECK((layout.bars[2] == Rect{ 19, 6.5, 1, 7 }));
    // Label at (cursor/dpr − 20, Y + 10); bubble (tw + 8, th + 4); text inset (4, 2).
    CHECK((layout.bubble == Rect{ -9, 19.5, 38, 17 }));
    CHECK((layout.text == Rect{ -5, 21.5, 30, 13 }));
}

TEST(RulerLayout_DprOneShiftsHalfAPoint)
{
    Ruler::ViewMap map{ 1.0, { 0, 0 }, 1.0, { 1000, 800 } };
    auto const layout = Ruler::LayoutLive({ 10, 20, 30, 0 }, false, false, { 22, 20 }, map, { 30, 13 });
    CHECK((layout.bars[0] == Rect{ 10, 19, 30, 1 }));
    CHECK((layout.bars[2] == Rect{ 39, 16, 1, 7 }));
}

TEST(RulerLayout_LabelFlipsNearTopEdge)
{
    // Y = 9.5 at dpr 2: the label flips when area.height/2 <= Y + 30 = 39.5.
    Ruler::ViewMap flips{ 1.0, { 0, 0 }, 2.0, { 1000, 79 } };
    Ruler::ViewMap stays{ 1.0, { 0, 0 }, 2.0, { 1000, 80 } };
    CHECK_EQ(Ruler::LayoutLive({ 10, 20, 30, 0 }, false, false, { 22, 20 }, flips, { 30, 13 }).bubble->y, -20.5);
    CHECK_EQ(Ruler::LayoutLive({ 10, 20, 30, 0 }, false, false, { 22, 20 }, stays, { 30, 13 }).bubble->y, 19.5);
}

TEST(RulerLayout_VerticalZoomedAndFlipsLeft)
{
    Ruler::ViewMap map{ 4.0, { 100, 50 }, 2.0, { 1000, 800 } };
    auto const layout = Ruler::LayoutLive({ 5, 3, 0, 10 }, true, false, { 5.9, 7.2 }, map, { 30, 13 });
    // View origin (120, 62), h = 40: X = trunc(120 + 2)/2 − 0.5 = 60.5, Y = 31, H = 20.
    CHECK((layout.bars[0] == Rect{ 60.5, 31, 1, 20 }));
    CHECK((layout.bars[1] == Rect{ 57.5, 31, 7, 1 }));
    CHECK((layout.bars[2] == Rect{ 57.5, 50, 7, 1 }));
    // Label (X + 20, cursor pixel (5, 7) -> view y 78 -> 78/2 − 10).
    CHECK((layout.bubble == Rect{ 80.5, 29, 38, 17 }));
    // Near the right edge (area.width/2 <= X + 20 + 40 = 120.5) it goes to X − 60.
    Ruler::ViewMap narrow{ 4.0, { 100, 50 }, 2.0, { 241, 800 } };
    Ruler::ViewMap wide{ 4.0, { 100, 50 }, 2.0, { 242, 800 } };
    CHECK_EQ(Ruler::LayoutLive({ 5, 3, 0, 10 }, true, false, { 5.9, 7.2 }, narrow, { 30, 13 }).bubble->x, 0.5);
    CHECK_EQ(Ruler::LayoutLive({ 5, 3, 0, 10 }, true, false, { 5.9, 7.2 }, wide, { 30, 13 }).bubble->x, 80.5);
}

TEST(RulerLayout_GuideHasOnlyTheLine)
{
    Ruler::ViewMap map{ 1.0, { 0, 0 }, 2.0, { 1000, 800 } };
    auto const layout = Ruler::LayoutLive({ 0, 20, 300, 0 }, false, true, { 22, 20 }, map, { 30, 13 });
    CHECK_EQ(layout.bars.size(), std::size_t{ 1 });
    CHECK(!layout.bubble.has_value());
}
