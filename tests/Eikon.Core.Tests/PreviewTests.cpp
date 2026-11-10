#include "pch.h"
#include "TestFramework.h"

#include "Core/OcrText.h"
#include "Core/PreviewLayout.h"

using namespace Eikon::Core;

// ---- OCR text ----

TEST(OcrText_FullWidthPunctuation)
{
    CHECK_EQ(OcrText::CleanUp(L"a\uFF0Cb\uFF08c\uFF09d\uFF0De\uFF0Ef\uFF01g\uFF04h\uFF05i\uFF02j"),
             std::wstring{ L"a, b (c) d-e. f! g$h%i\"j" });
    // Other full-width characters (colon, question mark) are left alone.
    CHECK_EQ(OcrText::CleanUp(L"\uFF1A\uFF1F"), std::wstring{ L"\uFF1A\uFF1F" });
}

TEST(OcrText_DoubleSpaceReplacedInOnePass)
{
    CHECK_EQ(OcrText::CleanUp(L"a  b"), std::wstring{ L"a b" });
    CHECK_EQ(OcrText::CleanUp(L"a   b"), std::wstring{ L"a  b" });   // three become two
    CHECK_EQ(OcrText::CleanUp(L"a    b"), std::wstring{ L"a  b" });  // four become two
    // "，" + space gives ", " + " " = three spaces after the comma's own one... the pass runs after
    // the replacements: "x，  y" -> "x,   y" -> "x,  y".
    CHECK_EQ(OcrText::CleanUp(L"x\uFF0C  y"), std::wstring{ L"x,  y" });
    // "）" then "（": ") " + " (" = two spaces -> one.
    CHECK_EQ(OcrText::CleanUp(L"\uFF09\uFF08"), std::wstring{ L") (" });
}

TEST(OcrText_RemoveBreaksExactly)
{
    CHECK_EQ(OcrText::RemoveBreaks(L"a\nb"), std::wstring{ L"a b" });
    CHECK_EQ(OcrText::RemoveBreaks(L"a\n\n\nb"), std::wstring{ L"a b" });
    CHECK_EQ(OcrText::RemoveBreaks(L"a \nb"), std::wstring{ L"a  b" });     // spaces kept
    CHECK_EQ(OcrText::RemoveBreaks(L"a\r\nb"), std::wstring{ L"a\r b" });   // \r untouched
    CHECK_EQ(OcrText::RemoveBreaks(L"co-\noperate"), std::wstring{ L"co- operate" }); // no hyphen joining
    CHECK_EQ(OcrText::RemoveBreaks(L"\nx\n"), std::wstring{ L" x " });      // no trimming
}

TEST(OcrText_NotificationBodyTruncation)
{
    std::wstring const exactly120(120, L'x');
    CHECK_EQ(OcrText::NotificationBody(exactly120), exactly120);
    std::wstring const longer = std::wstring(107, L'a') + std::wstring(14, L'b');   // 121
    CHECK_EQ(OcrText::NotificationBody(longer), std::wstring(107, L'a') + L"...");
    CHECK_EQ(OcrText::NotificationBody(L"line1\nline2"), std::wstring{ L"line1 line2" });
    // A surrogate pair counts as one character and is never cut in half.
    std::wstring emoji;
    for (int i = 0; i < 121; ++i)
    {
        emoji += L"\U0001F600";
    }
    std::wstring expected;
    for (int i = 0; i < 107; ++i)
    {
        expected += L"\U0001F600";
    }
    CHECK_EQ(OcrText::NotificationBody(emoji), expected + L"...");
}

TEST(OcrText_EditorSizeLimit)
{
    CHECK(!OcrText::IsTooLargeForEditor(6000, 3000));
    CHECK(OcrText::IsTooLargeForEditor(6001, 10));
    CHECK(OcrText::IsTooLargeForEditor(10, 3001));
}

TEST(OcrText_EngineLanguages)
{
    using V = std::vector<std::wstring>;
    CHECK((OcrText::EngineLanguages(L"en-US") == V{ L"en-US" }));
    CHECK((OcrText::EngineLanguages(L"zh-Hans") == V{ L"zh-Hans", L"zh-Hant", L"en-US" }));
    CHECK((OcrText::EngineLanguages(L"yue-Hans") == V{ L"zh-Hans", L"zh-Hant", L"en-US" }));
    CHECK((OcrText::EngineLanguages(L"de-DE") == V{ L"de-DE", L"en-US" }));
    CHECK((OcrText::EngineLanguages(L"uk-UA") == V{ L"ru-RU", L"en-US" }));
    CHECK((OcrText::EngineLanguages(L"ars-SA") == V{ L"ar-SA", L"en-US" }));
    CHECK((OcrText::EngineLanguages(L"nn-NO") == V{ L"nb-NO", L"en-US" }));
    CHECK((OcrText::EngineLanguages(L"vi-VT") == V{ L"en-US" }));
    CHECK((OcrText::EngineLanguages(L"th-TH") == V{ L"en-US" }));
    // "en-GB" is not exactly "en-US": X then en-US.
    CHECK((OcrText::EngineLanguages(L"en-GB") == V{ L"en-GB", L"en-US" }));
}

TEST(OcrText_MatchLanguage)
{
    std::vector<std::wstring> const available{ L"en-US", L"zh-Hans-CN", L"zh-Hant-TW", L"de-CH" };
    CHECK_EQ(OcrText::MatchLanguage(L"EN-us", available).value_or(99), std::size_t{ 0 });
    CHECK_EQ(OcrText::MatchLanguage(L"zh-Hant", available).value_or(99), std::size_t{ 2 });
    CHECK_EQ(OcrText::MatchLanguage(L"zh-Hans", available).value_or(99), std::size_t{ 1 });
    CHECK_EQ(OcrText::MatchLanguage(L"de-DE", available).value_or(99), std::size_t{ 3 });
    CHECK(!OcrText::MatchLanguage(L"fr-FR", available));
    // Region-only Chinese tags map to their script.
    CHECK_EQ(OcrText::MatchLanguage(L"zh-Hans", { L"zh-TW", L"zh-CN" }).value_or(99), std::size_t{ 1 });
    CHECK(!OcrText::MatchLanguage(L"zh-Hant", { L"zh-CN" }));
}

TEST(OcrText_JoinWords)
{
    CHECK_EQ(OcrText::JoinWords({ L"Hello", L"world" }), std::wstring{ L"Hello world" });
    CHECK_EQ(OcrText::JoinWords({ L"\u4F60", L"\u597D", L"\u4E16\u754C" }), std::wstring{ L"\u4F60\u597D\u4E16\u754C" });
    CHECK_EQ(OcrText::JoinWords({ L"Eikon", L"\u662F", L"\u597D" }), std::wstring{ L"Eikon \u662F\u597D" });
    CHECK_EQ(OcrText::JoinWords({ L"\u30C6\u30B9\u30C8", L"\u3002" }), std::wstring{ L"\u30C6\u30B9\u30C8\u3002" });
    CHECK_EQ(OcrText::JoinWords({ L"\uC548\uB155", L"\uD558\uC138\uC694" }), std::wstring{ L"\uC548\uB155 \uD558\uC138\uC694" }); // Hangul keeps spaces
}

// ---- Thumbnail ----

TEST(Preview_ThumbnailSizes)
{
    auto const wide = PreviewLayout::ThumbnailFor({ 1920, 1080 }, false);
    // imgH = round(186 · 1080 / 1920) = round(104.625) = 105; window 204 × (70 + 105).
    CHECK((wide.window == Size{ 204, 175 }));
    CHECK_NEAR(wide.image.width, 105.0 * 1920 / 1080, 1e-9);
    CHECK_NEAR(wide.image.x, (204 - 105.0 * 1920 / 1080) / 2, 1e-9);
    CHECK_EQ(wide.image.y, 25.0);
    CHECK_EQ(wide.image.height, 105.0);

    auto const automatic = PreviewLayout::ThumbnailFor({ 1920, 1080 }, true);
    CHECK((automatic.window == Size{ 204, 159 }));   // 16 pt shorter, paddingTop 9
    CHECK_EQ(automatic.image.y, 9.0);
    CHECK_EQ(automatic.image.height, 105.0);

    // Tall image: clamped to 190 high.
    auto const tall = PreviewLayout::ThumbnailFor({ 500, 2000 }, false);
    CHECK((tall.window == Size{ 204, 260 }));
    CHECK_EQ(tall.image.width, 47.5);

    // Rounding is half away from zero: 186 / 4 = 46.5 -> 47.
    CHECK_EQ(PreviewLayout::ThumbnailFor({ 400, 100 }, false).image.height, 47.0);

    // Very wide: 20 high minimum; the container (20 · 40 = 800) stays within the 204-wide window.
    auto const strip = PreviewLayout::ThumbnailFor({ 4000, 100 }, false);
    CHECK_EQ(strip.image.height, 20.0);
    CHECK_EQ(strip.image.width, 204.0);
    CHECK_EQ(strip.image.x, 0.0);
}

TEST(Preview_ThumbnailOrigin)
{
    Rect const screen{ 0, 0, 1920, 1080 };
    Rect const capture{ 100, 100, 500, 300 };
    Size const window{ 204, 175 };
    // Drag ended bottom-right: left of and above the cursor.
    CHECK((PreviewLayout::ThumbnailOrigin({ 600, 400 }, capture, window, screen) == Point{ 396, 225 }));
    // Top-left: right of and below.
    CHECK((PreviewLayout::ThumbnailOrigin({ 100, 100 }, capture, window, screen) == Point{ 100, 100 }));
    // Top-right: left of and below.
    CHECK((PreviewLayout::ThumbnailOrigin({ 600, 100 }, capture, window, screen) == Point{ 396, 100 }));
    // Clamped to the monitor (not the work area): bottom edge 1080.
    CHECK((PreviewLayout::ThumbnailOrigin({ 100, 1070 }, { 100, 1070, 50, 5 }, window, screen) == Point{ 100, 905 }));
    CHECK((PreviewLayout::ThumbnailOrigin({ 5, 5 }, { 5, 5, 10, 10 }, window, { 0, 0, 1920, 1080 }) == Point{ 5, 5 }));
    // Second monitor to the left (negative coordinates).
    CHECK((PreviewLayout::ThumbnailOrigin({ -1900, 10 }, { -1900, 10, 400, 400 }, window, { -1920, 0, 1920, 1080 }) == Point{ -1900, 10 }));

    // No drawn rect: the image-sized rect ends at the mouse, so the thumbnail sits left of and below it.
    Rect const atMouse = PreviewLayout::RectEndingAtMouse({ 800, 500 }, { 400, 300 });
    CHECK((PreviewLayout::ThumbnailOrigin({ 800, 500 }, atMouse, window, screen) == Point{ 596, 500 }));
}

// ---- Pins ----

TEST(Pin_StartScale)
{
    CHECK_EQ(PreviewLayout::PinStartScale({ 1440, 800 }, { 1920, 1080 }), 0.5);   // 0.75 · 1920 inclusive
    CHECK_EQ(PreviewLayout::PinStartScale({ 1439, 809 }, { 1920, 1080 }), 1.0);
    CHECK_EQ(PreviewLayout::PinStartScale({ 100, 810 }, { 1920, 1080 }), 0.5);
}

TEST(Pin_ResizeSnapsAndClamps)
{
    Size const original{ 400, 300 };
    CHECK((PreviewLayout::PinResize({ 210, 157.5 }, original, 1920) == Size{ 200, 150 }));   // 0.525 -> 0.5
    CHECK((PreviewLayout::PinResize({ 220, 165 }, original, 1920) == Size{ 220, 165 }));     // 0.55 is outside (strict)
    CHECK((PreviewLayout::PinResize({ 176, 132 }, original, 1920) == Size{ 176, 132 }));     // 0.44 is outside
    CHECK((PreviewLayout::PinResize({ 410, 307.5 }, original, 1920) == Size{ 400, 300 }));   // 1.025 -> 1
    CHECK((PreviewLayout::PinResize({ 790, 592.5 }, original, 1920) == Size{ 800, 600 }));   // 1.975 -> 2
    CHECK((PreviewLayout::PinResize({ 840, 630 }, original, 1920) == Size{ 840, 630 }));     // 2.1 not snapped
    CHECK((PreviewLayout::PinResize({ 4000, 3000 }, original, 1920) == Size{ 3840, 2880 }));  // max 2 × screen width
    // Minimum 44 on both sides with the aspect kept: height 44 -> width 58.67.
    Size const small = PreviewLayout::PinResize({ 30, 22.5 }, original, 1920);
    CHECK_NEAR(small.width, 44.0 / 0.75, 1e-9);
    CHECK_NEAR(small.height, 44.0, 1e-9);
}

TEST(Pin_ZoomPercentTruncates)
{
    CHECK_EQ(PreviewLayout::PinZoomPercent({ 200, 150 }, { 400, 300 }), std::int64_t{ 50 });
    CHECK_EQ(PreviewLayout::PinZoomPercent({ 401, 300 }, { 400, 300 }), std::int64_t{ 100 });   // 100.25
    CHECK_EQ(PreviewLayout::PinZoomPercent({ 399, 300.9 }, { 400, 300 }), std::int64_t{ 100 }); // max(99.75, 100.3)
    CHECK_EQ(PreviewLayout::PinZoomPercent({ 799, 599 }, { 400, 300 }), std::int64_t{ 199 });
}

TEST(Pin_ZoomSteps)
{
    auto step = [](std::int64_t zoom, bool in) { return PreviewLayout::PinZoomStep(zoom, in, 400, 1920).value_or(-1); };
    CHECK_EQ(step(100, true), std::int64_t{ 125 });   // 125.99 -> 125
    CHECK_EQ(step(125, true), std::int64_t{ 150 });   // 157.49 -> 150
    CHECK_EQ(step(175, true), std::int64_t{ 200 });   // 220.49 -> 200
    CHECK_EQ(step(100, false), std::int64_t{ 75 });   // 79.37 -> 75 (multiple of 5)
    CHECK_EQ(step(75, false), std::int64_t{ 55 });    // 59.53 -> 55
    CHECK_EQ(step(115, false), std::int64_t{ 75 });   // 91.27 > 90 -> multiple of 25
    CHECK_EQ(step(113, false), std::int64_t{ 85 });   // 89.69 <= 90 -> multiple of 5
    CHECK_EQ(step(26, false), std::int64_t{ 20 });    // displayed 104 > 100
    CHECK(!PreviewLayout::PinZoomStep(25, false, 400, 1920));   // displayed 100: ignored
    CHECK(!PreviewLayout::PinZoomStep(960, true, 400, 1920));   // displayed 3840 = 2 × screen: ignored
    CHECK_EQ(PreviewLayout::PinZoomStep(959, true, 400, 1920).value_or(-1), std::int64_t{ 1200 });
}
