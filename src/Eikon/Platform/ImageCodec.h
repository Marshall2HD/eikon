#pragma once

// Internal WIC helpers shared by the Platform layer (clipboard images, application icons).
// Not part of the cross-component contract. All functions are noexcept and return an empty vector
// on failure. They require COM to be initialized on the calling thread (they initialize it as MTA
// when it is not).

#include <windows.h>

#include <cstdint>
#include <span>
#include <vector>

namespace Eikon::ImageCodec
{
    // Decodes a packed DIB (CF_DIB / CF_DIBV5 payload: BITMAPINFOHEADER/V4/V5 + optional masks/colour
    // table + pixels) and encodes it as PNG. 32bpp BI_RGB / BI_BITFIELDS (with or without alpha) and
    // 24bpp are converted directly; other layouts go through the WIC BMP decoder.
    std::vector<std::uint8_t> DibToPng(std::span<std::uint8_t const> dib) noexcept;

    // Decodes PNG (or any WIC-readable image) and returns a CF_DIBV5 payload: BITMAPV5HEADER
    // (BI_BITFIELDS, BGRA masks, sRGB) followed by bottom-up 32bpp straight-alpha pixels.
    std::vector<std::uint8_t> PngToDibV5(std::span<std::uint8_t const> png) noexcept;

    // Encodes an icon / a 32bpp bitmap (premultiplied alpha, as returned by the shell) as PNG.
    std::vector<std::uint8_t> IconToPng(HICON icon) noexcept;
    std::vector<std::uint8_t> BitmapToPng(HBITMAP bitmap) noexcept;

    // Encodes top-down 32bpp BGRA pixels (stride width * 4) as PNG, with a pHYs resolution of `dpi`
    // when it is > 0.
    std::vector<std::uint8_t> BgraToPng(std::uint32_t width, std::uint32_t height, std::span<std::uint8_t const> pixels,
                                        bool premultiplied, double dpi) noexcept;

    // Decodes an image file and scales it to width × height (high-quality cubic) as top-down 32bpp
    // BGRA, premultiplied or straight.
    std::vector<std::uint8_t> DecodeFileScaled(wchar_t const* path, std::uint32_t width, std::uint32_t height, bool premultiplied) noexcept;
}
