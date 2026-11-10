#include "pch.h"
#include "Core/CutOut.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace Eikon::Core
{
    namespace
    {
        std::uint32_t PixelAt(Bitmap const& image, int x, int y) noexcept
        {
            std::uint32_t value = 0;
            std::memcpy(&value, image.pixels.data() + static_cast<std::size_t>(y) * image.Stride() + static_cast<std::size_t>(x) * 4, 4);
            return value;
        }
    }

    Bitmap CutOutBackground(Bitmap const& image)
    {
        Bitmap out = image;
        if (image.IsEmpty())
        {
            return out;
        }
        int const w = image.width;
        int const h = image.height;

        // The border pixels in order: top row, bottom row, then each middle row's two ends.
        std::vector<std::pair<int, int>> border;
        for (int x = 0; x < w; ++x)
        {
            border.emplace_back(x, 0);
        }
        if (h > 1)
        {
            for (int x = 0; x < w; ++x)
            {
                border.emplace_back(x, h - 1);
            }
        }
        for (int y = 1; y < h - 1; ++y)
        {
            border.emplace_back(0, y);
            if (w > 1)
            {
                border.emplace_back(w - 1, y);
            }
        }

        // The most frequent colour.
        std::unordered_map<std::uint32_t, std::size_t> counts;
        std::uint32_t background = PixelAt(image, 0, 0);
        std::size_t best = 0;
        for (auto const& [x, y] : border)
        {
            std::uint32_t const colour = PixelAt(image, x, y);
            std::size_t const count = ++counts[colour];
            if (count > best)
            {
                best = count;
                background = colour;
            }
        }

        std::vector<std::uint8_t> visited(static_cast<std::size_t>(w) * h, 0);
        std::vector<std::pair<int, int>> stack;
        for (auto const& point : border)
        {
            if (PixelAt(image, point.first, point.second) == background)
            {
                stack.push_back(point);
            }
        }
        while (!stack.empty())
        {
            auto const [x, y] = stack.back();
            stack.pop_back();
            std::size_t const index = static_cast<std::size_t>(y) * w + x;
            if (visited[index])
            {
                continue;
            }
            visited[index] = 1;
            std::memset(out.pixels.data() + index * 4, 0, 4);
            std::pair<int, int> const neighbours[]{ { x - 1, y }, { x + 1, y }, { x, y - 1 }, { x, y + 1 } };
            for (auto const& [nx, ny] : neighbours)
            {
                if (nx >= 0 && nx < w && ny >= 0 && ny < h && !visited[static_cast<std::size_t>(ny) * w + nx] && PixelAt(image, nx, ny) == background)
                {
                    stack.emplace_back(nx, ny);
                }
            }
        }
        return out;
    }
}

namespace Eikon::Core
{
    namespace
    {
        // Straight (not premultiplied) R, G, B of a pixel, 0..255.
        void StraightRgb(Bitmap const& image, int x, int y, float rgb[3]) noexcept
        {
            std::uint8_t const* p = image.pixels.data() + static_cast<std::size_t>(y) * image.Stride() + static_cast<std::size_t>(x) * 4;
            float const a = p[3];
            float const k = a > 0 && a < 255 ? 255.0f / a : 1.0f;
            rgb[0] = p[2] * k;
            rgb[1] = p[1] * k;
            rgb[2] = p[0] * k;
        }

        float Smoothstep(float edge0, float edge1, float x) noexcept
        {
            float const t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        // One box-blur pass (horizontal, then vertical) of `radius`, edges clamped.
        void BoxBlur(std::vector<std::uint8_t>& mask, int width, int height, int radius)
        {
            std::vector<std::uint8_t> temp(mask.size());
            int const window = 2 * radius + 1;
            for (int y = 0; y < height; ++y)
            {
                std::uint8_t const* row = mask.data() + static_cast<std::size_t>(y) * width;
                std::uint8_t* out = temp.data() + static_cast<std::size_t>(y) * width;
                int sum = 0;
                for (int i = -radius; i <= radius; ++i)
                {
                    sum += row[std::clamp(i, 0, width - 1)];
                }
                for (int x = 0; x < width; ++x)
                {
                    out[x] = static_cast<std::uint8_t>((sum + window / 2) / window);
                    sum += row[std::min(x + radius + 1, width - 1)] - row[std::max(x - radius, 0)];
                }
            }
            for (int x = 0; x < width; ++x)
            {
                auto at = [&](int y) { return temp[static_cast<std::size_t>(std::clamp(y, 0, height - 1)) * width + x]; };
                int sum = 0;
                for (int i = -radius; i <= radius; ++i)
                {
                    sum += at(i);
                }
                for (int y = 0; y < height; ++y)
                {
                    mask[static_cast<std::size_t>(y) * width + x] = static_cast<std::uint8_t>((sum + window / 2) / window);
                    sum += at(y + radius + 1) - at(y - radius);
                }
            }
        }
    }

    std::vector<float> SubjectModelInput(Bitmap const& image, int size)
    {
        std::size_t const plane = static_cast<std::size_t>(size > 0 ? size : 0) * static_cast<std::size_t>(size > 0 ? size : 0);
        std::vector<float> out(plane * 3, 0.0f);
        if (image.IsEmpty() || size <= 0)
        {
            return out;
        }
        int const w = image.width;
        int const h = image.height;
        double const fx = static_cast<double>(w) / size;
        double const fy = static_cast<double>(h) / size;
        bool const shrink = fx >= 1 && fy >= 1;
        float brightest = 0;
        for (int ty = 0; ty < size; ++ty)
        {
            for (int tx = 0; tx < size; ++tx)
            {
                float sum[3]{};
                if (shrink)
                {
                    // Area average over the source pixels the target pixel covers.
                    int const x0 = static_cast<int>(tx * fx);
                    int const x1 = std::max(x0 + 1, std::min(w, static_cast<int>(std::ceil((tx + 1) * fx))));
                    int const y0 = static_cast<int>(ty * fy);
                    int const y1 = std::max(y0 + 1, std::min(h, static_cast<int>(std::ceil((ty + 1) * fy))));
                    for (int y = y0; y < y1; ++y)
                    {
                        for (int x = x0; x < x1; ++x)
                        {
                            float rgb[3];
                            StraightRgb(image, x, y, rgb);
                            sum[0] += rgb[0];
                            sum[1] += rgb[1];
                            sum[2] += rgb[2];
                        }
                    }
                    float const n = static_cast<float>((x1 - x0) * (y1 - y0));
                    sum[0] /= n;
                    sum[1] /= n;
                    sum[2] /= n;
                }
                else
                {
                    double const sx = std::clamp((tx + 0.5) * fx - 0.5, 0.0, static_cast<double>(w - 1));
                    double const sy = std::clamp((ty + 0.5) * fy - 0.5, 0.0, static_cast<double>(h - 1));
                    int const x0 = static_cast<int>(sx);
                    int const y0 = static_cast<int>(sy);
                    int const x1 = std::min(x0 + 1, w - 1);
                    int const y1 = std::min(y0 + 1, h - 1);
                    float const ax = static_cast<float>(sx - x0);
                    float const ay = static_cast<float>(sy - y0);
                    float c00[3], c10[3], c01[3], c11[3];
                    StraightRgb(image, x0, y0, c00);
                    StraightRgb(image, x1, y0, c10);
                    StraightRgb(image, x0, y1, c01);
                    StraightRgb(image, x1, y1, c11);
                    for (int c = 0; c < 3; ++c)
                    {
                        sum[c] = (c00[c] * (1 - ax) + c10[c] * ax) * (1 - ay) + (c01[c] * (1 - ax) + c11[c] * ax) * ay;
                    }
                }
                std::size_t const i = static_cast<std::size_t>(ty) * size + tx;
                for (int c = 0; c < 3; ++c)
                {
                    out[c * plane + i] = sum[c];
                    brightest = std::max(brightest, sum[c]);
                }
            }
        }
        constexpr float mean[3]{ 0.485f, 0.456f, 0.406f };
        constexpr float deviation[3]{ 0.229f, 0.224f, 0.225f };
        float const scale = 1.0f / std::max(brightest, 1e-6f);
        for (int c = 0; c < 3; ++c)
        {
            for (std::size_t i = 0; i < plane; ++i)
            {
                float& v = out[c * plane + i];
                v = (v * scale - mean[c]) / deviation[c];
            }
        }
        return out;
    }

    std::vector<std::uint8_t> SubjectMaskFromModel(std::span<float const> output, int size, int width, int height, int feather)
    {
        std::size_t const plane = static_cast<std::size_t>(size > 0 ? size : 0) * static_cast<std::size_t>(size > 0 ? size : 0);
        if (size <= 0 || width <= 0 || height <= 0 || output.size() < plane)
        {
            return {};
        }
        auto const values = output.first(plane);
        auto const [lowest, highest] = std::minmax_element(values.begin(), values.end());
        float const lo = *lowest;
        float const hi = *highest;
        std::size_t const above = static_cast<std::size_t>(std::count_if(values.begin(), values.end(), [](float v) { return v > 0.5f; }));
        if (hi < 0.5f || above * 200 < plane)
        {
            return {};
        }
        float const range = std::max(hi - lo, 1e-6f);
        auto at = [&](int x, int y) { return (values[static_cast<std::size_t>(y) * size + x] - lo) / range; };
        std::vector<std::uint8_t> mask(static_cast<std::size_t>(width) * height);
        double const fx = static_cast<double>(size) / width;
        double const fy = static_cast<double>(size) / height;
        for (int y = 0; y < height; ++y)
        {
            double const sy = std::clamp((y + 0.5) * fy - 0.5, 0.0, static_cast<double>(size - 1));
            int const y0 = static_cast<int>(sy);
            int const y1 = std::min(y0 + 1, size - 1);
            float const ay = static_cast<float>(sy - y0);
            for (int x = 0; x < width; ++x)
            {
                double const sx = std::clamp((x + 0.5) * fx - 0.5, 0.0, static_cast<double>(size - 1));
                int const x0 = static_cast<int>(sx);
                int const x1 = std::min(x0 + 1, size - 1);
                float const ax = static_cast<float>(sx - x0);
                float const v = (at(x0, y0) * (1 - ax) + at(x1, y0) * ax) * (1 - ay) + (at(x0, y1) * (1 - ax) + at(x1, y1) * ax) * ay;
                mask[static_cast<std::size_t>(y) * width + x] = static_cast<std::uint8_t>(std::lround(Smoothstep(0.3f, 0.7f, v) * 255.0f));
            }
        }
        FeatherMask(mask, width, height, feather);
        return mask;
    }

    bool MaskHasSubject(std::span<std::uint8_t const> mask)
    {
        if (mask.empty())
        {
            return false;
        }
        std::size_t const covered = static_cast<std::size_t>(std::count_if(mask.begin(), mask.end(), [](std::uint8_t v) { return v >= 128; }));
        return covered * 200 >= mask.size();
    }

    void FeatherMask(std::vector<std::uint8_t>& mask, int width, int height, int radius)
    {
        if (radius <= 0 || width <= 0 || height <= 0 || mask.size() < static_cast<std::size_t>(width) * height)
        {
            return;
        }
        BoxBlur(mask, width, height, radius);
        BoxBlur(mask, width, height, radius);
    }

    Bitmap ApplySubjectMask(Bitmap const& image, std::span<std::uint8_t const> mask)
    {
        Bitmap out = image;
        std::size_t const count = static_cast<std::size_t>(std::max(image.width, 0)) * static_cast<std::size_t>(std::max(image.height, 0));
        if (image.IsEmpty() || mask.size() < count)
        {
            return out;
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            unsigned const m = mask[i];
            std::uint8_t* p = out.pixels.data() + i * 4;
            for (int c = 0; c < 4; ++c)
            {
                p[c] = static_cast<std::uint8_t>((p[c] * m + 127) / 255);
            }
        }
        return out;
    }
}
