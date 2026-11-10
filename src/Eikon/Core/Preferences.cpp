#include "pch.h"
#include "Core/Preferences.h"

#include <cmath>
#include <cwctype>

namespace Eikon::Core::Preferences
{
    namespace
    {
        bool IsSpace(wchar_t c) noexcept
        {
            return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\v' || c == L'\f' || c == 0x00A0 || c == 0x2028 ||
                   c == 0x2029 || c == 0x3000 || (c >= 0x2000 && c <= 0x200A) || c == 0x0085;
        }

        bool IsHorizontalSpace(wchar_t c) noexcept   // CharacterSet.whitespaces (no newlines)
        {
            return c == L' ' || c == L'\t' || c == 0x00A0 || c == 0x3000 || (c >= 0x2000 && c <= 0x200A);
        }

        bool HasScheme(std::wstring_view text) noexcept   // ^[A-Za-z][A-Za-z0-9+.\-]*://
        {
            if (text.empty() || !((text[0] >= L'a' && text[0] <= L'z') || (text[0] >= L'A' && text[0] <= L'Z')))
            {
                return false;
            }
            for (std::size_t i = 1; i < text.size(); ++i)
            {
                wchar_t const c = text[i];
                if (c == L':')
                {
                    return text.substr(i).starts_with(L"://");
                }
                bool const ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'+' || c == L'.' ||
                                c == L'-';
                if (!ok)
                {
                    return false;
                }
            }
            return false;
        }

        std::wstring NormalizeUrl(std::wstring_view input, bool trailingSlash)
        {
            std::wstring text = TrimField(input);
            if (text.empty())
            {
                return text;
            }
            if (!HasScheme(text))
            {
                text = L"https://" + text;
            }
            bool const slash = text.back() == L'/';
            if (!trailingSlash && slash)
            {
                text.pop_back();
            }
            else if (trailingSlash && !slash)
            {
                text.push_back(L'/');
            }
            return text;
        }
    }

    std::optional<std::wstring> NormalizeSolidColor(std::wstring_view input)
    {
        std::wstring text;
        for (wchar_t c : input)
        {
            if (c != L'#')
            {
                text.push_back(static_cast<wchar_t>(std::towupper(c)));
            }
        }
        std::size_t begin = 0;
        std::size_t end = text.size();
        while (begin < end && IsHorizontalSpace(text[begin]))
        {
            ++begin;
        }
        while (end > begin && IsHorizontalSpace(text[end - 1]))
        {
            --end;
        }
        std::wstring_view const hex = std::wstring_view{ text }.substr(begin, end - begin);
        if (hex.size() != 6)
        {
            return std::nullopt;
        }
        for (wchar_t c : hex)
        {
            if (!((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'F')))
            {
                return std::nullopt;
            }
        }
        return L"#" + std::wstring{ hex };
    }

    std::int64_t ParseScrollingMax(std::wstring_view input)
    {
        std::int64_t value = 0;
        bool digits = false;
        for (wchar_t c : input)
        {
            if (c >= L'0' && c <= L'9')
            {
                digits = true;
                if (value < 1'000'000'000)
                {
                    value = value * 10 + (c - L'0');
                }
            }
            else if (digits && c != L',' && c != L' ' && c != 0x00A0 && c != 0x202F)
            {
                break;   // integerValue stops at the first character that is not part of the number
            }
        }
        if (value < 11)
        {
            value = 10;
        }
        if (value > 399999)
        {
            value = 400000;
        }
        return value;
    }

    AfterGrab SetShow(AfterGrab state, bool show) noexcept
    {
        state.show = show;
        if (!state.show && !state.copy && !state.save)
        {
            state.copy = true;
        }
        return state;
    }

    AfterGrab SetCopy(AfterGrab state, bool copy) noexcept
    {
        state.copy = copy;
        if (!state.copy && !state.save)
        {
            state.show = true;
        }
        return state;
    }

    AfterGrab SetSave(AfterGrab state, bool save) noexcept
    {
        state.save = save;
        if (!state.copy && !state.save)
        {
            state.show = true;
        }
        return state;
    }

    std::array<Expiration, 7> const& Expirations()
    {
        static std::array<Expiration, 7> const table{ {
            { 3600, L"Upload_Expiration1Hour", L"1 hour" },
            { 21600, L"Upload_Expiration6Hours", L"6 hrs" },
            { 43200, L"Upload_Expiration12Hours", L"12 hrs" },
            { 86400, L"Upload_Expiration24Hours", L"24 hrs" },
            { 172800, L"Upload_Expiration2Days", L"2 days" },
            { 345600, L"Upload_Expiration4Days", L"4 days" },
            { 604800, L"Upload_Expiration7Days", L"7 days" },
        } };
        return table;
    }

    int ExpirationIndex(std::int64_t seconds) noexcept
    {
        auto const& table = Expirations();
        int best = 0;
        std::int64_t bestDistance = INT64_MAX;
        for (int i = 0; i < static_cast<int>(table.size()); ++i)
        {
            std::int64_t const distance = seconds > table[i].seconds ? seconds - table[i].seconds : table[i].seconds - seconds;
            if (distance < bestDistance)
            {
                best = i;
                bestDistance = distance;
            }
        }
        return best;
    }

    int SnapExpiration(double sliderValue) noexcept
    {
        if (!std::isfinite(sliderValue))
        {
            return 6;
        }
        int const index = static_cast<int>(std::lround(sliderValue));
        return index < 0 ? 0 : (index > 6 ? 6 : index);
    }

    std::wstring NormalizeEndpoint(std::wstring_view input)
    {
        return NormalizeUrl(input, false);
    }

    std::wstring NormalizePublicUrl(std::wstring_view input)
    {
        return NormalizeUrl(input, true);
    }

    std::wstring TrimField(std::wstring_view input)
    {
        std::size_t begin = 0;
        std::size_t end = input.size();
        while (begin < end && IsSpace(input[begin]))
        {
            ++begin;
        }
        while (end > begin && IsSpace(input[end - 1]))
        {
            --end;
        }
        return std::wstring{ input.substr(begin, end - begin) };
    }

    bool HasUppercase(std::wstring_view bucket) noexcept
    {
        for (wchar_t c : bucket)
        {
            if (std::iswupper(c))
            {
                return true;
            }
        }
        return false;
    }

    std::vector<OcrLanguage> const& OcrLanguages()
    {
        // The OCR language list in menu order with display names; zh-Hant and yue-Hant are not offered.
        static std::vector<OcrLanguage> const languages{
            { L"en-US", L"English+" },     { L"fr-FR", L"French" },   { L"it-IT", L"Italian" },
            { L"de-DE", L"German" },       { L"es-ES", L"Spanish" },  { L"pt-BR", L"Portuguese (Brazil)" },
            { L"zh-Hans", L"Chinese" },    { L"yue-Hans", L"Chinese (Cantonese)" },
            { L"ko-KR", L"Korean" },       { L"ja-JP", L"Japanese" }, { L"ru-RU", L"Cyrillic" },
            { L"uk-UA", L"Ukrainian" },    { L"th-TH", L"Thai" },     { L"vi-VT", L"Vietnamese" },
            { L"ar-SA", L"Arabic" },       { L"ars-SA", L"Arabic (Najdi)" },
            { L"tr-TR", L"Turkish" },      { L"id-ID", L"Indonesian" }, { L"cs-CZ", L"Czech" },
            { L"da-DK", L"Danish" },       { L"nl-NL", L"Dutch" },    { L"no-NO", L"Norwegian" },
            { L"nn-NO", L"Norwegian (Nynorsk)" }, { L"nb-NO", L"Norwegian (Bokm\u00E5l)" },
            { L"ms-MY", L"Malay" },        { L"pl-PL", L"Polish" },   { L"ro-RO", L"Romanian" },
            { L"sv-SE", L"Swedish" },      { L"fi-FI", L"Finnish" },  { L"hi-IN", L"Hindi" },
            { L"mr-IN", L"Marathi" },
        };
        return languages;
    }

    std::wstring OcrLanguageName(std::wstring_view code)
    {
        for (auto const& language : OcrLanguages())
        {
            if (language.code == code)
            {
                return std::wstring{ language.name };
            }
        }
        return std::wstring{ code };
    }
}
