#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

// Localized text for Core. The strings live in Strings/<language>/Resources.resw, which Core can't
// read (no WinRT here): the app installs a lookup (MRT Core's ResourceLoader, see Strings.h) at
// startup. Without one, as in the unit tests, Text returns the English fallback.
namespace Eikon::Localization
{
    // Returns the string for a resource key, or an empty string when there is none.
    using Lookup = std::wstring (*)(std::wstring_view key);
    void SetLookup(Lookup lookup) noexcept;

    // The localized string for `key`, or `fallback` (the en-US text) when no lookup is installed or
    // the key is missing.
    std::wstring Text(std::wstring_view key, std::wstring_view fallback);

    // Replaces the numbered placeholders {0}..{9} in `pattern` with `args`, in any order and any
    // number of times, so that translations can reorder them. "{{" and "}}" are literal braces;
    // placeholders without an argument are kept as is.
    std::wstring Format(std::wstring_view pattern, std::initializer_list<std::wstring_view> args);
}
