#pragma once

// Platform-neutral parts of the editor chrome: the tool list in display
// order, the toolbar's capacity and overflow, tooltip splitting and the info-bar labels.

#include "Core/Geometry.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Eikon::Core
{
    struct EditorTool
    {
        std::wstring_view id;         // "arrow", "text", ... (CreateObject's tool argument)
        std::wstring_view tooltipKey; // Resources.resw key of "Arrow (A)", ...
        std::wstring_view tooltip;    // the en-US tooltip
        std::wstring_view asset;      // 20-* image name
    };

    // In display order.
    inline constexpr std::array<EditorTool, 15> kEditorTools{ {
        { L"arrow", L"Tool_Arrow", L"Arrow (A)", L"20-arrow-sf" },
        { L"text", L"Tool_Text", L"Text (T)", L"20-text-sf" },
        { L"ruler", L"Tool_Ruler", L"Ruler (1 or 2)", L"20-measure-lite-sf" },
        { L"rectangle", L"Tool_Rectangle", L"Rectangle (R)", L"20-rectangle-sf" },
        { L"backdrop", L"Tool_Backdrop", L"Backdrop (K)", L"20-backdrop" },
        { L"freehand", L"Tool_Freehand", L"Freehand Drawing (D)", L"20-pen" },
        { L"magnifier", L"Tool_Magnifier", L"Magnifier (M)", L"20-lens-sf" },
        { L"append", L"Tool_Append", L"Add Capture (Ctrl+A)", L"20-appendcapture" },
        { L"blur", L"Tool_Blur", L"Blur/Erase (B)", L"20-blur-lite-sf" },
        { L"highlighter", L"Tool_Highlighter", L"Highlighter (H)", L"20-highlighter" },
        { L"spotlight", L"Tool_Spotlight", L"Spotlight (S)", L"20-spotlight" },
        { L"counter", L"Tool_Counter", L"Counter (C)", L"20-counter-lite-sf" },
        { L"oval", L"Tool_Oval", L"Oval (O)", L"20-oval-sf" },
        { L"line", L"Tool_Line", L"Line (L)", L"20-line-sf" },
        { L"image", L"Tool_Image", L"Paste Image (Ctrl+V)", L"20-image" },
    } };

    // Index of `id` in kEditorTools, or kEditorTools.size().
    std::size_t EditorToolIndex(std::wstring_view id) noexcept;

    // CtrlToolbar layout.
    struct ToolbarMetrics
    {
        // Items are 30 × 26; Eikon's Candy toolbar uses 32 × 32 icon buttons.
        static constexpr double padding = 2;
        static constexpr double itemWidth = 32;
        static constexpr double itemHeight = 32;
    };

    // How many slots fit in `width`: floor((width + padding) / (padding + itemWidth)).
    std::size_t ToolbarCapacity(double width) noexcept;
    // Which tools are shown directly: all of them when they fit, else the first capacity − 1 (the
    // last slot becomes the "more" button). `overflow` receives the hidden ones, in order.
    std::size_t ToolbarVisibleCount(std::size_t toolCount, std::size_t capacity) noexcept;
    // The width the toolbar asks for: count × (itemWidth + padding) − padding.
    double ToolbarWidth(std::size_t slots) noexcept;

    // Splits a tooltip at "(": "Arrow (A)" -> {"Arrow", "(A)"}; no key -> {"Arrow", ""}.
    struct TooltipParts
    {
        std::wstring name;
        std::wstring key;
    };
    TooltipParts SplitTooltip(std::wstring_view tooltip);
    // The "more" menu's label: the tooltip without its trailing " (X)" shortcut.
    std::wstring StripShortcut(std::wstring_view tooltip);
    // Tooltip width: name width + key width + 40, or + 32 without a key.
    double TooltipWidth(double nameWidth, double keyWidth, bool hasKey) noexcept;

    // Info-bar size label: "<w>×<h>" + U+200A + "px" with realPixels, else the
    // point size (pixels / dpr, rounded) with "pt".
    std::wstring SizeLabel(double widthPixels, double heightPixels, double dpr, bool realPixels);
    // Info-bar zoom label: Int(zoom × 100) + "%".
    std::wstring ZoomLabel(double zoom);
}
