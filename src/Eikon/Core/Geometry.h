#pragma once

// Geometry shared by Core, the editor and the capture overlays.
// Coordinates are image pixels with y pointing down (ARCHITECTURE.md, "Coordinates").

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Eikon::Core
{
    // Doubles become integers by rounding to nearest, ties away from zero (FRINTA), in the zoom and
    // geometry code. Unlike floor(v + 0.5) it rounds -2.5 to -3 and
    // 0.49999999999999994 to 0.
    inline double frinta(double value) noexcept
    {
        return std::round(value);
    }

    // frinta followed by an integer conversion. NaN gives 0; values outside int64 saturate.
    inline std::int64_t frintaInt(double value) noexcept
    {
        if (std::isnan(value))
        {
            return 0;
        }
        double const rounded = std::round(value);
        if (rounded >= 9.2233720368547758e18)
        {
            return INT64_MAX;
        }
        if (rounded <= -9.2233720368547758e18)
        {
            return INT64_MIN;
        }
        return static_cast<std::int64_t>(rounded);
    }

    struct Point
    {
        double x = 0;
        double y = 0;

        bool operator==(Point const&) const = default;
        Point operator+(Point other) const noexcept { return { x + other.x, y + other.y }; }
        Point operator-(Point other) const noexcept { return { x - other.x, y - other.y }; }
    };

    struct Size
    {
        double width = 0;
        double height = 0;

        bool operator==(Size const&) const = default;
    };

    // Rect: origin + size. Width and height may be negative while an
    // object is being created (the drag can go any direction); Standardized() makes them positive.
    struct Rect
    {
        double x = 0;
        double y = 0;
        double width = 0;
        double height = 0;

        bool operator==(Rect const&) const = default;

        double Left() const noexcept { return (std::min)(x, x + width); }
        double Right() const noexcept { return (std::max)(x, x + width); }
        double Top() const noexcept { return (std::min)(y, y + height); }
        double Bottom() const noexcept { return (std::max)(y, y + height); }
        Point Origin() const noexcept { return { x, y }; }
        Point Center() const noexcept { return { x + width / 2, y + height / 2 }; }
        Size GetSize() const noexcept { return { width, height }; }
        bool IsEmpty() const noexcept { return width == 0 || height == 0; }

        Rect Standardized() const noexcept { return { Left(), Top(), Right() - Left(), Bottom() - Top() }; }
        // Half-open on the right and bottom edges.
        bool Contains(Point p) const noexcept { return p.x >= Left() && p.x < Right() && p.y >= Top() && p.y < Bottom(); }
        bool Intersects(Rect const& other) const noexcept
        {
            return Left() < other.Right() && other.Left() < Right() && Top() < other.Bottom() && other.Top() < Bottom();
        }
        // Empty rect at the origin when they do not intersect.
        Rect Intersection(Rect const& other) const noexcept
        {
            double const l = (std::max)(Left(), other.Left());
            double const t = (std::max)(Top(), other.Top());
            double const r = (std::min)(Right(), other.Right());
            double const b = (std::min)(Bottom(), other.Bottom());
            if (r <= l || b <= t)
            {
                return {};
            }
            return { l, t, r - l, b - t };
        }
        Rect Union(Rect const& other) const noexcept
        {
            double const l = (std::min)(Left(), other.Left());
            double const t = (std::min)(Top(), other.Top());
            double const r = (std::max)(Right(), other.Right());
            double const b = (std::max)(Bottom(), other.Bottom());
            return { l, t, r - l, b - t };
        }
        Rect Inset(double dx, double dy) const noexcept
        {
            Rect const s = Standardized();
            return { s.x + dx, s.y + dy, s.width - 2 * dx, s.height - 2 * dy };
        }
        Rect Offset(double dx, double dy) const noexcept { return { x + dx, y + dy, width, height }; }
    };

    // ---- Editor window ----

    // Content size (points/DIPs) of the editor window for an image of `imagePixels` captured at
    // `dpr`, on a screen of `screenPoints`:
    //   f = 0.8 if preferLargeWindow else 0.5; maxW = max(836, f·screenW); maxH = max(maxW/2, f·screenH)
    //   w = imgW < maxW ? max(836, imgW) : maxW; h = imgH < maxH ? max(w/2, imgH) : maxH
    // The height is rounded (frinta); the width is kept as is.
    Size InitialEditorContentSize(Size imagePixels, double dpr, Size screenPoints, bool preferLargeWindow) noexcept;

    // Largest zoom (device pixels per image pixel) at which `imagePixels` fits `areaPixels`, capped
    // at 1.0. This is a plain fit; the full Framer zoom-to-rect (20·dpr padding, zoom steps, the
    // preferLargeWindow 0.8 rule) belongs to Editor/ShotView.
    double FitZoom(Size imagePixels, Size areaPixels) noexcept;
}
