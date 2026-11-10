#include "pch.h"
#include "TestFramework.h"

#include "Core/Library.h"
#include "Core/LibraryLayout.h"
#include "Core/Preferences.h"

using namespace Eikon::Core;
namespace L = Eikon::Core::LibraryLayout;

// ---- Line format ----

TEST(Library_ReadsPlainJsonLines)
{
    auto const record = Library::ParseLine("{\"filename\":\"SCR-20240102-kvqe.png\",\"ocr\":\"Hello\\nWorld\",\"timestamp\":\"1704196800\"}");
    CHECK(record.has_value());
    if (record)
    {
        CHECK_EQ(Library::Field(*record, Library::kFilename), std::wstring{ L"SCR-20240102-kvqe.png" });
        CHECK_EQ(Library::Field(*record, Library::kOcr), std::wstring{ L"Hello\nWorld" });
        CHECK_EQ(Library::IntField(*record, Library::kTimestamp), std::optional<std::int64_t>{ 1704196800 });
    }
}

TEST(Library_WritesOneJsonObjectPerLine)
{
    Library::Record record{ { L"filename", L"SCR-20240102-kvqe.png" }, { L"ocr", L"Hello\nWorld" }, { L"timestamp", L"1704196800" } };
    CHECK_EQ(Library::EncodeLine(record), std::string{ "{\"filename\":\"SCR-20240102-kvqe.png\",\"ocr\":\"Hello\\nWorld\",\"timestamp\":\"1704196800\"}" });
}

TEST(Library_EncryptedOrForeignLinesAreSkipped)
{
    // A base64 line (the encrypted format Lucida does not read) is skipped like any other non-JSON line.
    constexpr char kEncrypted[] =
        "AQIDBAUGBwgJCgsMPoqaCDojXtvq7vogD+pdw6tB3AVW3Ne++KAWv8TkTEwtOqqVGH6ykyiV532QppgecROhP7cTXvciWA57DxS+zViYEm4X/tuckZgdeEwmjmJkIONJLpjpqPE2FOXFDifXSNU=";
    CHECK(!Library::ParseLine(kEncrypted));
    CHECK(!Library::ParseLine("{\"bytes\":12}"));   // values must be strings
    CHECK(!Library::ParseLine("[\"a\"]"));
    CHECK(!Library::ParseLine(""));
    auto const plain = Library::ParseLine("  {\"bytes\":\"12\",\"app\":\"Code\"}\r");
    CHECK(plain.has_value() && Library::Field(*plain, Library::kApp) == L"Code");

    std::string const db = std::string{ kEncrypted } + "\r\n\ngarbage\n{\"mode\":\"fullscreen\"}\n{\"app\":\"Code\"}\n";
    auto const records = Library::ParseDatabase(db);
    CHECK_EQ(records.size(), std::size_t{ 2 });
}

TEST(Library_JsonEscapesTheSolidus)
{
    Library::Record record{ { L"window", L"a/b \"c\"" } };
    CHECK_EQ(Library::RecordJson(record), std::string{ "{\"window\":\"a\\/b \\\"c\\\"\"}" });
    auto const back = Library::ParseLine(Library::EncodeLine(record));
    CHECK(back.has_value() && Library::Field(*back, Library::kWindow) == L"a/b \"c\"");
}

// ---- File names ----

TEST(Library_TimeCodeIsQuarterSecondsOffsetBy26Cubed)
{
    CHECK_EQ(Library::SecondOfDayFromCode(L"kvqe"), std::optional<int>{ 43200 });   // example
    CHECK_EQ(Library::SecondOfDayFromCode(L"baaa"), std::optional<int>{ 0 });
    CHECK_EQ(Library::SecondOfDayFromCode(L"urgh"), std::optional<int>{ 86399 });   // 23:59:59.75
    CHECK(!Library::SecondOfDayFromCode(L"aaaa"));                                   // below 26^3
    CHECK(!Library::SecondOfDayFromCode(L"kvq"));
    CHECK(!Library::SecondOfDayFromCode(L"KVQE"));
}

TEST(Library_TimestampsFromFileNames)
{
    using T = Library::LocalTime;
    CHECK(Library::TimeFromFileName(L"SCR-20240102-kvqe.png") == (std::optional<T>{ T{ 2024, 1, 2, 12, 0, 0 } }));
    CHECK(Library::TimeFromFileName(L"SCR-20240102-baab.png") == (std::optional<T>{ T{ 2024, 1, 2, 0, 0, 0 } }));   // 1/4 s
    CHECK(Library::TimeFromFileName(L"shot-20231231-.png") == (std::optional<T>{ T{ 2023, 12, 31, 0, 0, 0 } }));
    CHECK(Library::TimeFromFileName(L"Screenshot 2024-01-02 at 10.11.12.png") == (std::optional<T>{ T{ 2024, 1, 2, 10, 11, 12 } }));
    CHECK(Library::TimeFromFileName(L"Screenshot 2024-01-02 at 1.02.03 PM.png") == (std::optional<T>{ T{ 2024, 1, 2, 13, 2, 3 } }));
    CHECK(Library::TimeFromFileName(L"Screenshot 2024-01-02 at 12.30.00 AM.png") == (std::optional<T>{ T{ 2024, 1, 2, 0, 30, 0 } }));
    CHECK(Library::TimeFromFileName(L"Screenshot 2026-11-08 143012.png") == (std::optional<T>{ T{ 2026, 11, 8, 14, 30, 12 } }));
    CHECK(Library::TimeFromFileName(L"notes 2024-02-29.png") == (std::optional<T>{ T{ 2024, 2, 29, 0, 0, 0 } }));
    CHECK(!Library::TimeFromFileName(L"notes 2023-02-29.png"));   // not a leap year
    CHECK(!Library::TimeFromFileName(L"SCR-20241302-kvqe.png"));  // month 13
    CHECK(!Library::TimeFromFileName(L"holiday.png"));
}

TEST(Library_WhichFilesAreScreenshots)
{
    CHECK(Library::IsImageFile(L"a.PNG"));
    CHECK(Library::IsImageFile(L"a.jpeg"));
    CHECK(Library::IsImageFile(L"a.Jpg"));
    CHECK(!Library::IsImageFile(L"a.gif"));
    CHECK(!Library::IsImageFile(L"png"));
    CHECK(Library::IsScreenshotName(L"SCR-20240102-kvqe.png"));
    CHECK(Library::IsScreenshotName(L"Screenshot 2026-11-08 143012.png"));
    CHECK(Library::IsScreenshotName(L"Screenshot 2024-01-02 at 10.11.12.png"));
    CHECK(!Library::IsScreenshotName(L"SCR-2024-kvqe.png"));
    CHECK(!Library::IsScreenshotName(L"holiday.png"));
}

// ---- Status line ----

TEST(Library_StatusTexts)
{
    CHECK_EQ(Library::CountText(0), std::wstring{ L"0 images" });
    CHECK_EQ(Library::CountText(1), std::wstring{ L"1 image" });
    CHECK_EQ(Library::CountText(2), std::wstring{ L"2 images" });
    CHECK_EQ(Library::FoundText(3), std::wstring{ L"3 images found" });
    CHECK_EQ(Library::SelectedText(4), std::wstring{ L"4 images selected" });
    CHECK_EQ(Library::DateText({ 2024, 1, 2, 0, 5, 0 }), std::wstring{ L"Jan 2, 2024 at 12:05am" });
    CHECK_EQ(Library::DateText({ 2024, 12, 31, 13, 30, 59 }), std::wstring{ L"Dec 31, 2024 at 1:30pm" });
    CHECK_EQ(Library::DateText({ 2024, 7, 4, 12, 0, 0 }), std::wstring{ L"Jul 4, 2024 at 12:00pm" });
}

TEST(Library_SizeTextSwitchesToMegabytesAbove1048576Bytes)
{
    CHECK_EQ(Library::SizeText(0), std::wstring{});
    CHECK_EQ(Library::SizeText(1023), std::wstring{ L"0kb" });
    CHECK_EQ(Library::SizeText(535'552 + 1000), std::wstring{ L"523kb" });
    CHECK_EQ(Library::SizeText(1'048'576), std::wstring{ L"1024kb" });   // not above the limit
    CHECK_EQ(Library::SizeText(1'048'577), std::wstring{ L"1.0mb" });
    CHECK_EQ(Library::SizeText(1'572'864), std::wstring{ L"1.5mb" });
    CHECK_EQ(Library::SizeText(1'677'721), std::wstring{ L"1.5mb" });    // 1.59999 truncates
    CHECK_EQ(Library::SingleText({ 2024, 1, 2, 12, 0, 0 }, 2048), std::wstring{ L"Jan 2, 2024 at 12:00pm  \u2219  2kb" });
    CHECK_EQ(Library::SingleText({ 2024, 1, 2, 12, 0, 0 }, 0), std::wstring{ L"Jan 2, 2024 at 12:00pm" });
}

// ---- Search ----

TEST(Library_SearchIsCaseInsensitiveOverOcrOnly)
{
    Library::Record record{ { L"ocr", L"Hello\nWorld" }, { L"filename", L"invoice.png" }, { L"app", L"Mail" } };
    CHECK_EQ(Library::TrimQuery(L"  \tHELLO \n"), std::wstring{ L"HELLO" });
    CHECK(Library::Matches(record, L"HELLO"));
    CHECK(Library::Matches(record, L"o\nw"));
    CHECK(!Library::Matches(record, L"invoice"));   // file name is not searched
    CHECK(!Library::Matches(record, L"Mail"));      // nor the app
    CHECK(!Library::Matches(Library::Record{}, L"x"));
}

TEST(Library_SaveTimeFields)
{
    // First letter after "Obj"; Ruler, Conceal and Image get I, B and P; sorted, duplicates kept.
    std::vector<std::wstring_view> const names{ L"ObjText", L"ObjArrow", L"ObjRuler", L"ObjRectangle", L"ObjConceal", L"ObjCounter",
                                                L"ObjImage", L"ObjArrow" };
    CHECK_EQ(Library::ObjectsField(names), std::wstring{ L"AABCIPRT" });
    CHECK_EQ(Library::ObjectsField({}), std::wstring{});
    // Texts joined with two newlines.
    std::vector<std::wstring_view> const texts{ L"Step one", L"two" };
    CHECK_EQ(Library::LabelsField(texts), std::wstring{ L"Step one\n\ntwo" });
    // Shortest round-trip digits.
    CHECK_EQ(Library::DprField(2.0), std::wstring{ L"2.0" });
    CHECK_EQ(Library::DprField(1.25), std::wstring{ L"1.25" });
    CHECK_EQ(Library::DprField(1.5), std::wstring{ L"1.5" });
}

TEST(Library_DomainsTakeTheFirstUrlOfEachLine)
{
    CHECK_EQ(Library::DomainsFromText(L"Visit www.Example.com/path today or a.org\nno url here\nsee github.io"),
             std::wstring{ L"example.com/path\ngithub.io" });
    CHECK_EQ(Library::DomainsFromText(L"nothing"), std::wstring{});
}

TEST(Library_FacetsAreUniqueInFirstSeenOrder)
{
    std::vector<Library::Record> records{
        { { L"app", L"Code" }, { L"mode", L"fullscreen" }, { L"objects", L"ObjArrow,ObjText" } },
        { { L"app", L"Mail" }, { L"mode", L"fullscreen" }, { L"domains", L"a.com\nb.org" } },
        { { L"app", L"Code" } },
    };
    std::vector<Library::LocalTime> times{ { 2024, 1, 2 }, { 2024, 1, 30 }, { 2023, 12, 1 } };
    auto const facets = Library::BuildFacets(records, times);
    CHECK(facets.apps == (std::vector<std::wstring>{ L"Code", L"Mail" }));
    CHECK(facets.types == std::vector<std::wstring>{ L"fullscreen" });
    CHECK(facets.drawings == (std::vector<std::wstring>{ L"ObjArrow", L"ObjText" }));
    CHECK(facets.domains == (std::vector<std::wstring>{ L"a.com", L"b.org" }));
    CHECK(facets.months == (std::vector<std::wstring>{ L"January 2024", L"December 2023" }));
}

// ---- Layouts ----

TEST(LibraryLayout_KindFromSettings)
{
    CHECK(L::KindFor(L"list", L"small") == L::Kind::List);
    CHECK(L::KindFor(L"square80", L"medium") == L::Kind::Square80);
    CHECK(L::KindFor(L"large", L"small") == L::Kind::Single);
    CHECK(L::KindFor(L"large", L"medium") == L::Kind::Pairs);
    CHECK(L::KindFor(L"medium", L"small") == L::Kind::Pairs);
    CHECK(L::KindFor(L"medium", L"medium") == L::Kind::Justified3);
    CHECK(L::KindFor(L"bogus", L"bogus") == L::Kind::Pairs);
    CHECK_EQ(L::MediumHeight(1000), 900.0);
    CHECK_EQ(L::MediumHeight(600), 580.0);
}

TEST(LibraryLayout_ListRowsAre24High)
{
    auto const r = L::Compute(L::Kind::List, 365, { { 100, 50 }, { 50, 100 } });
    CHECK(r.sizes == (std::vector<L::Size>{ { 365, 24 }, { 365, 24 } }));
    CHECK_EQ(r.lineSpacing, 0.0);
    CHECK_EQ(r.itemsPerRow, 1);
}

TEST(LibraryLayout_Square80ColumnsAndSide)
{
    // n = floor(W / 92); side = floor((W - (n - 1) * 12) / n).
    auto r = L::Compute(L::Kind::Square80, 365, { { 1000, 500 } });
    CHECK_EQ(r.itemsPerRow, 3);
    CHECK(r.sizes[0] == (L::Size{ 113, 113 }));
    CHECK(r.uncropped[0] == (L::Size{ 226, 113 }));   // aspect fill of a 2:1 image
    r = L::Compute(L::Kind::Square80, 367, { { 10, 10 } });
    CHECK_EQ(r.itemsPerRow, 3);
    r = L::Compute(L::Kind::Square80, 368, { { 10, 10 } });
    CHECK_EQ(r.itemsPerRow, 4);
    CHECK(r.sizes[0] == (L::Size{ 83, 83 }));
}

TEST(LibraryLayout_SingleColumnCapsAt1Point2Width)
{
    auto const r = L::Compute(L::Kind::Single, 365, { { 1000, 500 }, { 500, 1000 } });
    CHECK(r.sizes[0] == (L::Size{ 365, 182 }));
    CHECK(r.sizes[1] == (L::Size{ 365, 438 }));   // 730 capped at floor(1.2 W)
}

TEST(LibraryLayout_PairsSplitTheRowInSixths)
{
    // W - 12 = 353, a sixth = 58.
    auto r = L::Compute(L::Kind::Pairs, 365, { { 1000, 500 }, { 500, 1000 } });
    CHECK(r.sizes[0] == (L::Size{ 295, 147 }));
    CHECK(r.sizes[1] == (L::Size{ 58, 147 }));
    r = L::Compute(L::Kind::Pairs, 365, { { 500, 1000 }, { 1000, 500 } });
    CHECK(r.sizes[0] == (L::Size{ 121, 242 }));
    CHECK(r.sizes[1] == (L::Size{ 232, 242 }));
    r = L::Compute(L::Kind::Pairs, 365, { { 500, 1000 }, { 500, 1000 } });
    CHECK(r.sizes[0] == (L::Size{ 179, 340 }));   // 358 capped at 340
    CHECK(r.sizes[1] == (L::Size{ 174, 340 }));
    // Aspect ratios are clamped to [0.33, 3] and an image gets at least one sixth.
    r = L::Compute(L::Kind::Pairs, 365, { { 3000, 1000 }, { 1000, 9000 } });
    CHECK_EQ(r.sizes[1].width, 58.0);
    CHECK_EQ(r.sizes[0].width, 295.0);
}

TEST(LibraryLayout_PairsLastOddImageIsAtMost180High)
{
    auto const r = L::Compute(L::Kind::Pairs, 365, { { 1000, 1000 }, { 1000, 1000 }, { 1000, 500 } });
    CHECK_EQ(r.sizes.size(), std::size_t{ 3 });
    CHECK(r.sizes[2] == (L::Size{ 360, 180 }));
    auto const small = L::Compute(L::Kind::Pairs, 365, { { 1000, 250 } });
    CHECK(small.sizes[0] == (L::Size{ 364, 91 }));   // 91.25 -> 91, width 91 / 0.25 = 364
}

TEST(LibraryLayout_JustifiedRowsOfThreeFillWMinus24)
{
    // W = 545: row 521, unit floor(521 / 18) = 28, at most 16 units each.
    auto const r = L::Compute(L::Kind::Justified3, 545, { { 1000, 1000 }, { 2000, 1000 }, { 500, 1000 }, { 1000, 1000 } });
    CHECK_EQ(r.sizes.size(), std::size_t{ 4 });
    CHECK(r.sizes[0] == (L::Size{ 140, 148 }));
    CHECK(r.sizes[1] == (L::Size{ 280, 148 }));
    CHECK(r.sizes[2] == (L::Size{ 101, 148 }));   // the last takes the rest
    CHECK_EQ(r.sizes[0].width + r.sizes[1].width + r.sizes[2].width, 521.0);
    // A trailing single item gets a third of the row.
    CHECK_NEAR(r.sizes[3].width, 521.0 / 3.0, 1e-9);
    CHECK_EQ(r.sizes[3].height, 173.0);
    CHECK_EQ(r.itemsPerRow, 3);
}

TEST(LibraryLayout_FlowJustifiesRowsAndCentresALoneCell)
{
    // 380-wide view: 12 + 365 + 3.
    auto const flow = L::Place({ { 295, 147 }, { 58, 147 }, { 360, 180 } }, 380, 12);
    CHECK_EQ(flow.rowStarts.size(), std::size_t{ 2 });
    CHECK(flow.frames[0] == (L::Rect{ 12, 2, 295, 147 }));
    CHECK(flow.frames[1] == (L::Rect{ 319, 2, 58, 147 }));   // 365 - 353 = 12 between them
    CHECK(flow.frames[2] == (L::Rect{ 14.5, 161, 360, 180 }));
    CHECK_EQ(flow.height, 341.0);
    // Cells that don't fit with the 11-pt spacing wrap.
    auto const wrapped = L::Place({ { 200, 10 }, { 160, 20 } }, 380, 12);
    CHECK_EQ(wrapped.rowStarts.size(), std::size_t{ 2 });
}

// ---- Preferences ----

TEST(Preferences_SolidColorInput)
{
    CHECK(Preferences::NormalizeSolidColor(L" #c4c6c8 ") == std::optional<std::wstring>{ L"#C4C6C8" });
    CHECK(Preferences::NormalizeSolidColor(L"404448") == std::optional<std::wstring>{ L"#404448" });
    CHECK(!Preferences::NormalizeSolidColor(L"12345"));
    CHECK(!Preferences::NormalizeSolidColor(L"#abcdef0"));
    CHECK(!Preferences::NormalizeSolidColor(L"GGGGGG"));
    CHECK(!Preferences::NormalizeSolidColor(L"ab cdef"));
}

TEST(Preferences_ScrollingMaxIsClampedTo10And400000)
{
    CHECK_EQ(Preferences::ParseScrollingMax(L"5"), std::int64_t{ 10 });
    CHECK_EQ(Preferences::ParseScrollingMax(L"10"), std::int64_t{ 10 });
    CHECK_EQ(Preferences::ParseScrollingMax(L"11"), std::int64_t{ 11 });
    CHECK_EQ(Preferences::ParseScrollingMax(L"399999"), std::int64_t{ 399999 });
    CHECK_EQ(Preferences::ParseScrollingMax(L"400001"), std::int64_t{ 400000 });
    CHECK_EQ(Preferences::ParseScrollingMax(L"abc"), std::int64_t{ 10 });
    CHECK_EQ(Preferences::ParseScrollingMax(L"20000"), std::int64_t{ 20000 });
}

TEST(Preferences_AtLeastOneAfterScreenshotActionStays)
{
    using A = Preferences::AfterGrab;
    CHECK(Preferences::SetShow(A{ true, false, false }, false) == (A{ false, true, false }));
    CHECK(Preferences::SetShow(A{ true, false, true }, false) == (A{ false, false, true }));
    CHECK(Preferences::SetCopy(A{ false, true, false }, false) == (A{ true, false, false }));
    CHECK(Preferences::SetCopy(A{ false, true, true }, false) == (A{ false, false, true }));
    CHECK(Preferences::SetSave(A{ false, false, true }, false) == (A{ true, false, false }));
    CHECK(Preferences::SetSave(A{ false, true, false }, true) == (A{ false, true, true }));
}

TEST(Preferences_PresignExpirationTable)
{
    auto const& table = Preferences::Expirations();
    CHECK_EQ(table.size(), std::size_t{ 7 });
    CHECK_EQ(table[0].seconds, std::int64_t{ 3600 });
    CHECK_EQ(std::wstring{ table[1].label }, std::wstring{ L"6 hrs" });
    CHECK_EQ(table[6].seconds, std::int64_t{ 604800 });
    CHECK_EQ(Preferences::ExpirationIndex(604800), 6);
    CHECK_EQ(Preferences::ExpirationIndex(3600), 0);
    CHECK_EQ(Preferences::ExpirationIndex(50000), 2);      // nearer 12 hrs than 24 hrs
    CHECK_EQ(Preferences::ExpirationIndex(70000), 3);
    CHECK_EQ(Preferences::SnapExpiration(2.6), 3);
    CHECK_EQ(Preferences::SnapExpiration(-1), 0);
    CHECK_EQ(Preferences::SnapExpiration(9), 6);
}

TEST(Preferences_EndpointAndPublicUrlNormalisation)
{
    CHECK_EQ(Preferences::NormalizeEndpoint(L" s3.example.com/ \n"), std::wstring{ L"https://s3.example.com" });
    CHECK_EQ(Preferences::NormalizeEndpoint(L"http://x.y//"), std::wstring{ L"http://x.y/" });   // one slash removed
    CHECK_EQ(Preferences::NormalizeEndpoint(L"minio+s3://host"), std::wstring{ L"minio+s3://host" });
    CHECK_EQ(Preferences::NormalizeEndpoint(L"  "), std::wstring{});
    CHECK_EQ(Preferences::NormalizePublicUrl(L"cdn.x.com"), std::wstring{ L"https://cdn.x.com/" });
    CHECK_EQ(Preferences::NormalizePublicUrl(L"https://cdn.x.com/"), std::wstring{ L"https://cdn.x.com/" });
    CHECK(Preferences::HasUppercase(L"myBucket"));
    CHECK(!Preferences::HasUppercase(L"my-bucket"));
}

TEST(Preferences_OcrLanguageNames)
{
    CHECK_EQ(Preferences::OcrLanguageName(L"en-US"), std::wstring{ L"English+" });
    CHECK_EQ(Preferences::OcrLanguageName(L"ru-RU"), std::wstring{ L"Cyrillic" });
    CHECK_EQ(Preferences::OcrLanguageName(L"nb-NO"), std::wstring{ L"Norwegian (Bokm\u00E5l)" });
    CHECK_EQ(Preferences::OcrLanguageName(L"xx-YY"), std::wstring{ L"xx-YY" });
    bool hant = false;
    for (auto const& language : Preferences::OcrLanguages())
    {
        hant = hant || language.code == L"zh-Hant" || language.code == L"yue-Hant";
    }
    CHECK(!hant);
}

TEST(Library_DominantColorsMergeNearShadesAndSortByFrequency)
{
    // 100 pixels: 60 red, 25 near-red (merged into red: L1 = 0.04 < 0.2, adds 1), 15 blue. One transparent
    // pixel is ignored.
    std::vector<std::uint8_t> px;
    auto add = [&](int n, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
        for (int i = 0; i < n; ++i)
        {
            px.insert(px.end(), { b, g, r, a });
        }
    };
    add(15, 0, 0, 255);
    add(25, 250, 5, 0);
    add(59, 255, 0, 0);
    add(1, 9, 9, 9, 0);
    CHECK_EQ(Library::DominantColors(px, 10, 10), std::wstring{ L"#FF0000,#0000FF" });
    CHECK_EQ(Library::DominantColors({}, 0, 0), std::wstring{ L"#000000" });
    // Average of opaque pixels: (15*0 + 25*250 + 59*255) / 99 = 215.1 -> D7.
    CHECK_EQ(Library::AverageColor(px, 10, 10), std::wstring{ L"#D70127" });
}
