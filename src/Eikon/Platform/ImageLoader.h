#pragma once

// Image sources other than the screen: WIC decoding of files and clipboard
// data into Core::Bitmap, scaling, the clipboard reader and the "Open Image" dialog. UI thread.

#include "Core/Bitmap.h"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Eikon::ImageLoader
{
    // Any WIC-decodable image (first frame) as premultiplied BGRA, dpr 1. nullopt on failure.
    std::optional<Core::Bitmap> DecodeBytes(std::span<std::uint8_t const> bytes);
    std::optional<Core::Bitmap> DecodeFile(std::wstring const& path);

    // High-quality (Fant) resize. Returns the input when the size is unchanged; empty on failure.
    Core::Bitmap Scale(Core::Bitmap const& image, int width, int height);

    // The clipboard's image: "PNG", CF_DIBV5 / CF_DIB, or a single image file
    // (CF_HDROP), in that order. nullopt when there is no image.
    std::optional<Core::Bitmap> ReadClipboard(HWND owner);

    // "Open Image": one existing file of a WIC-decodable type. nullopt if cancelled.
    std::optional<std::wstring> ChooseImageFile(HWND owner, std::wstring const& title);
}
