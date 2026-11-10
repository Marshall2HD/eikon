#include "pch.h"
#include "Core/Objects/Conceal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Eikon::Core::Conceal
{
    namespace
    {
        struct Rgba
        {
            int b = 0, g = 0, r = 0, a = 0;
        };

        Rgba Pixel(Bitmap const& bmp, long long x, long long y)
        {
            x = std::clamp<long long>(x, 0, bmp.width - 1);
            y = std::clamp<long long>(y, 0, bmp.height - 1);
            std::uint8_t const* p = bmp.pixels.data() + static_cast<std::size_t>(y) * bmp.Stride() + static_cast<std::size_t>(x) * 4;
            return { p[0], p[1], p[2], p[3] };
        }

        void Put(Bitmap& bmp, long long x, long long y, Rgba c)
        {
            if (x < 0 || y < 0 || x >= bmp.width || y >= bmp.height) return;
            std::uint8_t* p = bmp.pixels.data() + static_cast<std::size_t>(y) * bmp.Stride() + static_cast<std::size_t>(x) * 4;
            p[0] = static_cast<std::uint8_t>(c.b);
            p[1] = static_cast<std::uint8_t>(c.g);
            p[2] = static_cast<std::uint8_t>(c.r);
            p[3] = static_cast<std::uint8_t>(c.a);
        }

        int Diff(Rgba a, Rgba b)
        {
            return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
        }

        // "average": the integer mean of every pixel of `cell` (clipped to the image).
        Rgba Average(Bitmap const& src, Cell const& cell)
        {
            long long const x0 = (std::max)(0LL, static_cast<long long>(cell.x));
            long long const y0 = (std::max)(0LL, static_cast<long long>(cell.y));
            long long const x1 = (std::min)(static_cast<long long>(src.width), static_cast<long long>(cell.x + cell.w));
            long long const y1 = (std::min)(static_cast<long long>(src.height), static_cast<long long>(cell.y + cell.h));
            unsigned long long sb = 0, sg = 0, sr = 0, sa = 0, n = 0;
            for (long long y = y0; y < y1; ++y)
            {
                std::uint8_t const* p = src.pixels.data() + static_cast<std::size_t>(y) * src.Stride() + static_cast<std::size_t>(x0) * 4;
                for (long long x = x0; x < x1; ++x, p += 4)
                {
                    sb += p[0];
                    sg += p[1];
                    sr += p[2];
                    sa += p[3];
                    ++n;
                }
            }
            if (n == 0) return {};
            return { static_cast<int>(sb / n), static_cast<int>(sg / n), static_cast<int>(sr / n), static_cast<int>(sa / n) };
        }

        bool Inside(double v, double a, double b)
        {
            return (std::min)(a, b) < v && v < (std::max)(a, b);
        }
    }

    double BlockSize(double minSide, double dpr, int size)
    {
        double b0 = std::round(minSide / 6.0);
        if (dpr * 5.0 <= b0)
        {
            b0 = dpr * 5.0;
        }
        return std::round(b0 * std::pow(1.35, static_cast<double>(size)));
    }

    std::vector<Cell> Grid(Rect const& area, double block, Rng const& rng)
    {
        std::vector<Cell> out;
        double const b = (std::max)(2.0, block);
        double const b3 = b * 3.0;
        double const cols = std::ceil(area.width / b3 * 0.5);
        double const rows = std::ceil(area.height / b3 * 0.5);
        if (!(cols >= 1) || !(rows >= 1))
        {
            return out;
        }
        double const left = area.Left();
        double const top = area.Top();
        std::vector<Cell> stack;
        for (long long i = 0; i < static_cast<long long>(cols); ++i)
        {
            double const x0 = std::round(left + area.width / cols * static_cast<double>(i));
            double const x1 = std::round(left + area.width / cols * static_cast<double>(i + 1));
            for (long long j = 0; j < static_cast<long long>(rows); ++j)
            {
                double const y0 = std::round(top + area.height / rows * static_cast<double>(j));
                double const y1 = std::round(top + area.height / rows * static_cast<double>(j + 1));
                stack.push_back({ x0, y0, x1 - x0, y1 - y0 });
            }
        }
        double const b2 = b + b;
        while (!stack.empty())
        {
            Cell const c = stack.back();
            stack.pop_back();
            double w = c.w;
            if (b2 < c.w)
            {
                w = (std::min)(b3, b + std::round((c.w - b2) * rng()));
            }
            double h = c.h;
            if (b2 < c.h)
            {
                h = (std::min)(b3, b + std::round((c.h - b2) * rng()));
            }
            out.push_back({ c.x, c.y, w, h });
            if (w != c.w)
            {
                stack.push_back({ c.x + w, c.y, c.w - w, h });
            }
            if (h != c.h)
            {
                stack.push_back({ c.x, c.y + h, w, c.h - h });
                if (w != c.w)
                {
                    stack.push_back({ c.x + w, c.y + h, c.w - w, c.h - h });
                }
            }
        }
        return out;
    }

    void Pixelate(Bitmap const& source, Rect const& area, int size, double dpr, Rng const& rng, Bitmap& out, Point outOrigin)
    {
        if (source.IsEmpty() || area.width <= 0 || area.height <= 0)
        {
            return;
        }
        double const minSide = (std::min)(area.width, area.height);
        double const block = BlockSize(minSide, dpr, size);
        std::vector<Cell> const cells = Grid(area, block, rng);
        bool const scatter = minSide / 7.0 < block;
        for (Cell const& cell : cells)
        {
            int const k = static_cast<int>((std::min)(4.0, std::floor(rng() * 5.0)));
            Cell pick = cell;
            if (scatter && k < 4)
            {
                // The neighbour across one edge.
                Point probe;
                switch (k)
                {
                case 0: probe = { cell.x + cell.w / 2, cell.y - 1.0 }; break;
                case 1: probe = { cell.x + cell.w + 1.0, cell.y + cell.h / 2 }; break;
                case 2: probe = { cell.x + cell.w / 2, cell.y + cell.h + 1.0 }; break;
                default: probe = { cell.x - 1.0, cell.y + cell.h / 2 }; break;
                }
                for (Cell const& other : cells)
                {
                    if (Inside(probe.x, other.x, other.x + other.w) && Inside(probe.y, other.y, other.y + other.h))
                    {
                        pick = other;
                    }
                }
            }
            Rgba const c = Average(source, pick);
            long long const x0 = static_cast<long long>(cell.x - outOrigin.x);
            long long const y0 = static_cast<long long>(cell.y - outOrigin.y);
            for (long long y = y0; y < y0 + static_cast<long long>(cell.h); ++y)
            {
                for (long long x = x0; x < x0 + static_cast<long long>(cell.w); ++x)
                {
                    Put(out, x, y, c);
                }
            }
        }
    }

    void EraseCosts(Bitmap const& source, Rect const& area, double& horizontal, double& vertical)
    {
        long long const x0 = static_cast<long long>(area.Left());
        long long const y0 = static_cast<long long>(area.Top());
        long long const w = static_cast<long long>(area.width);
        long long const h = static_cast<long long>(area.height);
        double rowsDiff = 0, colsDiff = 0, topBottom = 0, leftRight = 0;
        for (long long y = 0; y < h; ++y)
        {
            rowsDiff += Diff(Pixel(source, x0, y0 + y), Pixel(source, x0 + w - 1, y0 + y));
        }
        for (long long x = 0; x < w; ++x)
        {
            colsDiff += Diff(Pixel(source, x0 + x, y0), Pixel(source, x0 + x, y0 + h - 1));
        }
        for (long long x = 0; x + 1 < w; ++x)
        {
            topBottom += Diff(Pixel(source, x0 + x, y0), Pixel(source, x0 + x + 1, y0));
            topBottom += Diff(Pixel(source, x0 + x, y0 + h - 1), Pixel(source, x0 + x + 1, y0 + h - 1));
        }
        for (long long y = 0; y + 1 < h; ++y)
        {
            leftRight += Diff(Pixel(source, x0, y0 + y), Pixel(source, x0, y0 + y + 1));
            leftRight += Diff(Pixel(source, x0 + w - 1, y0 + y), Pixel(source, x0 + w - 1, y0 + y + 1));
        }
        double const hd = static_cast<double>((std::max)(h, 1LL));
        double const wd = static_cast<double>((std::max)(w, 1LL));
        horizontal = rowsDiff / hd + 0.5 * topBottom / wd;
        vertical = colsDiff / wd + 0.5 * leftRight / hd;
    }

    void Erase(Bitmap const& source, Rect const& area, Bitmap& out, Point outOrigin)
    {
        if (source.IsEmpty() || area.width < 1 || area.height < 1)
        {
            return;
        }
        double hc = 0, vc = 0;
        EraseCosts(source, area, hc, vc);
        long long const x0 = static_cast<long long>(area.Left());
        long long const y0 = static_cast<long long>(area.Top());
        long long const w = static_cast<long long>(area.width);
        long long const h = static_cast<long long>(area.height);
        long long const ox = static_cast<long long>(area.Left() - outOrigin.x);
        long long const oy = static_cast<long long>(area.Top() - outOrigin.y);
        auto lerp = [](Rgba a, Rgba b, double t) {
            auto mix = [t](int p, int q) { return static_cast<int>(std::lround(p + (q - p) * t)); };
            return Rgba{ mix(a.b, b.b), mix(a.g, b.g), mix(a.r, b.r), mix(a.a, b.a) };
        };
        if (hc <= vc)
        {
            for (long long y = 0; y < h; ++y)
            {
                Rgba const l = Pixel(source, x0, y0 + y);
                Rgba const r = Pixel(source, x0 + w - 1, y0 + y);
                for (long long x = 0; x < w; ++x)
                {
                    Put(out, ox + x, oy + y, lerp(l, r, w > 1 ? static_cast<double>(x) / static_cast<double>(w - 1) : 0.0));
                }
            }
        }
        else
        {
            for (long long x = 0; x < w; ++x)
            {
                Rgba const t = Pixel(source, x0 + x, y0);
                Rgba const b = Pixel(source, x0 + x, y0 + h - 1);
                for (long long y = 0; y < h; ++y)
                {
                    Put(out, ox + x, oy + y, lerp(t, b, h > 1 ? static_cast<double>(y) / static_cast<double>(h - 1) : 0.0));
                }
            }
        }
    }

    Rect ClampArea(Rect const& rect, double imageWidth, double imageHeight)
    {
        Rect const s = rect.Standardized();
        double l = std::clamp(std::round(s.Left()), 0.0, imageWidth);
        double t = std::clamp(std::round(s.Top()), 0.0, imageHeight);
        double r = std::clamp(std::round(s.Right()), 0.0, imageWidth);
        double b = std::clamp(std::round(s.Bottom()), 0.0, imageHeight);
        double w = r - l;
        double h = b - t;
        if (w <= 0) w = 1;
        if (h <= 0) h = 1;
        if (l + w > imageWidth) l = (std::max)(0.0, imageWidth - w);
        if (t + h > imageHeight) t = (std::max)(0.0, imageHeight - h);
        return { l, t, w, h };
    }

    Bitmap Generate(Bitmap const& source, Rect const& rect, std::wstring_view fillMode, int size, double dpr,
                    std::vector<Rect> const& textBoxes, Rng const& rng)
    {
        Rect const area = ClampArea(rect, source.width, source.height);
        Bitmap out = Bitmap::Create(static_cast<std::int32_t>(area.width), static_cast<std::int32_t>(area.height), source.dpr);
        std::fill(out.pixels.begin(), out.pixels.end(), std::uint8_t{ 0 });
        bool const textOnly = fillMode.size() > 5 && fillMode.substr(fillMode.size() - 5) == L"-text";
        bool const erase = fillMode.rfind(L"erase", 0) == 0;
        auto apply = [&](Rect const& a) {
            if (a.width < 1 || a.height < 1) return;
            if (erase) Erase(source, a, out, area.Origin());
            else Pixelate(source, a, size, dpr, rng, out, area.Origin());
        };
        if (!textOnly)
        {
            apply(area);
            return out;
        }
        for (Rect const& box : textBoxes)
        {
            // Each recognised box grows by 2 px, clipped to the object.
            Rect grown{ std::trunc(box.x + area.x) - 2.0, std::trunc(box.y + area.y) - 2.0, box.width + 4.0, box.height + 4.0 };
            apply(grown.Intersection(area));
        }
        return out;
    }
}
