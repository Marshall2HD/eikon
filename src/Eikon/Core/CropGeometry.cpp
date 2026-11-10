#include "pch.h"
#include "Core/CropGeometry.h"

#include <cmath>
#include <cstdlib>

namespace Eikon::Core
{
    namespace
    {
        constexpr double kOutside = 30;     // active area outside the rect
        constexpr double kBand = 30;        // edge band
        constexpr double kSmallSide = 150;  // sides up to this use a fifth of the side
        constexpr double kTinySide = 36;    // sides under this have no inner band

        double Band(double side) noexcept
        {
            if (side < kTinySide)
            {
                return 0;
            }
            return side <= kSmallSide ? side / 5 : kBand;
        }
    }

    std::wstring_view CropHitZone(Rect view, Point p) noexcept
    {
        Rect const r = view.Standardized();
        double const l = r.Left();
        double const t = r.Top();
        double const rt = r.Right();
        double const b = r.Bottom();
        if (p.x < l - kOutside || p.x > rt + kOutside || p.y < t - kOutside || p.y > b + kOutside)
        {
            return CropZone::None;
        }
        double const bx = Band(r.width);
        double const by = Band(r.height);
        int const col = p.x < l + bx ? -1 : (p.x > rt - bx ? 1 : 0);
        int const row = p.y < t + by ? -1 : (p.y > b - by ? 1 : 0);
        if (col == 0 && row == 0)
        {
            return CropZone::Move;
        }
        if (row == 0)
        {
            return col < 0 ? CropZone::Left : CropZone::Right;
        }
        if (col == 0)
        {
            return row < 0 ? CropZone::Top : CropZone::Bottom;
        }
        if (row < 0)
        {
            return col < 0 ? CropZone::TopLeft : CropZone::TopRight;
        }
        return col < 0 ? CropZone::BottomLeft : CropZone::BottomRight;
    }

    std::wstring_view CursorForZone(std::wstring_view zone) noexcept
    {
        if (zone == CropZone::Move)
        {
            return L"cursor-move";
        }
        if (zone == CropZone::Left || zone == CropZone::Right)
        {
            return L"cursor-l-resize";
        }
        if (zone == CropZone::Top || zone == CropZone::Bottom)
        {
            return L"cursor-t-resize";
        }
        if (zone == CropZone::TopLeft || zone == CropZone::BottomRight)
        {
            return L"cursor-tl-resize";
        }
        if (zone == CropZone::TopRight || zone == CropZone::BottomLeft)
        {
            return L"cursor-bl-resize";
        }
        return {};
    }

    Rect DragSelection(Rect start, std::wstring_view zone, double dx, double dy) noexcept
    {
        Rect r = start.Standardized();
        if (zone == CropZone::Move)
        {
            return r.Offset(dx, dy);
        }
        bool const left = zone == CropZone::Left || zone == CropZone::TopLeft || zone == CropZone::BottomLeft;
        bool const right = zone == CropZone::Right || zone == CropZone::TopRight || zone == CropZone::BottomRight;
        bool const top = zone == CropZone::Top || zone == CropZone::TopLeft || zone == CropZone::TopRight;
        bool const bottom = zone == CropZone::Bottom || zone == CropZone::BottomLeft || zone == CropZone::BottomRight;
        if (left)
        {
            r.x += dx;
            r.width -= dx;
        }
        if (right)
        {
            r.width += dx;
        }
        if (top)
        {
            r.y += dy;
            r.height -= dy;
        }
        if (bottom)
        {
            r.height += dy;
        }
        return r;
    }

    Rect KeepInside(Rect rect, Rect bounds) noexcept
    {
        Rect r = rect.Standardized();
        Rect const b = bounds.Standardized();
        if (r.width > b.width || r.height > b.height)
        {
            return r.Intersection(b);
        }
        r.x = std::clamp(r.x, b.Left(), b.Right() - r.width);
        r.y = std::clamp(r.y, b.Top(), b.Bottom() - r.height);
        return r;
    }

    Rect ApplySelectionKey(Rect rect, SelectionKey key, double step, bool resize) noexcept
    {
        Rect r = rect.Standardized();
        switch (key)
        {
        case SelectionKey::Grow:
            return { r.x - step, r.y - step, r.width + 2 * step, r.height + 2 * step };
        case SelectionKey::Shrink:
            if (r.width - 2 * step <= 0 || r.height - 2 * step <= 0)
            {
                return r;
            }
            return { r.x + step, r.y + step, r.width - 2 * step, r.height - 2 * step };
        default:
            break;
        }
        double dx = 0;
        double dy = 0;
        switch (key)
        {
        case SelectionKey::Left: dx = -step; break;
        case SelectionKey::Right: dx = step; break;
        case SelectionKey::Up: dy = -step; break;
        case SelectionKey::Down: dy = step; break;
        default: break;
        }
        if (resize)
        {
            Rect const resized{ r.x, r.y, r.width + dx, r.height + dy };
            return resized.width > 0 && resized.height > 0 ? resized : r;
        }
        return r.Offset(dx, dy);
    }

    Rect SnapSelection(Rect rect, double dpr, bool realPixels) noexcept
    {
        Rect r = rect.Standardized();
        double unit = 1;
        if (!realPixels && dpr > 1 && dpr == std::floor(dpr))
        {
            unit = dpr;
        }
        double const left = frinta(r.Left());
        double const top = frinta(r.Top());
        double const width = frinta(r.width / unit) * unit;
        double const height = frinta(r.height / unit) * unit;
        return { left, top, width, height };
    }

    namespace
    {
        struct Pixels
        {
            Bitmap const& image;
            std::uint32_t At(int x, int y) const noexcept
            {
                std::uint8_t const* p = image.pixels.data() + image.Stride() * static_cast<std::size_t>(y) + static_cast<std::size_t>(x) * 4;
                return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
                       (static_cast<std::uint32_t>(p[3]) << 24);
            }
        };

        bool Close(std::uint32_t a, std::uint32_t b) noexcept
        {
            constexpr int kTolerance = 8;
            for (int shift = 0; shift < 32; shift += 8)
            {
                int const ca = static_cast<int>((a >> shift) & 0xFF);
                int const cb = static_cast<int>((b >> shift) & 0xFF);
                if (std::abs(ca - cb) > kTolerance)
                {
                    return false;
                }
            }
            return true;
        }

        // True when the column x (rows y0..y1-1) is all close to `reference`.
        bool UniformColumn(Pixels const& px, int x, int y0, int y1, std::uint32_t reference) noexcept
        {
            for (int y = y0; y < y1; ++y)
            {
                if (!Close(px.At(x, y), reference))
                {
                    return false;
                }
            }
            return true;
        }

        bool UniformRow(Pixels const& px, int y, int x0, int x1, std::uint32_t reference) noexcept
        {
            for (int x = x0; x < x1; ++x)
            {
                if (!Close(px.At(x, y), reference))
                {
                    return false;
                }
            }
            return true;
        }
    }

    std::optional<Rect> SmartAdjust(Bitmap const& image, Rect selection, std::wstring_view edge)
    {
        if (image.IsEmpty())
        {
            return std::nullopt;
        }
        Rect const s = selection.Standardized().Intersection({ 0, 0, static_cast<double>(image.width), static_cast<double>(image.height) });
        if (s.IsEmpty())
        {
            return std::nullopt;
        }
        int x0 = static_cast<int>(std::floor(s.Left()));
        int y0 = static_cast<int>(std::floor(s.Top()));
        int x1 = static_cast<int>(std::ceil(s.Right()));
        int y1 = static_cast<int>(std::ceil(s.Bottom()));
        Pixels const px{ image };
        bool const all = edge.empty();
        if (all || edge == L"left")
        {
            std::uint32_t const ref = px.At(x0, y0);
            while (x0 < x1 - 1 && UniformColumn(px, x0, y0, y1, ref))
            {
                ++x0;
            }
        }
        if (all || edge == L"right")
        {
            std::uint32_t const ref = px.At(x1 - 1, y0);
            while (x1 - 1 > x0 && UniformColumn(px, x1 - 1, y0, y1, ref))
            {
                --x1;
            }
        }
        if (all || edge == L"top")
        {
            std::uint32_t const ref = px.At(x0, y0);
            while (y0 < y1 - 1 && UniformRow(px, y0, x0, x1, ref))
            {
                ++y0;
            }
        }
        if (all || edge == L"bottom")
        {
            std::uint32_t const ref = px.At(x0, y1 - 1);
            while (y1 - 1 > y0 && UniformRow(px, y1 - 1, x0, x1, ref))
            {
                --y1;
            }
        }
        if (x1 - x0 <= 1 && y1 - y0 <= 1)
        {
            return std::nullopt;
        }
        return Rect{ static_cast<double>(x0), static_cast<double>(y0), static_cast<double>(x1 - x0), static_cast<double>(y1 - y0) };
    }
}
