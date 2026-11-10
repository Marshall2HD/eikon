#include "pch.h"
#include "Core/PreviewLayout.h"

#include <cmath>

namespace Eikon::Core::PreviewLayout
{
    Thumbnail ThumbnailFor(Size imagePoints, bool autoClosing) noexcept
    {
        double const w = imagePoints.width > 0 ? imagePoints.width : 1.0;
        double const h = imagePoints.height > 0 ? imagePoints.height : 1.0;
        double const imgH = std::clamp(std::round(kImageMaxWidth * h / w), 20.0, 190.0);
        double const extra = autoClosing ? 54.0 : 70.0;
        // Not limited to 186: the rounding of imgH can make it slightly wider (186.67 for 16:9). Only a
        // container wider than the window is limited, and the image is aspect-fitted inside it.
        double const imgW = (std::min)(imgH * w / h, kThumbnailWidth);

        Thumbnail result;
        result.paddingTop = autoClosing ? kPaddingTopAuto : kPaddingTopManual;
        result.window = { kThumbnailWidth, extra + imgH };
        result.image = { (kThumbnailWidth - imgW) / 2, result.paddingTop, imgW, result.window.height - result.paddingTop - kBottomStrip };
        return result;
    }

    Point ThumbnailOrigin(Point mouse, Rect capture, Size window, Rect screen) noexcept
    {
        Rect const r = capture.Standardized();
        double x = std::abs(mouse.x - r.Right()) < std::abs(mouse.x - r.Left()) ? mouse.x - window.width : mouse.x;
        double y = std::abs(mouse.y - r.Top()) < std::abs(mouse.y - r.Bottom()) ? mouse.y : mouse.y - window.height;
        x = (std::max)(screen.Left(), (std::min)(x, screen.Right() - window.width));
        y = (std::max)(screen.Top(), (std::min)(y, screen.Bottom() - window.height));
        return { x, y };
    }

    Rect RectEndingAtMouse(Point mouse, Size imageSize) noexcept
    {
        return { mouse.x - imageSize.width, mouse.y, imageSize.width, imageSize.height };
    }

    double PinStartScale(Size pinPoints, Size screenPoints) noexcept
    {
        return pinPoints.width >= 0.75 * screenPoints.width || pinPoints.height >= 0.75 * screenPoints.height ? 0.5 : 1.0;
    }

    Size PinResize(Size proposed, Size original, double screenWidth, double minimum) noexcept
    {
        if (original.width <= 0 || original.height <= 0)
        {
            return proposed;
        }
        double const aspect = original.height / original.width;
        double w = proposed.width;
        double const ratio = w / original.width;
        if (ratio > 0.44 && ratio < 0.55)
        {
            w = original.width * 0.5;
        }
        else if (ratio > 0.95 && ratio < 1.05)
        {
            w = original.width;
        }
        else if (ratio > 1.92 && ratio < 2.08)
        {
            w = original.width * 2.0;
        }
        w = (std::min)(w, 2.0 * screenWidth);
        // Both sides at least `minimum`, aspect kept.
        w = (std::max)({ w, minimum, minimum / aspect });
        return { w, w * aspect };
    }

    std::int64_t PinZoomPercent(Size current, Size original) noexcept
    {
        if (original.width < 0.5 || original.height <= 0)
        {
            return 100;
        }
        double const zoom = (std::max)(current.width * 100.0 / original.width, current.height * 100.0 / original.height);
        return static_cast<std::int64_t>(zoom);
    }

    std::optional<std::int64_t> PinZoomStep(std::int64_t zoom, bool zoomIn, double originalWidth, double screenWidth) noexcept
    {
        double const displayed = originalWidth * static_cast<double>(zoom) / 100.0;
        if (!zoomIn && displayed <= 100.0)
        {
            return std::nullopt;
        }
        if (zoomIn && displayed >= 2.0 * screenWidth)
        {
            return std::nullopt;
        }
        double const value = (zoomIn ? 1.25993 : 0.7936948878112277) * static_cast<double>(zoom);
        double const step = value <= 90.0 ? 5.0 : 25.0;
        return static_cast<std::int64_t>(step * static_cast<double>(static_cast<std::int64_t>(value / step)));
    }
}
