#include "pch.h"
#include "Core/ImageResize.h"

#include "Core/Geometry.h"

#include <cmath>

namespace Eikon::Core
{
    std::optional<int> ResizePresetIndex(std::int64_t width, std::int64_t originalWidth) noexcept
    {
        if (originalWidth <= 0)
        {
            return std::nullopt;
        }
        for (int i = 0; i < static_cast<int>(std::size(kResizePresets)); ++i)
        {
            if (frintaInt(static_cast<double>(originalWidth) * kResizePresets[i]) == width)
            {
                return i;
            }
        }
        return std::nullopt;
    }

    std::int64_t ResizePresetSize(std::int64_t original, int preset) noexcept
    {
        if (preset < 0 || preset >= static_cast<int>(std::size(kResizePresets)))
        {
            return original;
        }
        return (std::max)(std::int64_t{ 1 }, frintaInt(static_cast<double>(original) * kResizePresets[preset]));
    }

    std::int64_t AspectLocked(std::int64_t edited, std::int64_t editedOriginal, std::int64_t otherOriginal) noexcept
    {
        if (editedOriginal <= 0 || edited <= 0)
        {
            return edited <= 0 ? 0 : otherOriginal;
        }
        double const other = static_cast<double>(edited) * static_cast<double>(otherOriginal) / static_cast<double>(editedOriginal);
        return (std::max)(std::int64_t{ 1 }, frintaInt(other));
    }

    ResizeCheck CheckResize(std::int64_t width, std::int64_t height, std::int64_t currentWidth, std::int64_t currentHeight) noexcept
    {
        constexpr std::int64_t kMaxSide = 22000;
        if (currentWidth <= 0 || currentHeight <= 0)
        {
            return ResizeCheck::NoImage;
        }
        if (width <= 0 || height <= 0)
        {
            return ResizeCheck::TooSmall;
        }
        if (width > kMaxSide || height > kMaxSide)
        {
            return ResizeCheck::TooLarge;
        }
        if (width == currentWidth && height == currentHeight)
        {
            return ResizeCheck::SameSize;
        }
        return ResizeCheck::Ok;
    }

    ResizeQuality ResizeQualityFor(double scale) noexcept
    {
        if (scale == 0.5)
        {
            return ResizeQuality::Low;
        }
        if (scale >= 1.0 && scale == std::floor(scale))
        {
            return ResizeQuality::Nearest;
        }
        return ResizeQuality::High;
    }
}
