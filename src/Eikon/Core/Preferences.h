#pragma once

// Rules of the Settings panes that are not UI: input normalisation, the
// "after screenshot" invariant, the presigned-URL expiration table and the OCR language names.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Eikon::Core::Preferences
{
    // updateSolidColor: strip "#", upper-case, trim whitespace; six hex digits give "#RRGGBB",
    // anything else is rejected (the stored colour stays).
    std::optional<std::wstring> NormalizeSolidColor(std::wstring_view input);

    // updateScrollingMax: the field's integer (digits only; none = 0), clamped to [10, 400000].
    std::int64_t ParseScrollingMax(std::wstring_view input);

    // "After screenshot" Show / Copy / Save: at least one stays checked.
    struct AfterGrab
    {
        bool show = true;
        bool copy = false;
        bool save = false;
        bool operator==(AfterGrab const&) const = default;
    };
    // Unchecking Show with Copy and Save off turns Copy on.
    AfterGrab SetShow(AfterGrab state, bool show) noexcept;
    // Copy or Save off with the other one off turns Show on.
    AfterGrab SetCopy(AfterGrab state, bool copy) noexcept;
    AfterGrab SetSave(AfterGrab state, bool save) noexcept;

    // The presigned URL lifetimes.
    struct Expiration
    {
        std::int64_t seconds;
        wchar_t const* labelKey;   // Resources.resw key
        wchar_t const* label;      // en-US text
    };
    std::array<Expiration, 7> const& Expirations();
    // The slider index for a stored value: the nearest entry (604800 = index 6).
    int ExpirationIndex(std::int64_t seconds) noexcept;
    // A slider position snapped to an index 0..6.
    int SnapExpiration(double sliderValue) noexcept;

    // editS3Endpoint / editS3PublicURL: trim; empty stays empty; "https://" is
    // prepended when there is no scheme; an endpoint loses one trailing "/", a public URL gets one.
    std::wstring NormalizeEndpoint(std::wstring_view input);
    std::wstring NormalizePublicUrl(std::wstring_view input);
    // Trim whitespace and newlines (bucket, region, prefix fields).
    std::wstring TrimField(std::wstring_view input);
    // The "Uppercase Bucket Name" warning.
    bool HasUppercase(std::wstring_view bucket) noexcept;

    // Primary OCR language popup: the recognition languages minus zh-Hant and yue-Hant, with display
    // names (codes missing from the table show the code).
    struct OcrLanguage
    {
        std::wstring_view code;
        std::wstring_view name;
    };
    std::vector<OcrLanguage> const& OcrLanguages();
    std::wstring OcrLanguageName(std::wstring_view code);
}
