#include "pch.h"
#include "Core/OcrText.h"

#include <algorithm>
#include <cwctype>

namespace Eikon::Core::OcrText
{
    namespace
    {
        void ReplaceAll(std::wstring& text, std::wstring_view from, std::wstring_view to)
        {
            std::size_t at = 0;
            while ((at = text.find(from, at)) != std::wstring::npos)
            {
                text.replace(at, from.size(), to);
                at += to.size();
            }
        }

        std::wstring Lower(std::wstring_view text)
        {
            std::wstring result{ text };
            for (wchar_t& c : result)
            {
                if (c >= L'A' && c <= L'Z')
                {
                    c = static_cast<wchar_t>(c - L'A' + L'a');
                }
            }
            return result;
        }

        std::vector<std::wstring> Subtags(std::wstring_view tag)
        {
            std::vector<std::wstring> parts;
            std::size_t start = 0;
            while (start <= tag.size())
            {
                std::size_t const dash = tag.find_first_of(L"-_", start);
                parts.push_back(Lower(tag.substr(start, dash == std::wstring_view::npos ? std::wstring_view::npos : dash - start)));
                if (dash == std::wstring_view::npos)
                {
                    break;
                }
                start = dash + 1;
            }
            return parts;
        }

        // "hans"/"hant" for a Chinese tag, from its script or its region.
        std::wstring ChineseScript(std::vector<std::wstring> const& parts)
        {
            for (std::size_t i = 1; i < parts.size(); ++i)
            {
                if (parts[i] == L"hans" || parts[i] == L"hant")
                {
                    return parts[i];
                }
            }
            for (std::size_t i = 1; i < parts.size(); ++i)
            {
                if (parts[i] == L"tw" || parts[i] == L"hk" || parts[i] == L"mo")
                {
                    return L"hant";
                }
                if (parts[i] == L"cn" || parts[i] == L"sg")
                {
                    return L"hans";
                }
            }
            return L"hans";
        }

        char32_t FirstCodePoint(std::wstring_view text) noexcept
        {
            if (text.empty())
            {
                return 0;
            }
            char32_t c = text[0];
            if (c >= 0xD800 && c <= 0xDBFF && text.size() > 1)
            {
                c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<char32_t>(text[1]) - 0xDC00);
            }
            return c;
        }

        char32_t LastCodePoint(std::wstring_view text) noexcept
        {
            if (text.empty())
            {
                return 0;
            }
            char32_t c = text.back();
            if (c >= 0xDC00 && c <= 0xDFFF && text.size() > 1)
            {
                char32_t const high = text[text.size() - 2];
                c = 0x10000 + ((high - 0xD800) << 10) + (c - 0xDC00);
            }
            return c;
        }

        bool IsCjk(char32_t c) noexcept
        {
            return (c >= 0x3000 && c <= 0x303F)      // CJK symbols and punctuation
                   || (c >= 0x3040 && c <= 0x30FF)   // Hiragana, Katakana
                   || (c >= 0x31F0 && c <= 0x31FF)   // Katakana phonetic extensions
                   || (c >= 0x3400 && c <= 0x4DBF)   // CJK extension A
                   || (c >= 0x4E00 && c <= 0x9FFF)   // CJK unified ideographs
                   || (c >= 0xF900 && c <= 0xFAFF)   // CJK compatibility ideographs
                   || (c >= 0xFF00 && c <= 0xFFEF)   // half-width and full-width forms
                   || (c >= 0x20000 && c <= 0x3FFFF); // extensions B..
        }
    }

    std::wstring CleanUp(std::wstring text)
    {
        static constexpr std::pair<std::wstring_view, std::wstring_view> kReplacements[] = {
            { L"\uFF0C", L", " },  // ，
            { L"\uFF08", L" (" },  // （
            { L"\uFF09", L") " },  // ）
            { L"\uFF0D", L"-" },   // －
            { L"\uFF0E", L". " },  // ．
            { L"\uFF01", L"! " },  // ！
            { L"\uFF04", L"$" },   // ＄
            { L"\uFF05", L"%" },   // ％
            { L"\uFF02", L"\"" },  // ＂
        };
        for (auto const& [from, to] : kReplacements)
        {
            ReplaceAll(text, from, to);
        }
        // replacingOccurrences("  ", " ") scans once, left to right, without rescanning its output.
        std::wstring result;
        result.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == L' ' && i + 1 < text.size() && text[i + 1] == L' ')
            {
                result.push_back(L' ');
                ++i;
                continue;
            }
            result.push_back(text[i]);
        }
        return result;
    }

    std::wstring RemoveBreaks(std::wstring_view text)
    {
        std::wstring result;
        result.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == L'\n')
            {
                while (i + 1 < text.size() && text[i + 1] == L'\n')
                {
                    ++i;
                }
                result.push_back(L' ');
                continue;
            }
            result.push_back(text[i]);
        }
        return result;
    }

    std::wstring NotificationBody(std::wstring_view text)
    {
        std::wstring flat{ text };
        std::replace(flat.begin(), flat.end(), L'\n', L' ');
        // Count code points; cut at a code point boundary.
        std::size_t count = 0;
        std::size_t cut = flat.size();
        for (std::size_t i = 0; i < flat.size(); ++i)
        {
            if (flat[i] >= 0xDC00 && flat[i] <= 0xDFFF && i > 0 && flat[i - 1] >= 0xD800 && flat[i - 1] <= 0xDBFF)
            {
                continue; // second half of a pair
            }
            if (count == 107)
            {
                cut = i;
            }
            ++count;
        }
        if (count <= 120)
        {
            return flat;
        }
        return flat.substr(0, cut) + L"...";
    }

    bool IsTooLargeForEditor(std::int64_t widthPx, std::int64_t heightPx) noexcept
    {
        return heightPx >= 3001 || widthPx >= 6001;
    }

    std::vector<std::wstring> EngineLanguages(std::wstring_view primaryOcrLang)
    {
        std::wstring const lang{ primaryOcrLang };
        if (lang == L"en-US")
        {
            return { L"en-US" };
        }
        if (lang.find(L"zh") != std::wstring::npos || lang.find(L"yue") != std::wstring::npos)
        {
            return { L"zh-Hans", L"zh-Hant", L"en-US" };
        }
        std::wstring const primary = Subtags(lang).front();
        std::wstring mapped = lang;
        if (primary == L"uk")
        {
            mapped = L"ru-RU";      // Cyrillic
        }
        else if (primary == L"ars")
        {
            mapped = L"ar-SA";      // Najdi Arabic is Arabic script
        }
        else if (primary == L"nn" || primary == L"no")
        {
            mapped = L"nb-NO";      // Norwegian Bokmål is Windows' Norwegian engine
        }
        else if (primary == L"id" || primary == L"ms" || primary == L"vi" || primary == L"th" || primary == L"hi" || primary == L"mr")
        {
            mapped.clear();         // no Windows engine: the en-US engine reads the Latin-script ones
        }
        std::vector<std::wstring> result;
        if (!mapped.empty())
        {
            result.push_back(mapped);
        }
        result.push_back(L"en-US");
        return result;
    }

    std::optional<std::size_t> MatchLanguage(std::wstring_view wanted, std::vector<std::wstring> const& available)
    {
        std::wstring const wantedLower = Lower(wanted);
        for (std::size_t i = 0; i < available.size(); ++i)
        {
            if (Lower(available[i]) == wantedLower)
            {
                return i;
            }
        }
        auto const wantedParts = Subtags(wanted);
        bool const chinese = wantedParts.front() == L"zh";
        std::wstring const wantedScript = chinese ? ChineseScript(wantedParts) : std::wstring{};
        for (std::size_t i = 0; i < available.size(); ++i)
        {
            auto const parts = Subtags(available[i]);
            if (parts.front() != wantedParts.front())
            {
                continue;
            }
            if (!chinese || ChineseScript(parts) == wantedScript)
            {
                return i;
            }
        }
        return std::nullopt;
    }

    std::wstring JoinWords(std::vector<std::wstring> const& words)
    {
        std::wstring result;
        for (auto const& word : words)
        {
            if (word.empty())
            {
                continue;
            }
            if (!result.empty() && !(IsCjk(LastCodePoint(result)) && IsCjk(FirstCodePoint(word))))
            {
                result.push_back(L' ');
            }
            result += word;
        }
        return result;
    }
}
