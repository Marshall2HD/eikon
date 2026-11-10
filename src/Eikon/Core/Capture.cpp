#include "pch.h"
#include "Core/Capture.h"

#include <cmath>
#include <cstring>

namespace Eikon::Core
{
    namespace
    {
        constexpr std::uint8_t kAlphaThreshold = 150;   // 0x96
        constexpr int kMaxScan = 300;

        std::uint8_t AlphaAt(Bitmap const& image, int x, int y) noexcept
        {
            return image.pixels[static_cast<std::size_t>(y) * image.Stride() + static_cast<std::size_t>(x) * 4 + 3];
        }

        // Source-over of a premultiplied BGRA pixel.
        void Blend(std::uint8_t* dst, std::uint8_t const* src) noexcept
        {
            unsigned const inverse = 255u - src[3];
            for (int c = 0; c < 4; ++c)
            {
                dst[c] = static_cast<std::uint8_t>(src[c] + (dst[c] * inverse + 127) / 255);
            }
        }

        // Draws `image` onto `canvas` (same width) moved down by `shift` rows, clipped.
        void DrawShifted(Bitmap& canvas, Bitmap const& image, int shift) noexcept
        {
            for (int y = 0; y < image.height; ++y)
            {
                int const ty = y + shift;
                if (ty < 0 || ty >= canvas.height)
                {
                    continue;
                }
                std::uint8_t const* src = image.pixels.data() + static_cast<std::size_t>(y) * image.Stride();
                std::uint8_t* dst = canvas.pixels.data() + static_cast<std::size_t>(ty) * canvas.Stride();
                int const width = (std::min)(image.width, canvas.width);
                for (int x = 0; x < width; ++x)
                {
                    Blend(dst + x * 4, src + x * 4);
                }
            }
        }

        Bitmap BlankLike(Bitmap const& image)
        {
            Bitmap canvas;
            canvas.width = image.width;
            canvas.height = image.height;
            canvas.dpr = image.dpr;
            canvas.screenRect = image.screenRect;
            canvas.pixels.assign(image.Stride() * static_cast<std::size_t>(image.height), 0);
            return canvas;
        }

        // Truncation toward zero.
        int Trunc(double value) noexcept
        {
            return static_cast<int>(std::trunc(value));
        }
    }

    std::optional<ShadowMode> ParseShadowMode(std::wstring_view value) noexcept
    {
        if (value == L"transparent") return ShadowMode::Transparent;
        if (value == L"trimmed") return ShadowMode::Trimmed;
        if (value == L"wallpaper") return ShadowMode::Wallpaper;
        if (value == L"solid") return ShadowMode::Solid;
        return std::nullopt;
    }

    ShadowPaddings ComputeShadowPaddings(Bitmap const& image) noexcept
    {
        ShadowPaddings paddings;
        if (image.IsEmpty())
        {
            return paddings;
        }
        // The scan walks the raw buffer with byte offsets and stops at its end, so a scan longer than a
        // row runs into the neighbouring row.
        auto const total = static_cast<std::int64_t>(image.Stride()) * image.height;
        auto const stride = static_cast<std::int64_t>(image.Stride());
        auto opaqueAt = [&](std::int64_t offset, bool& outside) {
            if (offset < 0 || offset + 3 >= total)
            {
                outside = true;
                return false;
            }
            return image.pixels[static_cast<std::size_t>(offset + 3)] > kAlphaThreshold;
        };
        // Index of the first opaque pixel along `step` from `start`; the count reached when the buffer
        // ends; 0 when nothing opaque is within 300 px. Padding = max(index, 1) - 1.
        auto scan = [&](std::int64_t start, std::int64_t step) {
            int index = 0;
            for (; index < kMaxScan; ++index)
            {
                bool outside = false;
                if (opaqueAt(start + step * index, outside) || outside)
                {
                    break;
                }
            }
            if (index == kMaxScan)
            {
                index = 0;
            }
            return (std::max)(index, 1) - 1;
        };

        std::int64_t const middleRow = static_cast<std::int64_t>(image.height / 2) * stride;
        paddings.left = scan(middleRow, 4);
        paddings.right = scan(middleRow + static_cast<std::int64_t>(image.width - 1) * 4, -4);
        int const column = paddings.left + (image.width - paddings.left - paddings.right) / 2;
        paddings.top = scan(static_cast<std::int64_t>(column) * 4, stride);
        paddings.bottom = scan(static_cast<std::int64_t>(image.height - 1) * stride + static_cast<std::int64_t>(column) * 4, -stride);
        return paddings;
    }

    Bitmap TrimShadow(Bitmap image)
    {
        auto const p = ComputeShadowPaddings(image);
        if (p.left + p.right < image.width && p.top + p.bottom < image.height)
        {
            return CropBitmap(image, p.left, p.top, image.width - p.left - p.right, image.height - p.top - p.bottom);
        }
        return image;
    }

    bool IsEdgeTransparent(Bitmap const& image) noexcept
    {
        if (image.IsEmpty())
        {
            return false;
        }
        int const x = image.width / 2;
        return AlphaAt(image, x, 0) != 255 || AlphaAt(image, x, image.height - 1) != 255;
    }

    Bitmap AddWallpaper(Bitmap image, Bitmap const& wallpaper)
    {
        if (image.IsEmpty() || !IsEdgeTransparent(image) || wallpaper.IsEmpty())
        {
            return image;
        }
        int const w = image.width;
        int const h = image.height;
        // The centred W×H crop, intersected with the wallpaper and made integral like
        // Cropped, then drawn stretched into (0, 0, W, H).
        Rect const wanted{ (wallpaper.width - w) / 2.0, (wallpaper.height - h) / 2.0, static_cast<double>(w), static_cast<double>(h) };
        Rect crop = wanted.Intersection(Rect{ 0, 0, static_cast<double>(wallpaper.width), static_cast<double>(wallpaper.height) });
        if (crop.IsEmpty())
        {
            return image;
        }
        double const left = std::floor(crop.x);
        double const top = std::floor(crop.y);
        crop = Rect{ left, top, std::ceil(crop.x + crop.width) - left, std::ceil(crop.y + crop.height) - top };

        Bitmap canvas = BlankLike(image);
        for (int y = 0; y < h; ++y)
        {
            int const sy = (std::min)(wallpaper.height - 1, static_cast<int>(crop.y + (y + 0.5) * crop.height / h));
            std::uint8_t const* srcRow = wallpaper.pixels.data() + static_cast<std::size_t>(sy) * wallpaper.Stride();
            std::uint8_t* dstRow = canvas.pixels.data() + static_cast<std::size_t>(y) * canvas.Stride();
            for (int x = 0; x < w; ++x)
            {
                int const sx = (std::min)(wallpaper.width - 1, static_cast<int>(crop.x + (x + 0.5) * crop.width / w));
                std::memcpy(dstRow + x * 4, srcRow + sx * 4, 4);
            }
        }
        auto const p = ComputeShadowPaddings(image);
        DrawShifted(canvas, image, Trunc((p.bottom - p.top) / 2.0));
        return canvas;
    }

    Bitmap AddColor(Bitmap const& image, Color color)
    {
        if (image.IsEmpty())
        {
            return image;
        }
        Bitmap canvas = BlankLike(image);
        // windowSolidColor has alpha 1.0; premultiplied BGRA.
        std::uint8_t const pixel[4] = {
            static_cast<std::uint8_t>((color.b * color.a + 127) / 255),
            static_cast<std::uint8_t>((color.g * color.a + 127) / 255),
            static_cast<std::uint8_t>((color.r * color.a + 127) / 255),
            color.a,
        };
        for (std::size_t i = 0; i < canvas.pixels.size(); i += 4)
        {
            std::memcpy(canvas.pixels.data() + i, pixel, 4);
        }
        auto const p = ComputeShadowPaddings(image);
        DrawShifted(canvas, image, Trunc((p.bottom - p.top) / 2.0));
        return canvas;
    }

    Bitmap CropBitmap(Bitmap const& image, int x, int y, int width, int height)
    {
        int const left = std::clamp(x, 0, image.width);
        int const top = std::clamp(y, 0, image.height);
        int const right = std::clamp(x + width, left, image.width);
        int const bottom = std::clamp(y + height, top, image.height);
        Bitmap out;
        out.width = right - left;
        out.height = bottom - top;
        out.dpr = image.dpr;
        out.screenRect = image.screenRect.IsEmpty()
            ? Rect{}
            : Rect{ image.screenRect.x + left, image.screenRect.y + top, static_cast<double>(out.width), static_cast<double>(out.height) };
        out.pixels.resize(out.Stride() * static_cast<std::size_t>(out.height));
        for (int row = 0; row < out.height; ++row)
        {
            std::memcpy(out.pixels.data() + static_cast<std::size_t>(row) * out.Stride(),
                        image.pixels.data() + static_cast<std::size_t>(top + row) * image.Stride() + static_cast<std::size_t>(left) * 4,
                        out.Stride());
        }
        return out;
    }

    Rect SelectorCutout(Point start, Point current, Size screen) noexcept
    {
        double x1 = current.x;
        double y1 = current.y;
        if (x1 > start.x && x1 == screen.width - 1)
        {
            x1 += 1;
        }
        if (y1 > start.y && y1 == screen.height - 1)
        {
            y1 += 1;
        }
        Rect const rect{ (std::min)(start.x, x1), (std::min)(start.y, y1), std::abs(x1 - start.x), std::abs(y1 - start.y) };
        return rect.Intersection(Rect{ 0, 0, screen.width, screen.height });
    }

    bool IsSelectionTooSmall(Rect pixels, double dpr) noexcept
    {
        double const scale = dpr > 0 ? dpr : 1.0;
        return pixels.Standardized().width / scale < 5 || pixels.Standardized().height / scale < 5;
    }

    StatusLabelLayout LayoutStatusLabel(Rect cutout, Size text, Size screen, double dpr) noexcept
    {
        double const gap = 10 * dpr;
        double const margin = 6 * dpr;
        double const x = std::trunc(cutout.x + cutout.width / 2 - text.width / 2);
        // Below the cutout, or above it when that is < 6 from the bottom.
        double y = cutout.Bottom() + gap;
        if (screen.height - (y + text.height) < margin)
        {
            y = cutout.Top() - gap - text.height;
        }
        y = std::trunc(y);
        StatusLabelLayout layout;
        layout.text = Rect{ x, y, text.width, text.height };
        layout.box = Rect{ x - 8 * dpr, y - 6 * dpr, text.width + 16 * dpr, text.height + 12 * dpr };
        return layout;
    }

    bool NeedsAreaCaptureQuestion(CaptureActions const& actions, std::wstring_view mode, bool afterGrabShow,
                                  std::wstring_view areaCaptureMode) noexcept
    {
        return actions.useSettings && mode == L"interactive" && afterGrabShow && areaCaptureMode == L"none";
    }

    ResolvedActions ResolveCaptureActions(CaptureActions const& actions, std::wstring_view mode, bool afterGrabShow,
                                          bool afterGrabCopy, bool afterGrabSave, std::wstring_view areaCaptureMode) noexcept
    {
        ResolvedActions resolved;
        if (!actions.useSettings)
        {
            resolved.copy = actions.copy;
            resolved.save = actions.save;
            resolved.edit = actions.edit;
            resolved.pin = actions.pin;
            resolved.thumbnail = actions.thumbnail;
            return resolved;
        }
        resolved.copy = afterGrabCopy;
        resolved.save = afterGrabSave;
        resolved.thumbnail = mode == L"interactive" && afterGrabShow && areaCaptureMode == L"preview";
        resolved.edit = afterGrabShow && !resolved.thumbnail;
        resolved.pin = false;
        return resolved;
    }

    Point AddCapturePosition(Rect captureRect, double canvasMaxX, Size object, double dpr) noexcept
    {
        double const maxX = (std::max)(captureRect.x, captureRect.x + captureRect.width);
        double const maxY = (std::max)(captureRect.y, captureRect.y + captureRect.height);
        double const x = (std::min)(maxX + 20 * dpr, canvasMaxX + 1920 * dpr - object.width - 20 * dpr);
        return { x, maxY - object.height };
    }
}
