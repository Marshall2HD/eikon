#include "pch.h"
#include "Core/EditorTools.h"

#include <cmath>

namespace Eikon::Core
{
    std::size_t EditorToolIndex(std::wstring_view id) noexcept
    {
        for (std::size_t i = 0; i < kEditorTools.size(); ++i)
        {
            if (kEditorTools[i].id == id)
            {
                return i;
            }
        }
        return kEditorTools.size();
    }

    std::size_t ToolbarCapacity(double width) noexcept
    {
        if (!(width > 0))
        {
            return 0;
        }
        double const n = std::floor((width + ToolbarMetrics::padding) / (ToolbarMetrics::padding + ToolbarMetrics::itemWidth));
        return n > 0 ? static_cast<std::size_t>(n) : 0;
    }

    std::size_t ToolbarVisibleCount(std::size_t toolCount, std::size_t capacity) noexcept
    {
        if (capacity >= toolCount)
        {
            return toolCount;
        }
        return capacity > 0 ? capacity - 1 : 0;
    }

    double ToolbarWidth(std::size_t slots) noexcept
    {
        if (slots == 0)
        {
            return 0;
        }
        return static_cast<double>(slots) * (ToolbarMetrics::itemWidth + ToolbarMetrics::padding) - ToolbarMetrics::padding;
    }

    TooltipParts SplitTooltip(std::wstring_view tooltip)
    {
        TooltipParts parts;
        auto const open = tooltip.find(L'(');
        if (open == std::wstring_view::npos)
        {
            parts.name = std::wstring{ tooltip };
        }
        else
        {
            parts.name = std::wstring{ tooltip.substr(0, open) };
            parts.key = std::wstring{ tooltip.substr(open) };
        }
        while (!parts.name.empty() && parts.name.back() == L' ')
        {
            parts.name.pop_back();
        }
        return parts;
    }

    std::wstring StripShortcut(std::wstring_view tooltip)
    {
        // Regex " ?\([^ ]..." : an optional space, "(" and a non-space, to the end.
        for (std::size_t i = 0; i + 1 < tooltip.size(); ++i)
        {
            if (tooltip[i] == L'(' && tooltip[i + 1] != L' ')
            {
                std::size_t cut = i;
                if (cut > 0 && tooltip[cut - 1] == L' ')
                {
                    --cut;
                }
                return std::wstring{ tooltip.substr(0, cut) };
            }
        }
        return std::wstring{ tooltip };
    }

    double TooltipWidth(double nameWidth, double keyWidth, bool hasKey) noexcept
    {
        return hasKey ? nameWidth + keyWidth + 40 : nameWidth + 32;
    }

    std::wstring SizeLabel(double widthPixels, double heightPixels, double dpr, bool realPixels)
    {
        if (!(dpr > 0))
        {
            dpr = 1;
        }
        double const w = realPixels ? widthPixels : widthPixels / dpr;
        double const h = realPixels ? heightPixels : heightPixels / dpr;
        std::wstring text = std::to_wstring(frintaInt(std::abs(w)));
        text += L'\u00D7';
        text += std::to_wstring(frintaInt(std::abs(h)));
        text += L'\u200A';
        text += realPixels ? L"px" : L"pt";
        return text;
    }

    std::wstring ZoomLabel(double zoom)
    {
        return std::to_wstring(frintaInt(zoom * 100)) + L"%";
    }
}
