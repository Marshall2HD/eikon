#include "pch.h"
#include "Core/Stitch.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace Eikon::Core::Stitch
{
    namespace
    {
        // 0.15 computed as 0.3 * 0.5 with 0.3 = 0.30000000000000004.
        constexpr double kFifteen = 0.30000000000000004 * 0.5;

        void Log(std::wstring const& function, std::wstring const& message)
        {
            OutputDebugStringW((L"[Eikon] " + function + L": " + message + L"\n").c_str());
        }

        // Truncation toward zero.
        std::int64_t Trunc(double value) noexcept
        {
            return static_cast<std::int64_t>(value);
        }

        Bitmap Transparent(std::int64_t width, std::int64_t height, double dpr)
        {
            Bitmap bitmap;
            bitmap.width = static_cast<std::int32_t>((std::max)(width, std::int64_t{ 0 }));
            bitmap.height = static_cast<std::int32_t>((std::max)(height, std::int64_t{ 0 }));
            bitmap.dpr = dpr;
            bitmap.pixels.assign(bitmap.Stride() * static_cast<std::size_t>(bitmap.height), 0);
            return bitmap;
        }

        std::uint8_t const* Row(Bitmap const& image, std::int64_t y) noexcept
        {
            return image.pixels.data() + static_cast<std::size_t>(y) * image.Stride();
        }

        // Source-over drawing, premultiplied BGRA.
        void BlendRow(std::uint8_t* dst, std::uint8_t const* src, std::size_t pixels) noexcept
        {
            for (std::size_t i = 0; i < pixels; ++i, dst += 4, src += 4)
            {
                std::uint32_t const alpha = src[3];
                if (alpha == 255)
                {
                    std::memcpy(dst, src, 4);
                }
                else if (alpha != 0)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        dst[c] = static_cast<std::uint8_t>(src[c] + (dst[c] * (255 - alpha) + 127) / 255);
                    }
                }
            }
        }

        // Draws rows [srcTop, srcTop + count) of `src` with the first of them at canvas row `dstTop`
        // (top-down), at x = 0, clipped to the canvas.
        void DrawRows(Bitmap& canvas, Bitmap const& src, std::int64_t srcTop, std::int64_t count, std::int64_t dstTop)
        {
            std::size_t const pixels = static_cast<std::size_t>((std::min)(canvas.width, src.width));
            for (std::int64_t j = 0; j < count; ++j)
            {
                std::int64_t const sy = srcTop + j;
                std::int64_t const dy = dstTop + j;
                if (sy < 0 || sy >= src.height || dy < 0 || dy >= canvas.height)
                {
                    continue;
                }
                BlendRow(canvas.pixels.data() + static_cast<std::size_t>(dy) * canvas.Stride(), Row(src, sy), pixels);
            }
        }

        // Row texture profile: sum of |B-B_prev| + |G-G_prev| + |R-R_prev| over
        // `count` pixels from x0 (the first pixel's "previous" is 0), divided by count*3.
        double Profile(Bitmap const& image, std::int64_t y, std::int64_t x0, std::int64_t count)
        {
            if (count <= 0)
            {
                return 0.0;
            }
            if (y < 0 || y >= image.height)
            {
                return 0.0; // past the buffer; it cannot happen with T <= H/3
            }
            std::int64_t const end = (std::min)(x0 + count, static_cast<std::int64_t>(image.width));
            std::uint8_t const* row = Row(image, y);
            std::int64_t pb = 0;
            std::int64_t pg = 0;
            std::int64_t pr = 0;
            std::uint64_t sum = 0;
            for (std::int64_t x = x0; x < end; ++x)
            {
                std::uint8_t const* p = row + x * 4;
                sum += static_cast<std::uint64_t>(std::llabs(pb - p[0]) + std::llabs(pg - p[1]) + std::llabs(pr - p[2]));
                pb = p[0];
                pg = p[1];
                pr = p[2];
            }
            return static_cast<double>(sum) / (static_cast<double>(count) * 3.0);
        }

        std::wstring Number(double value)
        {
            wchar_t buffer[64]{};
            swprintf_s(buffer, L"%.17g", value);
            return buffer;
        }

        struct Descriptors
        {
            static constexpr int kBins = 32;
            std::int64_t rows = 0;
            std::vector<float> values; // rows * kBins
        };

        // Mean luma of 32 equal-width column bins per row of the crop.
        Descriptors Describe(Bitmap const& image, std::int64_t x0, std::int64_t y0, std::int64_t width, std::int64_t height)
        {
            Descriptors d;
            d.rows = height;
            d.values.assign(static_cast<std::size_t>(height) * Descriptors::kBins, 0.0f);
            std::array<std::int64_t, Descriptors::kBins + 1> edges{};
            for (int b = 0; b <= Descriptors::kBins; ++b)
            {
                edges[b] = b * width / Descriptors::kBins;
            }
            for (std::int64_t y = 0; y < height; ++y)
            {
                std::uint8_t const* row = Row(image, y0 + y) + x0 * 4;
                float* out = d.values.data() + static_cast<std::size_t>(y) * Descriptors::kBins;
                for (int b = 0; b < Descriptors::kBins; ++b)
                {
                    std::int64_t const first = (std::min)(edges[b], width - 1);
                    std::int64_t const last = (std::max)(edges[b + 1], first + 1);
                    float sum = 0.0f;
                    for (std::int64_t x = first; x < last; ++x)
                    {
                        std::uint8_t const* p = row + x * 4;
                        sum += 0.299f * p[2] + 0.587f * p[1] + 0.114f * p[0];
                    }
                    out[b] = sum / static_cast<float>(last - first);
                }
            }
            return d;
        }

        double Variance(Descriptors const& d)
        {
            if (d.values.empty())
            {
                return 0.0;
            }
            double sum = 0.0;
            double squares = 0.0;
            for (float v : d.values)
            {
                sum += v;
                squares += static_cast<double>(v) * v;
            }
            double const n = static_cast<double>(d.values.size());
            double const mean = sum / n;
            return (std::max)(0.0, squares / n - mean * mean);
        }
    }

    // ---- ----

    AutoGeometry AutoScrollGeometry(std::int64_t speed, double heightPoints)
    {
        std::size_t const index = static_cast<std::size_t>(std::clamp<std::int64_t>(speed, 0, 3));
        AutoGeometry geometry;
        geometry.delay = kScrollDelays[index];
        std::int64_t const cap = Trunc(std::floor(heightPoints / 3.0));
        geometry.step = static_cast<int>((std::min)(static_cast<std::int64_t>(kScrollSteps[index]), cap));
        geometry.margin = static_cast<int>((Trunc(heightPoints) - geometry.step) / 2);
        return geometry;
    }

    // ---- ----

    std::uint32_t Crc32(std::span<std::uint8_t const> bytes, std::uint32_t crc)
    {
        static auto const table = [] {
            std::array<std::uint32_t, 256> t{};
            for (std::uint32_t i = 0; i < 256; ++i)
            {
                std::uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                {
                    c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                }
                t[i] = c;
            }
            return t;
        }();
        crc = ~crc;
        for (std::uint8_t byte : bytes)
        {
            crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
        }
        return ~crc;
    }

    std::uint32_t CentreHash(Bitmap const& frame)
    {
        if (frame.IsEmpty())
        {
            return 0;
        }
        std::int64_t const x0 = Trunc(frame.width * kFifteen);
        std::int64_t const y0 = Trunc(frame.height * kFifteen);
        std::int64_t const w = Trunc(frame.width * 0.7);
        std::int64_t const h = Trunc(frame.height * 0.7);
        if (w <= 0 || h <= 0)
        {
            return 0;
        }
        std::uint32_t crc = 0;
        for (std::int64_t y = y0; y < y0 + h; ++y)
        {
            crc = Crc32({ Row(frame, y) + x0 * 4, static_cast<std::size_t>(w) * 4 }, crc);
        }
        return crc;
    }

    Bitmap CropRows(Bitmap const& image, std::int64_t top, std::int64_t count)
    {
        std::int64_t const first = std::clamp<std::int64_t>(top, 0, image.height);
        std::int64_t const last = std::clamp<std::int64_t>(top + count, first, image.height);
        Bitmap out;
        out.width = image.width;
        out.height = static_cast<std::int32_t>(last - first);
        out.dpr = image.dpr;
        out.screenRect = image.screenRect;
        auto const begin = image.pixels.begin() + static_cast<std::ptrdiff_t>(first * static_cast<std::int64_t>(image.Stride()));
        out.pixels.assign(begin, begin + static_cast<std::ptrdiff_t>((last - first) * static_cast<std::int64_t>(image.Stride())));
        return out;
    }

    // ---- ----

    std::uint64_t OverlayCompareSegment(Bitmap const& img1, Bitmap const& img2, std::int64_t overlap, std::int64_t area,
                                        int direction, std::uint64_t threshold)
    {
        constexpr std::uint64_t kFailed = 99'999'999;
        static std::wstring const kName = L"overlayCompareSegment(img1:img2:overlap:area:direction:threshold:)";
        if (img1.IsEmpty() || img2.IsEmpty())
        {
            return kFailed;
        }
        if (overlap < area)
        {
            Log(kName, L"Scrolling capture overlay failed, area > overlap");
            return kFailed;
        }

        std::int64_t const hs1 = img1.height;
        std::int64_t const hs2 = img2.height;
        std::int64_t const tail = Trunc(static_cast<double>(area) * 0.2);
        std::int64_t start = 0;
        std::int64_t end = 0;
        std::int64_t offset1 = 0; // row of img1 compared with img2's row r is r + offset1 ...
        std::int64_t offset2 = 0; // ... and img2's is r + offset2
        if (direction < 1)
        {
            start = overlap - area;
            end = overlap - tail;
            offset1 = hs1 - overlap;
        }
        else
        {
            std::int64_t const mid = (2 * (overlap - area)) / 3;
            start = mid + tail;
            end = mid + area;
            offset2 = hs2 - overlap;
            if (offset2 < 0)
            {
                Log(kName, L"is st2 supposed to be negative? " + std::to_wstring(offset2));
                offset2 = 0;
            }
        }
        if (start >= end)
        {
            return 0;
        }
        if (end - 1 + offset1 >= hs1)
        {
            Log(kName, L"rangey is likely out of bound (1): " + std::to_wstring(end) + L", " + std::to_wstring(hs1));
        }
        if (end - 1 + offset2 >= hs2)
        {
            Log(kName, L"rangey is likely out of bound (2): " + std::to_wstring(end) + L", " + std::to_wstring(hs2));
        }

        std::int64_t const x0 = Trunc(img1.width * kFifteen);
        std::int64_t const count = Trunc(img1.width * 0.7);
        std::int64_t const byteStart = 4 * x0;
        std::int64_t const byteEnd = (std::min)(byteStart + 4 * count, 4 * static_cast<std::int64_t>((std::min)(img1.width, img2.width)));

        std::uint64_t sum = 0;
        for (std::int64_t r = start; r < end; ++r)
        {
            std::int64_t const y1 = r + offset1;
            std::int64_t const y2 = r + offset2;
            if (y1 < 0 || y1 >= hs1 || y2 < 0 || y2 >= hs2)
            {
                continue; // outside the buffers (logged above)
            }
            std::uint8_t const* a = Row(img1, y1);
            std::uint8_t const* b = Row(img2, y2);
            for (std::int64_t x = byteStart; x < byteEnd; ++x)
            {
                sum += static_cast<std::uint64_t>(std::abs(static_cast<int>(a[x]) - static_cast<int>(b[x])));
                if (sum > threshold)
                {
                    return sum;
                }
            }
        }
        return sum;
    }

    // ---- ----

    std::optional<std::int64_t> OverlayShiftStamp(Bitmap const& img1, Bitmap const& img2, std::int64_t travel, int direction)
    {
        static std::wstring const kName = L"overlayShiftStamp(img1:img2:travel:direction:)";
        if (img1.IsEmpty() || img2.IsEmpty() || travel < 0)
        {
            return std::nullopt;
        }
        std::int64_t const hs1 = img1.height;
        std::int64_t const hs2 = img2.height;
        std::int64_t const half = Trunc(static_cast<double>(hs2) / 2.0);
        std::int64_t const len = half - travel;
        if (len < 0)
        {
            return std::nullopt; // traps; T <= H/3 keeps it positive
        }
        std::int64_t const m = (hs2 - len - travel) / 2;
        std::int64_t r1 = m;
        std::int64_t r2 = m;
        if (direction < 1)
        {
            r1 = hs1 - len - m;
            r2 = hs2 - len - travel - m;
        }
        std::int64_t const x0 = Trunc(img1.width * kFifteen);
        std::int64_t const count = Trunc(img1.width * 0.7);

        std::vector<double> p1(static_cast<std::size_t>(len));
        for (std::int64_t i = 0; i < len; ++i)
        {
            p1[static_cast<std::size_t>(i)] = Profile(img1, r1 + i, x0, count);
        }
        std::vector<double> p2(static_cast<std::size_t>(len + travel));
        for (std::int64_t j = 0; j < len + travel; ++j)
        {
            p2[static_cast<std::size_t>(j)] = Profile(img2, r2 + j, x0, count);
        }

        std::vector<std::pair<double, std::int64_t>> candidates;
        candidates.reserve(static_cast<std::size_t>(travel));
        for (std::int64_t s = 0; s < travel; ++s)
        {
            double score = 0.0;
            for (std::int64_t i = 0; i < len; ++i)
            {
                score += std::abs(p1[static_cast<std::size_t>(i)] - p2[static_cast<std::size_t>(s + i)]);
            }
            std::int64_t const overlap = direction < 0 ? s + hs2 - travel : hs2 - s;
            candidates.emplace_back(score, overlap);
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](auto const& a, auto const& b) { return a.first < b.first; });

        if (candidates.empty())
        {
            Log(kName, L"Stamp overlay: no candidates");
            return std::nullopt;
        }
        if (candidates.size() < 6)
        {
            return std::nullopt;
        }
        double const best = candidates[0].first;
        if (best * 1.2 < candidates[1].first && best * 1.4 < candidates[5].first)
        {
            return candidates[0].second;
        }
        std::wstring scores;
        for (std::size_t i = 0; i < 6; ++i)
        {
            scores += L", " + Number(candidates[i].first);
        }
        Log(kName, L"Stamp based overlay failed" + scores);
        return std::nullopt;
    }

    // ---- step 5 ----

    Overlap FindOverlap(Bitmap const& img1, Bitmap const& img2, std::int64_t travel, int direction)
    {
        if (auto const stamp = OverlayShiftStamp(img1, img2, travel, direction))
        {
            return { *stamp, true, 0 };
        }
        std::uint64_t best = 999'999'999'999;
        std::int64_t bestOverlap = 0;
        for (std::int64_t k = 1; k <= travel && best != 0; ++k)
        {
            std::int64_t const overlap = img2.height - travel + k;
            std::uint64_t const score = OverlayCompareSegment(img1, img2, overlap, travel, direction, best);
            if (score < best)
            {
                best = score;
                bestOverlap = overlap;
            }
        }
        Log(L"findShift", L"MIN: " + std::to_wstring(best) + L" " + std::to_wstring(bestOverlap));
        return { bestOverlap, false, best };
    }

    // ---- ----

    Compositor::Compositor(Bitmap const& first, std::int64_t margin, int direction)
        : m_width(first.width), m_dpr(first.dpr), m_margin(margin), m_up(direction >= 1), m_height(first.height)
    {
        if (m_up)
        {
            // shots[0] at y = 0; then y += Hs(drawn) - M.
            DrawUp(first, 0);
            m_cursor = first.height - margin;
        }
        else
        {
            DrawDown(first, 0);
            m_cursor = first.height; // bottom edge of the last drawn image
        }
    }

    void Compositor::Add(Bitmap const& strip, std::int64_t advance)
    {
        m_height += advance;
        if (m_up)
        {
            DrawUp(strip, m_cursor);
            m_cursor += strip.height - m_margin;
        }
        else
        {
            m_cursor += advance;
            DrawDown(strip, m_cursor - strip.height);
        }
    }

    Bitmap Compositor::Result() const
    {
        Bitmap out = Transparent(m_width, m_height, m_dpr);
        std::size_t const stride = out.Stride();
        std::int64_t const rows = (std::min)(m_height, m_stored);
        for (std::int64_t g = 0; g < rows; ++g)
        {
            std::int64_t const y = m_up ? m_height - 1 - g : g;
            std::memcpy(out.pixels.data() + static_cast<std::size_t>(y) * stride, m_rows.data() + static_cast<std::size_t>(g) * stride, stride);
        }
        return out;
    }

    std::uint8_t* Compositor::Ensure(std::int64_t g)
    {
        std::size_t const stride = static_cast<std::size_t>(m_width) * 4;
        if (g >= m_stored)
        {
            std::size_t const needed = static_cast<std::size_t>(g + 1) * stride;
            if (m_rows.capacity() < needed)
            {
                m_rows.reserve((std::max)(needed, m_rows.capacity() * 3 / 2));
            }
            m_rows.resize(needed, 0);
            m_stored = g + 1;
        }
        return m_rows.data() + static_cast<std::size_t>(g) * stride;
    }

    void Compositor::DrawDown(Bitmap const& image, std::int64_t top)
    {
        std::size_t const pixels = static_cast<std::size_t>((std::min)(m_width, image.width));
        for (std::int64_t j = 0; j < image.height; ++j)
        {
            std::int64_t const g = top + j;
            if (g >= 0)
            {
                BlendRow(Ensure(g), Row(image, j), pixels);
            }
        }
    }

    void Compositor::DrawUp(Bitmap const& image, std::int64_t y)
    {
        std::size_t const pixels = static_cast<std::size_t>((std::min)(m_width, image.width));
        for (std::int64_t j = 0; j < image.height; ++j)
        {
            std::int64_t const g = y + image.height - 1 - j;
            if (g >= 0)
            {
                BlendRow(Ensure(g), Row(image, j), pixels);
            }
        }
    }


    Bitmap Combine(std::span<Bitmap const> shots, std::span<std::int64_t const> advances, std::int64_t margin, int direction)
    {
        if (shots.empty())
        {
            return {};
        }
        Compositor compositor{ shots[0], margin, direction };
        for (std::size_t i = 1; i < shots.size(); ++i)
        {
            compositor.Add(shots[i], i - 1 < advances.size() ? advances[i - 1] : 0);
        }
        return compositor.Result();
    }

    // ---- steps 7-8 ----

    Bitmap ComposeFinal(Bitmap const& combined, Bitmap const& last, std::int64_t overlap, std::int64_t stepPoints, int direction)
    {
        std::int64_t const hc = combined.height;
        std::int64_t const hl = last.height;
        std::int64_t const height = hc + hl - overlap;
        Bitmap out = Transparent(combined.width, height, combined.dpr);
        std::int64_t const keep = (std::min)((hl - stepPoints) / 2, hl);
        if (direction < 1)
        {
            DrawRows(out, combined, 0, hc, 0);
            DrawRows(out, last, hl - keep, keep, height - keep);
        }
        else
        {
            DrawRows(out, combined, 0, hc, height - hc);
            DrawRows(out, last, 0, keep, 0);
        }
        return out;
    }

    // ---- ----

    int NotchesPerStep(std::int64_t targetTravel, std::int64_t notchTravel, std::int64_t heightPx)
    {
        if (notchTravel <= 0)
        {
            return 1;
        }
        std::int64_t const third = heightPx / 3;
        std::int64_t n = (std::max)(std::int64_t{ 1 }, targetTravel / notchTravel);
        while (n > 1 && n * notchTravel > third)
        {
            --n;
        }
        return n * notchTravel > third ? 0 : static_cast<int>(n);
    }

    // ---- ----

    std::optional<int> AlignRows(Bitmap const& prev, Bitmap const& next)
    {
        if (prev.IsEmpty() || next.IsEmpty() || prev.width != next.width || prev.height != next.height)
        {
            return std::nullopt;
        }
        std::int64_t const ix = next.width / 7;
        std::int64_t const iy = next.height / 5;
        std::int64_t const w = next.width - 2 * ix;
        std::int64_t const h = next.height - 2 * iy;
        if (w <= 0 || h <= 0)
        {
            return std::nullopt;
        }
        Descriptors const dp = Describe(prev, ix, iy, w, h);
        Descriptors const dn = Describe(next, ix, iy, w, h);
        constexpr double kBlankVariance = 0.5;
        if (Variance(dp) < kBlankVariance && Variance(dn) < kBlankVariance)
        {
            return 0;
        }

        constexpr int bins = Descriptors::kBins;
        std::int64_t const ovMin = (std::max)(std::int64_t{ 16 }, h / 4);
        std::int64_t const range = (std::max)(std::int64_t{ 0 }, h - ovMin);
        struct Candidate
        {
            double cost;
            std::int64_t d;
        };
        std::vector<Candidate> costs;
        costs.reserve(static_cast<std::size_t>(2 * range + 1));
        for (std::int64_t d = -range; d <= range; ++d)
        {
            std::int64_t const y0 = (std::max)(std::int64_t{ 0 }, -d);
            std::int64_t const y1 = (std::min)(h, h - d);
            if (y1 <= y0)
            {
                continue;
            }
            double total = 0.0;
            for (std::int64_t y = y0; y < y1; ++y)
            {
                float const* a = dn.values.data() + static_cast<std::size_t>(y) * bins;
                float const* b = dp.values.data() + static_cast<std::size_t>(y + d) * bins;
                float row = 0.0f;
                for (int k = 0; k < bins; ++k)
                {
                    row += std::abs(a[k] - b[k]);
                }
                total += row;
            }
            costs.push_back({ total / static_cast<double>(y1 - y0), d });
        }
        if (costs.empty())
        {
            return 0;
        }
        auto const better = [](Candidate const& a, Candidate const& b) {
            if (a.cost != b.cost)
            {
                return a.cost < b.cost;
            }
            if (std::llabs(a.d) != std::llabs(b.d))
            {
                return std::llabs(a.d) < std::llabs(b.d);
            }
            return a.d < b.d;
        };
        std::size_t const top = (std::min)(costs.size(), std::size_t{ 3 });
        std::partial_sort(costs.begin(), costs.begin() + static_cast<std::ptrdiff_t>(top), costs.end(), better);

        // Step 3: exact per-pixel SAD over the overlap on every 4th column.
        std::optional<Candidate> answer;
        for (std::size_t i = 0; i < top; ++i)
        {
            std::int64_t const d = costs[i].d;
            std::int64_t const y0 = (std::max)(std::int64_t{ 0 }, -d);
            std::int64_t const y1 = (std::min)(h, h - d);
            std::uint64_t sad = 0;
            std::uint64_t samples = 0;
            for (std::int64_t y = y0; y < y1; ++y)
            {
                std::uint8_t const* a = Row(next, iy + y) + ix * 4;
                std::uint8_t const* b = Row(prev, iy + y + d) + ix * 4;
                for (std::int64_t x = 0; x < w; x += 4)
                {
                    std::uint8_t const* pa = a + x * 4;
                    std::uint8_t const* pb = b + x * 4;
                    sad += static_cast<std::uint64_t>(std::abs(pa[0] - pb[0]) + std::abs(pa[1] - pb[1]) + std::abs(pa[2] - pb[2]));
                    ++samples;
                }
            }
            Candidate const exact{ samples ? static_cast<double>(sad) / static_cast<double>(samples) : 0.0, d };
            if (!answer || better(exact, *answer))
            {
                answer = exact;
            }
        }
        return static_cast<int>(answer->d);
    }

    // ---- ----

    Bitmap StitchManual(Bitmap const& base, Bitmap const& next, std::int64_t shift)
    {
        std::int64_t const magnitude = shift < 0 ? -shift : shift;
        std::int64_t const height = base.height + magnitude;
        Bitmap out = Transparent(base.width, height, base.dpr);
        out.screenRect = base.screenRect;
        // CG: base at y = max(shift, 0) from the bottom.
        DrawRows(out, base, 0, base.height, height - (std::max)(shift, std::int64_t{ 0 }) - base.height);
        std::int64_t const s = Trunc(next.height * 0.7);
        if (shift > 0)
        {
            DrawRows(out, next, next.height - s, s, height - s);
        }
        else
        {
            DrawRows(out, next, 0, s, 0);
        }
        return out;
    }

    // ---- / ----

    AutoScrollSession::AutoScrollSession(AutoScrollParams const& params, Bitmap first, std::uint32_t prevHash, std::uint32_t lastHash)
        : m_params(params), m_prevHash(prevHash), m_lastHash(lastHash)
    {
        double const dpr = params.dpr > 0 ? params.dpr : 1.0;
        m_params.dpr = dpr;
        m_heightPx = first.height;
        m_geometry = AutoScrollGeometry(params.speed, static_cast<double>(first.height) / dpr);
        m_target = (std::max)(std::int64_t{ 1 }, static_cast<std::int64_t>(std::llround(m_geometry.step * dpr)));
        m_marginPx = static_cast<std::int64_t>(std::llround(m_geometry.margin * dpr));
        m_canvas.emplace(first, m_marginPx, params.direction);
        m_previous = first;
        m_first = std::move(first);
    }

    ScrollRequest AutoScrollSession::NextScroll() const noexcept
    {
        return { m_method, m_units };
    }

    std::int64_t AutoScrollSession::Measure(Bitmap const& previous, Bitmap const& current)
    {
        std::int64_t const third = current.height / 3;
        if (m_unitTravel == 0 || m_unitTravel > third)
        {
            // Calibration, or increments too large for matchers.
            auto const ty = AlignRows(previous, current);
            std::int64_t const travel = ty ? (m_params.direction < 0 ? *ty : -*ty) : 0;
            if (m_unitTravel == 0 && travel > 0)
            {
                if (m_method == ScrollMethod::Wheel)
                {
                    int const n = NotchesPerStep(m_target, travel, current.height);
                    if (n == 0)
                    {
                        m_method = ScrollMethod::Automation; // the increment is calibrated next
                        m_units = 1;
                    }
                    else
                    {
                        m_unitTravel = travel;
                        m_units = n;
                    }
                }
                else
                {
                    m_unitTravel = travel;
                    m_units = 1;
                }
            }
            return (std::max)(travel, std::int64_t{ 0 });
        }
        std::int64_t const tmax = (std::min)(third, 2 * m_units * m_unitTravel);
        Overlap const found = FindOverlap(previous, current, tmax, m_params.direction);
        return current.height - found.overlap;
    }

    void AutoScrollSession::AppendStrip(Bitmap const& frame, std::int64_t travel)
    {
        std::int64_t const rows = (std::min)(travel + m_marginPx, static_cast<std::int64_t>(frame.height));
        Bitmap strip = m_params.direction < 1 ? CropRows(frame, frame.height - rows, rows) : CropRows(frame, 0, rows);
        // The previous strip is final now; the newest stays apart because the end path drops it.
        if (m_pending)
        {
            m_canvas->Add(*m_pending, m_pendingAdvance);
        }
        m_pending = std::move(strip);
        m_pendingAdvance = travel;
        ++m_strips;
    }

    Bitmap AutoScrollSession::CombineAll()
    {
        if (m_pending)
        {
            m_canvas->Add(*m_pending, m_pendingAdvance);
            m_pending.reset();
        }
        Bitmap result = m_canvas->Result();
        result.dpr = m_params.dpr;
        result.screenRect = m_first.screenRect;
        return result;
    }

    Bitmap AutoScrollSession::EndReached(Bitmap const& last)
    {
        // findShift: drop the last two shots (this step's, which is not kept, and the previous one).
        Bitmap combined;
        if (m_strips == 0)
        {
            Log(L"combine", L"OOPS: Trying to combine zero images");
            combined = m_first;
        }
        else
        {
            combined = m_canvas->Result();
        }

        // T = step*trunc(dpr). Here: the calibrated travel per step, or the last measured
        // travel when that was larger, so the search covers the last real scroll.
        std::int64_t travel = m_target;
        if (m_unitTravel > 0)
        {
            travel = (std::max)(m_units * m_unitTravel, m_pending ? m_pendingAdvance : 0);
        }
        travel = std::clamp<std::int64_t>(travel, 1, (std::max)(std::int64_t{ 1 }, static_cast<std::int64_t>(last.height) / 3));
        Overlap const found = FindOverlap(combined, last, travel, m_params.direction);
        Log(L"findShift", std::wstring{ L"overlay algo " } + (found.fast ? L"fast" : L"fallback"));
        Bitmap result = ComposeFinal(combined, last, found.overlap, m_geometry.step, m_params.direction);
        result.dpr = m_params.dpr;
        result.screenRect = m_first.screenRect;
        return result;
    }

    AutoStepResult AutoScrollSession::Step(std::optional<Bitmap> frame, bool stopRequested)
    {
        AutoStepResult result;
        ++m_counter;
        std::uint32_t const newHash = frame ? CentreHash(*frame) : 0;
        if (frame)
        {
            ++m_shotCount;
        }
        double const estimate = static_cast<double>(m_heightPx) + m_params.dpr * m_geometry.step * static_cast<double>(m_shotCount);
        bool const atMax = estimate >= static_cast<double>(m_params.scrollingMax);

        enum class Outcome
        {
            Continue,
            End,
            Interrupt,
        } outcome;
        if (atMax)
        {
            outcome = newHash == m_lastHash ? Outcome::End : Outcome::Interrupt;
            result.maxHeightNotice = outcome == Outcome::Interrupt;
        }
        else if (newHash != m_lastHash)
        {
            outcome = stopRequested ? Outcome::Interrupt : Outcome::Continue;
        }
        else
        {
            outcome = Outcome::End;
        }

        if (outcome != Outcome::End && frame)
        {
            std::int64_t const travel = Measure(*m_previous, *frame);
            AppendStrip(*frame, travel);
            m_previous = std::move(frame);
        }

        switch (outcome)
        {
        case Outcome::Continue:
            m_prevHash = m_lastHash;
            m_lastHash = newHash;
            return result;
        case Outcome::Interrupt:
            Log(L"step", L"Max img height reach or canceled");
            result.finished = true;
            result.stopped = atMax ? AutoStop::MaxHeight : (stopRequested ? AutoStop::Interrupted : AutoStop::None);
            result.image = CombineAll();
            return result;
        case Outcome::End:
        default:
            result.finished = true;
            if (newHash == m_prevHash)
            {
                Log(L"step", L"Scroll stopped without starting");
                result.stopped = AutoStop::WithoutStarting;
                result.cantScrollAlert = true;
                result.image = m_first;
            }
            else
            {
                Log(L"step", L"Scroll stopped, will do comparison pass");
                result.stopped = AutoStop::EndReached;
                // lastFrame: this step's full frame (the previous one if this capture failed).
                result.image = EndReached(frame ? *frame : *m_previous);
            }
            return result;
        }
    }

    // ---- ----

    void ManualSession::Begin(double now, double regionHeightPoints)
    {
        if (m_capturing)
        {
            return;
        }
        m_regionHeight = regionHeightPoints;
        m_lastScreenshot.reset();
        m_stitched.reset();
        m_shifts.clear();
        m_nilShifts = 0;
        m_direction = 0;
        m_cancelPlease = false;
        m_capturing = true;
        m_lastShiftDetectedTime = now;
    }

    void ManualSession::Stop()
    {
        m_capturing = false;
        m_lastScreenshot.reset();
        m_stitched.reset();
        m_lastShiftDetectedTime.reset();
    }

    ManualTickResult ManualSession::Finish(bool error)
    {
        ManualTickResult result;
        result.finished = true;
        result.error = error;
        result.image = std::move(m_stitched);
        Stop();
        return result;
    }

    ManualTickResult ManualSession::Tick(std::optional<Bitmap> frame, double now)
    {
        if (!m_capturing)
        {
            return {};
        }
        if (m_cancelPlease)
        {
            return Finish();
        }
        if (!frame || frame->IsEmpty())
        {
            return {};
        }
        if (!m_lastScreenshot)
        {
            m_lastScreenshot = *frame;
            m_stitched = std::move(frame);
            return {};
        }
        if (m_lastShiftDetectedTime)
        {
            std::int64_t const magnitude = m_direction < 0 ? -m_direction : m_direction;
            double const limit = magnitude > 100 ? kNoShiftTimeout : kStartTimeout;
            if (now - *m_lastShiftDetectedTime > limit)
            {
                // The rest of tick works on a stopped session and is discarded.
                return Finish();
            }
        }
        double h = m_regionHeight;
        if (m_lastScreenshot->height > Trunc(h))
        {
            h = m_lastScreenshot->height;
        }

        if (m_lastScreenshot->width != frame->width || m_lastScreenshot->height != frame->height)
        {
            // A frame of another size: finish with what is stitched, toast and beep.
            ++m_nilShifts;
            return Finish(true);
        }
        auto const aligned = AlignRows(*m_lastScreenshot, *frame);
        if (!aligned)
        {
            ++m_nilShifts;
            return {};
        }
        std::int64_t const shift = *aligned;
        if (shift != 0)
        {
            m_shifts.push_back(shift);
            if ((m_direction < 0 ? -m_direction : m_direction) < 100)
            {
                m_direction += shift;
            }
        }

        ManualTickResult result;
        if ((m_direction < 0 ? -m_direction : m_direction) > 99 && now - m_lastWarning > 1.2)
        {
            result.label = m_direction < 1 ? ManualLabel::KeepUp : ManualLabel::KeepDown;
        }
        if ((shift < 0 ? -shift : shift) > Trunc(h / 3.0))
        {
            result.label = ManualLabel::TooFast;
            m_lastWarning = now;
        }

        if (shift == 0)
        {
            m_lastScreenshot = std::move(frame);
            return result;
        }
        if ((shift > 0 && m_direction < 0) || (shift < 0 && m_direction > 0))
        {
            return result; // scrolling back: ignored, lastScreenshot unchanged
        }
        m_lastShiftDetectedTime = now;
        m_stitched = StitchManual(m_stitched ? *m_stitched : *m_lastScreenshot, *frame, shift);
        m_lastScreenshot = std::move(frame);
        return result;
    }
}
