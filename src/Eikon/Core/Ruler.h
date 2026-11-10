#pragma once

// The keyboard ruler's maths: the edge scout, the sensitivity threshold, the
// measurement rect, the label text and the line/label geometry. The key handling, timers and drawing
// are in Editor/PixelTools.
//
// Coordinates (see Core/Color.h): image points and rects are crop-relative with y UP, and view geometry
// is in a y-up view space (device pixels for the mapping, points for the result). The editor flips the result once.

#include "Core/Geometry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Eikon::Core
{
    struct Bitmap;
}

namespace Eikon::Core::Ruler
{
    inline constexpr double kDefaultSensitivity = 3.54;
    inline constexpr double kMaxSensitivity = 20.0;
    inline constexpr double kHideDelaySeconds = 0.8;      // timerHide
    inline constexpr double kImprintTextDistance = 12.0;  // ObjRuler.tdist on imprint
    inline constexpr Rect kInitialRect{ 10, 10, 1000, 1 };

    // T = trunc(2 · sensitivity²): a step between neighbouring pixels is an edge when diff > T.
    std::int64_t Threshold(double sensitivity) noexcept;
    // scrollWheel while on: min(max(sensitivity + deltaY, 0), 20).
    double AdjustSensitivity(double sensitivity, double deltaY) noexcept;

    // [start, end) along the scanned line: x for horizontal, buffer row (top-down, crop-relative) for
    // vertical.
    struct Run
    {
        std::int64_t start = 0;
        std::int64_t end = 0;
        bool operator==(Run const&) const = default;
    };

    // The edge scout on the line through crop-relative pixel
    // (x, row), row counted from the top of the crop. Each side walks away from the cursor comparing
    // every pixel to the previous one (|ΔR| + |ΔG| + |ΔB| on raw bytes); a step above Threshold is an
    // edge unless the next pixel jumps at least twice as much (a spike), and `persistence` edges per
    // side are skipped. No pixel data gives {0, 0}.
    Run Scout(Bitmap const& image, Rect const& crop, std::int64_t x, std::int64_t row, bool vertical,
              std::int64_t persistence, double sensitivity);

    struct Measurement
    {
        Rect rect;      // y-up, crop-relative; zero cross-axis size
        double value;   // its length in image pixels
    };
    // The measuring part of for a crop-relative y-up cursor: nullopt when
    // (trunc(x), trunc(y)) is outside [0, crop.w) × [0, crop.h). Horizontal: (start, y, end − start, 0);
    // vertical: (x, H − end, 0, end − start) with the run in top-down rows; a guide is the whole line,
    // (0, y, crop.w, 0) or (x, 0, 0, crop.h).
    std::optional<Measurement> Measure(Bitmap const& image, Rect const& crop, Point cursor, bool vertical,
                                       std::int64_t persistence, bool guide, double sensitivity);

    // The shortest round-trip digits, with ".0" for whole numbers
    // (21 → "21.0") and an exponent ("1e-05", "1e+16") outside [1e-4, 2^53).
    std::wstring ShortestDescription(double value);
    // The live label: realPixels → "<Int(value)>px"; else v = value / dpr, "<Int(v)>pt"
    // when whole, otherwise ShortestDescription(v) + "pt" (7 px at 1.5 → "4.666666666666667pt").
    std::wstring LabelText(double value, double dpr, bool realPixels);
    // ObjRuler's label: length / dpr, "px" when dpr == 1, else "pt".
    std::wstring ObjectLabelText(double length, double dpr);

    // Image→view mapping in y-up view device pixels:
    // view = image · zoom + offset; `scale` is device pixels per point (dpr; on Windows the
    // window's DPI scale) and `area` is the view size in device pixels (Framer.area).
    struct ViewMap
    {
        double zoom = 1;
        Point offset;
        double scale = 1;
        Size area;

        Point ToView(Point image) const noexcept { return { image.x * zoom + offset.x, image.y * zoom + offset.y }; }
        Point ToImage(Point view) const noexcept { return { (view.x - offset.x) / zoom, (view.y - offset.y) / zoom }; }
    };

    // Everything in y-up view points.
    struct Layout
    {
        std::vector<Rect> bars;          // the 1 pt line and, unless guide, the two 7 pt caps
        std::optional<Rect> bubble;      // rounded rect, radius 2 (not for guides)
        Rect text;                       // where the label text goes (not for guides)
    };

    // (scaled: true) + for the live ruler. `rect` is the measured rect,
    // `cursor` the crop-relative y-up cursor (truncated to whole pixels for the label position) and
    // `textSize` the label's measured size in points.
    Layout LayoutLive(Rect const& rect, bool vertical, bool guide, Point cursor, ViewMap const& map, Size textSize);
}
