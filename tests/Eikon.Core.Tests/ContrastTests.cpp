#include "pch.h"
#include "TestFramework.h"

#include "Core/Bitmap.h"
#include "Core/Contrast.h"

using namespace Eikon::Core;

// Expected values: WCAG 2 ratios published by WebAIM (#767676 on white = 4.54, #777777 = 4.48) and
// colorjs.io's contrast test suite; APCA 0.0.98G-4g vectors from the apca-w3 test suite
// (git.apcacontrast.com/documentation/README); rounding, the checks, the sampling grid and the
// badge layout worked out by hand.

namespace
{
    constexpr Color kWhite{ 255, 255, 255, 255 };
    constexpr Color kBlack{ 0, 0, 0, 255 };

    Color Grey(std::uint8_t v) { return { v, v, v, 255 }; }

    Color Hex(std::uint32_t rgb)
    {
        return { static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8), static_cast<std::uint8_t>(rgb), 255 };
    }

    Bitmap Filled(int width, int height, Color c)
    {
        Bitmap image = Bitmap::Create(width, height);
        for (std::size_t i = 0; i < image.pixels.size(); i += 4)
        {
            image.pixels[i] = c.b;
            image.pixels[i + 1] = c.g;
            image.pixels[i + 2] = c.r;
        }
        return image;
    }

    void SetPixel(Bitmap& image, int x, int y, Color c)
    {
        auto* p = image.pixels.data() + (static_cast<std::size_t>(y) * image.width + x) * 4;
        p[0] = c.b;
        p[1] = c.g;
        p[2] = c.r;
    }
}

TEST(Wcag_PublishedRatios)
{
    CHECK_NEAR(Contrast::WcagRatio(kWhite, kBlack), 21.0, 1e-12);
    CHECK_NEAR(Contrast::WcagRatio(kWhite, kWhite), 1.0, 1e-12);
    CHECK_NEAR(Contrast::WcagRatio(kWhite, Grey(0x76)), 4.54, 0.005);
    CHECK_NEAR(Contrast::WcagRatio(kWhite, Grey(0x77)), 4.48, 0.005);
    // colorjs.io "WCAG 2.1 contrast, sRGB". colorjs takes Y from its sRGB→XYZ matrix (0.2126729,
    // 0.7151522, 0.0721750) instead of WCAG's 0.2126 / 0.7152 / 0.0722, which moves the ratio by up to
    // 2e-5; the rounded values shown are the same.
    CHECK_NEAR(Contrast::WcagRatio(kWhite, Hex(0xFFFFEE)), 1.010070, 5e-5);
    CHECK_NEAR(Contrast::WcagRatio(kWhite, Hex(0xAFBAAE)), 2.008125, 5e-5);
    CHECK_NEAR(Contrast::WcagRatio(kWhite, Hex(0x8B9986)), 3.000644, 5e-5);
    CHECK_NEAR(Contrast::WcagRatio(kWhite, Hex(0x776655)), 5.502984, 5e-5);
}

TEST(Wcag_RoundsToOneOrTwoDecimals)
{
    CHECK_EQ(Contrast::RoundWcag(Contrast::WcagRatio(kWhite, Grey(0x76))), 4.5);
    CHECK_EQ(Contrast::RoundWcag(Contrast::WcagRatio(kWhite, Grey(0x77))), 4.5);   // 4.48 -> one decimal
    CHECK_EQ(Contrast::RoundWcag(Contrast::WcagRatio(kWhite, Grey(0xFA))), 1.04);  // below 1.5: two decimals
    CHECK_EQ(Contrast::RoundWcag(Contrast::WcagRatio(kWhite, Hex(0xFFFFEE))), 1.01);
    CHECK_EQ(Contrast::RoundWcag(Contrast::WcagRatio(kWhite, Hex(0x8B9986))), 3.0);
    // The 1.5 switch and half away from zero (banker's rounding would give 1.12 and 2.2).
    CHECK_EQ(Contrast::RoundWcag(1.494), 1.49);
    CHECK_EQ(Contrast::RoundWcag(1.4951), 1.5);
    CHECK_EQ(Contrast::RoundWcag(1.125), 1.13);
    CHECK_EQ(Contrast::RoundWcag(1.5), 1.5);
    CHECK_EQ(Contrast::RoundWcag(1.549), 1.5);
    CHECK_EQ(Contrast::RoundWcag(2.25), 2.3);
}

TEST(Wcag_LuminanceLinearSegment)
{
    // 10/255 = 0.0392 < 0.04045: linear segment; 11/255 = 0.0431: power segment.
    CHECK_NEAR(Contrast::WcagLuminance(Grey(10)), (10 / 255.0) / 12.92, 1e-15);
    CHECK_NEAR(Contrast::WcagLuminance(Grey(11)), std::pow((11 / 255.0 + 0.055) / 1.055, 2.4), 1e-15);
    CHECK_EQ(Contrast::WcagLuminance(kWhite), 1.0);
}

TEST(Apca_ReferenceVectors)
{
    struct Vector { std::uint32_t text, background; double lc; };
    Vector const vectors[] = {
        { 0x888888, 0xFFFFFF, 63.056469930209424 },
        { 0xFFFFFF, 0x888888, -68.54146436644962 },
        { 0x000000, 0xAAAAAA, 58.146262578561334 },
        { 0xAAAAAA, 0x000000, -56.24113336839742 },
        { 0x112233, 0xDDEEFF, 91.66830811481631 },
        { 0xDDEEFF, 0x112233, -93.06770049484275 },
        { 0x112233, 0x444444, 8.32326136957393 },
        { 0x444444, 0x112233, -7.526878460278154 },
    };
    for (auto const& v : vectors)
    {
        double const lc = Contrast::ApcaContrast(Contrast::ApcaLuminance(Hex(v.text)), Contrast::ApcaLuminance(Hex(v.background)));
        CHECK_NEAR(lc, v.lc, 1e-9);
    }
}

TEST(Apca_ChecksAndClips)
{
    auto lc = [](Color text, Color background) {
        return Contrast::RoundApca(Contrast::ApcaContrast(Contrast::ApcaLuminance(text), Contrast::ApcaLuminance(background)));
    };
    CHECK_EQ(lc(kBlack, kWhite), 106.0);
    CHECK_EQ(lc(Grey(0x76), kWhite), 71.6);
    CHECK_EQ(lc(Grey(0xFA), kWhite), 0.0);   // S below loClip 0.1
    CHECK_EQ(lc(kWhite, kWhite), 0.0);       // |ΔY| < 0.0005
    CHECK_EQ(Contrast::RoundApca(-68.54146436644962), -68.5);
    CHECK_EQ(Contrast::RoundApca(0.25), 0.3);     // half away from zero after ×10 (2.5 -> 3)
    CHECK_EQ(Contrast::RoundApca(-0.25), -0.3);
}

TEST(ContrastMeasure_LightestAndDarkestInSelection)
{
    Bitmap image = Filled(20, 10, kWhite);
    SetPixel(image, 5, 5, Grey(0x76));
    Rect const crop{ 0, 0, 20, 10 };
    CHECK_EQ(Contrast::Measure(image, crop, { 0, 0, 20, 10 }), (Contrast::Result{ 4.5, 71.6 }));
    SetPixel(image, 15, 2, kBlack);
    CHECK_EQ(Contrast::Measure(image, crop, { 0, 0, 20, 10 }), (Contrast::Result{ 21.0, 106.0 }));
    // y-up selection (10, 7, 10, 3) is the top three rows of the right half: only the black pixel at
    // top-down row 2 is in it.
    CHECK_EQ(Contrast::Measure(image, crop, { 10, 7, 10, 3 }), (Contrast::Result{ 21.0, 106.0 }));
    CHECK_EQ(Contrast::Measure(image, crop, { 10, 0, 10, 7 }), (Contrast::Result{ 1.0, 0.0 }));
    // Negative size and parts outside the crop.
    CHECK_EQ(Contrast::Measure(image, crop, { 30, 12, -25, -20 }), (Contrast::Result{ 21.0, 106.0 }));
}

TEST(ContrastMeasure_SamplingGridStepTwo)
{
    // 1600 × 1600: step = √(1600·1600 / 640000) = 2. Samples are at y-up 0, 2, 4, …, which are the
    // odd top-down rows 1599, 1597, …, and at the even columns.
    Bitmap evenRows = Filled(1600, 1600, kWhite);
    for (int y = 0; y < 1600; y += 2)
    {
        for (int x = 0; x < 1600; ++x)
        {
            SetPixel(evenRows, x, y, kBlack);
        }
    }
    Rect const crop{ 0, 0, 1600, 1600 };
    Rect const all{ 0, 0, 1600, 1600 };
    // Only white is sampled: lightest white, darkest still the starting white.
    CHECK_EQ(Contrast::Measure(evenRows, crop, all), (Contrast::Result{ 1.0, 0.0 }));

    Bitmap oddRows = Filled(1600, 1600, kWhite);
    for (int x = 0; x < 1600; ++x)
    {
        SetPixel(oddRows, x, 1599, kBlack);
    }
    CHECK_EQ(Contrast::Measure(oddRows, crop, all), (Contrast::Result{ 21.0, 106.0 }));

    Bitmap oddColumns = Filled(1600, 1600, kWhite);
    for (int y = 0; y < 1600; ++y)
    {
        SetPixel(oddColumns, 1, y, kBlack);
    }
    CHECK_EQ(Contrast::Measure(oddColumns, crop, all), (Contrast::Result{ 1.0, 0.0 }));

    // Below 640 000 pixels every pixel is sampled.
    Bitmap small = Filled(800, 800, kWhite);
    SetPixel(small, 1, 0, kBlack);
    CHECK_EQ(Contrast::Measure(small, Rect{ 0, 0, 800, 800 }, Rect{ 0, 0, 800, 800 }), (Contrast::Result{ 21.0, 106.0 }));
}

TEST(ContrastBadge_TextAndLayout)
{
    CHECK_EQ(Contrast::BadgeText({ 21.0, 106.0 }, L"wcag2"), std::wstring{ L"W: 21.0" });
    CHECK_EQ(Contrast::BadgeText({ 21.0, 106.0 }, L"apca"), std::wstring{ L"A: 106.0" });
    CHECK_EQ(Contrast::BadgeText({ 1.04, 0.0 }, L"wcag2"), std::wstring{ L"W: 1.04" });
    CHECK_EQ(Contrast::BadgeText({ 4.5, 71.6 }, L"something"), std::wstring{ L"W: 4.5" });

    // Centred on the selection, text bottom 8 pt above its top edge (y up), background (−5, −3, +10, +6).
    auto const a = Contrast::LayoutBadge({ 100, 50, 80, 40 }, { 30, 13 });
    CHECK((a.text == Rect{ 125, 98, 30, 13 }));
    CHECK((a.background == Rect{ 120, 95, 40, 19 }));
    auto const b = Contrast::LayoutBadge({ 180, 90, -80, -40 }, { 30, 13 });
    CHECK((b.text == a.text));
    // Truncation of fractional positions.
    auto const c = Contrast::LayoutBadge({ 100.4, 50.3, 80, 40.2 }, { 31, 13 });
    CHECK((c.text == Rect{ 124, 98, 31, 13 }));
    CHECK((c.background == Rect{ 119, 95, 41, 19 }));
}
