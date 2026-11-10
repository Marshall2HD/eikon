#include "pch.h"
#include "Core/Framer.h"

#include <cmath>

namespace Eikon::Core
{
    namespace
    {
        constexpr double kSqrt2Approx = 1.414;          // a literal 1.414, not sqrt(2)
        constexpr double kFineFactor = 1.189207115002721; // 2^0.25

        // d = frinta(2·log2 z), even when negative; returns sqrt(2^d).
        double HalfStepPower(double z) noexcept
        {
            double d = frinta(std::log2(z) * 2);
            if (d < 0)
            {
                d = frinta(d * 0.5) * 2;
            }
            return std::sqrt(std::exp2(d));
        }
    }

    Point Framer::ToView(Point image) const noexcept
    {
        Point const p = RoundedPan();
        return { p.x + image.x * zoom, p.y + image.y * zoom };
    }

    Point Framer::ToImage(Point view) const noexcept
    {
        Point const p = RoundedPan();
        return { (view.x - p.x) / zoom, (view.y - p.y) / zoom };
    }

    Rect Framer::ToViewRect(Rect image) const noexcept
    {
        Point const origin = ToView(image.Origin());
        return { origin.x, origin.y, image.width * zoom, image.height * zoom };
    }

    double Framer::SnapCoarse(double z) noexcept
    {
        double s = HalfStepPower(z);
        if (s > 2.0)
        {
            s = frinta(s * 0.5) * 2;
        }
        else if (s >= 1.0)
        {
            s = frinta(s);
        }
        return s;
    }

    double Framer::StepCoarse(double z, bool zoomIn) noexcept
    {
        if (zoomIn)
        {
            return z < 4.0 ? z * 2 : frinta(z * kSqrt2Approx * 0.5) * 2;
        }
        return z <= 4.0 ? z * 0.5 : frinta(z / kSqrt2Approx * 0.5) * 2;
    }

    double Framer::SnapFine(double z) noexcept
    {
        if (z > 6.0)
        {
            return frinta(z);
        }
        if (z > 2.0)
        {
            return frinta(z * 2) * 0.5;
        }
        if (z > 0.65)
        {
            return frinta(z * 4) * 0.25;
        }
        if (z >= 0.5)
        {
            return frinta(z * 10) / 10;
        }
        if (z >= 0.2)
        {
            return frinta(z * 20) / 20;
        }
        return z;
    }

    void Framer::ZoomAt(double newZoom, Point anchor) noexcept
    {
        Point const image = ToImage(anchor);
        zoom = (std::min)((std::max)(newZoom, minZoom), kMaxZoom);
        pan = { anchor.x - image.x * zoom, anchor.y - image.y * zoom };
    }

    void Framer::ZoomStep(Point anchor, bool zoomIn) noexcept
    {
        ZoomAt(SnapCoarse(StepCoarse(zoom, zoomIn)), anchor);
    }

    void Framer::ZoomFine(Point anchor, bool zoomIn) noexcept
    {
        double const raw = zoomIn ? zoom * kFineFactor : zoom / kFineFactor;
        ZoomAt(SnapFine(raw), anchor);
    }

    void Framer::Fit(Rect rect, bool exact) noexcept
    {
        rect = rect.Standardized();
        if (!(rect.width > 0) || !(rect.height > 0) || !(area.width > 0) || !(area.height > 0))
        {
            return;
        }
        double const byWidth = area.width / rect.width;
        double const byHeight = area.height / rect.height;
        double const plain = (std::min)(byWidth, byHeight);
        double z;
        if (plain == 0.5)
        {
            z = 0.5; // an exact half fit keeps no margin and is not snapped
        }
        else
        {
            double fit = (std::min)((area.height - dpr * 20) / rect.height, (area.width - dpr * 20) / rect.width);
            if (plain == 1.0)
            {
                fit = 1.0;
            }
            z = fit;
            if (!exact && fit >= 0.8)
            {
                // The largest ladder value <= fit, walking up from minZoom.
                double best = minZoom;
                double candidate = minZoom;
                while (candidate < kMaxZoom)
                {
                    if (candidate <= fit)
                    {
                        best = candidate;
                    }
                    candidate = candidate < 4.0 ? std::exp2(frinta(std::log2(candidate * 2))) : frinta(candidate * kSqrt2Approx * 0.5) * 2;
                }
                z = best;
            }
        }
        zoom = z;
        Point const center = rect.Center();
        pan = { area.width * 0.5 - center.x * zoom, area.height * 0.5 - center.y * zoom };
    }

    void Framer::Center(Rect rect) noexcept
    {
        Point const center = rect.Standardized().Center();
        pan = { area.width * 0.5 - center.x * zoom, area.height * 0.5 - center.y * zoom };
    }

    void Framer::Clamp(bool allowMargin) noexcept
    {
        double margin = allowMargin ? 20.0 : 0.0;
        if (zoom > 1.0)
        {
            margin *= zoom;
        }
        Rect const view = ToViewRect(canvas.Standardized());
        // The shift that brings [low, high] back into [0, extent].
        auto shiftFor = [margin](double low, double high, double extent) -> double {
            if (high - low <= extent)
            {
                if (low < 0)
                {
                    return -low;
                }
                if (high > extent)
                {
                    return extent - high;
                }
                return 0;
            }
            if (high < extent - margin)
            {
                return (extent - margin) - high;
            }
            if (low > margin)
            {
                return margin - low;
            }
            return 0;
        };
        pan.x += shiftFor(view.Left(), view.Right(), area.width);
        pan.y += shiftFor(view.Top(), view.Bottom(), area.height);
    }

    void Framer::ZoomTo100() noexcept
    {
        ZoomAt(1.0, AreaCenter());
        Clamp(false);
        if (canvas.Standardized().width < area.width && canvas.Standardized().height < area.height)
        {
            Center(canvas);
        }
    }

    void Framer::InitialFit(bool preferLargeWindow, bool keepWindowSize) noexcept
    {
        minZoom = kInitialMinZoom;
        Fit(canvas);
        zoom = (std::min)(zoom, 1.0);
        if (!keepWindowSize && preferLargeWindow && zoom < 1.0)
        {
            Rect const c = canvas.Standardized();
            if (c.width * 0.8 <= area.width && c.height * 0.8 <= area.height)
            {
                zoom = 0.8;
            }
            if (c.width <= area.width && c.height <= area.height)
            {
                zoom = 1.0;
            }
        }
        minZoom = zoom;
        Center(canvas);
    }

    void Framer::FocusOn(Point imagePoint, double focusZoom) noexcept
    {
        zoom = (std::min)((std::max)(focusZoom, minZoom), kMaxZoom);
        pan = { area.width * 0.5 - imagePoint.x * zoom, area.height * 0.5 - imagePoint.y * zoom };
        Clamp(false);
    }

    double Framer::NextPreset(double z) noexcept
    {
        if (z == 4.0)
        {
            return 16.0;
        }
        if (z == 16.0)
        {
            return 64.0;
        }
        return 4.0;
    }

    void Framer::LowerMinZoomFor(Size size) noexcept
    {
        if (!(size.width > 0) || !(size.height > 0))
        {
            return;
        }
        double width = area.width;
        if (size.width < area.width)
        {
            width -= dpr * 20;
        }
        double const fit = (std::min)(width / size.width, area.height / size.height);
        if (fit < minZoom)
        {
            minZoom = HalfStepPower(fit);
        }
    }
}
