#pragma once

// The contrast badge's maths: WCAG 2.0 contrast ratio and APCA 0.0.98G-4g Lc of
// the lightest and darkest colours in the crop selection. Coordinates as in Core/Color.h:
// crop-relative, y up.

#include "Core/Color.h"
#include "Core/Geometry.h"

#include <string>
#include <string_view>

namespace Eikon::Core
{
    struct Bitmap;
}

namespace Eikon::Core::Contrast
{
    // WCAG relative luminance with the sRGB piecewise curve:
    // c < 0.04045 → c/12.92, else ((c + 0.055)/1.055)^2.4; 0.2126 R + 0.7152 G + 0.0722 B, clamped to [0, 1].
    double WcagLuminance(Color color) noexcept;
    // (L_lighter + 0.05) / (L_darker + 0.05), unrounded.
    double WcagRatio(Color lighter, Color darker) noexcept;
    // Rounding: two decimals below 1.5, one decimal from 1.5 up, half away from zero.
    double RoundWcag(double ratio) noexcept;

    // APCA screen luminance: 0.2126729 (R/255)^2.4 + 0.7151522 (G/255)^2.4 + 0.0721750 (B/255)^2.4.
    double ApcaLuminance(Color color) noexcept;
    // APCAcontrast(txtY, bgY) of APCA 0.0.98G-4g (before rounding), scaled ×100 and
    // signed: positive for dark text on a light background, negative for the reverse.
    double ApcaContrast(double textY, double backgroundY) noexcept;
    // round(Lc · 10) / 10, half away from zero.
    double RoundApca(double lc) noexcept;

    struct Result
    {
        double wcag = 0;   // already rounded
        double apca = 0;   // already rounded
        bool operator==(Result const&) const = default;
    };

    // getContrast over `selection` (crop-relative, y up), clamped to the crop. The
    // sampling grid has step = max(1, √(w·h / 640000)) in both axes, positions minX + step·i (outer)
    // and minY + step·j (inner), truncated. The lightest sample (largest WCAG luminance, strictly
    // greater, from black) and the darkest (strictly smaller, from white) give the WCAG ratio and the
    // APCA Lc with the darkest as text. An empty selection gives the two starting colours.
    Result Measure(Bitmap const& image, Rect const& crop, Rect const& selection);

    // "W: " + wcag or "A: " + apca (contrastType == "apca"), with the shortest round-trip digits.
    std::wstring BadgeText(Result const& result, std::wstring_view contrastType);

    // The badge laid out above the selection, in y-up view points. `selection` is the
    // selection rect in view points (any sign of width/height) and `textSize` the text's size.
    struct BadgeLayout
    {
        Rect text;          // the text frame (centred text)
        Rect background;    // text frame expanded by (−5, −3, +10, +6), corner radius 3
    };
    BadgeLayout LayoutBadge(Rect const& selection, Size textSize) noexcept;
}
