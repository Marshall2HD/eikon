#pragma once

// A captured or loaded image (ARCHITECTURE.md, "Images"). Platform-neutral: Win2D wraps the pixels
// with CanvasBitmap::CreateFromBytes when the editor shows them.

#include "Core/Geometry.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Eikon::Core
{
    struct Bitmap
    {
        // Pixel size. Rows are top-down and tightly packed: stride = width * 4.
        std::int32_t width = 0;
        std::int32_t height = 0;
        // Scale of the screen it came from.
        double dpr = 1.0;
        // Where it was on the virtual desktop, in physical pixels (empty for clipboard/file images).
        Rect screenRect;
        // BGRA8, premultiplied alpha (DirectXPixelFormat::B8G8R8A8UIntNormalized, CanvasAlphaMode::Premultiplied).
        std::vector<std::uint8_t> pixels;

        std::size_t Stride() const noexcept { return static_cast<std::size_t>(width) * 4; }
        bool IsEmpty() const noexcept { return width <= 0 || height <= 0 || pixels.size() < Stride() * static_cast<std::size_t>(height); }
        // Size in points (image pixels / dpr): the image size shown in the editor.
        Size PointSize() const noexcept { return { width / dpr, height / dpr }; }

        // An opaque black image of the given size.
        static Bitmap Create(std::int32_t width, std::int32_t height, double dpr = 1.0)
        {
            Bitmap bitmap;
            bitmap.width = width > 0 ? width : 0;
            bitmap.height = height > 0 ? height : 0;
            bitmap.dpr = dpr;
            bitmap.pixels.assign(bitmap.Stride() * static_cast<std::size_t>(bitmap.height), 0);
            for (std::size_t i = 3; i < bitmap.pixels.size(); i += 4)
            {
                bitmap.pixels[i] = 0xFF;
            }
            return bitmap;
        }
    };
}
