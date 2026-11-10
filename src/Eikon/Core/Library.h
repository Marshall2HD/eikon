#pragma once

// The screenshot Library's data: records of imagelib.shdb, the line
// format, the file-name timestamps, the status line, the OCR search and the filter facets.
// Platform-neutral; the scanner, analyser and UI are in Platform/LibraryIndex and LibraryWindow.

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Eikon::Core::Library
{
    // DataLayer.records is [[String: String]]: every field is a string ("bytes" "123456",
    // "timestamp" "1704196800", "colors" "#112233,#445566").
    using Record = std::map<std::wstring, std::wstring, std::less<>>;

    // Field names (save time, analysis).
    inline constexpr std::wstring_view kApp = L"app";
    inline constexpr std::wstring_view kWindow = L"window";
    inline constexpr std::wstring_view kDpr = L"dpr";
    inline constexpr std::wstring_view kMode = L"mode";
    inline constexpr std::wstring_view kLabels = L"labels";
    inline constexpr std::wstring_view kObjects = L"objects";
    inline constexpr std::wstring_view kTimestamp = L"timestamp";
    inline constexpr std::wstring_view kBytes = L"bytes";
    inline constexpr std::wstring_view kFilename = L"filename";
    inline constexpr std::wstring_view kWidth = L"width";
    inline constexpr std::wstring_view kHeight = L"height";
    inline constexpr std::wstring_view kOcr = L"ocr";
    inline constexpr std::wstring_view kDomains = L"domains";
    inline constexpr std::wstring_view kColors = L"colors";
    inline constexpr std::wstring_view kBaseColor = L"basecolor";

    inline constexpr wchar_t kDatabaseName[] = L"imagelib.shdb";

    std::wstring Field(Record const& record, std::wstring_view key);
    // Strict integer parse: optional sign and ASCII digits only.
    std::optional<std::int64_t> IntField(Record const& record, std::wstring_view key);

    // ---- Save-time fields ----
    // `objects`: one letter per object, the first letter of the class name
    // after "Obj", except ObjRuler "I", ObjConceal "B" and ObjImage "P" (so they don't clash with
    // Rectangle, Counter); sorted, duplicates kept: {ObjText, ObjArrow, ObjArrow} -> "AAT".
    std::wstring ObjectsField(std::span<std::wstring_view const> classNames);
    // `labels`: the texts of the ObjText objects, joined with "\n\n".
    std::wstring LabelsField(std::span<std::wstring_view const> texts);
    // `dpr`: shortest round-trip digits, "2.0", "1.25".
    std::wstring DprField(double dpr);

    // ---- Line format ----
    // A line is JSON: an object of strings. Returns nullopt for anything else (other JSON, garbage,
    // or an encrypted line, which Eikon does not read).
    std::optional<Record> ParseLine(std::string_view line);
    // Compact JSON: no spaces, "/" escaped as "\/".
    std::string RecordJson(Record const& record);
    // One database line (the record's JSON, without the newline).
    std::string EncodeLine(Record const& record);
    // Splits a database file into records (CR/LF tolerant, bad lines skipped).
    std::vector<Record> ParseDatabase(std::string_view content);
    std::string EncodeDatabase(std::vector<Record> const& records);

    // ---- File names ----
    struct LocalTime
    {
        int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
        bool operator==(LocalTime const&) const = default;
    };
    // "-YYYYMMDD-tttt" (%t = time of day in quarter seconds, base 26),
    // "-YYYYMMDD-" alone (midnight), "Screenshot YYYY-MM-DD at H.MM.SS" (with an optional
    // AM/PM), Windows "Screenshot YYYY-MM-DD HHMMSS", or a bare "YYYY-MM-DD".
    std::optional<LocalTime> TimeFromFileName(std::wstring_view fileName);
    // Decodes %t ("kvqe" -> 43200 s); nullopt unless 4 lower-case letters.
    std::optional<int> SecondOfDayFromCode(std::wstring_view code);
    // .png, .jpg, .jpeg (case-insensitive).
    bool IsImageFile(std::wstring_view fileName);
    // Naming or a system screenshot name (there is no kMDItemIsScreenCapture on Windows).
    bool IsScreenshotName(std::wstring_view fileName);

    // ---- Status line ----
    std::wstring CountText(std::size_t count);                 // "1 image", "2 images"
    std::wstring FoundText(std::size_t count);                 // "2 images found"
    std::wstring SelectedText(std::size_t count);              // "3 images selected"
    // "MMM d, yyyy 'at' h:mma" in en_US_POSIX with "am"/"pm".
    std::wstring DateText(LocalTime const& time);
    // "523kb", or "1.5mb" above 1 048 576 bytes; "" for 0.
    std::wstring SizeText(std::int64_t bytes);
    // DateText + "  ∙  " + SizeText (just the date when the size is empty).
    std::wstring SingleText(LocalTime const& time, std::int64_t bytes);

    // ---- Search ----
    std::wstring TrimQuery(std::wstring_view query);
    // localizedCaseInsensitiveContains over `ocr` only.
    bool Matches(Record const& record, std::wstring_view trimmedQuery);
    // The first URL-regex match of each OCR line, lower-cased without "www.", joined
    // with "\n".
    std::wstring DomainsFromText(std::wstring_view ocr);

    // ---- Colours (colors, basecolor) ----
    // ColorMagic over top-down BGRA pixels that are already scaled
    // by 0.2: colours counted more than trunc(total * 0.0001) times, the 500 most frequent, merged
    // into the closest kept colour when the L1 distance of the 0-1 components is < 0.2, the first
    // `maxColors` by frequency, as "#RRGGBB" joined with ","; "#000000" when there are none.
    // Fully transparent pixels are skipped.
    std::wstring DominantColors(std::span<std::uint8_t const> bgra, std::uint32_t width, std::uint32_t height, std::size_t maxColors = 8);
    // The average colour of the opaque pixels as "#RRGGBB" ("#000000" for none).
    std::wstring AverageColor(std::span<std::uint8_t const> bgra, std::uint32_t width, std::uint32_t height);

    // ---- Facets (stored, never shown) ----
    struct Facets
    {
        std::vector<std::wstring> months;    // "MMMM yyyy"
        std::vector<std::wstring> domains;
        std::vector<std::wstring> drawings;  // objects
        std::vector<std::wstring> types;     // mode
        std::vector<std::wstring> apps;      // app
    };
    // `times` are the records' local times (same order); empty fields are skipped, values unique
    // in first-seen order.
    Facets BuildFacets(std::vector<Record> const& records, std::vector<LocalTime> const& times);
}
