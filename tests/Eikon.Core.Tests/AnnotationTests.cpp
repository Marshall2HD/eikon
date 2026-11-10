#include "pch.h"
#include "TestFramework.h"
#include "TestSupport.h"

#include "Core/Bitmap.h"
#include "Core/Objects/ArrHelper.h"
#include "Core/Objects/Conceal.h"
#include "Core/Objects/Factory.h"
#include "Core/Objects/ObjArrow.h"
#include "Core/Objects/ObjConceal.h"
#include "Core/Objects/ObjCounter.h"
#include "Core/Objects/ObjFreehand.h"
#include "Core/Objects/ObjLine.h"
#include "Core/Objects/ObjOval.h"
#include "Core/Objects/ObjRectangleBasic.h"
#include "Core/Objects/ObjRuler.h"
#include "Core/Objects/ObjText.h"
#include "Core/Objects/Random.h"
#include "Core/Settings.h"

#include <cmath>

using namespace Eikon;
using namespace Eikon::Core;

namespace
{
    bool Near(double a, double b, double tolerance = 1e-9)
    {
        return std::fabs(a - b) <= tolerance;
    }

    std::vector<std::wstring> MemberNames(JsonValue const& json)
    {
        std::vector<std::wstring> keys;
        for (auto const& [key, value] : json.Members())
        {
            keys.push_back(key);
        }
        return keys;
    }

    // Dump → DumpWithType → text → LoadObject → Dump gives the same JSON.
    void CheckRoundTrip(ProtoObject const& object)
    {
        std::wstring const text = DumpWithType(object).Serialize();
        auto const parsed = JsonValue::Parse(text);
        CHECK(parsed.has_value());
        ObjectPtr loaded = LoadObject(*parsed);
        CHECK(loaded != nullptr);
        if (loaded)
        {
            CHECK_EQ(loaded->ClassName(), object.ClassName());
            CHECK_EQ(loaded->Dump().Serialize(), object.Dump().Serialize());
        }
    }

    Bitmap Image(std::vector<std::vector<Color>> const& rows)
    {
        Bitmap bitmap = Bitmap::Create(static_cast<std::int32_t>(rows[0].size()), static_cast<std::int32_t>(rows.size()));
        for (std::size_t y = 0; y < rows.size(); ++y)
        {
            for (std::size_t x = 0; x < rows[y].size(); ++x)
            {
                std::uint8_t* p = bitmap.pixels.data() + y * bitmap.Stride() + x * 4;
                p[0] = rows[y][x].b;
                p[1] = rows[y][x].g;
                p[2] = rows[y][x].r;
                p[3] = 255;
            }
        }
        return bitmap;
    }

    Color PixelAt(Bitmap const& bitmap, int x, int y)
    {
        std::uint8_t const* p = bitmap.pixels.data() + static_cast<std::size_t>(y) * bitmap.Stride() + static_cast<std::size_t>(x) * 4;
        return { p[2], p[1], p[0], p[3] };
    }
}

TEST(Random_Xorshift64StarValues)
{
    // s ^= s >> 12; s ^= s << 25; s ^= s >> 27; s *= 0x2545F4914F6CDD1D; (s & 0xFFFFFFFF) / 4294967295.
    auto const one = Random::Values(1, 3);
    CHECK_EQ(one.size(), std::size_t{ 3 });
    CHECK(Near(one[0], 0.5368173787223216));
    CHECK(Near(one[1], 0.7776442034583642));
    CHECK(Near(one[2], 0.8185948698824725));
    auto const other = Random::Values(0x123456789ABCDEFULL, 2);
    CHECK(Near(other[0], 0.17465880796654587));
    CHECK(Near(other[1], 0.0487508289629479));
    CHECK_EQ(Random::Values(42).size(), std::size_t{ 100 });
    // Seeds survive the JSON double of the Dump.
    for (int i = 0; i < 50; ++i)
    {
        std::uint64_t const seed = Random::NewSeed();
        CHECK(seed != 0);
        CHECK_EQ(static_cast<std::uint64_t>(static_cast<double>(seed)), seed);
    }
    CHECK_EQ(Random::Hash(25).size(), std::size_t{ 25 });
}

TEST(ArrHelper_ProfilesHaveTheExpectedCommandCounts)
{
    // Command counts of the four arrow outlines and the two pins.
    std::size_t const counts[4] = { 20, 18, 23, 16 };
    for (int style = 0; style < 4; ++style)
    {
        CHECK_EQ(ArrHelper::ArrowProfile(style).size(), counts[style]);
        CHECK_EQ(ArrHelper::ArrowProfile(style).front().op, L'M');
    }
    CHECK_EQ(ArrHelper::PinProfile(0).size(), std::size_t{ 7 });
    CHECK_EQ(ArrHelper::PinProfile(1).size(), std::size_t{ 5 });
    // The first profile's tip is at (100, 0) and its half height about 22.
    auto const& first = ArrHelper::ArrowProfile(0);
    CHECK(Near(first[0].points[0].x, 53.7652875) && Near(first[0].points[0].y, -22.0862065));
    CHECK(Near(ArrHelper::CircleCentre({ 0, 0 }, { 10, 10 }, { 20, 0 }).x, 10) && Near(ArrHelper::CircleCentre({ 0, 0 }, { 10, 10 }, { 20, 0 }).y, 0));
}

TEST(ObjArrow_WidthFormula)
{
    // min(len/10, T2[size]) × dpr × clamp(√((len/dpr + 550)/600), 1, 1.5); rounded when > 4 at dpr 1.
    CHECK(Near(ObjArrow::Width(100, 2, 1), 3.122498999199199));
    CHECK(Near(ObjArrow::Width(300, 5, 1), 12.0));                 // 11.90 rounds (frinta) at dpr 1
    CHECK(Near(ObjArrow::Width(300, 5, 2), 21.60246899469287));    // not rounded at dpr 2
    CHECK(Near(ObjArrow::Width(3000, 9, 1), 60.0));                 // grow capped at 1.5
    CHECK(Near(ObjArrow::Width(20, 9, 1), 2.0));                    // grow √(570/600) < 1 is ignored
    double head = 0, tail = 0;
    ObjArrow::HeadTail(1020, 2, head, tail);
    CHECK(Near(tail, 0.8) && Near(head, 1.25));
    ObjArrow::HeadTail(100, 2, head, tail);   // hs = 0.7·56 < 100: below 200 scales by len/200
    CHECK(Near(head, 0.5) && Near(tail, 0.5));
    ObjArrow::HeadTail(300, 8, head, tail);   // hs = 4.2·56 = 235.2 ≥ 100: below 2·hs
    CHECK(Near(head, 300.0 / 470.4) && Near(tail, 300.0 / 470.4));
    ObjArrow::HeadTail(600, 2, head, tail);
    CHECK(Near(tail, 0.92) && Near(head, 1.0 / 0.92));
}

TEST(ObjArrow_CurveKnobAndDump)
{
    ObjArrow arrow;
    arrow.rect = { 10, 20, 100, 0 };
    CHECK((arrow.CurveKnob() == Point{ 60, 20 }));   // midpd 0.5, midpa 0: the midpoint
    arrow.state = L"point-curve";
    arrow.MouseDown({ 60, 20 }, Modifiers::None);
    arrow.MouseDragged({ 60, 70 }, Modifiers::None);
    CHECK(Near(arrow.midpd, std::hypot(50.0, 50.0) / 100.0));
    CHECK(Near(arrow.midpa, std::atan2(50.0, 50.0)));
    CHECK(arrow.IsCurved());
    arrow.type = ObjArrow::Handdrawn;
    arrow.seed = 123456789;
    auto const keys = MemberNames(arrow.Dump());
    CHECK((keys == std::vector<std::wstring>{ L"rect", L"size", L"color", L"type", L"style", L"midpa", L"midpd", L"seed" }));
    CheckRoundTrip(arrow);
    // The same seed draws the same hand-drawn arrow; another seed a different one.
    ObjArrow straight;
    straight.rect = { 0, 0, 300, 40 };
    straight.type = ObjArrow::Handdrawn;
    straight.seed = 99;
    auto const a = straight.Shapes(1, 1);
    auto const b = straight.Shapes(1, 1);
    CHECK_EQ(a.size(), std::size_t{ 1 });
    CHECK(!a[0].path.Empty());
    CHECK_EQ(a[0].path.commands.size(), b[0].path.commands.size());
    CHECK((a[0].path.commands.back().p == b[0].path.commands.back().p));
    straight.seed = 100;
    auto const c = straight.Shapes(1, 1);
    CHECK(!(c[0].path.commands.size() == a[0].path.commands.size() && c[0].path.commands[1].p == a[0].path.commands[1].p));
}

TEST(ObjArrow_CreateReversedAndShiftLock)
{
    ObjArrow arrow;
    arrow.MouseDown({ 10, 10 }, Modifiers::Ctrl);
    arrow.MouseDragged({ 110, 13 }, Modifiers::Ctrl | Modifiers::Shift);   // Shift drops the smaller axis
    arrow.MouseUp({ 110, 13 }, Modifiers::Ctrl | Modifiers::Shift);
    // Ctrl put the head at the press point: the rect now runs from the release point back to it.
    CHECK((arrow.rect == Rect{ 110, 10, -100, 0 }));
    CHECK(!arrow.reversed);
}

TEST(ObjOval_SquarishOvalConstants)
{
    ObjOval oval;
    oval.size = 2;
    Path const square = oval.OutlinePath({ 0, 0, 100, 100 }, 1, 1);
    CHECK((square.commands[0].p == Point{ 22, 6 }));
    CHECK(Near(square.commands[1].c1.x, 38) && Near(square.commands[1].c1.y, -2));
    CHECK(Near(square.commands[1].c2.x, 62) && Near(square.commands[1].c2.y, -2));
    CHECK(Near(square.commands[1].p.x, 78) && Near(square.commands[1].p.y, 6));
    CHECK(Near(square.commands[2].c1.x, 84.67) && Near(square.commands[2].c1.y, 9.34));
    // A wide box pulls the x coefficients towards the sides by 0.75·h/w + 0.25.
    Path const wide = oval.OutlinePath({ 0, 0, 200, 100 }, 1, 1);
    CHECK(Near(wide.commands[0].p.x, 200 * 0.625 * 0.22) && Near(wide.commands[0].p.y, 6));
    CHECK(Near(wide.commands[1].p.x, 200 * (1 - 0.625 * 0.22)));
    Path const tall = oval.OutlinePath({ 0, 0, 100, 200 }, 1, 1);
    CHECK(Near(tall.commands[0].p.x, 22) && Near(tall.commands[0].p.y, 200 * 0.625 * 0.06));
    // Rectangle corner radius min(minSide/3, 2·widths·dpr): size 2 → 6 at dpr 1.
    ObjRectangle rect;
    rect.shape = L"rect";
    rect.size = 2;
    Path const r = rect.OutlinePath({ 0, 0, 100, 50 }, 1, 1);
    CHECK((r.commands[0].p == Point{ 6, 0 }));
    rect.rect = { 0, 0, 100, 50 };
    CHECK(Near(rect.StrokeWidth(2), 6));   // min(50/10, 3) × 2
}

TEST(ObjOval_KeysToggleBorderFillAndAlpha)
{
    ObjOval oval;
    oval.state = std::wstring{ ObjectState::Selected };
    KeyContext const context{};
    CHECK_EQ(oval.KeyDown({ 'B', Modifiers::None }, context), KeyResult::Handled);
    CHECK(oval.showBorder);   // border can't go off while the fill is off
    oval.KeyDown({ '5', Modifiers::None }, context);
    CHECK(oval.showBackground && Near(oval.backgroundAlpha, 0.5));
    oval.KeyDown({ 'B', Modifiers::None }, context);
    CHECK(!oval.showBorder);
    oval.KeyDown({ 'F', Modifiers::None }, context);
    CHECK(oval.showBackground);   // fill can't go off while the border is off
    oval.KeyDown({ 0x60, Modifiers::None }, context);   // keypad 0
    CHECK(Near(oval.backgroundAlpha, 1.0));
    oval.KeyDown({ '3', Modifiers::Ctrl }, context);
    CHECK(Near(oval.backgroundAlpha, 1.0));
    // "fill" option: −1 is hand-drawn, 40 = 0.4 alpha.
    EikonTests::ResetSettings();
    oval.SetProperty(L"fill", L"40");
    CHECK(Near(oval.backgroundAlpha, 0.4) && oval.showBackground && !oval.handdrawn);
    CHECK_EQ(oval.Property(L"fill"), std::wstring{ L"40" });
    oval.SetProperty(L"fill", L"-1");
    CHECK(oval.handdrawn && !oval.showBackground);
    CHECK_EQ(oval.Property(L"fill"), std::wstring{ L"-1" });
    CHECK_EQ(Settings::Current().ReadString(L"Eikon.ObjOval: handdrawn", L""), std::wstring{ L"true" });
}

TEST(ObjOval_DumpShapesAndRoundTrips)
{
    EikonTests::ResetSettings();
    ObjOval oval;
    oval.rect = { 1, 2, 30, 40 };
    oval.showBackground = true;
    oval.backgroundAlpha = 0.7;
    CHECK((MemberNames(oval.Dump()) == std::vector<std::wstring>{ L"rect", L"size", L"color", L"showBackground", L"showBorder", L"backgroundAlpha", L"shape" }));
    CheckRoundTrip(oval);
    ObjMagnifier magnifier;
    magnifier.rect = { 10, 10, 20, 20 };
    magnifier.factor = 4;
    CHECK((MemberNames(magnifier.Dump()) == std::vector<std::wstring>{ L"rect", L"size", L"color", L"showBackground", L"showBorder", L"backgroundAlpha", L"shape",
                                                               L"lensX", L"lensY", L"factor" }));
    CheckRoundTrip(magnifier);
    // Lens: factor × the source, centred at the source centre + (lensX·w, −lensY·h) on screen.
    magnifier.factor = 3;
    Rect const lens = magnifier.LensRect();
    CHECK(Near(lens.width, 60) && Near(lens.height, 60));
    CHECK(Near(lens.x + 30, 20 + 2.2 * 20) && Near(lens.y + 30, 20 + 1.4 * 20));
    ObjSpotlight spotlight;
    spotlight.rect = { 0, 0, 10, 10 };
    CheckRoundTrip(spotlight);
    ObjRectangle rectangle;
    rectangle.shape = L"rect";
    CheckRoundTrip(rectangle);
}

TEST(ObjOval_SpotlightDigitsSetTheCurtain)
{
    EikonTests::ResetSettings();
    CHECK(Near(ObjSpotlight::Transparency(Settings::Current()), 0.214));
    ObjSpotlight spotlight;
    spotlight.state = std::wstring{ ObjectState::Selected };
    spotlight.KeyDown({ '7', Modifiers::None }, {});
    CHECK(Near(ObjSpotlight::Transparency(Settings::Current()), 0.5, 1e-6));
    CHECK(spotlight.showBorder && !spotlight.showBackground);
}

TEST(ObjLine_DumpAndDots)
{
    ObjLine line;
    line.rect = { 0, 0, 100, 0 };
    line.size = 2;
    line.dotted = true;
    CHECK((MemberNames(line.Dump()) == std::vector<std::wstring>{ L"rect", L"size", L"color", L"startCap", L"endCap", L"dotted" }));
    CheckRoundTrip(line);
    auto const shapes = line.Shapes(2, 1);
    CHECK_EQ(shapes.size(), std::size_t{ 1 });
    // [1.5 dpr, (1.5 w + 2) dpr] with round caps.
    CHECK((shapes[0].dashes == std::vector<double>{ 3.0, 13.0 }));
    CHECK(shapes[0].cap == Shape::Cap::Round);
    CHECK(Near(shapes[0].strokeWidth, 6));
}

TEST(ObjText_TypographyAndDump)
{
    ObjText text;
    text.size = 2;   // 17 pt
    CHECK(Near(text.LineHeight(1), 20));   // round(17 × 1.15)
    text.nobg = true;
    CHECK((text.Insets(1) == Point{ 5, 2 }));   // round(17·0.03 + 4), round(17·0.11)
    text.nobg = false;
    text.text = std::wstring(50, L'x');
    CHECK((text.Insets(1) == Point{ 9, 5 }));   // f = 1.25 from 41 characters
    text.text = L"Hi";
    CHECK((text.Insets(1) == Point{ 7, 4 }));
    text.color = Color{ 0xFF, 0x0C, 0x01, 0xFF };   // luminance 0.31: white Medium
    CHECK((text.TextColor() == Color{ 255, 255, 255, 255 }) && text.UsesMediumWeight());
    text.color = Color{ 0xFF, 0xDC, 0x2C, 0xFF };   // #FFDC2C luminance 0.83: black Regular
    CHECK((text.TextColor() == Color{ 0, 0, 0, 255 }) && !text.UsesMediumWeight());
    text.style = AnnotationStyle::Goofy;
    CHECK(Near(text.FontSize(1), 17 * 1.2) && Near(text.LineHeight(1), 17 * 1.2 * 1.324));
    text.rect = { 5, 6, 80, 30 };
    CHECK((MemberNames(text.Dump()) == std::vector<std::wstring>{ L"rect", L"size", L"color", L"nobg", L"style", L"text", L"maxWidth", L"pPerc", L"pDist", L"pSide" }));
    CheckRoundTrip(text);
    CHECK(Near(ContrastRatio({ 0, 0, 0, 255 }, { 255, 255, 255, 255 }), 21.0));
    CHECK(Near(ContrastRatio({ 0xFF, 0xF6, 0x8D, 255 }, { 0xFF, 0xF6, 0x8D, 255 }), 1.0));
}

TEST(ObjText_DetectBackground)
{
    std::vector<std::vector<Color>> rows(20, std::vector<Color>(20, Color{ 10, 20, 30, 255 }));
    rows[3][3] = Color{ 200, 0, 0, 255 };
    Bitmap const image = Image(rows);
    auto const bg = DetectBackground(image, { 0, 0, 20, 20 });
    CHECK((bg == std::optional<Color>{ Color{ 10, 20, 30, 255 } }));
    Bitmap clear = image;
    for (std::size_t i = 3; i < clear.pixels.size(); i += 4) clear.pixels[i] = 0;
    CHECK(!DetectBackground(clear, { 0, 0, 20, 20 }).has_value());
}

TEST(ObjCounter_PinGeometryAndDump)
{
    EikonTests::ResetSettings();
    Environment().dpr = 2;
    Environment().nextCounter = [] { return 7; };
    ObjCounter counter;
    counter.ApplyDefaults(Settings::Current());
    CHECK_EQ(counter.style, 1);
    counter.size = 2;   // d = 36
    counter.MouseDown({ 100, 100 }, Modifiers::None);
    CHECK_EQ(counter.count, 7);   // the number is taken when the counter is placed
    // The click is the tip of a pin pointing down-left.
    double const r = 36.0;
    CHECK(Near(counter.rect.x, 100 + std::cos(3 * 3.141592653589793 / 4) * -r * 1.65, 1e-9));
    CHECK(Near(counter.rect.y, 100 - std::sin(3 * 3.141592653589793 / 4) * r * 1.65, 1e-9));
    CHECK(Near(std::hypot(counter.rect.width, counter.rect.height), r * 1.5));
    CHECK(Near(counter.FontSize(2), 36 * 1.1));
    counter.count = 12;
    CHECK(Near(counter.FontSize(2), 36 * 1.1 * 0.9));
    CHECK((MemberNames(counter.Dump()) == std::vector<std::wstring>{ L"rect", L"size", L"color", L"style", L"count" }));
    CheckRoundTrip(counter);
    counter.SetProperty(L"changeStyle", L"");
    counter.SetProperty(L"changeStyle", L"");
    CHECK_EQ(counter.style, 0);
    CHECK_EQ(Settings::Current().ReadString(L"Eikon.ObjCounter: counterStyle", L""), std::wstring{ L"0" });
    Environment() = ObjectEnvironment{};
}

TEST(ObjFreehand_StrokeAndDump)
{
    EikonTests::ResetSettings();
    ObjFreehand pen;
    pen.ApplyDefaults(Settings::Current());
    pen.MouseDown({ 10, 10 }, Modifiers::None);
    for (int i = 1; i <= 40; ++i)
    {
        pen.MouseDragged({ 10.0 + i * 3, 10.0 + std::sin(i * 0.3) * 8 }, Modifiers::None);
    }
    pen.MouseUp({ 130, 12 }, Modifiers::None);
    CHECK_EQ(pen.pointsHash.size(), std::size_t{ 25 });
    CHECK(!pen.IsEmpty());
    CHECK(!pen.Outline(1).Empty());
    CHECK((MemberNames(pen.Dump()) == std::vector<std::wstring>{ L"rect", L"size", L"color", L"resolution", L"smoothing", L"capStyle", L"shapeCorrection",
                                                         L"rawRect", L"pointsHash" }));
    CheckRoundTrip(pen);
    // Resizing maps the stroke from rawRect to rect.
    Rect const before = pen.Outline(1).ControlBounds();
    pen.rect = { pen.rawRect.x, pen.rawRect.y, pen.rawRect.width * 2, pen.rawRect.height };
    Rect const after = pen.Outline(1).ControlBounds();
    CHECK(after.width > before.width * 1.8);
    // Shift locks the first direction.
    ObjFreehand locked;
    locked.ApplyDefaults(Settings::Current());
    locked.MouseDown({ 0, 0 }, Modifiers::None);
    locked.MouseDragged({ 10, 2 }, Modifiers::Shift);
    locked.MouseDragged({ 20, 15 }, Modifiers::Shift);
    CHECK_EQ(locked.lockDirection, 1);
    CHECK(Near(locked.Points().back().y, 0));
}

TEST(Freehand_BoxCorrectionSnapsToMedianEdges)
{
    // A 100 × 60 loop with one spike above the top edge: the edges come from the medians, so the spike
    // does not move the top edge, and with strength 1 every sample lands on an edge.
    Freehand::Samples s;
    for (int x = 0; x < 100; x += 5) s.push_back(Freehand::At({ static_cast<double>(x), x == 50 ? -20.0 : 0.0 }));
    for (int y = 0; y < 60; y += 5) s.push_back(Freehand::At({ 100.0, static_cast<double>(y) }));
    for (int x = 100; x > 0; x -= 5) s.push_back(Freehand::At({ static_cast<double>(x), 60.0 }));
    for (int y = 60; y > 0; y -= 5) s.push_back(Freehand::At({ 0.0, static_cast<double>(y) }));
    Freehand::MorphBox(s, 1.0);
    for (auto const& p : s)
    {
        CHECK(Near(p.x, 0) || Near(p.x, 100) || Near(p.y, 0) || Near(p.y, 60));
    }
    CHECK(Near(s[10].y, 0));   // the spike at x = 50 is pulled onto the median top edge
}

TEST(ObjHighlighter_BlendByBackgroundLuminance)
{
    Color const base{ 0xFF, 0xF6, 0x8D, 0xFF };
    auto const light = ObjHighlighter::LookForLuminance(0.67, base);
    CHECK(light.blend == HighlighterLook::Blend::Multiply && Near(light.opacity, 1.0) && (light.color == base));
    auto const mid = ObjHighlighter::LookForLuminance(0.66, base);
    CHECK(mid.blend == HighlighterLook::Blend::Normal && Near(mid.opacity, 0.25));
    CHECK((mid.color == Color{ 255, 235, 0, 255 }));   // saturate(#FFF68D)
    auto const dark = ObjHighlighter::LookForLuminance(0.33, base);
    CHECK(dark.blend == HighlighterLook::Blend::Screen && Near(dark.opacity, 0.33));
    CHECK((Saturate(Color{ 0x00, 0xA4, 0x90, 0xFF }) == Color{ 0, 255, 224, 0xFF }));
    EikonTests::ResetSettings();
    ObjHighlighter marker;
    marker.ApplyDefaults(Settings::Current());
    CHECK((marker.color == base));
    CHECK_EQ(marker.shapeCorrection, std::wstring{ L"h" });
    CHECK(Near(marker.WidthFor(9), 154));   // eight widths; the slider has eight steps
    CHECK_EQ(marker.capStyle, 3);
}

TEST(Conceal_BlockSizeAndGrid)
{
    // round(min(round(minSide/6), 5·dpr) × 1.35^size)
    CHECK(Near(Conceal::BlockSize(120, 1, 2), 9));
    CHECK(Near(Conceal::BlockSize(24, 2, 0), 4));
    CHECK(Near(Conceal::BlockSize(24, 2, 3), std::round(4 * 1.35 * 1.35 * 1.35)));
    // ceil(100/15/2) = 4 columns and ceil(60/15/2) = 2 rows of 25 × 30, cut at min(15, 5 + round((side − 10)/2)).
    auto const half = [] { return 0.5; };
    auto const cells = Conceal::Grid({ 0, 0, 100, 60 }, 5, half);
    CHECK((cells.front() == Conceal::Cell{ 75, 30, 13, 15 }));
    double area = 0;
    for (auto const& c : cells)
    {
        area += c.w * c.h;
        CHECK(c.w >= 1 && c.w <= 15 && c.h >= 1 && c.h <= 15);
    }
    CHECK(Near(area, 6000));
    // Cells never overlap.
    bool overlap = false;
    for (std::size_t i = 0; i < cells.size(); ++i)
        for (std::size_t j = i + 1; j < cells.size(); ++j)
            if (cells[i].x < cells[j].x + cells[j].w && cells[j].x < cells[i].x + cells[i].w && cells[i].y < cells[j].y + cells[j].h &&
                cells[j].y < cells[i].y + cells[i].h)
                overlap = true;
    CHECK(!overlap);
    // rand 0 gives b-wide blocks, and a side ≤ 2b is kept as it is.
    auto const zero = Conceal::Grid({ 0, 0, 25, 9 }, 5, [] { return 0.0; });
    CHECK((zero.front() == Conceal::Cell{ 0, 0, 5, 9 }));
}

TEST(Conceal_PixelateFillsCellsWithTheirMean)
{
    // A 10 × 6 image with rand 0.99 (k = 4 keeps every cell's own colour): every pixel of the output
    // is the integer mean of its cell.
    std::vector<std::vector<Color>> rows(6, std::vector<Color>(10));
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 10; ++x)
            rows[y][x] = Color{ static_cast<std::uint8_t>(x * 20), static_cast<std::uint8_t>(y * 30), 7, 255 };
    Bitmap const image = Image(rows);
    Bitmap out = Bitmap::Create(10, 6);
    Conceal::Pixelate(image, { 0, 0, 10, 6 }, 0, 1, [] { return 0.99; }, out, { 0, 0 });
    // b = round(min(round(6/6), 5)) = 1 → b' = 2: one 10 × 6 cell, then cut to ≤ 6.
    auto const cells = Conceal::Grid({ 0, 0, 10, 6 }, 1, [] { return 0.99; });
    for (auto const& c : cells)
    {
        int sr = 0, sg = 0, n = 0;
        for (int y = static_cast<int>(c.y); y < c.y + c.h; ++y)
            for (int x = static_cast<int>(c.x); x < c.x + c.w; ++x)
            {
                sr += x * 20;
                sg += y * 30;
                ++n;
            }
        Color const p = PixelAt(out, static_cast<int>(c.x), static_cast<int>(c.y));
        CHECK_EQ(static_cast<int>(p.r), sr / n);
        CHECK_EQ(static_cast<int>(p.g), sg / n);
    }
}

TEST(Conceal_EraseChoosesTheCheaperDirection)
{
    Bitmap const image = Image({
        { { 0, 0, 0 }, { 250, 250, 250 }, { 0, 0, 0 }, { 250, 250, 250 }, { 0, 0, 0 } },
        { { 10, 20, 30 }, { 9, 9, 9 }, { 9, 9, 9 }, { 9, 9, 9 }, { 100, 50, 0 } },
        { { 40, 40, 40 }, { 0, 0, 0 }, { 250, 0, 0 }, { 0, 0, 0 }, { 60, 60, 60 } },
    });
    double h = 0, v = 0;
    Conceal::EraseCosts(image, { 0, 0, 5, 3 }, h, v);
    CHECK(Near(h, 450.0));
    CHECK(Near(v, 473.3333333333333));
    Bitmap out = Bitmap::Create(5, 3);
    Conceal::Erase(image, { 0, 0, 5, 3 }, out, { 0, 0 });
    // Rows: a gradient from the left to the right border pixel.
    CHECK((PixelAt(out, 2, 1) == Color{ 55, 35, 15, 255 }));
    CHECK((PixelAt(out, 0, 1) == Color{ 10, 20, 30, 255 }));
    CHECK((PixelAt(out, 4, 2) == Color{ 60, 60, 60, 255 }));
    // A tie goes to rows; columns win when they are cheaper.
    Bitmap const columns = Image({ { { 0, 0, 0 }, { 200, 0, 0 } }, { { 0, 0, 0 }, { 200, 0, 0 } }, { { 0, 0, 0 }, { 200, 0, 0 } } });
    Conceal::EraseCosts(columns, { 0, 0, 2, 3 }, h, v);
    CHECK(v < h);
}

TEST(Conceal_TextModeOnlyTouchesWordBoxes)
{
    std::vector<std::vector<Color>> rows(20, std::vector<Color>(40, Color{ 255, 255, 255, 255 }));
    Bitmap const image = Image(rows);
    Bitmap const out = Conceal::Generate(image, { 0, 0, 40, 20 }, L"erase-text", 2, 1, { Rect{ 10, 5, 6, 4 } }, [] { return 0.5; });
    CHECK_EQ(out.width, 40);
    CHECK_EQ(PixelAt(out, 0, 0).a, std::uint8_t{ 0 });     // outside the boxes: transparent
    CHECK_EQ(PixelAt(out, 8, 3).a, std::uint8_t{ 255 });   // the box grown by 2 px
    CHECK_EQ(PixelAt(out, 18, 11).a, std::uint8_t{ 0 });
    CHECK_EQ(PixelAt(out, 17, 10).a, std::uint8_t{ 255 });
    CHECK((Conceal::ClampArea({ -5, 10, 20, -30 }, 40, 20) == Rect{ 0, 0, 15, 10 }));
    CHECK((Conceal::ClampArea({ 50, 50, 10, 10 }, 40, 20) == Rect{ 39, 19, 1, 1 }));
}

TEST(ObjConceal_DumpHasNoColour)
{
    ObjConceal conceal;
    conceal.rect = { 1, 2, 3, 4 };
    conceal.fillMode = L"erase-text";
    CHECK((MemberNames(conceal.Dump()) == std::vector<std::wstring>{ L"rect", L"size", L"fillMode" }));
    CheckRoundTrip(conceal);
}

TEST(ObjImage_RulerAndBasicRoundTrip)
{
    ObjRectangleBasic basic;
    basic.rect = { 0, 0, 10, 10 };
    CHECK((MemberNames(basic.Dump()) == std::vector<std::wstring>{ L"rect", L"size", L"color", L"showBackground", L"showBorder", L"backgroundAlpha" }));
    CheckRoundTrip(basic);
    ObjImage image;
    auto pixels = std::make_shared<Bitmap const>(Bitmap::Create(300, 100));
    image.Insert(pixels, { 0, 0, 800, 600 }, { 800, 600 });
    CHECK_EQ(image.imageHash.size(), std::size_t{ 24 });
    CHECK((image.rect == Rect{ 250, 250, 300, 100 }));
    CHECK(image.Image() == pixels);
    CHECK((MemberNames(image.Dump()) == std::vector<std::wstring>{ L"rect", L"size", L"color", L"showBackground", L"showBorder", L"backgroundAlpha", L"imageHash" }));
    CheckRoundTrip(image);
    ObjImage wide;
    wide.Insert(std::make_shared<Bitmap const>(Bitmap::Create(900, 100)), { 0, 0, 800, 600 }, { 800, 600 });
    CHECK(Near(wide.rect.x, 100));
    ObjRuler ruler;
    ruler.rect = { 10.7, 20.2, 50.9, 2 };
    auto const bars = ruler.BarRects();
    CHECK((bars[0] == Rect{ 10, 19, 50, 2 }));
    CHECK((bars[1] == Rect{ 10, 16, 1, 7 }));
    CHECK((bars[2] == Rect{ 59, 16, 1, 7 }));
    CHECK((MemberNames(ruler.Dump()) == std::vector<std::wstring>{ L"rect", L"size", L"color", L"vertical", L"tperc", L"tdist", L"dpr" }));
    CheckRoundTrip(ruler);
}

TEST(Factory_ToolIdsMapToClasses)
{
    EikonTests::ResetSettings();
    std::pair<wchar_t const*, wchar_t const*> const tools[] = {
        { L"arrow", L"ObjArrow" }, { L"text", L"ObjText" }, { L"ruler", L"ObjRuler" }, { L"rectangle", L"ObjRectangle" },
        { L"freehand", L"ObjFreehand" }, { L"magnifier", L"ObjMagnifier" }, { L"blur", L"ObjConceal" },
        { L"highlighter", L"ObjHighlighter" }, { L"spotlight", L"ObjSpotlight" }, { L"counter", L"ObjCounter" },
        { L"oval", L"ObjOval" }, { L"line", L"ObjLine" }, { L"image", L"ObjImage" },
    };
    for (auto const& [tool, name] : tools)
    {
        ObjectPtr object = CreateObject(tool, Settings::Current());
        CHECK(object != nullptr);
        if (object)
        {
            CHECK_EQ(object->ClassName(), std::wstring_view{ name });
            CHECK(object->IsCreating());
        }
    }
    CHECK(CreateObject(L"backdrop", Settings::Current()) == nullptr);
    CHECK(CreateObject(L"append", Settings::Current()) == nullptr);
    auto rectangle = CreateObject(L"rectangle", Settings::Current());
    CHECK_EQ(rectangle->Property(L"shape"), std::wstring{ L"rect" });
    auto spotlight = CreateObject(L"spotlight", Settings::Current());
    CHECK(spotlight->wantsSpotlight);
    spotlight->SetProperty(L"shape", L"oval");
    CHECK_EQ(spotlight->Property(L"shape"), std::wstring{ L"oval" });
    auto arrow = CreateObject(L"arrow", Settings::Current());
    arrow->SetProperty(L"arrowType", L"4");
    CHECK_EQ(arrow->Property(L"arrowType"), std::wstring{ L"4" });
    CHECK_EQ(Settings::Current().ReadString(L"Eikon.ObjArrow: arrowType", L""), std::wstring{ L"4" });
    CHECK_EQ(CreateObject(L"arrow", Settings::Current())->Property(L"arrowType"), std::wstring{ L"4" });
    auto line = CreateObject(L"line", Settings::Current());
    line->SetProperty(L"lineType", L"dash");
    CHECK_EQ(line->Property(L"lineType"), std::wstring{ L"dash" });
    auto blur = CreateObject(L"blur", Settings::Current());
    blur->SetProperty(L"concealType", L"erase-text");
    CHECK_EQ(blur->Property(L"concealType"), std::wstring{ L"erase-text" });
    auto ruler = CreateObject(L"ruler", Settings::Current());
    ruler->SetProperty(L"rulerPixels", L"px");
    CHECK_EQ(ruler->Property(L"rulerPixels"), std::wstring{ L"px" });
    auto pen = CreateObject(L"freehand", Settings::Current());
    CHECK_EQ(pen->Property(L"correction"), std::wstring{ L"x" });
    pen->SetProperty(L"correction", L"c");
    CHECK_EQ(pen->Property(L"correction"), std::wstring{ L"c" });
    auto text = CreateObject(L"text", Settings::Current());
    text->SetProperty(L"style", L"1");
    CHECK_EQ(text->Property(L"style"), std::wstring{ L"1" });
    CHECK_EQ(text->Property(L"size"), std::to_wstring(text->size));
    CHECK(LoadObject(*JsonValue::Parse(L"{\"type\":\"ObjNope\"}")) == nullptr);
    CHECK(LoadObject(*JsonValue::Parse(L"{\"type\":\"ObjArrow\",\"object\":{\"rect\":{\"x1\":0,\"y1\":0,\"width\":1,\"height\":1},\"size\":1,\"color\":\"#000000\"}}")) == nullptr);
    CHECK(LoadObject(*JsonValue::Parse(L"{\"type\":\"ObjCounter\",\"object\":{\"rect\":{\"x1\":0,\"y1\":0,\"width\":1,\"height\":1},\"size\":1,\"color\":\"#000000\",\"style\":2,\"count\":4}}")) != nullptr);
}
