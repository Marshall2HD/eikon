#include "pch.h"
#include "TestFramework.h"

#include "Core/Geometry.h"
#include "Core/Json.h"

using namespace Eikon::Core;

TEST(Geometry_FrintaRoundsHalfAwayFromZero)
{
    CHECK_EQ(frinta(0.5), 1.0);
    CHECK_EQ(frinta(1.5), 2.0);
    CHECK_EQ(frinta(2.5), 3.0);   // banker's rounding would give 2
    CHECK_EQ(frinta(-0.5), -1.0);
    CHECK_EQ(frinta(-2.5), -3.0); // floor(x + 0.5) would give -2
    CHECK_EQ(frinta(2.4999999999999996), 2.0);
    CHECK_EQ(frinta(0.49999999999999994), 0.0); // floor(x + 0.5) gives 1
    CHECK_EQ(frinta(-1.2), -1.0);
    CHECK_EQ(frinta(7.0), 7.0);
}

TEST(Geometry_FrintaIntSaturatesAndHandlesNan)
{
    CHECK_EQ(frintaInt(2.5), std::int64_t{ 3 });
    CHECK_EQ(frintaInt(-2.5), std::int64_t{ -3 });
    CHECK_EQ(frintaInt(std::nan("")), std::int64_t{ 0 });
    CHECK_EQ(frintaInt(1e300), INT64_MAX);
    CHECK_EQ(frintaInt(-1e300), INT64_MIN);
}

TEST(Geometry_RectEdgesWithNegativeSize)
{
    Rect const r{ 10, 20, -4, -6 };
    CHECK_EQ(r.Left(), 6.0);
    CHECK_EQ(r.Right(), 10.0);
    CHECK_EQ(r.Top(), 14.0);
    CHECK_EQ(r.Bottom(), 20.0);
    CHECK((r.Standardized() == Rect{ 6, 14, 4, 6 }));
    CHECK((r.Contains({ 6, 14 })));
    CHECK((!r.Contains({ 10, 14 }))); // right edge excluded
    CHECK((Rect{ 0, 0, 10, 10 }.Intersection(Rect{ 5, 5, 10, 10 }) == Rect{ 5, 5, 5, 5 }));
    CHECK((Rect{ 0, 0, 10, 10 }.Intersection(Rect{ 20, 20, 1, 1 }).IsEmpty()));
}

TEST(Geometry_InitialEditorContentSize)
{
    Size const screen{ 1920, 1080 };
    // Small image: at least 836 wide and half as tall as wide.
    CHECK((InitialEditorContentSize({ 400, 300 }, 1, screen, true) == Size{ 836, 418 }));
    // A 2x image larger than 0.8 of the screen: clamped to maxW = 1536, maxH = 864.
    CHECK((InitialEditorContentSize({ 4000, 3000 }, 2, screen, true) == Size{ 1536, 864 }));
    // Same image without preferLargeWindow: f = 0.5 -> maxW = 960, maxH = max(480, 540) = 540.
    CHECK((InitialEditorContentSize({ 4000, 3000 }, 2, screen, false) == Size{ 960, 540 }));
    // Width between 836 and maxW is kept; the height is the image's.
    CHECK((InitialEditorContentSize({ 1000, 700 }, 1, screen, true) == Size{ 1000, 700 }));
    // Height rounds half away from zero: 867 / 2 = 433.5 -> 434.
    CHECK((InitialEditorContentSize({ 800, 867 }, 2, screen, true) == Size{ 836, 434 }));
    // A tiny screen still gives the 836-point minimum width.
    CHECK((InitialEditorContentSize({ 2000, 2000 }, 1, { 800, 600 }, true) == Size{ 836, 480 }));
    // dpr 1.5: 1500 px = 1000 pt.
    CHECK((InitialEditorContentSize({ 1500, 900 }, 1.5, screen, true) == Size{ 1000, 600 }));
}

TEST(Geometry_FitZoomCapsAtOne)
{
    CHECK_EQ(FitZoom({ 100, 100 }, { 1000, 1000 }), 1.0);
    CHECK_EQ(FitZoom({ 2000, 1000 }, { 1000, 1000 }), 0.5);
    CHECK_EQ(FitZoom({ 1000, 4000 }, { 1000, 1000 }), 0.25);
    CHECK_EQ(FitZoom({ 0, 0 }, { 1000, 1000 }), 1.0);
}

TEST(Json_RoundTripKeepsOrderAndTypes)
{
    auto const parsed = JsonValue::Parse(L" {\"rect\":{\"x1\":1.5,\"y1\":-2,\"width\":10,\"height\":0},\"size\":2,\"color\":\"#FF0C01\",\"on\":true,\"none\":null,\"list\":[1,\"a\\n\\u00e9\"]} ");
    CHECK(parsed.has_value());
    if (!parsed)
    {
        return;
    }
    CHECK_EQ(parsed->Serialize(), std::wstring{ L"{\"rect\":{\"x1\":1.5,\"y1\":-2,\"width\":10,\"height\":0},\"size\":2,\"color\":\"#FF0C01\",\"on\":true,\"none\":null,\"list\":[1,\"a\\n\u00e9\"]}" });
    CHECK_EQ(parsed->Members().front().first, std::wstring{ L"rect" });
    CHECK_EQ(parsed->Find(L"size")->AsNumber(), 2.0);
    CHECK_EQ(parsed->Find(L"color")->AsString(), std::wstring{ L"#FF0C01" });
    CHECK(parsed->Find(L"missing") == nullptr);
    CHECK_EQ(JsonValue::Parse(parsed->Serialize()), parsed);
}

TEST(Json_RejectsMalformedDocuments)
{
    for (auto text : { L"", L"{", L"[1,]", L"{\"a\":1,}", L"{'a':1}", L"01", L"1.", L"tru", L"\"\\x\"", L"[1] 2", L"\"a\nb\"" })
    {
        if (JsonValue::Parse(text).has_value())
        {
            EikonTests::ReportFailure(__FILE__, __LINE__, "accepted: " + EikonTests::Narrow(text));
        }
    }
    CHECK(JsonValue::Parse(L"-0.5e+2").has_value());
    CHECK_EQ(JsonValue::Parse(L"-0.5e+2")->AsNumber(), -50.0);
}
