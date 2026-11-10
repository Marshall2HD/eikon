#pragma once

// The crop selection's geometry: which zone of the
// selection a point is in, how a drag in a zone changes the rect, the keyboard moves and the smart
// adjust ("A") that fits the selection to the content.

#include "Core/Bitmap.h"
#include "Core/Geometry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Eikon::Core
{
    namespace CropZone
    {
        inline constexpr std::wstring_view None = L"default";
        inline constexpr std::wstring_view Move = L"move";
        inline constexpr std::wstring_view Left = L"l-resize";
        inline constexpr std::wstring_view Right = L"r-resize";
        inline constexpr std::wstring_view Top = L"t-resize";
        inline constexpr std::wstring_view Bottom = L"b-resize";
        inline constexpr std::wstring_view TopLeft = L"tl-resize";
        inline constexpr std::wstring_view TopRight = L"tr-resize";
        inline constexpr std::wstring_view BottomLeft = L"bl-resize";
        inline constexpr std::wstring_view BottomRight = L"br-resize";
    }

    // `view` is the selection in view pixels, `point` the cursor in view pixels. Active
    // up to 30 px outside the rect; the edge band is 30 px, or a fifth of a side of at most 150 px,
    // and 0 for a side under 36 px (then only the outside of that side resizes).
    std::wstring_view CropHitZone(Rect view, Point point) noexcept;

    // The cursor-* asset for a zone (mapping inferred): tl/br -> "cursor-tl-resize",
    // tr/bl -> "cursor-bl-resize", l/r -> "cursor-l-resize", t/b -> "cursor-t-resize", move -> "cursor-move".
    std::wstring_view CursorForZone(std::wstring_view zone) noexcept;

    // Applies a drag of (dx, dy) image pixels in `zone` to `start` (the rect at mouse-down). The
    // result may have a negative size while the drag crosses the opposite edge; Standardized() fixes it.
    Rect DragSelection(Rect start, std::wstring_view zone, double dx, double dy) noexcept;

    // Keeps a selection inside `bounds`: a moved rect is pushed back keeping its size; a rect
    // larger than the bounds is cut to them.
    Rect KeepInside(Rect rect, Rect bounds) noexcept;

    // The arrow/bracket keys with a selection: `step` is 1 point (dpr pixels) or 1
    // pixel with realPixels, ×10 with Shift.
    enum class SelectionKey
    {
        Left,
        Right,
        Up,
        Down,
        Grow,     // ] or 9
        Shrink,   // [ or 8
    };
    // Arrows move; `resize` (Ctrl) moves the right/bottom edge instead. Grow/Shrink change every side.
    Rect ApplySelectionKey(Rect rect, SelectionKey key, double step, bool resize) noexcept;

    // Rounds a selection to whole pixels (and to even pixel sizes in points mode), so
    // that the labels and the crop never show half pixels.
    Rect SnapSelection(Rect rect, double dpr, bool realPixels) noexcept;

    // Smart adjust (A,; click on an edge): moves the selection's edges
    // inward past rows/columns of the colour found at that edge, so the selection hugs the content.
    // `edges`: any of "left", "right", "top", "bottom"; empty = all four. Returns nullopt when
    // nothing would remain. * *
    std::optional<Rect> SmartAdjust(Bitmap const& image, Rect selection, std::wstring_view edge = {});
}
