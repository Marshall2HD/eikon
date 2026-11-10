#include "pch.h"
#include "Core/Localization.h"

#include <atomic>

namespace Eikon::Localization
{
    namespace
    {
        std::atomic<Lookup> g_lookup{ nullptr };
    }

    void SetLookup(Lookup lookup) noexcept
    {
        g_lookup.store(lookup);
    }

    std::wstring Text(std::wstring_view key, std::wstring_view fallback)
    {
        if (auto const lookup = g_lookup.load())
        {
            try
            {
                if (auto text = lookup(key); !text.empty())
                {
                    return text;
                }
            }
            catch (...)
            {
            }
        }
        return std::wstring{ fallback };
    }

    std::wstring Format(std::wstring_view pattern, std::initializer_list<std::wstring_view> args)
    {
        std::wstring result;
        result.reserve(pattern.size() + 32);
        for (std::size_t i = 0; i < pattern.size(); ++i)
        {
            wchar_t const c = pattern[i];
            if ((c == L'{' || c == L'}') && i + 1 < pattern.size() && pattern[i + 1] == c)
            {
                result += c;
                ++i;
                continue;
            }
            if (c == L'{' && i + 2 < pattern.size() && pattern[i + 1] >= L'0' && pattern[i + 1] <= L'9' && pattern[i + 2] == L'}')
            {
                auto const index = static_cast<std::size_t>(pattern[i + 1] - L'0');
                if (index < args.size())
                {
                    result += *(args.begin() + index);
                    i += 2;
                    continue;
                }
            }
            result += c;
        }
        return result;
    }
}
