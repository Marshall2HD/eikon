#include "pch.h"
#include "TestFramework.h"

#include "Core/Stitch.h"

#include <cstring>
#include <string_view>

using namespace Eikon::Core;
using namespace Eikon::Core::Stitch;

namespace
{
    std::uint32_t Mix(std::uint32_t a, std::uint32_t b)
    {
        std::uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
        h ^= h >> 15;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;
        h *= 0x297A2D39u;
        h ^= h >> 15;
        return h;
    }

    void SetPixel(Bitmap& b, std::int64_t x, std::int64_t y, std::uint8_t blue, std::uint8_t green, std::uint8_t red, std::uint8_t alpha = 255)
    {
        std::uint8_t* p = b.pixels.data() + static_cast<std::size_t>(y) * b.Stride() + static_cast<std::size_t>(x) * 4;
        p[0] = blue;
        p[1] = green;
        p[2] = red;
        p[3] = alpha;
    }

    std::uint8_t const* PixelAt(Bitmap const& b, std::int64_t x, std::int64_t y)
    {
        return b.pixels.data() + static_cast<std::size_t>(y) * b.Stride() + static_cast<std::size_t>(x) * 4;
    }

    // A text-like document: 14-row "lines" of random dark/light glyph columns of varying length,
    // separated by 6 blank rows (so many rows are identical, like real pages).
    Bitmap MakePage(int width, int height, std::uint32_t seed)
    {
        Bitmap page = Bitmap::Create(width, height);
        for (int y = 0; y < height; ++y)
        {
            int const line = y / 20;
            bool const ink = y % 20 < 14;
            int const length = 30 + static_cast<int>(Mix(static_cast<std::uint32_t>(line), seed) % static_cast<std::uint32_t>(width - 60));
            for (int x = 0; x < width; ++x)
            {
                std::uint8_t v = 250;
                if (ink && x >= 12 && x < 12 + length && (Mix(static_cast<std::uint32_t>(x / 3 + line * 977), static_cast<std::uint32_t>(y) ^ seed) & 3) == 0)
                {
                    v = static_cast<std::uint8_t>(20 + Mix(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)) % 60);
                }
                SetPixel(page, x, y, v, v, v);
            }
        }
        return page;
    }

    // Every pixel random: no two rows alike, so the stamp never sees a clear winner by accident.
    Bitmap MakeNoise(int width, int height, std::uint32_t seed)
    {
        Bitmap page = Bitmap::Create(width, height);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                std::uint32_t const h = Mix(static_cast<std::uint32_t>(x + y * 7919), seed);
                SetPixel(page, x, y, static_cast<std::uint8_t>(h), static_cast<std::uint8_t>(h >> 8), static_cast<std::uint8_t>(h >> 16));
            }
        }
        return page;
    }

    // A bar (toolbar / status bar): coloured, with a few random marks so its rows are distinct.
    Bitmap MakeBar(int width, int height, std::uint32_t seed)
    {
        Bitmap bar = Bitmap::Create(width, height);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                bool const mark = (Mix(static_cast<std::uint32_t>(x / 5), static_cast<std::uint32_t>(y) + seed) % 7) == 0;
                SetPixel(bar, x, y, mark ? 30 : 200, mark ? 30 : 120, mark ? 30 : 40);
            }
        }
        return bar;
    }

    // Stacks images vertically.
    Bitmap Stack(std::initializer_list<Bitmap const*> parts)
    {
        int width = 0;
        int height = 0;
        for (auto const* part : parts)
        {
            width = (std::max)(width, part->width);
            height += part->height;
        }
        Bitmap out = Bitmap::Create(width, height);
        std::size_t offset = 0;
        for (auto const* part : parts)
        {
            std::memcpy(out.pixels.data() + offset, part->pixels.data(), part->pixels.size());
            offset += part->pixels.size();
        }
        return out;
    }

    // The visible region of a scrolled page: header, `rows` rows of content from `offset`, footer.
    struct Viewport
    {
        Bitmap const* content = nullptr;
        Bitmap header;
        Bitmap footer;
        int height = 0;
        double dpr = 1.0;

        int ContentRows() const { return height - header.height - footer.height; }
        int MaxOffset() const { return content->height - ContentRows(); }

        Bitmap Frame(int offset) const
        {
            Bitmap rows = CropRows(*content, offset, ContentRows());
            Bitmap frame = Stack({ &header, &rows, &footer });
            frame.dpr = dpr;
            return frame;
        }
    };

    bool SameImage(Bitmap const& a, Bitmap const& b)
    {
        return a.width == b.width && a.height == b.height && a.pixels == b.pixels;
    }

    // The first row where the images differ (-1 when equal up to the shorter height).
    int FirstDifferentRow(Bitmap const& a, Bitmap const& b)
    {
        int const rows = (std::min)(a.height, b.height);
        for (int y = 0; y < rows; ++y)
        {
            if (std::memcmp(PixelAt(a, 0, y), PixelAt(b, 0, y), a.Stride()) != 0)
            {
                return y;
            }
        }
        return -1;
    }

    struct AutoRun
    {
        AutoStepResult result;
        int steps = 0;
        std::vector<ScrollRequest> scrolls;
    };

    // Drives an AutoScrollSession like Platform/ScrollCapture: post NextScroll (each notch moves the
    // page by `notch` px, clamped at the ends), then Step with the new frame.
    AutoRun RunAuto(Viewport const& view, int direction, int notch, std::int64_t maxHeight = 20000, int stopAtStep = 0,
                    std::int64_t speed = 2)
    {
        int offset = direction < 0 ? 0 : view.MaxOffset();
        Bitmap const first = view.Frame(offset);
        std::uint32_t const hash = CentreHash(view.Frame(offset));
        AutoScrollSession session{ { direction, view.dpr, speed, maxHeight }, first, hash, hash };
        AutoRun run;
        while (run.steps < 1000)
        {
            ScrollRequest const request = session.NextScroll();
            run.scrolls.push_back(request);
            int const travel = request.count * notch;
            offset = std::clamp(direction < 0 ? offset + travel : offset - travel, 0, view.MaxOffset());
            ++run.steps;
            run.result = session.Step(view.Frame(offset), run.steps == stopAtStep);
            if (run.result.finished)
            {
                break;
            }
        }
        return run;
    }

    Bitmap Solid(int width, int height, std::uint8_t value)
    {
        Bitmap b = Bitmap::Create(width, height);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                SetPixel(b, x, y, value, value, value);
            }
        }
        return b;
    }

    // Row y filled with base + y (mod 256) in B, G and R, and in alpha too unless opaque.
    Bitmap Rows(int width, int height, std::uint8_t base, bool opaque = true)
    {
        Bitmap b = Bitmap::Create(width, height);
        for (int y = 0; y < height; ++y)
        {
            std::uint8_t* row = b.pixels.data() + static_cast<std::size_t>(y) * b.Stride();
            std::memset(row, base + y, b.Stride());
            for (int x = 0; opaque && x < width; ++x)
            {
                row[x * 4 + 3] = 255;
            }
        }
        return b;
    }
}

// ---- ----

TEST(Stitch_GeometryTables)
{
    auto g = AutoScrollGeometry(2, 600);
    CHECK_EQ(g.delay, 0.12);
    CHECK_EQ(g.step, 100);
    CHECK_EQ(g.margin, 250);
    // The step is capped at floor(H/3); margin = trunc((trunc(H) - step) / 2).
    g = AutoScrollGeometry(2, 200);
    CHECK_EQ(g.step, 66);
    CHECK_EQ(g.margin, 67);
    g = AutoScrollGeometry(3, 900);
    CHECK_EQ(g.delay, 0.1);
    CHECK_EQ(g.step, 300);
    CHECK_EQ(g.margin, 300);
    g = AutoScrollGeometry(3, 899.5);
    CHECK_EQ(g.step, 299);
    CHECK_EQ(g.margin, 300);
    CHECK_EQ(AutoScrollGeometry(0, 600).delay, 0.8);
    CHECK_EQ(AutoScrollGeometry(1, 600).delay, 0.2);
    CHECK_EQ(AutoScrollGeometry(0, 600).step, 100);
    // Speeds outside 0...3 are clamped.
    CHECK_EQ(AutoScrollGeometry(7, 900).step, 300);
    CHECK_EQ(AutoScrollGeometry(-1, 900).delay, 0.8);
}

// ---- ----

TEST(Stitch_Crc32IsZlibs)
{
    std::string_view const text = "123456789";
    CHECK_EQ(Crc32({ reinterpret_cast<std::uint8_t const*>(text.data()), text.size() }), 0xCBF43926u);
    CHECK_EQ(Crc32({}), 0u);
    // Chaining equals one pass.
    std::uint32_t const part = Crc32({ reinterpret_cast<std::uint8_t const*>(text.data()), 4 });
    CHECK_EQ(Crc32({ reinterpret_cast<std::uint8_t const*>(text.data()) + 4, 5 }, part), 0xCBF43926u);
}

TEST(Stitch_CentreHashCoversOnlyTheCentreSeventyPercent)
{
    // 100 x 50: columns [15, 85), rows [7, 42) (trunc(50 * 0.15000000000000002) = 7, trunc(35.0) = 35).
    Bitmap const base = MakeNoise(100, 50, 3);
    std::uint32_t const hash = CentreHash(base);
    auto changed = [&](int x, int y) {
        Bitmap copy = base;
        SetPixel(copy, x, y, 1, 2, 3);
        return CentreHash(copy) != hash;
    };
    CHECK(!changed(14, 25));
    CHECK(changed(15, 25));
    CHECK(changed(84, 25));
    CHECK(!changed(85, 25));
    CHECK(!changed(50, 6));
    CHECK(changed(50, 7));
    CHECK(changed(50, 41));
    CHECK(!changed(50, 42));
    CHECK_EQ(CentreHash(Bitmap{}), 0u);
}

// ---- ----

TEST(Stitch_CompareSegmentRowsAndColumns)
{
    // Width 10: bytes [4 * 1, 4 * 1 + 4 * 7) = 28 bytes per row, alpha included.
    Bitmap zeros = Bitmap::Create(10, 10);
    std::fill(zeros.pixels.begin(), zeros.pixels.end(), std::uint8_t{ 0 });
    Bitmap const rows = Rows(10, 10, 1, false); // row r is r + 1 in every byte
    // Down, overlap 5, area 5: img2 rows [0, 5 - 1) -> 28 * (1 + 2 + 3 + 4).
    CHECK_EQ(OverlayCompareSegment(zeros, rows, 5, 5, -1, ~0ull), 280ull);
    // Up: mid = 0, rows [1, 5) of img1, img2 rows 10 - 5 + r = 6..9 -> 28 * (7 + 8 + 9 + 10).
    CHECK_EQ(OverlayCompareSegment(zeros, rows, 5, 5, 1, ~0ull), 952ull);
    // Early exit as soon as the sum passes the threshold (byte by byte).
    CHECK_EQ(OverlayCompareSegment(zeros, rows, 5, 5, -1, 10), 11ull);
    // area > overlap fails with the sentinel.
    CHECK_EQ(OverlayCompareSegment(zeros, rows, 4, 5, -1, ~0ull), 99'999'999ull);
    // An empty row range is 0 (area 0).
    CHECK_EQ(OverlayCompareSegment(zeros, rows, 5, 0, -1, ~0ull), 0ull);
    // Columns outside the centre 70 % are ignored.
    Bitmap edges = zeros;
    for (int y = 0; y < 10; ++y)
    {
        SetPixel(edges, 0, y, 9, 9, 9, 9);
        SetPixel(edges, 8, y, 9, 9, 9, 9);
        SetPixel(edges, 9, y, 9, 9, 9, 9);
    }
    CHECK_EQ(OverlayCompareSegment(zeros, edges, 5, 5, -1, ~0ull), 0ull);
}

// ---- / ----

TEST(Stitch_StampFindsKnownShiftsBothWays)
{
    Bitmap const page = MakePage(400, 1600, 11);
    for (int delta : { 1, 57, 99, 100 })
    {
        Bitmap const a = CropRows(page, 200, 600);
        Bitmap const b = CropRows(page, 200 + delta, 600);
        // Down: img1's bottom and img2's top share 600 - delta rows. Stamp range: delta in [1, T].
        CHECK_EQ(OverlayShiftStamp(a, b, 100, -1), std::optional<std::int64_t>{ 600 - delta });
        auto const found = FindOverlap(a, b, 100, -1);
        CHECK_EQ(found.overlap, std::int64_t{ 600 - delta });
        CHECK(found.fast);
    }
    for (int delta : { 0, 1, 57, 99 })
    {
        // Up: the content moved down; stamp range delta in [0, T - 1].
        Bitmap const a = CropRows(page, 600, 600);
        Bitmap const b = CropRows(page, 600 - delta, 600);
        CHECK_EQ(OverlayShiftStamp(a, b, 100, 1), std::optional<std::int64_t>{ 600 - delta });
    }
}

TEST(Stitch_StampNeedsSixCandidatesAndAClearWinner)
{
    Bitmap const page = MakePage(400, 1600, 5);
    Bitmap const a = CropRows(page, 0, 600);
    Bitmap const b = CropRows(page, 3, 600);
    CHECK_EQ(OverlayShiftStamp(a, b, 5, -1), std::optional<std::int64_t>{});   // T <= 5: no decision
    CHECK_EQ(OverlayShiftStamp(a, b, 6, -1), std::optional<std::int64_t>{ 597 });
    CHECK_EQ(OverlayShiftStamp(a, b, 0, -1), std::optional<std::int64_t>{});   // no candidates
}

TEST(Stitch_RepeatedContentFallsBackAndKeepsTheEarliestMatch)
{
    // Every 20-row block identical: shifts 40, 60 and 80 all match exactly.
    Bitmap const block = MakeNoise(300, 20, 9);
    Bitmap page = Bitmap::Create(300, 1200);
    for (int y = 0; y < 1200; ++y)
    {
        std::memcpy(page.pixels.data() + static_cast<std::size_t>(y) * page.Stride(), PixelAt(block, 0, y % 20), page.Stride());
    }
    Bitmap const a = CropRows(page, 0, 600);
    Bitmap const b = CropRows(page, 40, 600);
    CHECK_EQ(OverlayShiftStamp(a, b, 100, -1), std::optional<std::int64_t>{});  // best * 1.2 < second fails
    auto const found = FindOverlap(a, b, 100, -1);
    CHECK(!found.fast);
    CHECK_EQ(found.best, 0ull);
    // k = 1...T, o = Hs - T + k: the first zero is k = 20 (delta 80); ties keep the earlier k.
    CHECK_EQ(found.overlap, std::int64_t{ 600 - 80 });
}

TEST(Stitch_FallbackRangeIsOneBelowTheStamp)
{
    // Noise defeats the stamp; the fallback covers delta in [0, T - 1].
    Bitmap const page = MakeNoise(200, 900, 21);
    Bitmap const a = CropRows(page, 100, 300);
    auto same = FindOverlap(a, a, 100, -1);
    CHECK(!same.fast);
    CHECK_EQ(same.overlap, std::int64_t{ 300 });
    CHECK_EQ(same.best, 0ull);
    auto moved = FindOverlap(a, CropRows(page, 199, 300), 100, -1);
    CHECK_EQ(moved.overlap, std::int64_t{ 300 - 99 });
}

// ---- ----

TEST(Stitch_CombinePlacesStripsByDirection)
{
    // shots[0] 10 rows, strips T + M = 3 + 4 rows, T = 3.
    Bitmap const first = Solid(8, 10, 10);
    Bitmap const s1 = Solid(8, 7, 20);
    Bitmap const s2 = Solid(8, 7, 30);
    std::vector<Bitmap> const shots{ first, s1, s2 };
    std::vector<std::int64_t> const advances{ 3, 3 };

    Bitmap const down = Combine(shots, advances, 4, -1);
    CHECK_EQ(down.height, 16);
    CHECK_EQ(down.width, 8);
    for (int y = 0; y < 16; ++y)
    {
        // Strip i spans [Hs0 + i*T - (T + M), Hs0 + i*T): rows 6..12 and 9..15, later on top.
        int const expected = y < 6 ? 10 : (y < 9 ? 20 : 30);
        CHECK_EQ(static_cast<int>(PixelAt(down, 3, y)[0]), expected);
    }

    Bitmap const up = Combine(shots, advances, 4, 1);
    CHECK_EQ(up.height, 16);
    for (int y = 0; y < 16; ++y)
    {
        // shots[0] at the bottom (rows 6..15); strip 1 bottom at y-up 6 -> rows 3..9; strip 2 at y-up 9 -> rows 0..6.
        int const expected = y < 7 ? 30 : (y < 10 ? 20 : 10);
        CHECK_EQ(static_cast<int>(PixelAt(up, 3, y)[0]), expected);
    }

    // Measured advances: the canvas grows by each strip's own travel.
    std::vector<std::int64_t> const measured{ 2, 5 };
    std::vector<Bitmap> const shots2{ first, Solid(8, 6, 20), Solid(8, 9, 30) };
    Bitmap const varied = Combine(shots2, measured, 4, -1);
    CHECK_EQ(varied.height, 17);
    CHECK_EQ(static_cast<int>(PixelAt(varied, 0, 5)[0]), 10);
    CHECK_EQ(static_cast<int>(PixelAt(varied, 0, 6)[0]), 20);  // strip 1: [12 - 6, 12)
    CHECK_EQ(static_cast<int>(PixelAt(varied, 0, 7)[0]), 20);
    CHECK_EQ(static_cast<int>(PixelAt(varied, 0, 8)[0]), 30);  // strip 2: [17 - 9, 17)
    CHECK(Combine({}, {}, 4, -1).IsEmpty());
}

// ---- steps 7-8 ----

TEST(Stitch_ComposeFinalKeepsHalfOfTheLastFrameInPoints)
{
    Bitmap const c = Solid(6, 20, 10);
    Bitmap const l = Rows(6, 12, 100);
    // Hf = 20 + 12 - 9 = 23; keep = (12 - 2) / 2 = 5 rows of L.
    Bitmap const down = ComposeFinal(c, l, 9, 2, -1);
    CHECK_EQ(down.height, 23);
    CHECK_EQ(static_cast<int>(PixelAt(down, 0, 17)[0]), 10);
    CHECK_EQ(static_cast<int>(PixelAt(down, 0, 18)[0]), 107);  // L row 7
    CHECK_EQ(static_cast<int>(PixelAt(down, 0, 22)[0]), 111);  // L row 11
    Bitmap const up = ComposeFinal(c, l, 9, 2, 1);
    CHECK_EQ(up.height, 23);
    CHECK_EQ(static_cast<int>(PixelAt(up, 0, 0)[0]), 100);     // L row 0
    CHECK_EQ(static_cast<int>(PixelAt(up, 0, 4)[0]), 104);
    CHECK_EQ(static_cast<int>(PixelAt(up, 0, 5)[0]), 10);
    CHECK_EQ(static_cast<int>(PixelAt(up, 0, 22)[0]), 10);

    // 2x: step stays in points, so keep = (1200 - 100) / 2 = 550 rows, not 500.
    Bitmap const big = Rows(4, 1200, 0);
    Bitmap const top = Solid(4, 1300, 255);
    Bitmap const out = ComposeFinal(top, big, 1100, 100, -1);
    CHECK_EQ(out.height, 1400);
    CHECK_EQ(static_cast<int>(PixelAt(out, 0, 1400 - 551)[0]), 255);
    CHECK_EQ(static_cast<int>(PixelAt(out, 0, 1400 - 550)[0]), (1200 - 550) & 0xFF);
}

// ---- ----

TEST(Stitch_NotchesPerStep)
{
    CHECK_EQ(NotchesPerStep(100, 37, 600), 2);    // floor(100 / 37)
    CHECK_EQ(NotchesPerStep(100, 120, 600), 1);   // at least one notch
    CHECK_EQ(NotchesPerStep(100, 200, 600), 1);   // 200 <= floor(600 / 3)
    CHECK_EQ(NotchesPerStep(100, 201, 600), 0);   // one notch already moves more than H/3
    CHECK_EQ(NotchesPerStep(300, 40, 600), 5);    // 7 notches = 280 > 200, reduced to 5
    CHECK_EQ(NotchesPerStep(200, 19, 1200), 10);
}

// ---- /: whole sessions on simulated pages ----

TEST(Stitch_AutoScrollDownRebuildsThePage)
{
    Bitmap const page = MakePage(400, 3000, 1);
    Viewport const view{ &page, {}, {}, 600, 1.0 };
    AutoRun const run = RunAuto(view, -1, 37);
    CHECK(run.result.finished);
    CHECK(run.result.stopped == AutoStop::EndReached);
    CHECK(!run.result.cantScrollAlert);
    CHECK(!run.result.maxHeightNotice);
    // Calibration: one notch first, then floor(100 / 37) = 2 notches per step.
    CHECK_EQ(run.scrolls[0].count, 1);
    CHECK_EQ(run.scrolls[1].count, 2);
    CHECK(run.scrolls[1].method == ScrollMethod::Wheel);
    CHECK(run.result.image.has_value());
    if (run.result.image)
    {
        CHECK_EQ(run.result.image->height, 3000);
        CHECK_EQ(FirstDifferentRow(*run.result.image, page), -1);
        CHECK(SameImage(*run.result.image, page));
    }
}

TEST(Stitch_AutoScrollUpRebuildsThePage)
{
    Bitmap const page = MakePage(400, 3000, 2);
    Viewport const view{ &page, {}, {}, 600, 1.0 };
    AutoRun const run = RunAuto(view, 1, 37);
    CHECK(run.result.stopped == AutoStop::EndReached);
    CHECK(run.result.image.has_value());
    if (run.result.image)
    {
        CHECK_EQ(run.result.image->height, 3000);
        CHECK_EQ(FirstDifferentRow(*run.result.image, page), -1);
    }
}

TEST(Stitch_AutoScrollAtTwoXKeepsPixelUnits)
{
    Bitmap const page = MakePage(500, 4000, 3);
    Viewport const view{ &page, {}, {}, 1200, 2.0 };
    AutoRun const run = RunAuto(view, -1, 37);
    CHECK(run.result.stopped == AutoStop::EndReached);
    CHECK_EQ(run.scrolls[1].count, 5);  // T* = round(100 * 2) = 200 -> floor(200 / 37)
    if (run.result.image)
    {
        CHECK_EQ(run.result.image->height, 4000);
        CHECK_EQ(run.result.image->dpr, 2.0);
        CHECK_EQ(FirstDifferentRow(*run.result.image, page), -1);
    }
}

TEST(Stitch_AutoScrollKeepsOneStickyHeaderAndTheLastFooter)
{
    Bitmap const content = MakePage(400, 2530, 4);
    Viewport const view{ &content, MakeBar(400, 40, 1), MakeBar(400, 30, 2), 600, 1.0 };
    AutoRun const run = RunAuto(view, -1, 37);
    CHECK(run.result.stopped == AutoStop::EndReached);
    Bitmap const expected = Stack({ &view.header, &content, &view.footer });
    if (run.result.image)
    {
        CHECK_EQ(run.result.image->height, expected.height);
        CHECK_EQ(FirstDifferentRow(*run.result.image, expected), -1);
    }
}

TEST(Stitch_AutoScrollStopsAtMaxHeight)
{
    Bitmap const page = MakePage(400, 3000, 6);
    Viewport const view{ &page, {}, {}, 600, 1.0 };
    // est = 600 + 100 * shots.count reaches 1000 when shots.count = 4, at the third step.
    AutoRun const run = RunAuto(view, -1, 37, 1000);
    CHECK_EQ(run.steps, 3);
    CHECK(run.result.stopped == AutoStop::MaxHeight);
    CHECK(run.result.maxHeightNotice);
    if (run.result.image)
    {
        // All shots, measured: 600 + 37 + 74 + 74.
        CHECK_EQ(run.result.image->height, 785);
        CHECK_EQ(FirstDifferentRow(*run.result.image, CropRows(page, 0, 785)), -1);
    }
    // One below the limit: still running at step 3.
    AutoRun const longer = RunAuto(view, -1, 37, 1001);
    CHECK(longer.steps > 3);
}

TEST(Stitch_AutoScrollInterruptedKeepsEverythingCaptured)
{
    Bitmap const page = MakePage(400, 3000, 7);
    Viewport const view{ &page, {}, {}, 600, 1.0 };
    AutoRun const run = RunAuto(view, -1, 37, 20000, 4);
    CHECK_EQ(run.steps, 4);
    CHECK(run.result.stopped == AutoStop::Interrupted);
    CHECK(!run.result.maxHeightNotice);
    if (run.result.image)
    {
        CHECK_EQ(run.result.image->height, 600 + 37 + 3 * 74);
        CHECK_EQ(FirstDifferentRow(*run.result.image, CropRows(page, 0, run.result.image->height)), -1);
    }
}

TEST(Stitch_AutoScrollOnAPageThatDoesNotScroll)
{
    Bitmap const page = MakePage(400, 600, 8);
    Viewport const view{ &page, {}, {}, 600, 1.0 };
    AutoRun const run = RunAuto(view, -1, 37);
    CHECK_EQ(run.steps, 1);
    CHECK(run.result.stopped == AutoStop::WithoutStarting);
    CHECK(run.result.cantScrollAlert);
    CHECK(run.result.image.has_value() && SameImage(*run.result.image, page));
}

TEST(Stitch_AutoScrollEndAtFirstStepUsesShotZero)
{
    // The two start samples differed (something animated), then nothing moved: end path with no
    // strips left after dropping two ("OOPS: Trying to combine zero images"), compared with itself.
    Bitmap const page = MakeNoise(300, 600, 12);
    AutoScrollSession session{ { -1, 1.0, 2, 20000 }, page, 1, CentreHash(page) };
    auto const result = session.Step(page, false);
    CHECK(result.finished);
    CHECK(result.stopped == AutoStop::EndReached);
    CHECK(result.image.has_value() && SameImage(*result.image, page));
}

TEST(Stitch_AutoScrollStopIsIgnoredWhenNothingMoved)
{
    Bitmap const page = MakePage(400, 600, 13);
    Viewport const view{ &page, {}, {}, 600, 1.0 };
    AutoRun const run = RunAuto(view, -1, 37, 20000, 1);
    CHECK(run.result.stopped == AutoStop::WithoutStarting);
}

TEST(Stitch_AutoScrollFallsBackToAutomationForHugeNotches)
{
    Bitmap const page = MakePage(400, 3000, 14);
    Viewport const view{ &page, {}, {}, 600, 1.0 };
    // A 250 px notch is more than 600 / 3: the next scroll is a UI Automation increment.
    int offset = 0;
    std::uint32_t const hash = CentreHash(view.Frame(0));
    AutoScrollSession session{ { -1, 1.0, 2, 20000 }, view.Frame(0), hash, hash };
    offset += 250;
    auto const result = session.Step(view.Frame(offset), false);
    CHECK(!result.finished);
    CHECK(session.NextScroll().method == ScrollMethod::Automation);
    CHECK_EQ(session.NextScroll().count, 1);
}

// ---- ----

TEST(Stitch_AlignRowsMeasuresPureTranslations)
{
    Bitmap const page = MakePage(420, 2000, 15);
    Bitmap const a = CropRows(page, 500, 700);
    CHECK_EQ(AlignRows(a, CropRows(page, 557, 700)), std::optional<int>{ 57 });   // scrolled down
    CHECK_EQ(AlignRows(a, CropRows(page, 443, 700)), std::optional<int>{ -57 });  // scrolled up
    CHECK_EQ(AlignRows(a, a), std::optional<int>{ 0 });
    CHECK_EQ(AlignRows(a, CropRows(page, 501, 700)), std::optional<int>{ 1 });
    // Beyond a third of the frame (too fast) is still measured while the crops overlap.
    CHECK_EQ(AlignRows(a, CropRows(page, 800, 700)), std::optional<int>{ 300 });
    // Sticky header and footer stay outside the central crop.
    Viewport const view{ &page, MakeBar(420, 60, 3), MakeBar(420, 50, 4), 700, 1.0 };
    CHECK_EQ(AlignRows(view.Frame(100), view.Frame(180)), std::optional<int>{ 80 });
    // Blank content is 0; different sizes or an empty frame is nil.
    CHECK_EQ(AlignRows(Solid(300, 300, 250), Solid(300, 300, 250)), std::optional<int>{ 0 });
    CHECK_EQ(AlignRows(a, CropRows(page, 0, 699)), std::optional<int>{});
    CHECK_EQ(AlignRows(Bitmap{}, Bitmap{}), std::optional<int>{});
}

// ---- ----

TEST(Stitch_ManualStitchPastesSeventyPercent)
{
    Bitmap const base = Solid(5, 10, 10);
    Bitmap const next = Rows(5, 10, 100);
    // Down 3: base on top, the bottom trunc(7.0) = 7 rows of `next` (rows 3..9) at the bottom.
    Bitmap const down = StitchManual(base, next, 3);
    CHECK_EQ(down.height, 13);
    CHECK_EQ(static_cast<int>(PixelAt(down, 0, 5)[0]), 10);
    CHECK_EQ(static_cast<int>(PixelAt(down, 0, 6)[0]), 103);
    CHECK_EQ(static_cast<int>(PixelAt(down, 0, 12)[0]), 109);
    // Up 3: base at the bottom, the top 7 rows of `next` at the top.
    Bitmap const up = StitchManual(base, next, -3);
    CHECK_EQ(up.height, 13);
    CHECK_EQ(static_cast<int>(PixelAt(up, 0, 0)[0]), 100);
    CHECK_EQ(static_cast<int>(PixelAt(up, 0, 6)[0]), 106);
    CHECK_EQ(static_cast<int>(PixelAt(up, 0, 7)[0]), 10);
    // Too fast: |shift| > 0.7 H leaves a transparent gap.
    Bitmap const gap = StitchManual(base, next, 9);
    CHECK_EQ(gap.height, 19);
    CHECK_EQ(static_cast<int>(PixelAt(gap, 0, 9)[3]), 255);
    CHECK_EQ(static_cast<int>(PixelAt(gap, 0, 10)[3]), 0);
    CHECK_EQ(static_cast<int>(PixelAt(gap, 0, 11)[3]), 0);
    CHECK_EQ(static_cast<int>(PixelAt(gap, 0, 12)[0]), 103);
}

// ---- ----

TEST(Stitch_ManualSessionLocksDirectionAndStitches)
{
    Bitmap const page = MakePage(400, 3000, 16);
    auto frame = [&](int offset) { return CropRows(page, offset, 600); };
    ManualSession session;
    session.Begin(1000.0, 600);
    CHECK(session.Capturing());
    auto r = session.Tick(frame(0), 1000.3);        // first frame: just stored
    CHECK(!r.finished);
    CHECK(r.label == ManualLabel::None);
    r = session.Tick(frame(60), 1000.6);
    CHECK_EQ(session.Direction(), std::int64_t{ 60 });
    CHECK(r.label == ManualLabel::None);            // |direction| <= 99
    r = session.Tick(frame(120), 1000.9);
    CHECK_EQ(session.Direction(), std::int64_t{ 120 });
    CHECK(r.label == ManualLabel::KeepDown);
    r = session.Tick(frame(100), 1001.2);            // back up: ignored, direction locked
    CHECK_EQ(session.Direction(), std::int64_t{ 120 });
    CHECK_EQ(session.Shifts().size(), std::size_t{ 3 });
    r = session.Tick(frame(180), 1001.5);            // measured against frame(120), not frame(100)
    CHECK_EQ(session.Shifts().back(), std::int64_t{ 60 });
    r = session.Tick(frame(180), 1001.8);            // no shift: nothing stitched
    session.Cancel();                               // Esc finishes with what is stitched
    r = session.Tick(frame(400), 1002.1);
    CHECK(r.finished);
    CHECK(!r.error);
    CHECK(!session.Capturing());
    CHECK(r.image.has_value());
    if (r.image)
    {
        CHECK_EQ(r.image->height, 780);
        CHECK_EQ(FirstDifferentRow(*r.image, CropRows(page, 0, 780)), -1);
    }
}

TEST(Stitch_ManualSessionTooFastWarningHoldsForOnePointTwoSeconds)
{
    Bitmap const page = MakePage(400, 3000, 17);
    auto frame = [&](int offset) { return CropRows(page, offset, 600); };
    ManualSession session;
    session.Begin(9.7, 600);
    session.Tick(frame(0), 10.0);
    session.Tick(frame(150), 10.3);
    auto r = session.Tick(frame(150 + 201), 10.6);  // 201 > trunc(600 / 3)
    CHECK(r.label == ManualLabel::TooFast);
    r = session.Tick(frame(361), 10.9);
    CHECK(r.label == ManualLabel::None);            // within 1.2 s of the warning
    r = session.Tick(frame(371), 11.9);
    CHECK(r.label == ManualLabel::KeepDown);
    r = session.Tick(frame(371 + 200), 12.2);       // exactly a third is not too fast
    CHECK(r.label == ManualLabel::KeepDown);
}

TEST(Stitch_ManualSessionTimeouts)
{
    Bitmap const page = MakePage(400, 3000, 18);
    auto frame = [&](int offset) { return CropRows(page, offset, 600); };
    {
        // Not started (|direction| <= 100): 10 s.
        ManualSession session;
        session.Begin(0.0, 600);
        session.Tick(frame(0), 0.3);
        CHECK(!session.Tick(frame(0), 9.9).finished);
        CHECK(!session.Tick(frame(50), 10.0).finished);  // a shift of 50 resets the clock
        CHECK_EQ(session.Direction(), std::int64_t{ 50 });
        CHECK(!session.Tick(frame(50), 13.5).finished);  // still the 10 s limit at 50 px
        auto r = session.Tick(frame(50), 20.1);
        CHECK(r.finished);
        if (r.image)
        {
            CHECK_EQ(r.image->height, 650);
        }
    }
    {
        // Started (|direction| > 100): 3 s.
        ManualSession session;
        session.Begin(0.0, 600);
        session.Tick(frame(0), 0.3);
        session.Tick(frame(101), 0.6);
        CHECK(!session.Tick(frame(101), 3.5).finished);
        CHECK(session.Tick(frame(101), 3.7).finished);
    }
    {
        // Exactly 100 px keeps the 10 s limit.
        ManualSession session;
        session.Begin(0.0, 600);
        session.Tick(frame(0), 0.3);
        session.Tick(frame(100), 0.6);
        CHECK(!session.Tick(frame(100), 4.0).finished);
    }
}

TEST(Stitch_ManualSessionScrollingUp)
{
    Bitmap const page = MakePage(400, 3000, 19);
    auto frame = [&](int offset) { return CropRows(page, offset, 600); };
    ManualSession session;
    session.Begin(0.0, 600);
    session.Tick(frame(1000), 0.3);
    auto r = session.Tick(frame(880), 0.6);
    CHECK_EQ(session.Direction(), std::int64_t{ -120 });
    CHECK(r.label == ManualLabel::KeepUp);
    session.Tick(frame(940), 0.9);                   // down while locked up: dropped
    session.Tick(frame(800), 1.2);
    session.Cancel();
    r = session.Tick(std::nullopt, 1.5);
    CHECK(r.finished);
    if (r.image)
    {
        CHECK_EQ(r.image->height, 800);
        CHECK_EQ(FirstDifferentRow(*r.image, CropRows(page, 800, 800)), -1);
    }
}

TEST(Stitch_ManualSessionNilAndErrors)
{
    Bitmap const page = MakePage(400, 3000, 20);
    ManualSession session;
    session.Begin(0.0, 600);
    session.Tick(CropRows(page, 0, 600), 0.3);
    // A frame of another size: finish with what there is, toast.
    auto r = session.Tick(CropRows(page, 0, 500), 0.6);
    CHECK(r.finished);
    CHECK(r.error);
    CHECK(r.image.has_value() && r.image->height == 600);
    // A stopped session ignores ticks.
    CHECK(!session.Tick(CropRows(page, 0, 600), 0.9).finished);
}
