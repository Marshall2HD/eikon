#include "pch.h"
#include "TestFramework.h"

#include "Core/CropGeometry.h"
#include "Core/Document.h"
#include "Core/EditorTools.h"
#include "Core/Framer.h"
#include "Core/ImageResize.h"
#include "Core/Snapping.h"
#include "Core/Undo.h"

using namespace Eikon::Core;

// Expected values are worked out by hand.

TEST(Framer_CoarseLadderAboveFour)
{
    // Above 4 the ladder is 4, 6, 8, 12, 16, 22, 32, 46, 64.
    double z = 4;
    std::vector<double> up;
    for (int i = 0; i < 9; ++i)
    {
        z = (std::min)(Framer::SnapCoarse(Framer::StepCoarse(z, true)), Framer::kMaxZoom);
        up.push_back(z);
    }
    CHECK_EQ(up, (std::vector<double>{ 6, 8, 12, 16, 22, 32, 46, 64, 64 }));
    // Zooming out walks the same values back down.
    z = 64;
    std::vector<double> down;
    for (int i = 0; i < 8; ++i)
    {
        z = Framer::SnapCoarse(Framer::StepCoarse(z, false));
        down.push_back(z);
    }
    CHECK_EQ(down, (std::vector<double>{ 46, 32, 22, 16, 12, 8, 6, 4 }));
}

TEST(Framer_CoarseBelowFourDoublesAndSnapsToPowersOfTwo)
{
    CHECK_EQ(Framer::StepCoarse(1, true), 2.0);
    CHECK_EQ(Framer::StepCoarse(3, true), 6.0);      // below 4: 2z
    CHECK_EQ(Framer::StepCoarse(4, true), 6.0);      // 2·frinta(5.656 / 2) = 6
    CHECK_EQ(Framer::StepCoarse(4, false), 2.0);     // z <= 4 halves
    CHECK_EQ(Framer::SnapCoarse(0.8), 0.5);          // 2·log2(0.8) = -0.64 -> -1 -> even -2 -> 0.5
    CHECK_EQ(Framer::SnapCoarse(0.3), 0.25);
    CHECK_EQ(Framer::SnapCoarse(1.3), 1.0);          // sqrt(2) rounds to 1 between 1 and 2
    CHECK_EQ(Framer::SnapCoarse(1.6), 1.0);          // 2·log2(1.6) = 1.36 -> 1 -> sqrt(2) -> frinta -> 1
    CHECK_EQ(Framer::SnapCoarse(1.8), 2.0);          // 2·log2(1.8) = 1.70 -> 2 -> 2
    CHECK_EQ(Framer::SnapCoarse(3), 2.0);            // sqrt(8) = 2.83 -> even integer 2
    CHECK_EQ(Framer::SnapCoarse(5), 6.0);            // 2·log2 5 = 4.64 -> 5 -> 5.66 -> 6
}

TEST(Framer_FineSnapRanges)
{
    CHECK_EQ(Framer::SnapFine(7.3), 7.0);
    CHECK_EQ(Framer::SnapFine(6.0), 6.0);   // not > 6: multiple of 0.5
    CHECK_EQ(Framer::SnapFine(3.3), 3.5);
    CHECK_EQ(Framer::SnapFine(1.1), 1.0);   // 0.25 steps
    CHECK_EQ(Framer::SnapFine(0.6), 0.6);   // 0.1 steps
    CHECK_EQ(Framer::SnapFine(0.33), 0.35); // 0.05 steps: frinta(6.6) / 20
    CHECK_EQ(Framer::SnapFine(0.1), 0.1);   // below 0.2: unchanged
}

TEST(Framer_ZoomKeepsTheAnchorAndClamps)
{
    Framer f;
    f.area = { 1000, 800 };
    f.canvas = { 0, 0, 2000, 2000 };
    f.zoom = 1;
    f.minZoom = 0.5;
    f.ZoomStep({ 100, 100 }, true);
    CHECK_EQ(f.zoom, 2.0);
    CHECK((f.pan == Point{ -100, -100 }));
    CHECK((f.ToImage({ 100, 100 }) == Point{ 100, 100 }));
    f.ZoomAt(0.1, { 0, 0 });
    CHECK_EQ(f.zoom, 0.5);   // minZoom
    f.ZoomAt(100, { 0, 0 });
    CHECK_EQ(f.zoom, 64.0);
}

TEST(Framer_FitMarginLadderAndExactHalf)
{
    Framer f;
    f.area = { 1000, 800 };
    f.dpr = 1;
    // Exactly half: 0.5, no margin, no ladder.
    f.Fit({ 0, 0, 2000, 1000 });
    CHECK_EQ(f.zoom, 0.5);
    // Below 0.8: the margined fit as is. min(780/1000, 980/4000) = 0.245.
    f.Fit({ 0, 0, 4000, 1000 });
    CHECK_NEAR(f.zoom, 0.245, 1e-12);
    // >= 0.8 snaps down the ladder from minZoom: 0.891 -> 0.5; 1.95 -> 1.
    f.Fit({ 0, 0, 1100, 800 });
    CHECK_EQ(f.zoom, 0.5);
    f.Fit({ 0, 0, 500, 400 });
    CHECK_EQ(f.zoom, 1.0);
    // An exact plain fit of 1 stays 1 despite the margin.
    f.Fit({ 0, 0, 1000, 800 });
    CHECK_EQ(f.zoom, 1.0);
    // `exact` skips the ladder.
    f.Fit({ 0, 0, 500, 400 }, true);
    CHECK_NEAR(f.zoom, 1.95, 1e-12);
    // Centred: pan = area/2 - centre·zoom.
    f.Fit({ 100, 50, 1000, 800 });
    CHECK((f.pan == Point{ 500 - 600.0, 400 - 450.0 }));
    // The margin is 20·dpr.
    f.dpr = 2;
    f.Fit({ 0, 0, 4000, 1000 });
    CHECK_NEAR(f.zoom, (std::min)(760.0 / 1000, 960.0 / 4000), 1e-12);
}

TEST(Framer_InitialFitPreferLargeWindow)
{
    Framer f;
    f.area = { 1000, 800 };
    f.canvas = { 0, 0, 1100, 700 };
    // Ladder gives 0.5; it fits at 0.8 (880 × 560) but not at 1.0, so 0.8.
    f.InitialFit(true, false);
    CHECK_EQ(f.zoom, 0.8);
    CHECK_EQ(f.minZoom, 0.8);
    CHECK((f.pan == Point{ 500 - 550 * 0.8, 400 - 350 * 0.8 }));
    // Without preferLargeWindow (or keeping the size) the ladder value stays.
    f.InitialFit(false, false);
    CHECK_EQ(f.zoom, 0.5);
    f.InitialFit(true, true);
    CHECK_EQ(f.zoom, 0.5);
    // A small image opens at 1.0 (capped), not at the ladder value 2.
    f.canvas = { 0, 0, 300, 200 };
    f.InitialFit(true, false);
    CHECK_EQ(f.zoom, 1.0);
    // Fits at 1.0 only without the margin: 1.0 wins over 0.8.
    f.canvas = { 0, 0, 990, 790 };
    f.InitialFit(true, false);
    CHECK_EQ(f.zoom, 1.0);
}

TEST(Framer_ClampKeepsCanvasInView)
{
    Framer f;
    f.area = { 1000, 800 };
    f.zoom = 1;
    // Smaller than the area: pushed back inside.
    f.canvas = { 0, 0, 500, 400 };
    f.pan = { -10, 500 };
    f.Clamp(false);
    CHECK((f.pan == Point{ 0, 400 }));
    // Larger: no gap at the edges without margin, 20 px with margin (zoom <= 1 is not multiplied).
    f.canvas = { 0, 0, 2000, 2000 };
    f.pan = { 100, -1500 };
    f.Clamp(false);
    CHECK((f.pan == Point{ 0, -1200 }));
    f.pan = { 100, -1500 };
    f.Clamp(true);
    CHECK((f.pan == Point{ 20, -1220 }));
    // Above zoom 1 the margin is 20 × zoom.
    f.zoom = 4;
    f.pan = { 500, 0 };
    f.Clamp(true);
    CHECK((f.pan == Point{ 80, 0 }));
}

TEST(Framer_PresetsAndFocus)
{
    CHECK_EQ(Framer::NextPreset(1), 4.0);
    CHECK_EQ(Framer::NextPreset(4), 16.0);
    CHECK_EQ(Framer::NextPreset(16), 64.0);
    CHECK_EQ(Framer::NextPreset(64), 4.0);
    Framer f;
    f.area = { 1000, 800 };
    f.canvas = { 0, 0, 3000, 3000 };
    f.FocusOn({ 1000, 1200 });
    CHECK_EQ(f.zoom, 16.0);
    CHECK((f.ToView({ 1000, 1200 }) == Point{ 500, 400 }));
}

TEST(Framer_LowerMinZoomForGrownCanvas)
{
    Framer f;
    f.area = { 1000, 800 };
    f.minZoom = 1;
    f.LowerMinZoomFor({ 3000, 1000 });   // fit 0.333 -> 2·log2 = -3.17 -> -3 -> even -4 -> 0.25
    CHECK_EQ(f.minZoom, 0.25);
    f.LowerMinZoomFor({ 500, 500 });     // fits: unchanged
    CHECK_EQ(f.minZoom, 0.25);
}

TEST(CropGeometry_HitZones)
{
    Rect const r{ 100, 100, 300, 200 };   // bands of 30
    CHECK_EQ(CropHitZone(r, { 105, 200 }), CropZone::Left);
    CHECK_EQ(CropHitZone(r, { 75, 200 }), CropZone::Left);     // 25 outside
    CHECK_EQ(CropHitZone(r, { 60, 200 }), CropZone::None);     // 40 outside
    CHECK_EQ(CropHitZone(r, { 395, 200 }), CropZone::Right);
    CHECK_EQ(CropHitZone(r, { 250, 105 }), CropZone::Top);
    CHECK_EQ(CropHitZone(r, { 250, 295 }), CropZone::Bottom);
    CHECK_EQ(CropHitZone(r, { 390, 105 }), CropZone::TopRight);
    CHECK_EQ(CropHitZone(r, { 105, 290 }), CropZone::BottomLeft);
    CHECK_EQ(CropHitZone(r, { 250, 200 }), CropZone::Move);
    // A side of at most 150 uses a fifth of it: 100 -> 20.
    Rect const small{ 100, 100, 100, 100 };
    CHECK_EQ(CropHitZone(small, { 115, 150 }), CropZone::Left);
    CHECK_EQ(CropHitZone(small, { 125, 150 }), CropZone::Move);
    // Under 36: no inner band, only the outside resizes.
    Rect const tiny{ 100, 100, 20, 20 };
    CHECK_EQ(CropHitZone(tiny, { 110, 110 }), CropZone::Move);
    CHECK_EQ(CropHitZone(tiny, { 95, 110 }), CropZone::Left);
    CHECK_EQ(CropHitZone(tiny, { 95, 95 }), CropZone::TopLeft);
    CHECK_EQ(CursorForZone(CropZone::TopRight), std::wstring_view{ L"cursor-bl-resize" });
    CHECK_EQ(CursorForZone(CropZone::BottomRight), std::wstring_view{ L"cursor-tl-resize" });
}

TEST(CropGeometry_DragsAndKeys)
{
    Rect const r{ 10, 20, 100, 50 };
    CHECK((DragSelection(r, CropZone::TopLeft, 5, 6) == Rect{ 15, 26, 95, 44 }));
    CHECK((DragSelection(r, CropZone::Right, -120, 0).Standardized() == Rect{ -10, 20, 20, 50 }));
    CHECK((DragSelection(r, CropZone::Move, 3, -4) == Rect{ 13, 16, 100, 50 }));
    CHECK((KeepInside({ -5, 10, 20, 20 }, { 0, 0, 100, 100 }) == Rect{ 0, 10, 20, 20 }));
    CHECK((KeepInside({ 90, 95, 20, 20 }, { 0, 0, 100, 100 }) == Rect{ 80, 80, 20, 20 }));
    CHECK((KeepInside({ -10, -10, 200, 50 }, { 0, 0, 100, 100 }) == Rect{ 0, 0, 100, 40 }));
    CHECK((ApplySelectionKey(r, SelectionKey::Left, 2, false) == Rect{ 8, 20, 100, 50 }));
    CHECK((ApplySelectionKey(r, SelectionKey::Down, 20, true) == Rect{ 10, 20, 100, 70 }));
    CHECK((ApplySelectionKey(r, SelectionKey::Grow, 2, false) == Rect{ 8, 18, 104, 54 }));
    CHECK((ApplySelectionKey({ 0, 0, 3, 3 }, SelectionKey::Shrink, 2, false) == Rect{ 0, 0, 3, 3 }));
    CHECK((SnapSelection({ 10.4, 20.6, 101.2, 50.9 }, 2, false) == Rect{ 10, 21, 102, 50 }));   // 25.45 -> 25 points
    CHECK((SnapSelection({ 10.4, 20.6, 101.2, 50.9 }, 2, true) == Rect{ 10, 21, 101, 51 }));
}

TEST(CropGeometry_SmartAdjustTrimsUniformBorders)
{
    Bitmap image = Bitmap::Create(12, 10);
    for (auto& b : image.pixels)
    {
        b = 0xFF;   // white
    }
    for (int y = 3; y < 6; ++y)
    {
        for (int x = 4; x < 9; ++x)
        {
            std::uint8_t* p = image.pixels.data() + image.Stride() * y + x * 4;
            p[0] = p[1] = p[2] = 0;
        }
    }
    auto const all = SmartAdjust(image, { 0, 0, 12, 10 });
    CHECK(all.has_value());
    CHECK((all == Rect{ 4, 3, 5, 3 }));
    auto const left = SmartAdjust(image, { 0, 0, 12, 10 }, L"left");
    CHECK((left == Rect{ 4, 0, 8, 10 }));
}

TEST(Snapping_ClosestEdgeWithinThreshold)
{
    std::vector<Rect> const candidates{ { 63, 100, 40, 40 }, { 300, 300, 10, 10 } };
    auto const s = Snap({ 10, 10, 50, 50 }, candidates, 5);
    CHECK(s.snappedX);
    CHECK(!s.snappedY);
    CHECK_EQ(s.dx, 3.0);   // right edge 60 -> 63
    CHECK_EQ(s.guides.size(), std::size_t{ 1 });
    CHECK((s.guides[0] == SnapGuide{ { 63, 10 }, { 63, 140 } }));
    // Out of range: nothing.
    auto const none = Snap({ 10, 10, 50, 50 }, candidates, 2);
    CHECK(!none.snappedX && !none.snappedY);
    CHECK_EQ(SnapThreshold(2, 0.5), 20.0);
}

TEST(Snapping_CanvasGrowthAddsMarginAndCaps)
{
    Rect const crop{ 0, 0, 100, 100 };
    auto const g = CanvasGrowth(crop, { { -30, 10, 50, 50 }, { 90, 90, 3000, 20 } }, 1);
    CHECK_EQ(g.left, 50.0);
    CHECK_EQ(g.top, 0.0);
    CHECK_EQ(g.right, 1920.0);
    CHECK_EQ(g.bottom, 30.0);
    CHECK(CanvasGrowth(crop, { { 10, 10, 20, 20 } }, 2).IsZero());
    CHECK_EQ(CanvasGrowth(crop, { { 0, -5000, 10, 10 } }, 2).top, 2560.0);
}

TEST(Undo_RecordDedupesAndCutsRedoTail)
{
    Undo undo;
    CHECK(!undo.CanUndo());
    CHECK(undo.Record(L"", L"A", L"a"));
    CHECK(!undo.Record(L"x", L"A2", L"a"));   // same comparable: dropped
    CHECK(undo.Record(L"", L"B", L"b"));
    CHECK(undo.Record(L"", L"C", L"c"));
    CHECK_EQ(undo.Count(), std::size_t{ 3 });
    CHECK_EQ(undo.StepBack(), std::optional<std::wstring>{ L"B" });
    CHECK(undo.CanRedo());
    CHECK(undo.Record(L"", L"D", L"d"));      // cuts C
    CHECK(!undo.CanRedo());
    CHECK_EQ(undo.Count(), std::size_t{ 3 });
    CHECK_EQ(undo.StepBack(), std::optional<std::wstring>{ L"B" });
    CHECK_EQ(undo.StepBack(), std::optional<std::wstring>{ L"A" });
    CHECK_EQ(undo.StepBack(), std::optional<std::wstring>{});
    CHECK_EQ(undo.StepForward(), std::optional<std::wstring>{ L"B" });
}

TEST(Undo_SnapshotRoundTripAndComparableIgnoresSelectedObject)
{
    UndoSnapshot s;
    s.cropRect = { 1, 2, 30, 40 };
    s.selectionRect = Rect{ 5, 6, 7, 8 };
    s.counterCount = 3;
    s.selectedObj = 0;
    s.extras.emplace_back(L"backdropConfig", JsonValue::String(L"cfg"));
    auto const parsed = UndoSnapshot::Parse(s.Full());
    CHECK(parsed.has_value());
    CHECK((parsed->cropRect == Rect{ 1, 2, 30, 40 }));
    CHECK((parsed->selectionRect == Rect{ 5, 6, 7, 8 }));
    CHECK_EQ(parsed->counterCount, 3);
    CHECK_EQ(parsed->selectedObj, std::optional<std::int64_t>{ 0 });
    CHECK_EQ(parsed->extras.size(), std::size_t{ 1 });
    UndoSnapshot other = s;
    other.selectedObj.reset();
    CHECK(other.Comparable() == s.Comparable());
    CHECK(other.Full() != s.Full());
    other.selectionRect = Rect{ 5, 6, 7, 9 };
    CHECK(other.Comparable() != s.Comparable());
}

TEST(Document_CanvasGrowthAndReplaceImage)
{
    Document d{ Bitmap::Create(100, 80) };
    d.SetCrop({ 10, 10, 50, 40 });
    d.SetCanvasGrowth({ 5, 0, 20, 0 });
    CHECK((d.Canvas() == Rect{ 5, 10, 75, 40 }));
    d.ReplaceImage(Bitmap::Create(120, 80), { 20, 0 });
    CHECK((d.Crop() == Rect{ 30, 10, 50, 40 }));
    CHECK(d.CanvasGrowth().IsZero());
}

TEST(ImageResize_PresetsAspectAndChecks)
{
    CHECK_EQ(ResizePresetIndex(400, 1200), std::optional<int>{ 1 });    // 33%
    CHECK_EQ(ResizePresetIndex(600, 1200), std::optional<int>{ 2 });
    CHECK_EQ(ResizePresetIndex(1201, 1200), std::optional<int>{});
    CHECK_EQ(ResizePresetIndex(4800, 1200), std::optional<int>{ 5 });
    CHECK_EQ(ResizePresetSize(1001, 0), std::int64_t{ 250 });           // frinta(250.25)
    CHECK_EQ(AspectLocked(600, 1200, 801), std::int64_t{ 401 });         // 400.5 -> 401
    CHECK_EQ(AspectLocked(1, 1000, 10), std::int64_t{ 1 });              // floor of 1
    CHECK_EQ(CheckResize(0, 10, 100, 100), ResizeCheck::TooSmall);
    CHECK_EQ(CheckResize(22001, 10, 100, 100), ResizeCheck::TooLarge);
    CHECK_EQ(CheckResize(22000, 10, 100, 100), ResizeCheck::Ok);
    CHECK_EQ(CheckResize(100, 100, 100, 100), ResizeCheck::SameSize);
    CHECK_EQ(CheckResize(10, 10, 0, 0), ResizeCheck::NoImage);
    CHECK_EQ(ResizeQualityFor(0.5), ResizeQuality::Low);
    CHECK_EQ(ResizeQualityFor(2.0), ResizeQuality::Nearest);
    CHECK_EQ(ResizeQualityFor(1.5), ResizeQuality::High);
    CHECK_EQ(ResizeQualityFor(0.25), ResizeQuality::High);
}

TEST(EditorTools_ToolOrderOverflowAndTooltips)
{
    CHECK_EQ(kEditorTools.size(), std::size_t{ 15 });
    CHECK_EQ(kEditorTools[0].id, std::wstring_view{ L"arrow" });
    CHECK_EQ(kEditorTools[4].id, std::wstring_view{ L"backdrop" });
    CHECK_EQ(kEditorTools[14].asset, std::wstring_view{ L"20-image" });
    CHECK_EQ(EditorToolIndex(L"line"), std::size_t{ 13 });
    // Candy's 32-px items: a slot is 34.
    CHECK_EQ(ToolbarCapacity(100), std::size_t{ 3 });        // floor(102 / 34)
    CHECK_EQ(ToolbarCapacity(508), std::size_t{ 15 });       // exactly 15 slots
    CHECK_EQ(ToolbarCapacity(507), std::size_t{ 14 });
    CHECK_EQ(ToolbarVisibleCount(15, 15), std::size_t{ 15 });
    CHECK_EQ(ToolbarVisibleCount(15, 14), std::size_t{ 13 }); // the last slot is "more"
    CHECK_EQ(ToolbarWidth(15), 508.0);
    CHECK_EQ(StripShortcut(L"Add Capture (Ctrl+A)"), std::wstring{ L"Add Capture" });
    CHECK_EQ(StripShortcut(L"Ruler (1 or 2)"), std::wstring{ L"Ruler" });
    CHECK_EQ(StripShortcut(L"Blur/Erase (B)"), std::wstring{ L"Blur/Erase" });
    auto const parts = SplitTooltip(L"Resize ( Shift+] )");
    CHECK_EQ(parts.name, std::wstring{ L"Resize" });
    CHECK_EQ(parts.key, std::wstring{ L"( Shift+] )" });
    CHECK_EQ(SplitTooltip(L"Cut Out").key, std::wstring{});
    CHECK_EQ(TooltipWidth(50, 20, true), 110.0);
    CHECK_EQ(TooltipWidth(50, 0, false), 82.0);
    CHECK_EQ(SizeLabel(1000, 501, 2, false), std::wstring{ L"500\u00D7251\u200Apt" });   // 250.5 -> 251
    CHECK_EQ(SizeLabel(1000, 501, 2, true), std::wstring{ L"1000\u00D7501\u200Apx" });
    CHECK_EQ(ZoomLabel(15.5), std::wstring{ L"1550%" });
    CHECK_EQ(ZoomLabel(0.333), std::wstring{ L"33%" });
}
