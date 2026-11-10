#include "pch.h"
#include "TestFramework.h"

#include "Core/Capture.h"
#include "Core/CutOut.h"

#include <cmath>
#include <cstring>

using namespace Eikon::Core;

namespace
{
    // A fully transparent W×H image.
    Bitmap Transparent(int width, int height)
    {
        Bitmap image;
        image.width = width;
        image.height = height;
        image.pixels.assign(image.Stride() * static_cast<std::size_t>(height), 0);
        return image;
    }

    void Put(Bitmap& image, int x, int y, std::uint8_t b, std::uint8_t g, std::uint8_t r, std::uint8_t a)
    {
        std::uint8_t* p = image.pixels.data() + static_cast<std::size_t>(y) * image.Stride() + static_cast<std::size_t>(x) * 4;
        p[0] = b;
        p[1] = g;
        p[2] = r;
        p[3] = a;
    }

    void FillRect(Bitmap& image, int x0, int y0, int x1, int y1, std::uint8_t b, std::uint8_t g, std::uint8_t r, std::uint8_t a)
    {
        for (int y = y0; y <= y1; ++y)
        {
            for (int x = x0; x <= x1; ++x)
            {
                Put(image, x, y, b, g, r, a);
            }
        }
    }

    std::uint32_t Pixel(Bitmap const& image, int x, int y)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, image.pixels.data() + static_cast<std::size_t>(y) * image.Stride() + static_cast<std::size_t>(x) * 4, 4);
        return value;
    }

    constexpr std::uint32_t Bgra(std::uint8_t b, std::uint8_t g, std::uint8_t r, std::uint8_t a)
    {
        return b | (g << 8) | (r << 16) | (static_cast<std::uint32_t>(a) << 24);
    }

    // A 400×300 "window with shadow": opaque body x 20..369, y 10..259, so the margins differ on
    // every side (left 20, right 30, top 10, bottom 40 transparent pixels).
    Bitmap ShadowedWindow()
    {
        Bitmap image = Transparent(400, 300);
        FillRect(image, 20, 10, 369, 259, 0x10, 0x20, 0x30, 0xFF);
        return image;
    }
}

TEST(Capture_ParseShadowModeMatchesTheCaseTable)
{
    CHECK((ParseShadowMode(L"transparent") == ShadowMode::Transparent));
    CHECK((ParseShadowMode(L"trimmed") == ShadowMode::Trimmed));
    CHECK((ParseShadowMode(L"wallpaper") == ShadowMode::Wallpaper));
    CHECK((ParseShadowMode(L"solid") == ShadowMode::Solid));
    CHECK((!ParseShadowMode(L"Wallpaper").has_value()));   // case-sensitive
    CHECK((!ParseShadowMode(L"").has_value()));
    CHECK_EQ(static_cast<int>(ShadowMode::Solid), 3);
}

TEST(Capture_ShadowPaddingsAreFirstOpaqueIndexMinusOne)
{
    // First opaque pixel at index 20 from the left -> 19; 30 from the right -> 29; 10 from the top
    // -> 9; 40 from the bottom -> 39.
    CHECK((ComputeShadowPaddings(ShadowedWindow()) == ShadowPaddings{ 9, 29, 39, 19 }));
}

TEST(Capture_ShadowPaddingsThresholdIsAbove150)
{
    Bitmap image = ShadowedWindow();
    // Alpha 150 on the middle row does not count; 151 does.
    int const row = 150;
    Put(image, 5, row, 0, 0, 0, 150);
    CHECK_EQ(ComputeShadowPaddings(image).left, 19);
    Put(image, 6, row, 0, 0, 0, 151);
    CHECK_EQ(ComputeShadowPaddings(image).left, 5);
}

TEST(Capture_ShadowPaddingsSmallIndicesFloorAtZero)
{
    Bitmap image = Transparent(400, 300);
    FillRect(image, 0, 0, 399, 299, 0, 0, 0, 0xFF);
    CHECK((ComputeShadowPaddings(image) == ShadowPaddings{ 0, 0, 0, 0 }));
    // Opaque from index 1: max(1, 1) - 1 = 0. From index 2: 1.
    Bitmap one = Transparent(400, 300);
    FillRect(one, 1, 0, 399, 299, 0, 0, 0, 0xFF);
    CHECK_EQ(ComputeShadowPaddings(one).left, 0);
    Bitmap two = Transparent(400, 300);
    FillRect(two, 2, 0, 399, 299, 0, 0, 0, 0xFF);
    CHECK_EQ(ComputeShadowPaddings(two).left, 1);
}

TEST(Capture_ShadowPaddingsGiveUpAfter300Pixels)
{
    // Opaque only from x = 300 on: index 300 is beyond the 300 scanned pixels -> 0.
    Bitmap image = Transparent(700, 100);
    FillRect(image, 300, 0, 699, 99, 0, 0, 0, 0xFF);
    CHECK_EQ(ComputeShadowPaddings(image).left, 0);
    // Index 299 is the last one scanned -> 298.
    Bitmap edge = Transparent(700, 100);
    FillRect(edge, 299, 0, 699, 99, 0, 0, 0, 0xFF);
    CHECK_EQ(ComputeShadowPaddings(edge).left, 298);
}

TEST(Capture_ShadowPaddingsTopUsesTheCentreOfTheOpaqueSpan)
{
    // The column is left + (W - left - right) / 2 = 19 + (400 - 19 - 29) / 2 = 195, not W / 2 = 200.
    Bitmap image = ShadowedWindow();
    FillRect(image, 195, 10, 195, 49, 0, 0, 0, 0);   // a notch down column 195 only
    auto const p = ComputeShadowPaddings(image);
    CHECK_EQ(p.top, 49);
    CHECK_EQ(p.bottom, 39);
}

TEST(Capture_TrimShadowCropsThePaddings)
{
    Bitmap const trimmed = TrimShadow(ShadowedWindow());
    // (19, 9, 400 - 19 - 29, 300 - 9 - 39): one transparent pixel stays on each side.
    CHECK_EQ(trimmed.width, 352);
    CHECK_EQ(trimmed.height, 252);
    CHECK_EQ(Pixel(trimmed, 0, 0), 0u);
    CHECK_EQ(Pixel(trimmed, 1, 1), Bgra(0x10, 0x20, 0x30, 0xFF));
    // An opaque image stays as it is.
    Bitmap opaque = Bitmap::Create(50, 40);
    CHECK_EQ(TrimShadow(opaque).width, 50);
    CHECK_EQ(TrimShadow(opaque).height, 40);
}

TEST(Capture_IsEdgeTransparentChecksMidTopAndMidBottom)
{
    Bitmap image = Bitmap::Create(101, 50);
    CHECK((!IsEdgeTransparent(image)));
    Put(image, 50, 49, 0, 0, 0, 254);   // (W/2, H-1) with W/2 = 50
    CHECK((IsEdgeTransparent(image)));
    Bitmap corners = Bitmap::Create(101, 50);
    Put(corners, 0, 0, 0, 0, 0, 0);     // a transparent corner does not count
    Put(corners, 51, 0, 0, 0, 0, 0);
    CHECK((!IsEdgeTransparent(corners)));
}

TEST(Capture_AddWallpaperLeavesOpaqueEdgesAlone)
{
    Bitmap image = Bitmap::Create(40, 30);
    Bitmap wallpaper = Bitmap::Create(100, 100);
    FillRect(wallpaper, 0, 0, 99, 99, 9, 9, 9, 0xFF);
    Bitmap const out = AddWallpaper(image, wallpaper);
    CHECK_EQ(Pixel(out, 0, 0), Bgra(0, 0, 0, 0xFF));
}

TEST(Capture_AddWallpaperUsesTheCentredCropAndRecentresTheWindow)
{
    Bitmap const window = ShadowedWindow();
    // Wallpaper 600×500 whose pixel encodes its coordinates (b = x & 0xFF, g = y & 0xFF).
    Bitmap wallpaper = Transparent(600, 500);
    for (int y = 0; y < 500; ++y)
    {
        for (int x = 0; x < 600; ++x)
        {
            Put(wallpaper, x, y, static_cast<std::uint8_t>(x & 0xFF), static_cast<std::uint8_t>(y & 0xFF), 0x77, 0xFF);
        }
    }
    Bitmap const out = AddWallpaper(window, wallpaper);
    CHECK_EQ(out.width, 400);
    CHECK_EQ(out.height, 300);
    // Crop origin ((600 - 400) / 2, (500 - 300) / 2) = (100, 100).
    CHECK_EQ(Pixel(out, 0, 0), Bgra(100, 100, 0x77, 0xFF));
    CHECK_EQ(Pixel(out, 5, 3), Bgra(105, 103, 0x77, 0xFF));
    // Paddings top 9, bottom 39: the window moves down by Int((39 - 9) / 2) = 15, so its first
    // opaque row is 10 + 15 = 25 and row 24 still shows wallpaper.
    CHECK_EQ(Pixel(out, 200, 25), Bgra(0x10, 0x20, 0x30, 0xFF));
    CHECK_EQ(Pixel(out, 200, 24), Bgra(static_cast<std::uint8_t>((100 + 200) & 0xFF), 124, 0x77, 0xFF));
}

TEST(Capture_AddColorFillsAndShiftsEvenWhenOpaque)
{
    Bitmap const out = AddColor(ShadowedWindow(), Color{ 0x40, 0x44, 0x48, 0xFF });   // #404448
    CHECK_EQ(Pixel(out, 0, 0), Bgra(0x48, 0x44, 0x40, 0xFF));
    CHECK_EQ(Pixel(out, 200, 24), Bgra(0x48, 0x44, 0x40, 0xFF));
    CHECK_EQ(Pixel(out, 200, 25), Bgra(0x10, 0x20, 0x30, 0xFF));
    // The excess bottom shadow is clipped: the last body row 259 lands at 274 (< 300).
    CHECK_EQ(Pixel(out, 200, 274), Bgra(0x10, 0x20, 0x30, 0xFF));
    CHECK_EQ(Pixel(out, 200, 275), Bgra(0x48, 0x44, 0x40, 0xFF));
}

TEST(Capture_CropBitmapMovesTheScreenRect)
{
    Bitmap image = Bitmap::Create(100, 80, 1.5);
    image.screenRect = Rect{ 1920, 0, 100, 80 };
    Put(image, 30, 20, 1, 2, 3, 0xFF);
    Bitmap const crop = CropBitmap(image, 30, 20, 200, 10);   // clamped to the image
    CHECK_EQ(crop.width, 70);
    CHECK_EQ(crop.height, 10);
    CHECK_EQ(crop.dpr, 1.5);
    CHECK((crop.screenRect == Rect{ 1950, 20, 70, 10 }));
    CHECK_EQ(Pixel(crop, 0, 0), Bgra(1, 2, 3, 0xFF));
}

TEST(Capture_SelectorCutoutSpansPressToCursor)
{
    Size const screen{ 1920, 1080 };
    // Down-right and up-left drags give the same rectangle between the two points.
    CHECK((SelectorCutout({ 100, 200 }, { 150, 260 }, screen) == Rect{ 100, 200, 50, 60 }));
    CHECK((SelectorCutout({ 150, 260 }, { 100, 200 }, screen) == Rect{ 100, 200, 50, 60 }));
    // Dragging right onto the last column reaches the edge; dragging left from it does not add.
    CHECK((SelectorCutout({ 1800, 10 }, { 1919, 20 }, screen) == Rect{ 1800, 10, 120, 10 }));
    CHECK((SelectorCutout({ 1919, 10 }, { 1800, 20 }, screen) == Rect{ 1800, 10, 119, 10 }));
    CHECK((SelectorCutout({ 10, 1000 }, { 20, 1079 }, screen) == Rect{ 10, 1000, 10, 80 }));
    // Clipped to the monitor (the drag cannot leave it).
    CHECK((SelectorCutout({ 1900, 1070 }, { 2500, 1500 }, screen) == Rect{ 1900, 1070, 20, 10 }));
    CHECK((SelectorCutout({ 5, 5 }, { -40, -40 }, screen) == Rect{ 0, 0, 5, 5 }));
    // A click without a drag is empty.
    CHECK((SelectorCutout({ 5, 5 }, { 5, 5 }, screen).IsEmpty()));
}

TEST(Capture_SelectionUnderFivePointsIsACancel)
{
    CHECK((IsSelectionTooSmall(Rect{ 0, 0, 9, 100 }, 2.0)));    // 4.5 pt
    CHECK((!IsSelectionTooSmall(Rect{ 0, 0, 10, 10 }, 2.0)));   // 5 pt
    CHECK((IsSelectionTooSmall(Rect{ 0, 0, 100, 6 }, 1.25)));   // 4.8 pt
    CHECK((!IsSelectionTooSmall(Rect{ 0, 0, 7, 7 }, 1.25)));    // 5.6 pt
    CHECK((IsSelectionTooSmall(Rect{ 0, 0, 4, 50 }, 1.0)));
    CHECK((!IsSelectionTooSmall(Rect{ 0, 0, 5, 5 }, 1.0)));
}

TEST(Capture_StatusLabelSitsBelowOrAboveTheSelection)
{
    Size const screen{ 1000, 1000 };
    // Below: 10 pt under the selection, centred, box 8 × 6 pt larger per side.
    auto const below = LayoutStatusLabel(Rect{ 100, 100, 301, 200 }, Size{ 80, 15 }, screen, 1.0);
    CHECK((below.text == Rect{ 210, 310, 80, 15 }));   // trunc(250.5 - 40) = 210
    CHECK((below.box == Rect{ 202, 304, 96, 27 }));
    // Exactly 6 pt left under the label: stays below.
    auto const edge = LayoutStatusLabel(Rect{ 100, 900, 100, 69 }, Size{ 80, 15 }, screen, 1.0);
    CHECK_EQ(edge.text.y, 979.0);
    // 5 pt left: moves above, 10 pt over the selection.
    auto const above = LayoutStatusLabel(Rect{ 100, 900, 100, 70 }, Size{ 80, 15 }, screen, 1.0);
    CHECK_EQ(above.text.y, 875.0);   // 900 - 10 - 15
    // dpr 2: every constant doubles.
    auto const retina = LayoutStatusLabel(Rect{ 0, 0, 200, 100 }, Size{ 100, 30 }, Size{ 2000, 2000 }, 2.0);
    CHECK((retina.text == Rect{ 50, 120, 100, 30 }));
    CHECK((retina.box == Rect{ 34, 108, 132, 54 }));
}

TEST(Capture_FirstAreaCaptureQuestion)
{
    CaptureActions const settings;   // "use settings"
    CHECK((NeedsAreaCaptureQuestion(settings, L"interactive", true, L"none")));
    CHECK((!NeedsAreaCaptureQuestion(settings, L"interactive", true, L"editor")));
    CHECK((!NeedsAreaCaptureQuestion(settings, L"interactive", false, L"none")));
    CHECK((!NeedsAreaCaptureQuestion(settings, L"interactive-repeat", true, L"none")));
    CHECK((!NeedsAreaCaptureQuestion(settings, L"fullscreen", true, L"none")));
    CaptureActions url;
    url.useSettings = false;
    url.edit = true;
    CHECK((!NeedsAreaCaptureQuestion(url, L"interactive", true, L"none")));
}

TEST(Capture_ResolveCaptureActions)
{
    CaptureActions const settings;
    // Defaults (show on, copy/save off): the editor.
    CHECK((ResolveCaptureActions(settings, L"fullscreen", true, false, false, L"none") == ResolvedActions{ false, false, true, false, false }));
    // Area capture with "preview": the thumbnail instead of the editor; other modes still edit.
    CHECK((ResolveCaptureActions(settings, L"interactive", true, true, false, L"preview") == ResolvedActions{ true, false, false, false, true }));
    CHECK((ResolveCaptureActions(settings, L"interactive-repeat", true, false, true, L"preview") == ResolvedActions{ false, true, true, false, false }));
    // Show off: neither (the tray icon flashes ok).
    CHECK((ResolveCaptureActions(settings, L"interactive", false, true, false, L"preview") == ResolvedActions{ true, false, false, false, false }));
    // URL then= actions are used as given, pin included.
    CaptureActions url;
    url.useSettings = false;
    url.pin = true;
    url.save = true;
    CHECK((ResolveCaptureActions(url, L"interactive", true, true, false, L"preview") == ResolvedActions{ false, true, false, true, false }));
}

TEST(Capture_AddCapturePosition)
{
    // Room to the right: 20·dpr after the capture's right edge, bottoms aligned.
    Point const p = AddCapturePosition(Rect{ 100, 50, 300, 200 }, 1000, Size{ 120, 80 }, 1.0);
    CHECK_EQ(p.x, 420.0);
    CHECK_EQ(p.y, 170.0);   // 250 - 80
    // Capped at canvasMaxX + 1920·dpr - width - 20·dpr.
    Point const q = AddCapturePosition(Rect{ 5000, 0, 1000, 100 }, 2000, Size{ 400, 100 }, 2.0);
    CHECK_EQ(q.x, 2000.0 + 3840 - 400 - 40);
    CHECK_EQ(q.y, 0.0);
}

// ---- Cut Out ----

namespace
{
    Eikon::Core::Bitmap CutOutImage(std::vector<std::string> const& rows)
    {
        // 'w' white, 'r' red, 'b' blue; one character per pixel.
        Eikon::Core::Bitmap bitmap = Eikon::Core::Bitmap::Create(static_cast<std::int32_t>(rows[0].size()), static_cast<std::int32_t>(rows.size()));
        for (std::size_t y = 0; y < rows.size(); ++y)
        {
            for (std::size_t x = 0; x < rows[y].size(); ++x)
            {
                std::uint8_t* p = bitmap.pixels.data() + y * bitmap.Stride() + x * 4;
                switch (rows[y][x])
                {
                case 'w': p[0] = p[1] = p[2] = 0xFF; break;
                case 'r': p[2] = 0xFF; break;
                case 'b': p[0] = 0xFF; break;
                default: break;
                }
            }
        }
        return bitmap;
    }

    bool Cleared(Eikon::Core::Bitmap const& bitmap, int x, int y)
    {
        std::uint8_t const* p = bitmap.pixels.data() + static_cast<std::size_t>(y) * bitmap.Stride() + static_cast<std::size_t>(x) * 4;
        return p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0;
    }
}

TEST(CutOut_RemovesTheBorderColourReachableFromTheEdgeOnly)
{
    // White is the commonest border colour (blue only on two border pixels). The white hole inside
    // the red ring is not connected to the border and stays.
    auto const out = Eikon::Core::CutOutBackground(CutOutImage({
        "wwwwwwb",
        "wrrrrww",
        "wrwwrww",
        "wrrrrww",
        "bwwwwww",
    }));
    CHECK(Cleared(out, 0, 0));
    CHECK(Cleared(out, 5, 2));
    CHECK(Cleared(out, 6, 4));
    CHECK(!Cleared(out, 6, 0));   // blue corner: another colour
    CHECK(!Cleared(out, 0, 4));
    CHECK(!Cleared(out, 1, 1));   // the red ring
    CHECK(!Cleared(out, 2, 2));   // the enclosed white
    CHECK(!Cleared(out, 3, 2));
    CHECK_EQ(out.pixels[(2 * 7 + 2) * 4 + 3], std::uint8_t{ 0xFF });
}

TEST(CutOut_PicksTheCommonestBorderColourNotTheCornerColour)
{
    // The corner is red but blue covers most of the border: blue goes, red stays.
    auto const out = Eikon::Core::CutOutBackground(CutOutImage({
        "rbbbb",
        "bwwwb",
        "bbbbb",
    }));
    CHECK(!Cleared(out, 0, 0));
    CHECK(Cleared(out, 1, 0));
    CHECK(Cleared(out, 4, 2));
    CHECK(!Cleared(out, 2, 1));
}

// ---- Cut Out: the subject model's pre- and post-processing ----

TEST(CutOut_ModelInputIsNormalisedNchwOfTheBrightestValue)
{
    // A white 8 × 8 image at 4 × 4: every value is (1 − mean) / std per channel, in R, G, B planes.
    auto const white = CutOutImage(std::vector<std::string>(8, std::string(8, 'w')));
    auto const input = Eikon::Core::SubjectModelInput(white, 4);
    CHECK_EQ(input.size(), std::size_t{ 48 });
    CHECK(std::abs(input[0] - (1 - 0.485f) / 0.229f) < 1e-4f);
    CHECK(std::abs(input[16] - (1 - 0.456f) / 0.224f) < 1e-4f);
    CHECK(std::abs(input[47] - (1 - 0.406f) / 0.225f) < 1e-4f);
    // Red on blue: the red plane is high where red is, the blue plane where blue is.
    auto const mixed = Eikon::Core::SubjectModelInput(CutOutImage({ "rrbb", "rrbb", "rrbb", "rrbb" }), 2);
    CHECK(mixed[0] > mixed[1]);        // R plane: red left, blue right
    CHECK(mixed[8 + 1] > mixed[8]);    // B plane
}

TEST(CutOut_ModelOutputBecomesAFeatheredMaskOfTheImageSize)
{
    // A 10 × 10 output with a 4 × 4 subject in the middle, scaled to 100 × 100.
    std::vector<float> output(100, 0.02f);
    for (int y = 3; y < 7; ++y)
    {
        for (int x = 3; x < 7; ++x)
        {
            output[static_cast<std::size_t>(y) * 10 + x] = 0.98f;
        }
    }
    auto const mask = Eikon::Core::SubjectMaskFromModel(output, 10, 100, 100, 2);
    CHECK_EQ(mask.size(), std::size_t{ 10000 });
    CHECK_EQ(mask[50 * 100 + 50], std::uint8_t{ 255 });
    CHECK_EQ(mask[0], std::uint8_t{ 0 });
    CHECK_EQ(mask[50 * 100 + 5], std::uint8_t{ 0 });
    // The edge (near x = 30) ramps instead of jumping.
    int ramp = 0;
    for (int x = 20; x < 40; ++x)
    {
        std::uint8_t const v = mask[50 * 100 + x];
        ramp += v > 0 && v < 255 ? 1 : 0;
    }
    CHECK(ramp >= 3);
    CHECK(Eikon::Core::MaskHasSubject(mask));
}

TEST(CutOut_ModelOutputWithoutASubjectGivesNoMask)
{
    // Nothing above 0.5: no subject.
    CHECK(Eikon::Core::SubjectMaskFromModel(std::vector<float>(100, 0.3f), 10, 50, 50, 1).empty());
    // A single confident pixel in 400 is under 0.5 %.
    std::vector<float> speck(400, 0.0f);
    speck[210] = 0.9f;
    CHECK(Eikon::Core::SubjectMaskFromModel(speck, 20, 50, 50, 1).empty());
    CHECK(!Eikon::Core::MaskHasSubject(std::vector<std::uint8_t>(1000, 100)));
}

TEST(CutOut_ApplySubjectMaskScalesEveryPremultipliedChannel)
{
    auto const image = CutOutImage({ "ww" });
    std::vector<std::uint8_t> const mask{ 255, 51 };
    auto const out = Eikon::Core::ApplySubjectMask(image, mask);
    CHECK_EQ(out.pixels[0], std::uint8_t{ 255 });
    CHECK_EQ(out.pixels[3], std::uint8_t{ 255 });
    CHECK_EQ(out.pixels[4], std::uint8_t{ 51 });
    CHECK_EQ(out.pixels[7], std::uint8_t{ 51 });
}

TEST(CutOut_FeatherKeepsTheInteriorAndSoftensTheEdge)
{
    // Left half 255, right half 0, 40 × 4.
    std::vector<std::uint8_t> mask(160, 0);
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 20; ++x)
        {
            mask[static_cast<std::size_t>(y) * 40 + x] = 255;
        }
    }
    Eikon::Core::FeatherMask(mask, 40, 4, 2);
    CHECK_EQ(mask[1 * 40 + 5], std::uint8_t{ 255 });
    CHECK_EQ(mask[1 * 40 + 35], std::uint8_t{ 0 });
    CHECK(mask[1 * 40 + 19] > 128 && mask[1 * 40 + 19] < 255);
    CHECK(mask[1 * 40 + 20] < 128 && mask[1 * 40 + 20] > 0);
}
