#include "pch.h"
#include "Core/FileName.h"

#include <array>
#include <cmath>
#include <ctime>

namespace Eikon::Core::FileName
{
    namespace
    {
        constexpr std::array<wchar_t const*, 12> kMonths = { L"January", L"February", L"March", L"April", L"May", L"June",
                                                             L"July", L"August", L"September", L"October", L"November", L"December" };
        constexpr std::array<wchar_t const*, 7> kWeekdays = { L"Sunday", L"Monday", L"Tuesday", L"Wednesday",
                                                              L"Thursday", L"Friday", L"Saturday" };

        std::wstring Padded(int value, int width)
        {
            std::wstring text = std::to_wstring(value);
            while (static_cast<int>(text.size()) < width)
            {
                text.insert(text.begin(), L'0');
            }
            return text;
        }

        bool IsLeap(int year) noexcept { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

        // "ww" for en_US_POSIX: weeks start on Sunday and week 1 is the one containing 1 January, so the
        // last days of December can already be week 1.
        int WeekOfYear(DateParts const& d) noexcept
        {
            int const daysInYear = IsLeap(d.year) ? 366 : 365;
            if (d.dayOfYear + (6 - d.weekday) > daysInYear)
            {
                return 1;
            }
            int const jan1Weekday = ((d.weekday - (d.dayOfYear - 1)) % 7 + 7) % 7;
            return (d.dayOfYear - 1 + jan1Weekday) / 7 + 1;
        }

        void ReplaceAll(std::wstring& text, std::wstring_view token, std::wstring const& value)
        {
            std::size_t pos = 0;
            while ((pos = text.find(token, pos)) != std::wstring::npos)
            {
                text.replace(pos, token.size(), value);
                pos += value.size();
            }
        }

        bool IsSpace(wchar_t c) noexcept { return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == 0x00A0; }
    }

    DateParts LocalParts(std::chrono::system_clock::time_point time)
    {
        auto const seconds = std::chrono::floor<std::chrono::seconds>(time);
        std::time_t const t = std::chrono::system_clock::to_time_t(seconds);
        std::tm local{};
        localtime_s(&local, &t);
        DateParts d;
        d.year = local.tm_year + 1900;
        d.month = local.tm_mon + 1;
        d.day = local.tm_mday;
        d.hour = local.tm_hour;
        d.minute = local.tm_min;
        d.second = local.tm_sec;
        d.weekday = local.tm_wday;
        d.dayOfYear = local.tm_yday + 1;
        d.nanosecond = static_cast<int>(std::chrono::duration_cast<std::chrono::nanoseconds>(time - seconds).count());
        return d;
    }

    std::wstring QuarterSecondCode(int secondOfDay, int nanosecond)
    {
        long long n = static_cast<long long>(secondOfDay) * 4 + nanosecond / 250'000'000 + 17576;
        std::wstring code;
        do
        {
            code.insert(code.begin(), static_cast<wchar_t>(L'a' + n % 26));
            n /= 26;
        } while (n > 0);
        return code;
    }

    std::wstring HalfSecondCode(int secondOfDay)
    {
        long long n = static_cast<long long>(std::lround(secondOfDay / 2.0)) + 1296;
        std::wstring code;
        do
        {
            int const digit = static_cast<int>(n % 36);
            code.insert(code.begin(), static_cast<wchar_t>(digit < 10 ? L'0' + digit : L'a' + digit - 10));
            n /= 36;
        } while (n > 0);
        return code;
    }

    std::wstring ExpandTokens(std::wstring_view pattern, DateParts const& d, std::wstring_view appName)
    {
        std::wstring text{ pattern };
        int const hour12 = d.hour % 12 == 0 ? 12 : d.hour % 12;
        std::wstring const month = kMonths[static_cast<std::size_t>((d.month - 1 + 12) % 12)];
        std::wstring const weekday = kWeekdays[static_cast<std::size_t>((d.weekday % 7 + 7) % 7)];
        int const secondOfDay = d.hour * 3600 + d.minute * 60 + d.second;
        ReplaceAll(text, L"%Y", Padded(d.year, 4));
        ReplaceAll(text, L"%y", Padded(d.year % 100, 2));
        ReplaceAll(text, L"%m", Padded(d.month, 2));
        ReplaceAll(text, L"%b", month.substr(0, 3));
        ReplaceAll(text, L"%B", month);
        ReplaceAll(text, L"%d", Padded(d.day, 2));
        ReplaceAll(text, L"%j", Padded(d.dayOfYear, 3));
        ReplaceAll(text, L"%A", weekday);
        ReplaceAll(text, L"%a", weekday.substr(0, 3));
        ReplaceAll(text, L"%W", Padded(WeekOfYear(d), 2));
        ReplaceAll(text, L"%Q", std::to_wstring((d.month - 1) / 3 + 1));
        ReplaceAll(text, L"%H", Padded(d.hour, 2));
        ReplaceAll(text, L"%I", Padded(hour12, 2));
        ReplaceAll(text, L"%p", d.hour < 12 ? L"AM" : L"PM");
        ReplaceAll(text, L"%P", d.hour < 12 ? L"am" : L"pm");
        ReplaceAll(text, L"%M", Padded(d.minute, 2));
        ReplaceAll(text, L"%S", Padded(d.second, 2));
        ReplaceAll(text, L"%n", std::wstring{ appName });
        ReplaceAll(text, L"%t", QuarterSecondCode(secondOfDay, d.nanosecond));
        ReplaceAll(text, L"%T", HalfSecondCode(secondOfDay));
        return text;
    }

    std::wstring Sanitize(std::wstring_view name)
    {
        std::wstring out;
        bool inRun = false;
        for (wchar_t c : name)
        {
            bool const forbidden = c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' ||
                                   c == L'<' || c == L'>' || c == L'|';
            if (forbidden)
            {
                if (!inRun)
                {
                    out.push_back(L'-');
                }
                inRun = true;
                continue;
            }
            inRun = false;
            out.push_back(c);
        }
        std::size_t first = 0;
        while (first < out.size() && IsSpace(out[first]))
        {
            ++first;
        }
        std::size_t last = out.size();
        while (last > first && IsSpace(out[last - 1]))
        {
            --last;
        }
        return out.substr(first, last - first);
    }

    std::wstring Generate(std::wstring_view pattern, DateParts const& date, std::wstring_view appName)
    {
        return Sanitize(ExpandTokens(pattern.empty() ? kDefaultTemplate : pattern, date, appName));
    }

    std::wstring WithCollisionSuffix(std::wstring_view base, int attempt)
    {
        std::wstring name{ base };
        if (attempt >= 2)
        {
            name += L"-" + std::to_wstring(attempt);
        }
        return name;
    }

    bool JpegAllowed(double heightPx, double outerRadiusPx, double innerRadiusPx,
                     double padTop, double padBottom, double padLeft, double padRight) noexcept
    {
        // Tests (padBL + padBR) * (padBT + padBB), not the product of all four sides.
        return heightPx < 65534 && outerRadiusPx <= 0 && (innerRadiusPx <= 0 || (padLeft + padRight) * (padTop + padBottom) != 0);
    }

    bool PreferJpeg(std::size_t pngBytes, std::size_t jpegBytes) noexcept
    {
        // Sizes in MiB divides by 1024 twice.
        double const png = pngBytes / 1024.0 / 1024.0;
        double const jpeg = jpegBytes / 1024.0 / 1024.0;
        return (0.5 < png && jpeg * 4 < png) || (2.0 < png && jpeg + jpeg < png);
    }
}
