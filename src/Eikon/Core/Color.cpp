#include "pch.h"
#include "Core/Color.h"

#include "Core/Bitmap.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <numbers>
#include <unordered_map>

namespace Eikon::Core
{
    std::wstring Color::ToHex() const
    {
        static constexpr wchar_t kHex[] = L"0123456789ABCDEF";
        std::wstring text = L"#";
        for (std::uint8_t channel : { r, g, b })
        {
            text += kHex[channel >> 4];
            text += kHex[channel & 0xF];
        }
        return text;
    }

    std::optional<Color> Color::FromHex(std::wstring_view text)
    {
        auto isSpace = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r'; };
        while (!text.empty() && isSpace(text.front()))
        {
            text.remove_prefix(1);
        }
        while (!text.empty() && isSpace(text.back()))
        {
            text.remove_suffix(1);
        }
        if (!text.empty() && text.front() == L'#')
        {
            text.remove_prefix(1);
        }
        if (text.size() != 6)
        {
            return std::nullopt;
        }
        std::uint32_t value = 0;
        for (wchar_t c : text)
        {
            int digit = -1;
            if (c >= L'0' && c <= L'9') digit = c - L'0';
            else if (c >= L'a' && c <= L'f') digit = c - L'a' + 10;
            else if (c >= L'A' && c <= L'F') digit = c - L'A' + 10;
            if (digit < 0)
            {
                return std::nullopt;
            }
            value = (value << 4) | static_cast<std::uint32_t>(digit);
        }
        return Color{ static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>((value >> 8) & 0xFF),
                      static_cast<std::uint8_t>(value & 0xFF), 255 };
    }

    // ---- Sampling ----

    namespace
    {
        constexpr Color kBlack{ 0, 0, 0, 255 };

        // Truncation toward zero (NaN and huge values never reach here in practice).
        std::int64_t Trunc(double value) noexcept
        {
            if (!(value > -9.2e18 && value < 9.2e18))
            {
                return 0;
            }
            return static_cast<std::int64_t>(value);
        }

        // B, G, R bytes of buffer pixel (column, row), or nullptr outside the buffer.
        std::uint8_t const* PixelAt(Bitmap const& image, std::int64_t column, std::int64_t row) noexcept
        {
            if (image.IsEmpty() || column < 0 || row < 0 || column >= image.width || row >= image.height)
            {
                return nullptr;
            }
            return image.pixels.data() + (static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(column)) * 4;
        }

        // Converts from the capture's colour space to sRGB.
        // Windows captures are sRGB, so this is the identity.
        Color FromBytes(std::uint8_t const* p) noexcept
        {
            return Color{ p[2], p[1], p[0], 255 };
        }
    }

    Color PickPixel(Bitmap const& image, Rect const& crop, Point point) noexcept
    {
        std::int64_t const width = Trunc(crop.width);
        std::int64_t const height = Trunc(crop.height);
        std::int64_t const x = Trunc(point.x);
        std::int64_t const v = height - Trunc(point.y);
        if (x < 0 || x >= width || v < 0 || v >= height)
        {
            return kBlack;
        }
        auto const* p = PixelAt(image, Trunc(crop.x) + x, Trunc(crop.y) + v);
        return p ? FromBytes(p) : kBlack;
    }

    Color SampleArea(Bitmap const& image, Rect const& crop, Rect const& rect, SampleMode mode) noexcept
    {
        std::int64_t const width = Trunc(rect.width);
        std::int64_t const height = Trunc(rect.height);
        if (width == 0 || height == 0 || image.IsEmpty())
        {
            return kBlack;
        }
        std::int64_t const cropW = Trunc(crop.width);
        std::int64_t const cropH = Trunc(crop.height);
        std::int64_t const x = Trunc(rect.x);
        std::int64_t const y = Trunc(rect.y);
        std::int64_t const x0 = (std::max)((std::min)(x, x + width), std::int64_t{ 0 });
        std::int64_t const x1 = (std::min)((std::max)(x, x + width), cropW);
        std::int64_t const y0 = (std::max)((std::min)(y, y + height), std::int64_t{ 0 });
        std::int64_t const y1 = (std::min)((std::max)(y, y + height), cropH);
        if (x1 <= x0 || y1 <= y0)
        {
            return kBlack;
        }

        std::int64_t const cropX = Trunc(crop.x);
        std::int64_t const bottomRow = Trunc(crop.y) + cropH - 1;
        std::uint64_t sumR = 0, sumG = 0, sumB = 0, count = 0;
        std::uint8_t darkR = 255, darkG = 255, darkB = 255;
        int darkSum = 765;
        for (std::int64_t column = x0; column < x1; ++column)
        {
            for (std::int64_t up = y0; up < y1; ++up)
            {
                auto const* p = PixelAt(image, cropX + column, bottomRow - up);
                if (!p)
                {
                    continue;
                }
                sumR += p[2];
                sumG += p[1];
                sumB += p[0];
                ++count;
                int const sum = p[0] + p[1] + p[2];
                if (sum < darkSum)
                {
                    darkSum = sum;
                    darkR = p[2];
                    darkG = p[1];
                    darkB = p[0];
                }
            }
        }
        if (mode == SampleMode::Darkest)
        {
            return Color{ darkR, darkG, darkB, 255 };
        }
        if (count == 0)
        {
            return kBlack;
        }
        return Color{ static_cast<std::uint8_t>(sumR / count), static_cast<std::uint8_t>(sumG / count),
                      static_cast<std::uint8_t>(sumB / count), 255 };
    }

    Color DarkestAround(Bitmap const& image, Rect const& crop, Point point) noexcept
    {
        return SampleArea(image, crop, Rect{ point.x - 10.0, point.y - 10.0, 20.0, 20.0 }, SampleMode::Darkest);
    }

    // ---- Formats ----

    std::span<ColorFormat const> ColorFormats() noexcept
    {
        static constexpr std::array<ColorFormat, 8> kFormats{ {
            { L"HEX", L"HEX #FFC200", L"#%rh%gh%bh", L"#%rh%gh%bh", 102 },
            { L"HEXno", L"HEX FFC200", L"%rh%gh%bh", L"%rh%gh%bh", 102 },
            { L"RGBA", L"CSS rgba(255, 194, 0, 1)", L"%rd, %gd, %bd", L"rgba(%rd, %gd, %bd, 1)", 112 },
            { L"RGB1", L"RGB (1.00, 0.76, 0.00)", L"%rf, %gf, %bf", L"(%rf, %gf, %bf)", 120 },
            { L"Android", L"Android 0xFFFFC200", L"0xFF%rh%gh%bh", L"0xFF%rh%gh%bh", 112 },
            { L"UIColor", L"UIColor (red: 1.00, gre...", L"%rf, %gf, %bf", L"UIColor(red: %rf, green: %gf, blue: %bf, alpha: 1.00)", 120 },
            { L"CSharp", L"C# Color.fromArgb()", L"%rd, %gd, %bd", L"Color.fromArgb(%rd, %gd, %bd);", 112 },
            { L"OKLCH", L"OKLCH", L"L%ld C%cd H%hd", L"oklch(%lf% %cf %hf)", 112 },
        } };
        return kFormats;
    }

    std::size_t ColorFormatIndex(std::wstring_view key) noexcept
    {
        auto const formats = ColorFormats();
        for (std::size_t i = 0; i < formats.size(); ++i)
        {
            if (formats[i].key == key)
            {
                return i;
            }
        }
        return 0;
    }

    namespace
    {
        std::wstring Widen(char const* first, char const* last)
        {
            return std::wstring(first, last);
        }

        // String(format: "%02lX", lroundf(byte)).
        std::wstring Hex2(std::uint8_t value)
        {
            static constexpr wchar_t kHex[] = L"0123456789ABCDEF";
            return { kHex[value >> 4], kHex[value & 0xF] };
        }

        // String(format: "%0.2F", value): printf's correctly rounded fixed notation, C locale.
        std::wstring Fixed2(double value)
        {
            char buffer[64];
            auto const result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, 2);
            return Widen(buffer, result.ptr);
        }

        // The value truncated toward zero, as a decimal integer.
        std::wstring Integer(double value)
        {
            return std::to_wstring(frintaInt(value));
        }

        void ReplaceAll(std::wstring& text, std::wstring_view token, std::wstring const& value)
        {
            for (std::size_t at = text.find(token); at != std::wstring::npos; at = text.find(token, at + value.size()))
            {
                text.replace(at, token.size(), value);
            }
        }
    }

    FormattedColor FormatColor(Color color, std::wstring_view key)
    {
        ColorFormat const& format = ColorFormats()[ColorFormatIndex(key)];
        std::vector<std::pair<std::wstring_view, std::wstring>> values{
            { L"%rh", Hex2(color.r) },
            { L"%gh", Hex2(color.g) },
            { L"%bh", Hex2(color.b) },
            { L"%rd", std::to_wstring(color.r) },
            { L"%gd", std::to_wstring(color.g) },
            { L"%bd", std::to_wstring(color.b) },
            { L"%rf", Fixed2(color.r / 255.0) },
            { L"%gf", Fixed2(color.g / 255.0) },
            { L"%bf", Fixed2(color.b / 255.0) },
        };
        // Tests the key itself (the stored setting) for "OKLCH".
        if (key.find(L"OKLCH") != std::wstring_view::npos)
        {
            Oklch const lch = ToOklch(color);
            values.emplace_back(L"%ld", Integer(lch.l * 100.0));
            values.emplace_back(L"%cd", Integer(lch.c * 100.0));
            values.emplace_back(L"%hd", Integer(lch.h));
            values.emplace_back(L"%lf", Fixed2(lch.l * 100.0));
            values.emplace_back(L"%cf", Fixed2(lch.c));
            values.emplace_back(L"%hf", Fixed2(lch.h));
        }
        FormattedColor result{ std::wstring{ format.displayTemplate }, std::wstring{ format.copyTemplate } };
        for (auto const& [token, value] : values)
        {
            ReplaceAll(result.display, token, value);
            ReplaceAll(result.copy, token, value);
        }
        return result;
    }

    // ---- OKLCH ----

    double SrgbToLinear(double encoded) noexcept
    {
        double const magnitude = std::abs(encoded);
        if (magnitude <= 0.04045)
        {
            return encoded / 12.92;
        }
        return std::copysign(std::pow((magnitude + 0.055) / 1.055, 2.4), encoded);
    }

    namespace
    {
        using Matrix = std::array<double, 9>;

        // Linear sRGB → XYZ (D65),.
        constexpr Matrix kSrgbToXyz{
            0.41239079926595934, 0.35758433938387801, 0.18048078840183429,
            0.21263900587151027, 0.71516867876775603, 0.072192315360733714,
            0.019330818715591822, 0.11919477979462598, 0.9505321522496607,
        };
        // XYZ → LMS,.
        constexpr Matrix kXyzToLms{
            0.81902243799670305, 0.36190626005289039, -0.12887378152098791,
            0.032983653932388501, 0.92928686158634344, 0.036144666350642403,
            0.048177189359624201, 0.26423953175273079, 0.63354782846943092,
        };
        // LMS′ → Oklab,.
        constexpr Matrix kLmsToOklab{
            0.21045426830931399, 0.79361777470230543, -0.0040720430116193002,
            1.9779985324311684, -2.4285922420485799, 0.45059370961741102,
            0.0259040424655478, 0.78277171245752963, -0.80867575492307742,
        };

        std::array<double, 3> Multiply(Matrix const& m, double x, double y, double z) noexcept
        {
            return { m[0] * x + m[1] * y + m[2] * z, m[3] * x + m[4] * y + m[5] * z, m[6] * x + m[7] * y + m[8] * z };
        }
    }

    Oklab XyzToOklab(double x, double y, double z) noexcept
    {
        auto const lms = Multiply(kXyzToLms, x, y, z);
        auto const lab = Multiply(kLmsToOklab, std::cbrt(lms[0]), std::cbrt(lms[1]), std::cbrt(lms[2]));
        return { lab[0], lab[1], lab[2] };
    }

    Oklch ToOklch(Color color) noexcept
    {
        double const r = SrgbToLinear(color.r / 255.0);
        double const g = SrgbToLinear(color.g / 255.0);
        double const b = SrgbToLinear(color.b / 255.0);
        auto const xyz = Multiply(kSrgbToXyz, r, g, b);
        Oklab const lab = XyzToOklab(xyz[0], xyz[1], xyz[2]);

        Oklch lch;
        lch.l = lab.l;
        lch.c = std::sqrt(lab.a * lab.a + lab.b * lab.b);
        if (std::abs(lab.a) < 0.0002 && std::abs(lab.b) < 0.0002)
        {
            lch.h = 0;
        }
        else
        {
            lch.h = std::fmod(std::atan2(lab.b, lab.a) * 180.0 / std::numbers::pi + 360.0, 360.0);
        }
        return lch;
    }

    Bitmap ScaleImage(Bitmap const& image, double factor)
    {
        Bitmap out;
        if (image.IsEmpty())
        {
            return out;
        }
        auto const w = static_cast<std::int32_t>(factor * image.width);
        auto const h = static_cast<std::int32_t>(factor * image.height);
        if (w <= 0 || h <= 0)
        {
            return out;
        }
        out = Bitmap::Create(w, h, image.dpr);
        for (std::int32_t oy = 0; oy < h; ++oy)
        {
            std::int32_t const y0 = static_cast<std::int32_t>(static_cast<std::int64_t>(oy) * image.height / h);
            std::int32_t const y1 = std::max(y0 + 1, static_cast<std::int32_t>(static_cast<std::int64_t>(oy + 1) * image.height / h));
            for (std::int32_t ox = 0; ox < w; ++ox)
            {
                std::int32_t const x0 = static_cast<std::int32_t>(static_cast<std::int64_t>(ox) * image.width / w);
                std::int32_t const x1 = std::max(x0 + 1, static_cast<std::int32_t>(static_cast<std::int64_t>(ox + 1) * image.width / w));
                std::uint64_t sum[4] = {};
                for (std::int32_t y = y0; y < y1; ++y)
                {
                    auto const* p = image.pixels.data() + static_cast<std::size_t>(y) * image.Stride() + static_cast<std::size_t>(x0) * 4;
                    for (std::int32_t x = x0; x < x1; ++x, p += 4)
                    {
                        for (int c = 0; c < 4; ++c)
                        {
                            sum[c] += p[c];
                        }
                    }
                }
                std::uint64_t const n = static_cast<std::uint64_t>(x1 - x0) * static_cast<std::uint64_t>(y1 - y0);
                auto* q = out.pixels.data() + static_cast<std::size_t>(oy) * out.Stride() + static_cast<std::size_t>(ox) * 4;
                for (int c = 0; c < 4; ++c)
                {
                    q[c] = static_cast<std::uint8_t>((sum[c] + n / 2) / n);
                }
            }
        }
        return out;
    }

    std::vector<Color> DominantColors(std::vector<ColorCount> counts, std::size_t totalPixels,
                                      std::size_t maxColors, double minFrequency)
    {
        struct Frequency
        {
            Color color;
            double frequency;
        };
        auto byFrequency = [](Frequency const& a, Frequency const& b) { return a.frequency > b.frequency; };

        auto const threshold = static_cast<std::int64_t>(static_cast<double>(totalPixels) * 0.0001);
        std::vector<Frequency> candidates;
        for (ColorCount const& entry : counts)
        {
            if (static_cast<std::int64_t>(entry.count) > threshold)
            {
                candidates.push_back({ { entry.color.r, entry.color.g, entry.color.b, 255 }, static_cast<double>(entry.count) });
            }
        }
        std::stable_sort(candidates.begin(), candidates.end(), byFrequency);
        if (candidates.size() > 500)
        {
            candidates.resize(500);
        }

        auto distance = [](Color a, Color b) {
            return std::abs(a.r / 255.0 - b.r / 255.0) + std::abs(a.g / 255.0 - b.g / 255.0) + std::abs(a.b / 255.0 - b.b / 255.0);
        };
        std::vector<Frequency> kept;
        for (Frequency const& candidate : candidates)
        {
            std::size_t closest = kept.size();
            double best = 0;
            for (std::size_t i = 0; i < kept.size(); ++i)
            {
                double const d = distance(kept[i].color, candidate.color);
                if (closest == kept.size() || d < best)   // strict: the first of equally close wins
                {
                    closest = i;
                    best = d;
                }
            }
            if (closest < kept.size() && best < 0.2)
            {
                kept[closest].frequency += 1.0;
            }
            else
            {
                kept.push_back(candidate);
            }
        }

        if (kept.size() > maxColors)
        {
            kept.resize(maxColors);
        }
        std::stable_sort(kept.begin(), kept.end(), byFrequency);
        std::vector<Color> result;
        for (Frequency const& entry : kept)
        {
            if (entry.frequency > minFrequency)
            {
                result.push_back(entry.color);
            }
        }
        return result;
    }

    std::vector<Color> TopColors(Bitmap const& image, std::size_t maxColors, double minFrequency)
    {
        Bitmap const scaled = ScaleImage(image, 0.2);
        if (scaled.IsEmpty())
        {
            return {};
        }
        std::vector<ColorCount> counts;
        std::unordered_map<std::uint32_t, std::size_t> index;
        for (std::int32_t y = 0; y < scaled.height; ++y)
        {
            auto const* p = scaled.pixels.data() + static_cast<std::size_t>(y) * scaled.Stride();
            for (std::int32_t x = 0; x < scaled.width; ++x, p += 4)
            {
                int const b = p[0], g = p[1], r = p[2], a = p[3];
                int const hi = std::max({ r, g, b }), lo = std::min({ r, g, b });
                int const sum = r + g + b;
                if (a <= 150 || sum < 49 || sum > 749 || hi - lo <= 10)
                {
                    continue;
                }
                auto const key = static_cast<std::uint32_t>(r << 16 | g << 8 | b);
                auto const [it, inserted] = index.try_emplace(key, counts.size());
                if (inserted)
                {
                    counts.push_back({ { static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), 255 }, 0 });
                }
                ++counts[it->second].count;
            }
        }
        std::size_t const total = static_cast<std::size_t>(scaled.width) * static_cast<std::size_t>(scaled.height);
        return DominantColors(std::move(counts), total, maxColors, minFrequency);
    }

    std::vector<std::wstring> TopColorStrings(Bitmap const& image)
    {
        std::vector<std::wstring> strings;
        for (Color const& color : TopColors(image, 8, 400))
        {
            strings.push_back(color.ToHex());
        }
        if (strings.empty())
        {
            strings.push_back(L"#000000");
        }
        return strings;
    }
}
