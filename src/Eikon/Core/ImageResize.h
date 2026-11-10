#pragma once

// Resize Image (ctrlHelperPixels): the size presets, the aspect-locked fields and
// the checks before resampling.

#include <cstdint>
#include <optional>

namespace Eikon::Core
{
    // scSizes segments: 25%, 33%, 50%, 1×, 2×, 4×.
    inline constexpr double kResizePresets[] = { 0.25, 1.0 / 3.0, 0.5, 1.0, 2.0, 4.0 };

    // The preset segment to highlight for `width`: the first scale whose rounded
    // width equals it, else none.
    std::optional<int> ResizePresetIndex(std::int64_t width, std::int64_t originalWidth) noexcept;

    // The width/height a preset gives, rounded, at least 1.
    std::int64_t ResizePresetSize(std::int64_t original, int preset) noexcept;

    // Editing one field recomputes the other keeping the aspect ratio, rounded, at
    // least 1.
    std::int64_t AspectLocked(std::int64_t edited, std::int64_t editedOriginal, std::int64_t otherOriginal) noexcept;

    enum class ResizeCheck
    {
        Ok,
        NoImage,    // "Error" / "No image to resize"
        TooSmall,   // "Can't resize the image" / "New size is too small" (<= 0)
        TooLarge,   // "Can't resize the image" / "New size is too large" (> 22000)
        SameSize,   // just closes the helper
    };
    ResizeCheck CheckResize(std::int64_t width, std::int64_t height, std::int64_t currentWidth, std::int64_t currentHeight) noexcept;

    enum class ResizeQuality
    {
        High,      // high-quality interpolation
        Low,       // exactly 0.5
        Nearest,   // integer upscales (2×, 4×, ...)
    };
    // `scale` = new width / old width.
    ResizeQuality ResizeQualityFor(double scale) noexcept;
}
