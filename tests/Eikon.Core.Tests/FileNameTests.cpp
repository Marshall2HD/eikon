#include "pch.h"
#include "TestFramework.h"

#include "Core/FileName.h"

using namespace Eikon::Core::FileName;

// (and where the spec differs).

namespace
{
    DateParts Date(int year, int month, int day, int hour, int minute, int second, int weekday, int dayOfYear, int nanosecond = 0)
    {
        DateParts d;
        d.year = year;
        d.month = month;
        d.day = day;
        d.hour = hour;
        d.minute = minute;
        d.second = second;
        d.weekday = weekday;
        d.dayOfYear = dayOfYear;
        d.nanosecond = nanosecond;
        return d;
    }
}

TEST(FileName_QuarterSecondCodeBoundaries)
{
    CHECK_EQ(QuarterSecondCode(0, 0), std::wstring{ L"baaa" });                  // 00:00:00.00
    CHECK_EQ(QuarterSecondCode(0, 249'999'999), std::wstring{ L"baaa" });
    CHECK_EQ(QuarterSecondCode(0, 250'000'000), std::wstring{ L"baab" });
    CHECK_EQ(QuarterSecondCode(43200, 0), std::wstring{ L"kvqe" });              // 12:00:00.0
    CHECK_EQ(QuarterSecondCode(86399, 749'999'999), std::wstring{ L"urgg" });
    CHECK_EQ(QuarterSecondCode(86399, 750'000'000), std::wstring{ L"urgh" });    // 23:59:59.75
    CHECK_EQ(QuarterSecondCode(86399, 999'999'999), std::wstring{ L"urgh" });
}

TEST(FileName_HalfSecondCode)
{
    CHECK_EQ(HalfSecondCode(0), std::wstring{ L"100" });     // 36²
    CHECK_EQ(HalfSecondCode(1), std::wstring{ L"101" });     // round(0.5) = 1
    CHECK_EQ(HalfSecondCode(86399), std::wstring{ L"yc0" }); // 43200 + 1296 = 44496
}

TEST(FileName_DefaultTemplate)
{
    CHECK_EQ(Generate(kDefaultTemplate, Date(2026, 11, 8, 12, 0, 0, 0, 312), L"Code"), std::wstring{ L"SCR-20261108-kvqe" });
    CHECK_EQ(Generate(L"", Date(2026, 11, 8, 0, 0, 0, 0, 312), L""), std::wstring{ L"SCR-20261108-baaa" });
    CHECK_EQ(kPngExtension, std::wstring_view{ L".png" });
    CHECK_EQ(kJpegExtension, std::wstring_view{ L".jpeg" });
}

TEST(FileName_AllTokens)
{
    // Sunday 4 January 2026, 15:07:09. 1 January 2026 is a Thursday, so the 4th is in week 2.
    DateParts const d = Date(2026, 1, 4, 15, 7, 9, 0, 4);
    CHECK_EQ(ExpandTokens(L"%Y|%y|%m|%b|%B|%d|%j", d, L""), std::wstring{ L"2026|26|01|Jan|January|04|004" });
    CHECK_EQ(ExpandTokens(L"%A|%a|%W|%Q", d, L""), std::wstring{ L"Sunday|Sun|02|1" });
    CHECK_EQ(ExpandTokens(L"%H|%I|%p|%P|%M|%S", d, L""), std::wstring{ L"15|03|PM|pm|07|09" });
    CHECK_EQ(ExpandTokens(L"%n", d, L"Notepad"), std::wstring{ L"Notepad" });
    // Midnight and noon on the 12-hour clock.
    CHECK_EQ(ExpandTokens(L"%I %p", Date(2026, 1, 4, 0, 0, 0, 0, 4), L""), std::wstring{ L"12 AM" });
    CHECK_EQ(ExpandTokens(L"%I %P", Date(2026, 1, 4, 12, 0, 0, 0, 4), L""), std::wstring{ L"12 pm" });
    CHECK_EQ(ExpandTokens(L"%Q", Date(2026, 12, 4, 0, 0, 0, 5, 338), L""), std::wstring{ L"4" });
}

TEST(FileName_WeekOfYearAtYearEnd)
{
    // en_US_POSIX: Thursday 31 December 2026 is in the week of 1 January 2027, so week 1.
    CHECK_EQ(ExpandTokens(L"%W", Date(2026, 12, 31, 0, 0, 0, 4, 365), L""), std::wstring{ L"01" });
    // Saturday 26 December 2026 closes week 52.
    CHECK_EQ(ExpandTokens(L"%W", Date(2026, 12, 26, 0, 0, 0, 6, 360), L""), std::wstring{ L"52" });
    // Thursday 1 January 2026 is week 1; Saturday 3 January too; Sunday 4 starts week 2.
    CHECK_EQ(ExpandTokens(L"%W", Date(2026, 1, 1, 0, 0, 0, 4, 1), L""), std::wstring{ L"01" });
    CHECK_EQ(ExpandTokens(L"%W", Date(2026, 1, 3, 0, 0, 0, 6, 3), L""), std::wstring{ L"01" });
}

TEST(FileName_Sanitize)
{
    CHECK_EQ(Sanitize(L"a/b\\c::d*?e\"<>|f"), std::wstring{ L"a-b-c-d-e-f" });
    CHECK_EQ(Sanitize(L"  name  "), std::wstring{ L"name" });
    CHECK_EQ(Generate(L"%n %H:%M", Date(2026, 1, 4, 9, 5, 0, 0, 4), L"Visual Studio: Code"),
             std::wstring{ L"Visual Studio- Code 09-05" });
    CHECK_EQ(Generate(L"  %n", Date(2026, 1, 4, 9, 5, 0, 0, 4), L""), std::wstring{ L"" });
}

TEST(FileName_CollisionSuffix)
{
    CHECK_EQ(WithCollisionSuffix(L"SCR-20261108-kvqe", 1), std::wstring{ L"SCR-20261108-kvqe" });
    CHECK_EQ(WithCollisionSuffix(L"SCR-20261108-kvqe", 2), std::wstring{ L"SCR-20261108-kvqe-2" });
    CHECK_EQ(WithCollisionSuffix(L"SCR-20261108-kvqe", 3), std::wstring{ L"SCR-20261108-kvqe-3" });
}

TEST(FileName_JpegAllowed)
{
    CHECK(JpegAllowed(65533, 0, 0, 0, 0, 0, 0));
    CHECK(!JpegAllowed(65534, 0, 0, 0, 0, 0, 0));
    CHECK(!JpegAllowed(1000, 1, 0, 100, 100, 100, 100));     // rounded backdrop corners
    CHECK(!JpegAllowed(1000, 0, 10, 0, 0, 0, 0));            // rounded screenshot, no padding
    CHECK(JpegAllowed(1000, 0, 10, 50, 50, 50, 50));
    CHECK(!JpegAllowed(1000, 0, 10, 0, 0, 50, 50));          // no vertical padding
    CHECK(JpegAllowed(1000, 0, 10, 0, 50, 50, 0));           // (BL + BR) × (BT + BB) != 0
}

TEST(FileName_PreferJpeg)
{
    constexpr std::size_t MiB = 1024 * 1024;
    CHECK(!PreferJpeg(MiB / 2, 1));                    // not over 0.5 MiB
    CHECK(PreferJpeg(MiB / 2 + 1, MiB / 8));           // 4 × 0.125 MiB < PNG
    CHECK(!PreferJpeg(MiB / 2 + 1, MiB / 8 + 1));
    CHECK(PreferJpeg(3 * MiB, 14 * MiB / 10));         // over 2 MiB and 2 × JPEG < PNG
    CHECK(!PreferJpeg(3 * MiB, 15 * MiB / 10));
    CHECK(!PreferJpeg(2 * MiB, 9 * MiB / 10));         // exactly 2 MiB: only the 4× rule
}
