#pragma once

// Geometry of the post-capture thumbnail and of pins. Pure maths in whatever unit the caller uses consistently (points for sizes, the same
// unit for the screen and mouse); Windows passes physical pixels with sizes scaled by the monitor.

#include "Core/Geometry.h"

#include <cstdint>
#include <optional>

namespace Eikon::Core::PreviewLayout
{
    // ---- Thumbnail ----
    inline constexpr double kThumbnailWidth = 204.0;     // window width, points
    inline constexpr double kImageMaxWidth = 186.0;
    inline constexpr double kBottomStrip = 45.0;         // image container bottom inset
    inline constexpr double kPaddingTopManual = 25.0;    // paddingTop
    inline constexpr double kPaddingTopAuto = 9.0;

    struct Thumbnail
    {
        Size window;          // 204 × (extra + imgH)
        Rect image;           // image container inside the window (y down), centred
        double paddingTop = 0;
    };

    // imgH = clamp(round(186·H/W), 20, 190); extra = 54 ("auto") or 70; container width = imgH·W/H,
    // limited to the window width (a very wide image is letterboxed instead of overflowing).
    Thumbnail ThumbnailFor(Size imagePoints, bool autoClosing) noexcept;

    // Window origin (top-left, y down) next to the corner of `capture` nearest the mouse:
    // left of the cursor when the mouse is nearer the rect's right edge, below it when nearer the
    // top edge; then clamped into `screen` (the monitor rect, not the work area).
    Point ThumbnailOrigin(Point mouse, Rect capture, Size window, Rect screen) noexcept;
    // The rect used when a capture has no drawn rect (fullscreen, file, clipboard): the image's size,
    // ending at the mouse horizontally and starting at it vertically.
    Rect RectEndingAtMouse(Point mouse, Size imageSize) noexcept;

    // ---- Pins ----
    inline constexpr double kPinMinimum = 44.0;

    // 0.5 when the pin would cover ≥ 75 % of the screen width or height, else 1.
    double PinStartScale(Size pinPoints, Size screenPoints) noexcept;

    // While resizing a pin, the width snaps to 0.5×, 1× or 2× of `original` when its ratio
    // is inside (0.44, 0.55), (0.95, 1.05) or (1.92, 2.08); then width ≤ 2·screenWidth, both sides ≥
    // `minimum`, keeping the aspect ratio. `proposed` must already have the image's aspect ratio.
    Size PinResize(Size proposed, Size original, double screenWidth, double minimum = kPinMinimum) noexcept;

    // After a resize: zoom = Int(max(w·100/origW, h·100/origH)).
    std::int64_t PinZoomPercent(Size current, Size original) noexcept;

    // The next zoom for zoom in (×1.25993) or out (×0.7936948878), truncated to a
    // multiple of 25 above 90, else of 5. nullopt when ignored: zoom out at a displayed width ≤ 100,
    // zoom in at a displayed width ≥ 2·screenWidth.
    std::optional<std::int64_t> PinZoomStep(std::int64_t zoom, bool zoomIn, double originalWidth, double screenWidth) noexcept;
}
