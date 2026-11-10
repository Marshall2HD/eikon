#pragma once

// The gallery geometry of the screenshot Library (the five layouts). Sizes are in DIPs. Every layout
// first computes one cell size per item (`sizes`) and the size the thumbnail is drawn at inside it
// (`uncropped`, aspect fill), then a flow layout places the cells in rows.

#include <cstddef>
#include <string_view>
#include <vector>

namespace Eikon::Core::LibraryLayout
{
    // The flow layout: section inset top 2, left 12, bottom 0, right 3;
    // inter-item spacing 11; line spacing 12 (0 in the list).
    inline constexpr double kInsetTop = 2;
    inline constexpr double kInsetLeft = 12;
    inline constexpr double kInsetBottom = 0;
    inline constexpr double kInsetRight = 3;
    inline constexpr double kInteritemSpacing = 11;

    // Popover sizes.
    inline constexpr double kSmallWidth = 380;
    inline constexpr double kSmallHeight = 580;
    inline constexpr double kMediumWidth = 560;
    double MediumHeight(double visibleScreenHeight) noexcept;   // max(580, visible height - 100)

    struct Size
    {
        double width = 0;
        double height = 0;
        bool operator==(Size const&) const = default;
    };

    struct Rect
    {
        double x = 0;
        double y = 0;
        double width = 0;
        double height = 0;
        bool operator==(Rect const&) const = default;
    };

    enum class Kind
    {
        List,        // "list": rows W x 24
        Square80,    // "square80": square tiles of about 80
        Single,      // "large" in the small popover: one image per row
        Pairs,       // "medium" in the small popover, "large" in the medium one
        Justified3,  // "medium" in the medium popover: justified rows of 3
    };

    // libLayout ("list", "square80", "medium", "large"; anything else counts as
    // medium) and libPopoveerSize ("medium" or anything else = small).
    Kind KindFor(std::wstring_view libLayout, std::wstring_view popoverSize) noexcept;

    struct Result
    {
        std::vector<Size> sizes;      // cell size per item
        std::vector<Size> uncropped;  // thumbnail draw size per item (aspect fill of the cell)
        int itemsPerRow = 1;          // used by Up/Down in the gallery
        double lineSpacing = 12;
        double contentHeight = 0;     // the layout function's own return value (not used by the flow)
    };

    // `width` = the content width W (scroll view width - insets; overlay scrollers take no room).
    // `images` = pixel sizes of the records, in gallery order.
    Result Compute(Kind kind, double width, std::vector<Size> const& images);

    // The flow layout: cells in rows left to right, a new row when the next cell plus the 11-pt
    // spacing does not fit; rows with several cells are justified, a lone cell is centred; cells are
    // centred vertically in their row. Frames are relative to the collection view (the inset is
    // included). `viewWidth` = W + left + right inset.
    struct Flow
    {
        std::vector<Rect> frames;
        std::vector<std::size_t> rowStarts;   // index of the first item of each row
        double height = 0;                    // total height including the insets
    };
    Flow Place(std::vector<Size> const& sizes, double viewWidth, double lineSpacing);
}
