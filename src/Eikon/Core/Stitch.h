#pragma once

// Scrolling capture maths: auto-scroll constants, screen hash,
// row matchers and compositing, the Windows wheel calibration and measured-shift compositing,
// and manual mode's stitcher.
// Platform-neutral: Platform/ScrollCapture captures the frames, posts the scrolls and runs the
// timers; everything that decides what the image looks like is here.
//
// Images are Core::Bitmap (BGRA8, top-down rows). Y-up CG drawing is flipped once here;
// row indices in the matchers are data rows (top-down) byte loops. Directions use
// Sign: -1 = scroll down (reveal content below), +1 = scroll up.

#include "Core/Bitmap.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Eikon::Core::Stitch
{
    // ---- constants ----
    inline constexpr std::array<double, 4> kScrollDelays{ 0.8, 0.2, 0.12, 0.1 };   // d (s) per scrollingSpeed
    inline constexpr std::array<int, 4> kScrollSteps{ 100, 100, 100, 300 };        // step (pt) per scrollingSpeed

    struct AutoGeometry
    {
        double delay = 0.12;   // d
        int step = 100;        // points
        int margin = 0;        // "rectOffset", points
    };

    // step = min(stepTable[speed], trunc(floor(H/3))), margin = trunc((trunc(H) - step)/2), with H the
    // region height in points (DIPs). A speed outside 0...3 is clamped.
    AutoGeometry AutoScrollGeometry(std::int64_t speed, double heightPoints);

    // ---- step 8: the centre-70 % screen hash ----
    // zlib crc32(crc, bytes).
    std::uint32_t Crc32(std::span<std::uint8_t const> bytes, std::uint32_t crc = 0);
    // CRC-32 of the raw bytes of the centre trunc(W*0.7) x trunc(H*0.7) pixels starting at
    // (trunc(W*0.15), trunc(H*0.15)). 0 for an empty frame.
    std::uint32_t CentreHash(Bitmap const& frame);

    // Rows [top, top + count) of `image` (clipped), same dpr.
    Bitmap CropRows(Bitmap const& image, std::int64_t top, std::int64_t count);

    // ---- overlayCompareSegment ----
    // Sum of absolute byte differences (all four bytes) over the centre 70 % columns of img1, with
    // early exit once the sum exceeds `threshold`. 99 999 999 when overlap < area.
    std::uint64_t OverlayCompareSegment(Bitmap const& img1, Bitmap const& img2, std::int64_t overlap, std::int64_t area,
                                        int direction, std::uint64_t threshold);

    // ---- overlayShiftStamp ----
    // Row texture profiles; returns the overlap (rows shared by img1's bottom and img2's top when
    // scrolling down, img1's top and img2's bottom when scrolling up), or nullopt when the best
    // candidate is not clearly better than the second (x1.2) and the sixth (x1.4).
    std::optional<std::int64_t> OverlayShiftStamp(Bitmap const& img1, Bitmap const& img2, std::int64_t travel, int direction);

    // ---- step 5: stamp, then the brute-force fallback ----
    struct Overlap
    {
        std::int64_t overlap = 0;
        bool fast = false;              // algo "fast" (stamp) or "fallback"
        std::uint64_t best = 0;         // fallback's MIN (0 for the stamp)
    };
    Overlap FindOverlap(Bitmap const& img1, Bitmap const& img2, std::int64_t travel, int direction);

    // ---- combine, incrementally ----
    // The canvas grows away from a fixed edge, the top (down) or the bottom (up), so rows already
    // drawn keep their place while strips are added. Down: each strip's bottom edge is `advance`
    // rows below the previous one's. Up: rule, each strip is drawn
    // Hs(previous) - margin above the previous one. Height: Hs(first) + the sum of advances.
    class Compositor
    {
    public:
        Compositor(Bitmap const& first, std::int64_t margin, int direction);
        void Add(Bitmap const& strip, std::int64_t advance);
        std::int64_t Height() const noexcept { return m_height; }
        // The canvas, transparent where nothing was drawn.
        Bitmap Result() const;

    private:
        std::uint8_t* Ensure(std::int64_t row);
        void DrawDown(Bitmap const& image, std::int64_t top);
        void DrawUp(Bitmap const& image, std::int64_t bottomFromBottom);

        std::int32_t m_width = 0;
        double m_dpr = 1.0;
        std::int64_t m_margin = 0;
        bool m_up = false;
        std::int64_t m_height = 0;
        std::int64_t m_cursor = 0;
        std::vector<std::uint8_t> m_rows;   // rows in distance-from-the-fixed-edge order
        std::int64_t m_stored = 0;
    };

    // ---- combine ----
    // shots[0] is the first full frame, shots[i] (i >= 1) the strips. Down: strip i's bottom edge
    // is advances[i-1] rows below strip i-1's. Up: rule, each strip is
    // drawn Hs(previous) - margin above the previous one. Canvas: Ws(shots[0]) x (Hs(shots[0]) + sum).
    // Transparent where nothing is drawn. Empty `shots` gives an empty bitmap.
    Bitmap Combine(std::span<Bitmap const> shots, std::span<std::int64_t const> advances, std::int64_t margin, int direction);

    // ---- steps 7-8: final compose ----
    // Hf = Hs(C) + Hs(L) - overlap; keep = min(trunc((Hs(L) - stepPoints)/2), Hs(L)) rows of L (the
    // points-not-pixels quirk) drawn over C at the bottom (down) or top (up).
    Bitmap ComposeFinal(Bitmap const& combined, Bitmap const& last, std::int64_t overlap, std::int64_t stepPoints, int direction);

    // ---- wheel calibration ----
    // Notches per step n = max(1, floor(target / notchTravel)), reduced while n * notchTravel >
    // floor(heightPx/3). 0 when even one notch moves more than heightPx/3 (fall back to UI Automation).
    int NotchesPerStep(std::int64_t targetTravel, std::int64_t notchTravel, std::int64_t heightPx);

    // ---- Frame alignment ----
    // Vertical translation between two frames, measured on the central crops (insets trunc(W/7) and
    // trunc(H/5) from `next`'s size). Positive when the content moved up (the user scrolled down):
    // next[y] ~ prev[y + ty]. nullopt when the crop is empty or the frames differ in size.
    std::optional<int> AlignRows(Bitmap const& prev, Bitmap const& next);

    // ---- stitch ----
    // base grows by |shift| rows; the bottom (shift > 0) or top (shift <= 0) trunc(0.7*Hs(new)) rows
    // of `next` are drawn over the new end. A transparent gap stays when |shift| > 0.7*Hs(new).
    Bitmap StitchManual(Bitmap const& base, Bitmap const& next, std::int64_t shift);

    // ---- /: the auto-scroll session ----
    struct AutoScrollParams
    {
        int direction = -1;                 // -1 down, +1 up
        double dpr = 1.0;                   // the region's scale
        std::int64_t speed = 2;             // scrollingSpeed
        std::int64_t scrollingMax = 20000;  // px
    };

    enum class ScrollMethod
    {
        Wheel,        // SendInput wheel notches
        Automation,   // UI Automation ScrollPattern SmallIncrement (keys when unavailable)
    };

    struct ScrollRequest
    {
        ScrollMethod method = ScrollMethod::Wheel;
        int count = 1;   // notches (wheel) or increments (automation)
    };

    enum class AutoStop
    {
        None,
        EndReached,        // "endReached": comparison pass
        WithoutStarting,   // "withoutStarting": the first scroll changed nothing
        MaxHeight,         // "maxHeight"
        Interrupted,       // "interrupted": a key or click
    };

    struct AutoStepResult
    {
        bool finished = false;
        AutoStop stopped = AutoStop::None;
        bool maxHeightNotice = false;   // "Max height reached" notification (before finishing)
        bool cantScrollAlert = false;   // "Lucida wasn't able to scroll" alert (before finishing)
        std::optional<Bitmap> image;    // the result when finished
    };

    class AutoScrollSession
    {
    public:
        // `first` is shots[0]; the hashes are the two samples of the starting screen.
        AutoScrollSession(AutoScrollParams const& params, Bitmap first, std::uint32_t prevHash, std::uint32_t lastHash);

        AutoGeometry const& Geometry() const noexcept { return m_geometry; }
        std::int64_t TargetTravel() const noexcept { return m_target; }     // T* = round(step*dpr)
        std::int64_t MarginPx() const noexcept { return m_marginPx; }       // M = round(margin*dpr)
        std::int64_t NotchTravel() const noexcept { return m_unitTravel; }  // t1, 0 until calibrated
        int Segments() const noexcept { return m_counter; }

        // The scroll to post next: one wheel notch until calibrated, then n notches (or one UI
        // Automation increment when a notch moves more than a third of the region).
        ScrollRequest NextScroll() const noexcept;

        // One `step` with the full region frame captured now (nullopt if the capture failed)
        // and whether a key or click asked to stop.
        AutoStepResult Step(std::optional<Bitmap> frame, bool stopRequested);

    private:
        std::int64_t Measure(Bitmap const& previous, Bitmap const& current);
        void AppendStrip(Bitmap const& frame, std::int64_t travel);
        Bitmap CombineAll();
        Bitmap EndReached(Bitmap const& last);

        AutoScrollParams m_params;
        AutoGeometry m_geometry;
        std::int64_t m_heightPx = 0;
        std::int64_t m_target = 0;
        std::int64_t m_marginPx = 0;
        Bitmap m_first;                            // shots[0]
        std::optional<Compositor> m_canvas;        // shots[0] and every strip but the newest
        std::optional<Bitmap> m_pending;           // the newest strip
        std::int64_t m_pendingAdvance = 0;         // its measured travel d_k
        std::size_t m_strips = 0;
        std::optional<Bitmap> m_previous;          // F_(k-1), the last full frame
        std::uint32_t m_prevHash = 0;
        std::uint32_t m_lastHash = 0;
        int m_counter = 0;
        std::int64_t m_shotCount = 1;              // shots.count (strips appended per step)
        ScrollMethod m_method = ScrollMethod::Wheel;
        std::int64_t m_unitTravel = 0;             // t1: travel of one notch (or one increment)
        int m_units = 1;                           // n
    };

    // ---- Manual mode ----
    enum class ManualLabel
    {
        None,
        Start,      // "Start Scrolling (Up or Down)"
        KeepDown,   // "Keep Scrolling DOWN, then hit Esc"
        KeepUp,     // "Keep Scrolling UP, then hit Esc"
        TooFast,    // "You're Going Too Fast!"
    };

    struct ManualTickResult
    {
        ManualLabel label = ManualLabel::None;   // the label after this tick (None = unchanged)
        bool finished = false;                   // callback(stitched) + stop
        bool error = false;                      // "Something went wrong" / "Try again" toast + beep
        std::optional<Bitmap> image;             // stitched (nullopt when nothing was stitched)
    };

    class ManualSession
    {
    public:
        static constexpr double kScreenshotInterval = 0.3;
        static constexpr double kNoShiftTimeout = 3.0;
        static constexpr double kStartTimeout = 10.0;

        // begin. `now` is in seconds; `regionHeightPoints` is height(captureRegion).
        void Begin(double now, double regionHeightPoints);
        void Cancel() noexcept { m_cancelPlease = true; }   // Esc
        bool Capturing() const noexcept { return m_capturing; }
        // captureAndProcess with the region captured now (nullopt if the capture failed).
        ManualTickResult Tick(std::optional<Bitmap> frame, double now);
        void Stop();

        std::int64_t Direction() const noexcept { return m_direction; }
        std::vector<std::int64_t> const& Shifts() const noexcept { return m_shifts; }
        std::int64_t NilShifts() const noexcept { return m_nilShifts; }

    private:
        ManualTickResult Finish(bool error = false);

        double m_regionHeight = 0;
        bool m_capturing = false;
        bool m_cancelPlease = false;
        std::optional<Bitmap> m_lastScreenshot;
        std::optional<Bitmap> m_stitched;
        std::optional<double> m_lastShiftDetectedTime;
        std::int64_t m_direction = 0;
        std::vector<std::int64_t> m_shifts;
        std::int64_t m_nilShifts = 0;
        double m_lastWarning = -1e300;   // "long ago"
    };
}
