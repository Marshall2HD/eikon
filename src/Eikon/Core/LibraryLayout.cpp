#include "pch.h"
#include "Core/LibraryLayout.h"

#include <cmath>

namespace Eikon::Core::LibraryLayout
{
    namespace
    {
        constexpr double kGap = 12;   // every gap except the list's line spacing

        // Truncation toward zero.
        double Trunc(double value) noexcept
        {
            return std::isfinite(value) ? std::trunc(value) : 0.0;
        }

        double Clamp(double value, double low, double high) noexcept
        {
            return value < low ? low : (value > high ? high : value);
        }

        double Ratio(Size const& image) noexcept   // h / w
        {
            return image.width > 0 ? image.height / image.width : 1.0;
        }

        double Aspect(Size const& image) noexcept  // w / h
        {
            return image.height > 0 ? image.width / image.height : 1.0;
        }

        // The `uncropped` sizes: scale the image to cover the cell (max of the two scales).
        Size Fill(Size const& image, Size const& cell) noexcept
        {
            if (image.width <= 0 || image.height <= 0)
            {
                return cell;
            }
            double const scale = (std::max)(cell.width / image.width, cell.height / image.height);
            return { Trunc(scale * image.width), Trunc(scale * image.height) };
        }

        void List(double width, std::vector<Size> const& images, Result& result)
        {
            result.itemsPerRow = 1;
            result.lineSpacing = 0;
            for (std::size_t i = 0; i < images.size(); ++i)
            {
                result.sizes.push_back({ width, 24 });
                result.uncropped.push_back({ width, 24 });
            }
            result.contentHeight = static_cast<double>(images.size()) * 24.0;
        }

        // (W, 80).
        void Square(double width, double side, std::vector<Size> const& images, Result& result)
        {
            double perRow = Trunc(width / (side + kGap));
            if (perRow < 1)
            {
                perRow = 1;   // below 92 pt; the popover is never that narrow
            }
            result.itemsPerRow = static_cast<int>(perRow);
            double const cell = Trunc((width - (perRow - 1) * kGap) / perRow);
            for (auto const& image : images)
            {
                result.sizes.push_back({ cell, cell });
                result.uncropped.push_back(Fill(image, { cell, cell }));
            }
            double const rows = Trunc(static_cast<double>(images.size()) / perRow);   // floor
            result.contentHeight = cell * rows + (rows - 1) * kGap;
        }

        void Single(double width, std::vector<Size> const& images, Result& result)
        {
            result.itemsPerRow = 1;
            double total = 0;
            for (auto const& image : images)
            {
                double const height = (std::min)(Trunc(width * Ratio(image)), Trunc(width * 1.2));
                Size const cell{ width, height };
                result.sizes.push_back(cell);
                result.uncropped.push_back(Fill(image, cell));
                total += height + kGap;
            }
            result.contentHeight = (std::max)(0.0, total - kGap);
        }

        // Images in pairs; the row width W - 12 is split in sixths.
        void Pairs(double width, std::vector<Size> const& images, Result& result)
        {
            result.itemsPerRow = 2;
            double const rowWidth = width - kGap;
            double const sixth = Trunc(rowWidth / 6.0);
            double total = 0;
            std::size_t const count = images.size();
            for (std::size_t i = 0; i < count; ++i)
            {
                if (i % 2 == 0)
                {
                    if (i == count - 1)
                    {
                        // A final odd image alone, at most 180 high.
                        double const ratio = Ratio(images[i]);
                        double const natural = width * ratio;
                        double const height = Trunc((std::min)(natural, 180.0));
                        Size const cell{ Trunc(height / ratio), height };
                        result.sizes.push_back(cell);
                        result.uncropped.push_back(cell);
                        total += Trunc(natural) + kGap;   // the uncapped height
                    }
                    continue;
                }
                double const first = Clamp(Ratio(images[i - 1]), 0.33, 3.0);
                double const second = Clamp(Ratio(images[i]), 0.33, 3.0);
                // Sixths for the second image, chosen so the two heights match best (1 to 5).
                double sixths = sixth > 0 ? Trunc(Trunc(rowWidth / (second / first + 1.0)) / sixth) : 1.0;
                sixths = Clamp(sixths, 1.0, 5.0);
                double const secondWidth = sixth * sixths;
                double const firstWidth = Trunc(rowWidth - secondWidth);
                double const height = (std::min)(Trunc(first * firstWidth), 340.0);
                Size const firstCell{ firstWidth, height };
                Size const secondCell{ secondWidth, height };
                result.sizes.push_back(firstCell);
                result.sizes.push_back(secondCell);
                result.uncropped.push_back(Fill(images[i - 1], firstCell));
                result.uncropped.push_back(Fill(images[i], secondCell));
                total += height + kGap;
            }
            result.contentHeight = (std::max)(0.0, total - kGap);
        }

        // One justified row (complete or the trailing one).
        void JustifiedRow(double rowWidth, double unit, double maxUnits, std::vector<double> const& ratios, std::size_t columns,
                          std::vector<Size> const& images, std::size_t firstIndex, Result& result)
        {
            double sum = 0;
            for (std::size_t j = 0; j < columns; ++j)
            {
                sum += ratios[j];
            }
            double used = 0;
            for (std::size_t j = 0; j < columns; ++j)
            {
                double const ideal = rowWidth * ratios[j] / sum;
                double const units = (std::min)(Trunc(ideal / unit), maxUnits);
                bool const last = j == columns - 1;
                double cellWidth = units >= 1 ? unit * units : unit;
                if (last)
                {
                    cellWidth = (std::max)(rowWidth - used, unit);
                }
                Size const cell{ cellWidth, Trunc(ideal / ratios[j]) };
                result.sizes.push_back(cell);
                result.uncropped.push_back(Fill(images[firstIndex + j], cell));
                used += cellWidth;
            }
        }

        // (W, 3): rows of three whose widths sum to W - 24.
        void Justified(double width, std::size_t perRow, std::vector<Size> const& images, Result& result)
        {
            result.itemsPerRow = static_cast<int>(perRow);
            double const rowWidth = width - static_cast<double>(perRow - 1) * kGap;
            double const units = static_cast<double>(perRow * 6);
            double const unit = Trunc(rowWidth / units);
            double const maxUnits = units - static_cast<double>(perRow - 1);
            std::vector<double> ratios(perRow, 0.0);
            std::size_t const count = images.size();
            for (std::size_t k = 0; k < count; ++k)
            {
                std::size_t const column = k % perRow;
                ratios[column] = Clamp(Aspect(images[k]), 0.33, 3.0);
                if (column == perRow - 1)
                {
                    JustifiedRow(rowWidth, unit, maxUnits, ratios, perRow, images, k + 1 - perRow, result);
                }
            }
            std::size_t const rest = count % perRow;
            if (rest != 0)
            {
                // The trailing row gets the share of the width its item count would take in a full row.
                double const partial = rowWidth * static_cast<double>(rest) / static_cast<double>(perRow);
                JustifiedRow(partial, unit, maxUnits, ratios, rest, images, count - rest, result);
            }
            result.contentHeight = 0;   // the flow layout sizes the view
        }
    }

    double MediumHeight(double visibleScreenHeight) noexcept
    {
        return (std::max)(kSmallHeight, visibleScreenHeight - 100.0);
    }

    Kind KindFor(std::wstring_view libLayout, std::wstring_view popoverSize) noexcept
    {
        bool const medium = popoverSize == L"medium";
        if (libLayout == L"square80")
        {
            return Kind::Square80;
        }
        if (libLayout == L"list")
        {
            return Kind::List;
        }
        if (libLayout == L"large")
        {
            return medium ? Kind::Pairs : Kind::Single;
        }
        return medium ? Kind::Justified3 : Kind::Pairs;
    }

    Result Compute(Kind kind, double width, std::vector<Size> const& images)
    {
        Result result;
        result.sizes.reserve(images.size());
        result.uncropped.reserve(images.size());
        switch (kind)
        {
        case Kind::List: List(width, images, result); break;
        case Kind::Square80: Square(width, 80, images, result); break;
        case Kind::Single: Single(width, images, result); break;
        case Kind::Pairs: Pairs(width, images, result); break;
        case Kind::Justified3: Justified(width, 3, images, result); break;
        }
        return result;
    }

    Flow Place(std::vector<Size> const& sizes, double viewWidth, double lineSpacing)
    {
        Flow flow;
        flow.frames.resize(sizes.size());
        double const available = viewWidth - kInsetLeft - kInsetRight;
        double y = kInsetTop;
        std::size_t i = 0;
        bool firstRow = true;
        while (i < sizes.size())
        {
            // Collect one row.
            std::size_t end = i;
            double sum = 0;
            while (end < sizes.size())
            {
                double const next = sum + (end > i ? kInteritemSpacing : 0) + sizes[end].width;
                if (end > i && next > available + 0.001)
                {
                    break;
                }
                sum = next;
                ++end;
            }
            std::size_t const n = end - i;
            double widths = 0;
            double rowHeight = 0;
            for (std::size_t k = i; k < end; ++k)
            {
                widths += sizes[k].width;
                rowHeight = (std::max)(rowHeight, sizes[k].height);
            }
            if (!firstRow)
            {
                y += lineSpacing;
            }
            firstRow = false;
            flow.rowStarts.push_back(i);
            double x = kInsetLeft;
            double gap = kInteritemSpacing;
            if (n == 1)
            {
                x = kInsetLeft + (std::max)(0.0, (available - widths) / 2);
            }
            else
            {
                gap = (available - widths) / static_cast<double>(n - 1);
            }
            for (std::size_t k = i; k < end; ++k)
            {
                flow.frames[k] = { x, y + (rowHeight - sizes[k].height) / 2, sizes[k].width, sizes[k].height };
                x += sizes[k].width + gap;
            }
            y += rowHeight;
            i = end;
        }
        flow.height = y + kInsetBottom;
        return flow;
    }
}
