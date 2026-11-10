#pragma once

// File names and the format rules of the Exporter.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Eikon::Core::FileName
{
    // The default template (used when `fileNameTemplate` is empty).
    inline constexpr std::wstring_view kDefaultTemplate = L"SCR-%Y%m%d-%t";
    // Saved files use ".png" and ".jpeg".
    inline constexpr std::wstring_view kPngExtension = L".png";
    inline constexpr std::wstring_view kJpegExtension = L".jpeg";

    // A local date and time, as DateFormatter sees it with the en_US_POSIX locale.
    struct DateParts
    {
        int year = 2000;
        int month = 1;        // 1…12
        int day = 1;          // 1…31
        int hour = 0;         // 0…23
        int minute = 0;
        int second = 0;
        int nanosecond = 0;
        int weekday = 0;      // 0 = Sunday … 6 = Saturday
        int dayOfYear = 1;    // 1…366
    };

    // The local time of `time`.
    DateParts LocalParts(std::chrono::system_clock::time_point time);

    // %t: four lowercase letters, the time of day in quarter seconds plus 26³, in base 26 (a…z).
    // "baaa" at 00:00:00.00 … "urgh" at 23:59:59.75.
    std::wstring QuarterSecondCode(int secondOfDay, int nanosecond);
    // %T: base 36 (0-9a-z) of round(secondOfDay / 2) + 36².
    std::wstring HalfSecondCode(int secondOfDay);

    // Replaces the tokens in order (%Y %y %m %b %B %d %j %A %a %W %Q %H %I %p %P %M %S %n %t
    // %T) without sanitising.
    std::wstring ExpandTokens(std::wstring_view pattern, DateParts const& date, std::wstring_view appName);
    // Runs of \ / : * ? " < > | become "-", then surrounding whitespace is trimmed.
    std::wstring Sanitize(std::wstring_view name);
    // The base name (no extension). An empty template falls back to the default.
    std::wstring Generate(std::wstring_view pattern, DateParts const& date, std::wstring_view appName);

    // "name" for attempt 1, "name-2", "name-3", … after.
    std::wstring WithCollisionSuffix(std::wstring_view base, int attempt);

    // JPEG is allowed only without transparent corners: height < 65534, no
    // outer radius, and with an inner radius only when there is padding on both axes.
    bool JpegAllowed(double heightPx, double outerRadiusPx, double innerRadiusPx,
                     double padTop, double padBottom, double padLeft, double padRight) noexcept;
    // "Auto" format: JPEG when the PNG is over 0.5 MiB and 4× the JPEG, or over 2 MiB and 2×.
    bool PreferJpeg(std::size_t pngBytes, std::size_t jpegBytes) noexcept;
}
