#pragma once

// Framer singleton: the zoom, pan and fit maths of the editor
// canvas, without any drawing. The view is the canvas area in device pixels, origin top-left, y down;
// a point of the image maps to the view as
//     view = pan + image × zoom
// where `zoom` is device pixels per image pixel. Formulas are symmetric around the area's
// centre, so flipping y does not change them.

#include "Core/Geometry.h"

namespace Eikon::Core
{
    // Growth of the canvas beyond the crop (expandable canvas), image pixels, each >= 0.
    struct Insets
    {
        double left = 0;
        double top = 0;
        double right = 0;
        double bottom = 0;

        bool operator==(Insets const&) const = default;
        bool IsZero() const noexcept { return left == 0 && top == 0 && right == 0 && bottom == 0; }
    };

    class Framer
    {
    public:
        static constexpr double kMaxZoom = 64.0;
        static constexpr double kInitialMinZoom = 0.03125;

        Size area;              // canvas area, device pixels (Framer.area)
        double dpr = 1.0;       // device pixels per DIP (Framer.dpr)
        double zoom = 1.0;      // device pixels per image pixel
        double minZoom = kInitialMinZoom;
        Point pan;              // bpx, bpy
        Rect canvas;            // what is drawn: the crop plus the canvas growth, image pixels

        // The pan rounded to whole pixels, which layout and the view use.
        Point RoundedPan() const noexcept { return { frinta(pan.x), frinta(pan.y) }; }
        Point ToView(Point image) const noexcept;
        Point ToImage(Point view) const noexcept;
        Rect ToViewRect(Rect image) const noexcept;

        // ---- Zoom levels ----
        // D = frinta(2·log2 z), made even when negative; z = sqrt(2^d); above 2 the
        // result is rounded to an even integer, from 1 to 2 to an integer. So the ladder is
        // …, 0.25, 0.5, 1, 2, 4, 6, 8, 12, 16, 22, 32, 46, 64.
        static double SnapCoarse(double z) noexcept;
        // Coarse zoom step before snapping: in: 2z below 4, else 2·frinta(1.414z / 2); out mirrors it.
        static double StepCoarse(double z, bool zoomIn) noexcept;
        // Snapping after × or ÷ 2^0.25.
        static double SnapFine(double z) noexcept;

        // ---- Operations (each keeps the image point under `anchor`, in view pixels, fixed) ----
        // Coarse step (menu, keys, wheel, zoom helper ±), clamped to [minZoom, 64]..
        void ZoomStep(Point anchor, bool zoomIn) noexcept;
        // Fine step (precision touchpad with Ctrl)..
        void ZoomFine(Point anchor, bool zoomIn) noexcept;
        // Sets the zoom (clamped to [minZoom, 64]) keeping `anchor` fixed.
        void ZoomAt(double newZoom, Point anchor) noexcept;
        // Fits `rect` (image pixels) into the area with a 20·dpr margin and centres it.
        // A fit of at least 0.8 is snapped down to the zoom ladder unless `exact`.
        void Fit(Rect rect, bool exact = false) noexcept;
        // Centres `rect` without changing the zoom.
        void Center(Rect rect) noexcept;
        // Keeps the canvas in view. A canvas that fits stays fully inside the area; a
        // larger one may leave a margin (20 px, × zoom above 1) only when `allowMargin` (panning, wheel).
        void Clamp(bool allowMargin) noexcept;
        // Zoom 1 around the area's centre; centred when the canvas fits.
        void ZoomTo100() noexcept;
        // The zoom a new capture opens at. minZoom becomes it.
        void InitialFit(bool preferLargeWindow, bool keepWindowSize) noexcept;
        // Selection Top Left / Bottom Right (Q / W): zoom 16 centred on
        // `imagePoint`.
        void FocusOn(Point imagePoint, double focusZoom = 16.0) noexcept;
        // Z + click presets: 4 -> 16 -> 64, anything else -> 4.
        static double NextPreset(double z) noexcept;
        // (after the canvas grows): lowers minZoom to the power of two at or below the
        // fit of a canvas of `size` image pixels (with the 20·dpr margin when it is wider than the area).
        void LowerMinZoomFor(Size size) noexcept;

        Point AreaCenter() const noexcept { return { area.width / 2, area.height / 2 }; }
    };
}
