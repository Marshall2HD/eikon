#pragma once

// Platform-neutral capture maths: the window background modes, the RegionSelector geometry, the
// after-capture routing and the Add Capture placement.
// Pixels, y down; y-up formulas are flipped here.

#include "Core/Bitmap.h"
#include "Core/Color.h"
#include "Core/Geometry.h"
#include "Core/UrlCommand.h"

#include <optional>
#include <string_view>

namespace Eikon::Core
{
    // ---- Window background modes (`windowShadow`) ----

    enum class ShadowMode : int
    {
        Transparent = 0,
        Trimmed = 1,
        Wallpaper = 2,
        Solid = 3,
    };

    // "transparent", "trimmed", "wallpaper", "solid"; anything else is nullopt.
    // grabScreenshot maps nullopt to Transparent, Add Capture to Wallpaper.
    std::optional<ShadowMode> ParseShadowMode(std::wstring_view value) noexcept;

    // Shadow paddings: transparent margin per side, alpha threshold > 150, at most
    // 300 px scanned. Left/right along the middle row, top/bottom along the column at the centre of
    // the opaque span. Each is (index of the first opaque pixel) - 1, floored at 0; 0 if none found.
    struct ShadowPaddings
    {
        int top = 0;
        int right = 0;
        int bottom = 0;
        int left = 0;

        bool operator==(ShadowPaddings const&) const = default;
    };
    ShadowPaddings ComputeShadowPaddings(Bitmap const& image) noexcept;

    // trimShadow: crops the paddings off when they leave something.
    Bitmap TrimShadow(Bitmap image);

    // isEdgeTransparent: alpha at (W/2, 0) or (W/2, H-1) is not 255.
    bool IsEdgeTransparent(Bitmap const& image) noexcept;

    // addWallpaper. Returns the image unchanged when (W/2, 0) and (W/2, H-1) are both
    // opaque. Otherwise a W×H canvas filled with the centred W×H crop of `wallpaper`
    // (stretched when the wallpaper is smaller), with the image drawn on top, moved down by
    // Int((bottom - top) / 2) of its shadow paddings. `wallpaper` empty: unchanged.
    Bitmap AddWallpaper(Bitmap image, Bitmap const& wallpaper);

    // addColor: a W×H canvas filled with `color`, the image drawn on top with the same
    // vertical shift. Applied even when the edges are opaque.
    Bitmap AddColor(Bitmap const& image, Color color);

    // A copy of `rect` (clamped to the image), keeping dpr and moving screenRect along.
    Bitmap CropBitmap(Bitmap const& image, int x, int y, int width, int height);

    // ---- RegionSelector geometry (pixels, y down, relative to the monitor) ----

    // The selection between the press pixel `start` and the pixel under the cursor `current` (both
    // integral), mouseDragged. The rectangle spans from the press point to the cursor
    // (the cursor's own row and column excluded); dragging right/down onto the last column/row adds
    // 1 so the screen edge can be reached; the result is clipped to the monitor.
    Rect SelectorCutout(Point start, Point current, Size screen) noexcept;

    // Width or height under 5 pt cancels the area capture.
    bool IsSelectionTooSmall(Rect pixels, double dpr) noexcept;

    // The status label (text size `text` in pixels) centred under the selection,
    // 10 pt below it, or 10 pt above it when it would come within 6 pt of the screen bottom. The box
    // is the text frame grown by 8 pt horizontally and 6 pt vertically. Values truncated.
    struct StatusLabelLayout
    {
        Rect text;
        Rect box;
    };
    StatusLabelLayout LayoutStatusLabel(Rect cutout, Size text, Size screen, double dpr) noexcept;

    // ---- After capture ----

    // Step 1: the first-area-capture question ("Show small Preview or open the Editor?").
    bool NeedsAreaCaptureQuestion(CaptureActions const& actions, std::wstring_view mode, bool afterGrabShow,
                                  std::wstring_view areaCaptureMode) noexcept;

    // Step 7: the action flags. With "use settings": copy/save from the settings, thumbnail for an
    // area capture with afterGrabShow and areaCaptureMode "preview", edit = show and not thumbnail.
    struct ResolvedActions
    {
        bool copy = false;
        bool save = false;
        bool edit = false;
        bool pin = false;
        bool thumbnail = false;

        bool operator==(ResolvedActions const&) const = default;
    };
    ResolvedActions ResolveCaptureActions(CaptureActions const& actions, std::wstring_view mode, bool afterGrabShow,
                                          bool afterGrabCopy, bool afterGrabSave, std::wstring_view areaCaptureMode) noexcept;

    // ---- Add Capture ----

    // X = min(R.maxX + 20·dpr, canvasMaxX + 1920·dpr − objectWidth − 20·dpr),
    // y = R.maxY − objectHeight (y down: the object's bottom on the capture's bottom).
    Point AddCapturePosition(Rect captureRect, double canvasMaxX, Size object, double dpr) noexcept;
}
