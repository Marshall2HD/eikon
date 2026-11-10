#pragma once

// Snapping while an object is dragged and the
// expandable canvas.

#include "Core/Framer.h"
#include "Core/Geometry.h"

#include <optional>
#include <vector>

namespace Eikon::Core
{
    // snappingMode: 0 off, 1 Snap to Objects, 2 Snap to Similar Objects (default).
    enum class SnappingMode : int
    {
        Off = 0,
        Objects = 1,
        SimilarObjects = 2,
    };

    // A dashed guide line in image pixels: vertical when x1 == x2, horizontal when y1 == y2.
    struct SnapGuide
    {
        Point from;
        Point to;
        bool operator==(SnapGuide const&) const = default;
    };

    struct SnapResult
    {
        double dx = 0;     // shift to apply to the moving rect
        double dy = 0;
        bool snappedX = false;
        bool snappedY = false;
        std::vector<SnapGuide> guides;   // at most two vertical and two horizontal
    };

    // Snaps `moving`'s left/right edges to the left/right edges of `candidates` and its top/bottom to
    // their top/bottom; per axis the closest candidate within `threshold` wins.
    SnapResult Snap(Rect moving, std::vector<Rect> const& candidates, double threshold);

    // The snap threshold: 5 screen points = 5·dpr / zoom image pixels.
    inline double SnapThreshold(double dpr, double zoom) noexcept { return zoom > 0 ? 5.0 * dpr / zoom : 0; }

    // How far the canvas grows beyond `crop` to hold `objectBounds`: on each side where an
    // object sticks out, its overhang plus 20·dpr, at most 1920·dpr horizontally and 1280·dpr
    // vertically. Sides with nothing outside stay at 0.
    Insets CanvasGrowth(Rect crop, std::vector<Rect> const& objectBounds, double dpr);
}
