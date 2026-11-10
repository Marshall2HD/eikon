#include "pch.h"
#include "Core/Contrast.h"

#include "Core/Bitmap.h"
#include "Core/Ruler.h"

#include <cmath>

namespace Eikon::Core::Contrast
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

        double Linear(std::uint8_t byte) noexcept
        {
            double const c = byte / 255.0;
            if (0.04045 <= c)
            {
                return std::pow((c + 0.055) / 1.055, 2.4);
            }
            return c / 12.92;
        }

        double Luminance(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept
        {
            double const l = (std::min)(Linear(r) * 0.2126 + Linear(g) * 0.7152 + Linear(b) * 0.0722, 1.0);
            return l < 0.0 ? 0.0 : l;
        }
    }

    double WcagLuminance(Color color) noexcept
    {
        return Luminance(color.r, color.g, color.b);
    }

    double WcagRatio(Color lighter, Color darker) noexcept
    {
        return (WcagLuminance(lighter) + 0.05) / (WcagLuminance(darker) + 0.05);
    }

    double RoundWcag(double ratio) noexcept
    {
        double const scale = ratio < 1.5 ? 100.0 : 10.0;
        return frinta(ratio * scale) / scale;
    }

    double ApcaLuminance(Color color) noexcept
    {
        return std::pow(color.r / 255.0, 2.4) * 0.2126729 + std::pow(color.g / 255.0, 2.4) * 0.7151522 +
               std::pow(color.b / 255.0, 2.4) * 0.072175;
    }

    double ApcaContrast(double textY, double backgroundY) noexcept
    {
        // Soft clamp near black (blkThrs 0.022, blkClmp 1.414).
        if (textY <= 0.022)
        {
            textY += std::pow(0.022 - textY, 1.414);
        }
        if (backgroundY <= 0.022)
        {
            backgroundY += std::pow(0.022 - backgroundY, 1.414);
        }
        if (std::abs(backgroundY - textY) < 0.0005)
        {
            return 0.0;
        }
        double lc = 0.0;
        if (backgroundY > textY)
        {
            // Dark text on a light background: normBG 0.56, normTXT 0.57.
            double const s = (std::pow(backgroundY, 0.56) - std::pow(textY, 0.57)) * 1.14;
            lc = s < 0.1 ? 0.0 : s - 0.027;
        }
        else
        {
            // Light text on a dark background: revBG 0.65, revTXT 0.62.
            double const s = (std::pow(backgroundY, 0.65) - std::pow(textY, 0.62)) * 1.14;
            lc = s > -0.1 ? 0.0 : s + 0.027;
        }
        return lc * 100.0;
    }

    double RoundApca(double lc) noexcept
    {
        return frinta(lc * 10.0) / 10.0;
    }

    Result Measure(Bitmap const& image, Rect const& crop, Rect const& selection)
    {
        Color lightest{ 0, 0, 0, 255 };
        Color darkest{ 255, 255, 255, 255 };
        double lightestL = 0.0;
        double darkestL = 1.0;

        Rect const area = selection.Standardized().Intersection(Rect{ 0, 0, crop.width, crop.height });
        double const minX = area.x;
        double const maxX = area.x + area.width;
        double const minY = area.y;
        double const maxY = area.y + area.height;
        if (!image.IsEmpty() && minX < maxX && minY < maxY)
        {
            double const step = (std::max)(1.0, std::sqrt(area.width * area.height / 640000.0));
            std::int64_t const cropX = Trunc(crop.x);
            std::int64_t const bottomRow = Trunc(crop.y) + Trunc(crop.height) - 1;
            double x = minX;
            for (std::int64_t i = 1;; ++i)
            {
                std::int64_t const column = cropX + Trunc(x);
                double y = minY;
                for (std::int64_t j = 1; y < maxY; ++j)
                {
                    std::int64_t const row = bottomRow - Trunc(y);
                    y = minY + step * static_cast<double>(j);
                    if (column < 0 || row < 0 || column >= image.width || row >= image.height)
                    {
                        continue;
                    }
                    std::uint8_t const* p = image.pixels.data() +
                        (static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(column)) * 4;
                    double const l = Luminance(p[2], p[1], p[0]);
                    if (l < darkestL)
                    {
                        darkestL = l;
                        darkest = Color{ p[2], p[1], p[0], 255 };
                    }
                    if (l > lightestL)
                    {
                        lightestL = l;
                        lightest = Color{ p[2], p[1], p[0], 255 };
                    }
                }
                x = minX + step * static_cast<double>(i);
                if (maxX <= x)
                {
                    break;
                }
            }
        }

        // Both are converted to sRGB (the identity on Windows) before the ratios.
        Result result;
        result.wcag = RoundWcag(WcagRatio(lightest, darkest));
        result.apca = RoundApca(ApcaContrast(ApcaLuminance(darkest), ApcaLuminance(lightest)));
        return result;
    }

    std::wstring BadgeText(Result const& result, std::wstring_view contrastType)
    {
        if (contrastType == L"apca")
        {
            return L"A: " + Ruler::ShortestDescription(result.apca);
        }
        return L"W: " + Ruler::ShortestDescription(result.wcag);
    }

    BadgeLayout LayoutBadge(Rect const& selection, Size textSize) noexcept
    {
        double const minX = (std::min)(selection.x, selection.x + selection.width);
        double const minY = (std::min)(selection.y, selection.y + selection.height);
        double const centreX = (minX + std::abs(selection.width) + minX) * 0.5;
        double const top = minY + std::abs(selection.height);   // y up: the selection's upper edge

        BadgeLayout layout;
        layout.text = { static_cast<double>(Trunc(centreX - textSize.width * 0.5)), static_cast<double>(Trunc(top + 8.0)),
                        textSize.width, textSize.height };
        layout.background = { static_cast<double>(Trunc(layout.text.x - 5.0)), static_cast<double>(Trunc(top + 8.0 - 3.0)),
                              textSize.width + 10.0, textSize.height + 6.0 };
        return layout;
    }
}
