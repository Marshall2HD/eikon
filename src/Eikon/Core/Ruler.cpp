#include "pch.h"
#include "Core/Ruler.h"

#include "Core/Bitmap.h"

#include <charconv>
#include <cmath>

namespace Eikon::Core::Ruler
{
    namespace
    {
        std::int64_t Trunc(double value) noexcept
        {
            if (!(value > -9.2e18 && value < 9.2e18))
            {
                return 0;
            }
            return static_cast<std::int64_t>(value);
        }

        std::int64_t Diff(std::uint8_t const* p, std::uint8_t const* q) noexcept
        {
            return std::abs(int{ p[0] } - int{ q[0] }) + std::abs(int{ p[1] } - int{ q[1] }) + std::abs(int{ p[2] } - int{ q[2] });
        }
    }

    std::int64_t Threshold(double sensitivity) noexcept
    {
        double const twice = sensitivity * sensitivity;
        return Trunc(twice + twice);
    }

    double AdjustSensitivity(double sensitivity, double deltaY) noexcept
    {
        double value = sensitivity + deltaY;
        if (value < 0.0)
        {
            value = 0.0;
        }
        return (std::min)(value, kMaxSensitivity);
    }

    Run Scout(Bitmap const& image, Rect const& crop, std::int64_t x, std::int64_t row, bool vertical,
              std::int64_t persistence, double sensitivity)
    {
        if (image.IsEmpty())
        {
            return {};   // "No pixel data"
        }
        std::int64_t const cropX = Trunc(crop.x);
        std::int64_t const cropY = Trunc(crop.y);
        std::int64_t const limit = vertical ? Trunc(crop.height) : Trunc(crop.width);
        std::int64_t const threshold = Threshold(sensitivity);

        // Pixel i on the scanned line; nullptr outside the buffer.
        auto pixel = [&](std::int64_t i) -> std::uint8_t const* {
            std::int64_t const column = cropX + (vertical ? x : i);
            std::int64_t const line = cropY + (vertical ? i : row);
            if (column < 0 || line < 0 || column >= image.width || line >= image.height)
            {
                return nullptr;
            }
            return image.pixels.data() + (static_cast<std::size_t>(line) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(column)) * 4;
        };
        static constexpr std::uint8_t kNone[4]{};
        auto at = [&](std::int64_t i) {
            auto const* p = pixel(i);
            return p ? p : kNone;
        };

        std::int64_t const cursor = vertical ? row : x;
        auto walk = [&](std::int64_t direction) -> std::int64_t {
            std::int64_t budget = persistence;
            std::uint8_t const* previous = at(cursor);
            std::int64_t i = cursor;
            while (true)
            {
                i += direction;
                if (i < 0 || i >= limit)
                {
                    return direction < 0 ? 0 : limit;
                }
                std::uint8_t const* current = at(i);
                std::int64_t const d = Diff(current, previous);
                previous = current;
                if (d > threshold)
                {
                    std::int64_t look = 0;
                    if (i < limit - 1 && i != 0)
                    {
                        look = Diff(at(i + direction), current);
                    }
                    if (look < Trunc(static_cast<double>(d) + static_cast<double>(d)))
                    {
                        if (budget == 0)
                        {
                            return direction < 0 ? i + 1 : i;
                        }
                        budget -= 1;
                    }
                }
            }
        };
        Run run;
        run.start = walk(-1);
        run.end = walk(+1);
        return run;
    }

    std::optional<Measurement> Measure(Bitmap const& image, Rect const& crop, Point cursor, bool vertical,
                                       std::int64_t persistence, bool guide, double sensitivity)
    {
        double const x = static_cast<double>(Trunc(cursor.x));
        double const y = static_cast<double>(Trunc(cursor.y));
        double const width = crop.width;
        double const height = crop.height;
        if (!(x >= 0.0 && y >= 0.0 && x < width && y < height))
        {
            return std::nullopt;
        }
        Measurement m{};
        if (!vertical)
        {
            double start = 0;
            double end = width;
            if (!guide)
            {
                // The row through the cursor: y-up y is top-down row H − 1 − y.
                Run const run = Scout(image, crop, static_cast<std::int64_t>(x), Trunc(height) - 1 - static_cast<std::int64_t>(y), false, persistence, sensitivity);
                start = static_cast<double>(run.start);
                end = static_cast<double>(run.end);
            }
            m.rect = { start, y, end - start, 0 };
        }
        else
        {
            double start = 0;
            double end = height;
            if (!guide)
            {
                Run const run = Scout(image, crop, static_cast<std::int64_t>(x), Trunc(height) - 1 - static_cast<std::int64_t>(y), true, persistence, sensitivity);
                // Top-down rows back to y-up.
                start = height - static_cast<double>(run.end);
                end = height - static_cast<double>(run.start);
            }
            m.rect = { x, start, 0, end - start };
        }
        m.value = vertical ? m.rect.height : m.rect.width;
        return m;
    }

    std::wstring ShortestDescription(double value)
    {
        if (std::isnan(value))
        {
            return L"nan";
        }
        if (std::isinf(value))
        {
            return value < 0 ? L"-inf" : L"inf";
        }
        char buffer[64];
        double const magnitude = std::abs(value);
        bool const exponential = magnitude != 0.0 && (magnitude < 1e-4 || magnitude >= 1e16);
        auto const result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                          exponential ? std::chars_format::scientific : std::chars_format::fixed);
        std::wstring text(buffer, result.ptr);
        if (!exponential && text.find(L'.') == std::wstring::npos)
        {
            text += L".0";
        }
        return text;
    }

    std::wstring LabelText(double value, double dpr, bool realPixels)
    {
        if (realPixels)
        {
            return std::to_wstring(Trunc(value)) + L"px";
        }
        double const points = value / dpr;
        if (points == static_cast<double>(Trunc(points)))
        {
            return std::to_wstring(Trunc(points)) + L"pt";
        }
        return ShortestDescription(points) + L"pt";
    }

    std::wstring ObjectLabelText(double length, double dpr)
    {
        double const v = length / dpr;
        std::wstring number = v == static_cast<double>(Trunc(v)) ? std::to_wstring(Trunc(v)) : ShortestDescription(v);
        return number + (dpr == 1.0 ? L"px" : L"pt");
    }

    Layout LayoutLive(Rect const& rect, bool vertical, bool guide, Point cursor, ViewMap const& map, Size textSize)
    {
        double const dpr = map.scale;
        double const zoom = map.zoom;
        Point const origin = map.ToView({ rect.x, rect.y });
        double const w = rect.width * zoom;
        double const h = rect.height * zoom;

        // Crisp offsets: half a pixel across the axis, and another half point at dpr 1.
        double const halfX = vertical ? 0.5 : 0.0;
        double const halfY = vertical ? 0.0 : 0.5;
        double const extraX = (vertical && dpr == 1.0) ? 0.5 : 0.0;
        double const extraY = (!vertical && dpr == 1.0) ? 0.5 : 0.0;
        double const X = static_cast<double>(Trunc(origin.x + zoom * halfX)) / dpr - halfX - extraX;
        double const Y = static_cast<double>(Trunc(origin.y + zoom * halfY)) / dpr - halfY - extraY;
        double const W = (std::max)(1.0, static_cast<double>(Trunc(w)) / dpr);
        double const H = (std::max)(1.0, static_cast<double>(Trunc(h)) / dpr);

        Layout layout;
        layout.bars.push_back({ X, Y, W, H });
        if (guide)
        {
            return layout;
        }
        if (vertical)
        {
            layout.bars.push_back({ X - 3, Y, 7, 1 });
            layout.bars.push_back({ X - 3, Y + H - 1, 7, 1 });
        }
        else
        {
            layout.bars.push_back({ X, Y - 3, 1, 7 });
            layout.bars.push_back({ X + W - 1, Y - 3, 1, 7 });
        }

        // The cursor pixel mapped back to the view.
        Point const pixel = map.ToView({ static_cast<double>(Trunc(cursor.x)), static_cast<double>(Trunc(cursor.y)) });
        double labelX = 0;
        double labelY = 0;
        if (!vertical)
        {
            labelY = Y + 10.0;
            if (map.area.height / dpr <= Y + 10.0 + 20.0)
            {
                labelY = Y - 10.0 - 20.0;
            }
            labelX = static_cast<double>(Trunc(pixel.x)) / dpr - 20.0;
        }
        else
        {
            labelX = X + 20.0;
            if (map.area.width / dpr <= X + 20.0 + 40.0)
            {
                labelX = X - 20.0 - 40.0;
            }
            labelY = static_cast<double>(Trunc(pixel.y)) / dpr - 10.0;
        }
        layout.bubble = Rect{ labelX, labelY, textSize.width + 8.0, textSize.height + 4.0 };
        layout.text = Rect{ labelX + 4.0, labelY + 2.0, textSize.width, textSize.height };
        return layout;
    }
}
