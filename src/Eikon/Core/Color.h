#pragma once

// sRGB colour with 8-bit channels and the "#RRGGBB" strings the settings store, plus
// the pixel tools' colour maths: the colour under the cursor and in an area,
// the eight colour formats and OKLCH. Contrast is in Core/Contrast.h.
//
// Sampling works in y-up coordinates:
// points and rects are relative to the crop's origin with y pointing UP (0 = bottom edge of the
// crop). The image buffer is top-down (Core::Bitmap), so a y-up row y is buffer row
// crop.y + crop.h − 1 − y (the ruler, the contrast) — except in the single-pixel
// picker, which reads crop.y + crop.h − y.

#include "Core/Geometry.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Eikon::Core
{
    struct Bitmap;

    struct Color
    {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 255;

        bool operator==(Color const&) const = default;

        // "#RRGGBB", upper-case; alpha is dropped.
        std::wstring ToHex() const;
        // Strips '#' and surrounding whitespace, case-insensitive, six hex digits.
        static std::optional<Color> FromHex(std::wstring_view text);
    };

    // ---- Colour under the cursor ----

    // The single-pixel picker. `point` is crop-relative, y up. x = trunc(point.x) must
    // be in [0, crop.w) and v = crop.h − trunc(point.y) in [0, crop.h); the pixel is buffer row
    // crop.y + v, column crop.x + x. Anything else (or no image) is opaque black.
    Color PickPixel(Bitmap const& image, Rect const& crop, Point point) noexcept;

    enum class SampleMode
    {
        Average,   // per-channel integer mean (truncated)
        Darkest,   // smallest R + G + B; the first one found wins a tie
    };
    // The average colour of an area. `rect` is crop-relative, y up; it is truncated,
    // normalised and clamped to (0, 0, crop.w, crop.h). Every pixel is visited, x outer and y-up
    // inner (bottom row first). An empty rect or no image gives black.
    Color SampleArea(Bitmap const& image, Rect const& crop, Rect const& rect, SampleMode mode) noexcept;

    // Shift+Tab / Shift-hover: the darkest colour of the 20 × 20 px square (x − 10, y − 10, 20, 20)
    // around a crop-relative y-up cursor.
    Color DarkestAround(Bitmap const& image, Rect const& crop, Point point) noexcept;

    // ---- Colour formats ----

    struct ColorFormat
    {
        std::wstring_view key;              // value of the colorFormat setting
        std::wstring_view menuTitle;        // ddlColorFormat item, verbatim
        std::wstring_view displayTemplate;  // toolbar readout
        std::wstring_view copyTemplate;     // what Tab / I / C copy
        double labelWidth;                  // constraintColorWidth (points)
    };

    // The static table, in menu order: HEX, HEXno, RGBA, RGB1, Android, UIColor,
    // CSharp, OKLCH.
    std::span<ColorFormat const> ColorFormats() noexcept;
    // Index of `key` in ColorFormats(); an unknown key falls back to 0 (HEX).
    std::size_t ColorFormatIndex(std::wstring_view key) noexcept;

    struct FormattedColor
    {
        std::wstring display;   // colorDisplayString
        std::wstring copy;      // colorCopyString
    };
    // Both templates of the format `key` with the placeholders replaced (%rh %gh %bh
    // two-digit upper-case hex, %rd %gd %bd decimal, %rf %gf %bf byte/255 with two decimals; for
    // OKLCH also %ld %cd %hd = round(L·100), round(C·100), round(H) and %lf %cf %hf = L·100, C, H with
    // two decimals). Always '.' as the decimal separator.
    FormattedColor FormatColor(Color color, std::wstring_view key);

    // ---- OKLCH ----

    struct Oklab
    {
        double l = 0;
        double a = 0;
        double b = 0;
    };
    struct Oklch
    {
        double l = 0;   // 0…1
        double c = 0;
        double h = 0;   // degrees in [0, 360); 0 when |a| and |b| are both below 0.0002
    };

    // sRGB transfer function of: |c| ≤ 0.04045 → c/12.92, else sign(c)·((|c|+0.055)/1.055)^2.4.
    double SrgbToLinear(double encoded) noexcept;
    // XYZ (D65, Y = 1 for white) → Oklab with XYZ→LMS and LMS′→Lab matrices.
    Oklab XyzToOklab(double x, double y, double z) noexcept;
    // Bytes → linear sRGB → XYZ → Oklab → LCh.
    Oklch ToOklch(Color color) noexcept;

    // ---- ColorMagic: dominant colours ----

    // The image redrawn at trunc(w·factor) × trunc(h·factor) with a box filter. An empty result
    // means the scaled image could not be made.
    Bitmap ScaleImage(Bitmap const& image, double factor);

    struct ColorCount
    {
        Color color;            // alpha ignored
        std::size_t count = 0;
    };
    // `counts` is the counted set (one entry per distinct colour, in the caller's
    // order; ties keep it). Keeps colours with
    // count > trunc(totalPixels · 0.0001), sorts by count (descending, stable), keeps 500, merges each
    // into the first-closest kept colour when the L1 distance on 0–1 components is < 0.2 (adding 1 to
    // its frequency), takes the first maxColors, sorts again and drops frequencies ≤ minFrequency.
    std::vector<Color> DominantColors(std::vector<ColorCount> counts, std::size_t totalPixels,
                                      std::size_t maxColors, double minFrequency);
    // Scale by 0.2, count the pixels with alpha > 150, 49 ≤ R+G+B ≤ 749 and
    // max − min channel > 10, then DominantColors.
    std::vector<Color> TopColors(Bitmap const& image, std::size_t maxColors, double minFrequency);
    // TopColors(image, 8, 400) as "#RRGGBB"; ["#000000"] when there are none.
    std::vector<std::wstring> TopColorStrings(Bitmap const& image);
}
