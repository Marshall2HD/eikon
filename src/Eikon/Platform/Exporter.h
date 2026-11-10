#pragma once

// Export of the composed image: PNG/JPEG/GIF encoding with WIC, saving to
// the screenshots folder (Auto PNG/JPEG rule, file names, collisions), Save As, the clipboard,
// drag-out of a temporary file and printing. UI thread unless stated.

#include "Core/Bitmap.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Eikon::Exporter
{
    // PNG bytes with a resolution of 96 × dpr DPI. Empty on failure.
    std::vector<std::uint8_t> EncodePng(Core::Bitmap const& bitmap);
    // JPEG at `quality` (0…1), alpha dropped, 96 × dpr DPI; with `screenshotComment` the EXIF
    // UserComment is "Screenshot". Empty on failure.
    std::vector<std::uint8_t> EncodeJpeg(Core::Bitmap const& bitmap, double quality, bool screenshotComment);
    // Two-frame animated GIF, each frame shown `delaySeconds`, looping forever. Empty on failure.
    std::vector<std::uint8_t> EncodeGif(Core::Bitmap const& first, Core::Bitmap const& second, double delaySeconds);

    // Decodes an image file to a premultiplied BGRA bitmap (dpr 1). Empty bitmap on failure.
    Core::Bitmap LoadImageFile(std::filesystem::path const& path);

    // Puts the image on the clipboard as "PNG" and CF_DIBV5. `owner` becomes the
    // clipboard owner (the hidden MessageWindow). Returns false when the clipboard could not be opened
    // or written.
    bool CopyToClipboard(Core::Bitmap const& bitmap, HWND owner);
    // Empties the clipboard right away.
    void ClearClipboard(HWND owner);

    // For downscaleOnSave: half the size when dpr >= 2 (2 × 2 box, which is what low
    // interpolation does at exactly 0.5), dpr halved. Otherwise the image itself.
    std::shared_ptr<Core::Bitmap const> DownscaleForSave(std::shared_ptr<Core::Bitmap const> image, bool downscaleOnSave);

    // What decides whether JPEG is allowed: the output's corner radii and backdrop padding.
    struct Corners
    {
        double outerPx = 0;
        double innerPx = 0;
        double padTop = 0;
        double padBottom = 0;
        double padLeft = 0;
        double padRight = 0;
    };

    // saveImgToFolder: encodes by `saveFormat` ("Auto", "PNG", "JPEG"), names the file
    // `baseName` + ".png"/".jpeg" (or name-2, name-3, … unless `overwrite`), creates the folder and
    // writes atomically. Returns the file written.
    std::optional<std::filesystem::path> SaveToFolder(Core::Bitmap const& image, Corners const& corners, std::wstring const& saveFormat,
                                                      std::filesystem::path const& folder, std::wstring const& baseName, bool overwrite);
    // Writes `bytes` to `path` through a temporary file in the same folder.
    bool WriteFileAtomic(std::filesystem::path const& path, std::vector<std::uint8_t> const& bytes);
    // `baseName` + `extension` in `folder`, with -2, -3, … until the name is free.
    std::filesystem::path FreeFileName(std::filesystem::path const& folder, std::wstring const& baseName, std::wstring_view extension);

    // The screenshots folder: `defaultFolder`, or the Desktop when it is empty.
    std::filesystem::path ScreenshotsFolder();

    // Save As: IFileSaveDialog titled "Save As" with PNG/JPEG types, proposing `baseName` in the
    // screenshots folder. PNG, or JPEG at 0.85 without EXIF. Returns the file written; nullopt when
    // cancelled or failed (`failed` tells which).
    std::optional<std::filesystem::path> SaveAs(HWND owner, Core::Bitmap const& image, std::wstring const& baseName, bool& failed);

    // Drag-out: the temporary folder (%TEMP%\Eikon), and removing its files older than 24 hours
    // (at start, at exit and before each drag).
    std::filesystem::path DragOutFolder();
    void CleanDragOutFolder();

    // Print: the print dialog, then the image fitted and centred on the page with zero margins.
    // `orientation` is printOrientation ("auto", or containing "p" / "l").
    bool Print(HWND owner, Core::Bitmap const& image, std::wstring const& orientation);

    // Opens File Explorer at `file`'s folder with the file selected.
    void ShowInExplorer(std::filesystem::path const& file);
}
