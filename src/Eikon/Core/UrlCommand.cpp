#include "pch.h"
#include "Core/UrlCommand.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <string>
#include <system_error>
#include <vector>

namespace Eikon::Core
{
    namespace
    {
        bool IsWhitespace(wchar_t c) noexcept
        {
            // CharacterSet.whitespacesAndNewlines (the common ones).
            return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\v' || c == L'\f' || c == 0x85 ||
                   c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
                   c == 0x202F || c == 0x205F || c == 0x3000;
        }

        std::wstring_view TrimWhitespace(std::wstring_view text) noexcept
        {
            while (!text.empty() && IsWhitespace(text.front()))
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && IsWhitespace(text.back()))
            {
                text.remove_suffix(1);
            }
            return text;
        }

        std::wstring Lowercase(std::wstring_view text)
        {
            std::wstring result{ text };
            for (auto& c : result)
            {
                if (c >= L'A' && c <= L'Z')
                {
                    c = static_cast<wchar_t>(c - L'A' + L'a');
                }
                else if (c > 0x7F)
                {
                    c = static_cast<wchar_t>(std::towlower(c));
                }
            }
            return result;
        }

        int HexValue(wchar_t c) noexcept
        {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            return -1;
        }

        void AppendUtf8(std::string& out, std::uint32_t code)
        {
            if (code < 0x80)
            {
                out.push_back(static_cast<char>(code));
            }
            else if (code < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            else if (code < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
        }

        // Strict UTF-8 -> UTF-16. nullopt on malformed input.
        std::optional<std::wstring> DecodeUtf8(std::string const& bytes)
        {
            std::wstring out;
            std::size_t i = 0;
            while (i < bytes.size())
            {
                auto const b0 = static_cast<std::uint8_t>(bytes[i]);
                std::uint32_t code = 0;
                int extra = 0;
                if (b0 < 0x80)
                {
                    code = b0;
                }
                else if ((b0 & 0xE0) == 0xC0)
                {
                    code = b0 & 0x1F;
                    extra = 1;
                }
                else if ((b0 & 0xF0) == 0xE0)
                {
                    code = b0 & 0x0F;
                    extra = 2;
                }
                else if ((b0 & 0xF8) == 0xF0)
                {
                    code = b0 & 0x07;
                    extra = 3;
                }
                else
                {
                    return std::nullopt;
                }
                if (extra > 0 && i + static_cast<std::size_t>(extra) >= bytes.size())
                {
                    return std::nullopt;
                }
                for (int k = 1; k <= extra; ++k)
                {
                    auto const b = static_cast<std::uint8_t>(bytes[i + k]);
                    if ((b & 0xC0) != 0x80)
                    {
                        return std::nullopt;
                    }
                    code = (code << 6) | (b & 0x3F);
                }
                static constexpr std::uint32_t kMinimum[] = { 0, 0x80, 0x800, 0x10000 };
                if (code < kMinimum[extra] || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
                {
                    return std::nullopt;
                }
                if (code >= 0x10000)
                {
                    code -= 0x10000;
                    out.push_back(static_cast<wchar_t>(0xD800 + (code >> 10)));
                    out.push_back(static_cast<wchar_t>(0xDC00 + (code & 0x3FF)));
                }
                else
                {
                    out.push_back(static_cast<wchar_t>(code));
                }
                i += static_cast<std::size_t>(extra) + 1;
            }
            return out;
        }

        // Percent-decoded text, or the text itself when it can't be decoded (query items).
        std::wstring DecodeOrKeep(std::wstring_view text)
        {
            return PercentDecode(text).value_or(std::wstring{ text });
        }
    }

    std::optional<std::wstring> PercentDecode(std::wstring_view text)
    {
        std::string bytes;
        bytes.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            wchar_t const c = text[i];
            if (c == L'%')
            {
                if (i + 2 >= text.size())
                {
                    return std::nullopt;
                }
                int const high = HexValue(text[i + 1]);
                int const low = HexValue(text[i + 2]);
                if (high < 0 || low < 0)
                {
                    return std::nullopt;
                }
                bytes.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
            std::uint32_t code = static_cast<std::uint32_t>(c);
            if (code >= 0xD800 && code <= 0xDBFF && i + 1 < text.size())
            {
                auto const next = static_cast<std::uint32_t>(text[i + 1]);
                if (next >= 0xDC00 && next <= 0xDFFF)
                {
                    code = 0x10000 + ((code - 0xD800) << 10) + (next - 0xDC00);
                    ++i;
                }
            }
            AppendUtf8(bytes, code);
        }
        return DecodeUtf8(bytes);
    }

    std::optional<UrlCommand> ParseUrlCommand(std::wstring_view url)
    {
        auto const colon = url.find(L':');
        if (colon == std::wstring_view::npos || colon == 0)
        {
            return std::nullopt;
        }
        std::wstring_view const schemeText = url.substr(0, colon);
        for (std::size_t i = 0; i < schemeText.size(); ++i)
        {
            wchar_t const c = schemeText[i];
            bool const alpha = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
            bool const other = (c >= L'0' && c <= L'9') || c == L'+' || c == L'-' || c == L'.';
            if (!(alpha || (i > 0 && other)))
            {
                return std::nullopt;
            }
        }
        UrlCommand result;
        result.scheme = Lowercase(schemeText);
        if (result.scheme != L"lucida")
        {
            return std::nullopt;
        }

        // Command: cut at '?', then at '=', then drop everything up to "://".
        std::wstring_view command = url;
        if (auto const q = command.find(L'?'); q != std::wstring_view::npos)
        {
            command = command.substr(0, q);
        }
        if (auto const eq = command.find(L'='); eq != std::wstring_view::npos)
        {
            command = command.substr(0, eq);
        }
        if (auto const sep = command.find(L"://"); sep != std::wstring_view::npos)
        {
            command = command.substr(sep + 3);
        }
        // Windows normalises an empty path to "/" before the URL reaches us ("lucida://show" arrives as
        // "lucida://show/", "lucida://settings?x" as "lucida://settings/?x"), so trailing slashes are
        // dropped; commands never end in one.
        command = TrimWhitespace(command);
        while (!command.empty() && command.back() == L'/')
        {
            command.remove_suffix(1);
        }
        result.command = Lowercase(TrimWhitespace(command));

        // Value: after the first '=' of the whole URL.
        if (auto const eq = url.find(L'='); eq != std::wstring_view::npos)
        {
            result.value = PercentDecode(url.substr(eq + 1));
        }

        // Host and query, as URLComponents splits them.
        std::wstring_view rest = url.substr(colon + 1);
        std::wstring_view beforeFragment = rest.substr(0, rest.find(L'#'));
        if (beforeFragment.substr(0, 2) == L"//")
        {
            std::wstring_view authority = beforeFragment.substr(2);
            authority = authority.substr(0, authority.find_first_of(L"/?"));
            if (auto const at = authority.rfind(L'@'); at != std::wstring_view::npos)
            {
                authority = authority.substr(at + 1);
            }
            if (!authority.empty() && authority.front() != L'[')
            {
                authority = authority.substr(0, authority.find(L':'));
            }
            result.host = DecodeOrKeep(authority);
        }
        if (auto const q = beforeFragment.find(L'?'); q != std::wstring_view::npos)
        {
            std::wstring_view query = beforeFragment.substr(q + 1);
            while (true)
            {
                auto const amp = query.find(L'&');
                std::wstring_view const item = query.substr(0, amp);
                auto const eq = item.find(L'=');
                std::wstring const name = DecodeOrKeep(item.substr(0, eq));
                if (eq == std::wstring_view::npos)
                {
                    // An item without a value removes the name.
                    if (auto it = result.query.find(name); it != result.query.end())
                    {
                        result.query.erase(it);
                    }
                }
                else
                {
                    result.query.insert_or_assign(name, DecodeOrKeep(item.substr(eq + 1)));
                }
                if (amp == std::wstring_view::npos)
                {
                    break;
                }
                query = query.substr(amp + 1);
            }
        }
        return result;
    }

    ThenParameter ParseThenParameter(UrlCommand const& command)
    {
        ThenParameter result;
        auto const it = command.query.find(std::wstring_view{ L"then" });
        if (it == command.query.end())
        {
            return result; // use settings
        }
        result.actions.useSettings = false;
        std::wstring const list = Lowercase(it->second);
        std::size_t start = 0;
        while (true)
        {
            auto const comma = list.find(L',', start);
            std::wstring_view const item = std::wstring_view{ list }.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
            if (item == L"copy") result.actions.copy = true;
            else if (item == L"save") result.actions.save = true;
            else if (item == L"edit") result.actions.edit = true;
            else if (item == L"pin") result.actions.pin = true;
            else if (item == L"thumbnail") result.actions.thumbnail = true;
            if (comma == std::wstring::npos)
            {
                break;
            }
            start = comma + 1;
        }
        auto const& a = result.actions;
        result.unrecognized = !(a.copy || a.save || a.edit || a.pin || a.thumbnail);
        return result;
    }

    double DelayedCaptureSeconds(UrlCommand const& command)
    {
        constexpr double kDefault = 3.0;
        if (!command.value || command.value->empty())
        {
            return kDefault;
        }
        std::string narrow;
        for (wchar_t c : *command.value)
        {
            if (static_cast<std::uint32_t>(c) > 0x7F)
            {
                return kDefault;
            }
            narrow.push_back(static_cast<char>(c));
        }
        char const* first = narrow.data();
        char const* last = first + narrow.size();
        if (*first == '+')
        {
            ++first;
        }
        double seconds = 0;
        auto const [ptr, ec] = std::from_chars(first, last, seconds);
        if (ec != std::errc{} || ptr != last || !std::isfinite(seconds))
        {
            return kDefault;
        }
        return seconds < 0 ? 0.0 : seconds;
    }
}
