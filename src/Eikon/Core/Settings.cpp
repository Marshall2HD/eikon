#include "pch.h"
#include "Core/Settings.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <system_error>

// No WinRT in this file: the LocalSettings backend lives in Core/AppData.cpp so that the unit tests
// can link Settings with an in-memory store.

namespace Eikon::Core
{
    namespace
    {
        bool ToAscii(std::wstring_view text, std::string& out)
        {
            out.clear();
            for (wchar_t c : text)
            {
                if (c == 0 || static_cast<std::uint32_t>(c) > 0x7F)
                {
                    return false;
                }
                out.push_back(static_cast<char>(c));
            }
            return true;
        }

        std::wstring_view Trim(std::wstring_view text) noexcept
        {
            while (!text.empty() && (text.front() == L' ' || text.front() == L'\t'))
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && (text.back() == L' ' || text.back() == L'\t'))
            {
                text.remove_suffix(1);
            }
            return text;
        }

        std::wstring FormatBool(bool value) { return value ? L"1" : L"0"; }

        std::optional<bool> ParseBool(std::wstring_view text)
        {
            text = Trim(text);
            if (text == L"1" || text == L"true" || text == L"True" || text == L"YES")
            {
                return true;
            }
            if (text == L"0" || text == L"false" || text == L"False" || text == L"NO")
            {
                return false;
            }
            return std::nullopt;
        }

        std::wstring FormatInt(std::int64_t value) { return std::to_wstring(value); }

        std::optional<std::int64_t> ParseInt(std::wstring_view text)
        {
            std::string narrow;
            if (!ToAscii(Trim(text), narrow) || narrow.empty())
            {
                return std::nullopt;
            }
            char const* first = narrow.data();
            char const* last = first + narrow.size();
            if (*first == '+')
            {
                ++first;
            }
            std::int64_t value = 0;
            auto [ptr, ec] = std::from_chars(first, last, value);
            if (ec != std::errc{} || ptr != last)
            {
                return std::nullopt;
            }
            return value;
        }

        std::wstring FormatDouble(double value)
        {
            char buffer[64] = {};
            auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer) - 1, value);
            if (ec != std::errc{})
            {
                return L"0";
            }
            return std::wstring(buffer, ptr);
        }

        std::optional<double> ParseDouble(std::wstring_view text)
        {
            std::string narrow;
            if (!ToAscii(Trim(text), narrow) || narrow.empty())
            {
                return std::nullopt;
            }
            char const* first = narrow.data();
            char const* last = first + narrow.size();
            if (*first == '+')
            {
                ++first;
            }
            double value = 0;
            auto [ptr, ec] = std::from_chars(first, last, value);
            if (ec != std::errc{} || ptr != last || !std::isfinite(value))
            {
                return std::nullopt;
            }
            return value;
        }

        // Upper-case 8-4-4-4-12 UUID (version 4).
        std::wstring NewUuid()
        {
            std::random_device device;
            std::mt19937_64 engine{ (static_cast<std::uint64_t>(device()) << 32) ^ device() };
            std::uint64_t const high = (engine() & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
            std::uint64_t const low = (engine() & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
            wchar_t buffer[40] = {};
            std::swprintf(buffer, std::size(buffer), L"%08X-%04X-%04X-%04X-%012llX",
                static_cast<unsigned>(high >> 32), static_cast<unsigned>((high >> 16) & 0xFFFF), static_cast<unsigned>(high & 0xFFFF),
                static_cast<unsigned>(low >> 48), static_cast<unsigned long long>(low & 0xFFFFFFFFFFFFull));
            return buffer;
        }

        class MemorySettingsStore final : public SettingsStore
        {
        public:
            std::optional<std::wstring> Read(std::wstring_view key) const override
            {
                for (auto const& [k, v] : m_values)
                {
                    if (k == key)
                    {
                        return v;
                    }
                }
                return std::nullopt;
            }
            void Write(std::wstring_view key, std::wstring_view value) override
            {
                for (auto& [k, v] : m_values)
                {
                    if (k == key)
                    {
                        v = value;
                        return;
                    }
                }
                m_values.emplace_back(std::wstring{ key }, std::wstring{ value });
            }
            void Remove(std::wstring_view key) override
            {
                std::erase_if(m_values, [key](auto const& entry) { return entry.first == key; });
            }

        private:
            std::vector<std::pair<std::wstring, std::wstring>> m_values;
        };
    }

    std::wstring_view HotKeyName(HotKey hotKey) noexcept
    {
        switch (hotKey)
        {
        case HotKey::Fullscreen: return L"fullscreen";
        case HotKey::Area: return L"area";
        case HotKey::Ocr: return L"ocr";
        case HotKey::Window: return L"window";
        case HotKey::AnyWindow: return L"anyWindow";
        case HotKey::RepeatArea: return L"repeatArea";
        case HotKey::Scrolling: return L"scrolling";
        case HotKey::Reopen: return L"reopen";
        case HotKey::LoadClipboard: return L"loadClipboard";
        case HotKey::Library: return L"library";
        }
        return L"";
    }

    // ---- infrastructure ----

    Settings& Settings::Current()
    {
        static Settings instance;
        return instance;
    }

    void Settings::UseStore(std::unique_ptr<SettingsStore> store)
    {
        auto& settings = Current();
        settings.m_store = std::move(store);
        settings.m_cache.clear();
    }

    SettingsStore& Settings::Store()
    {
        if (!m_store)
        {
            m_store = CreateDefaultSettingsStore();
            if (!m_store)
            {
                m_store = std::make_unique<MemorySettingsStore>();
            }
        }
        return *m_store;
    }

    std::optional<std::wstring> Settings::ReadRaw(std::wstring_view key)
    {
        for (auto const& [cachedKey, cachedValue] : m_cache)
        {
            if (cachedKey == key)
            {
                return cachedValue;
            }
        }
        std::optional<std::wstring> value;
        try
        {
            value = Store().Read(key);
        }
        catch (...)
        {
            value.reset();
        }
        m_cache.emplace_back(std::wstring{ key }, value);
        return value;
    }

    void Settings::StoreRaw(std::wstring_view key, std::wstring const& value)
    {
        bool cached = false;
        for (auto& [cachedKey, cachedValue] : m_cache)
        {
            if (cachedKey == key)
            {
                cachedValue = value;
                cached = true;
                break;
            }
        }
        if (!cached)
        {
            m_cache.emplace_back(std::wstring{ key }, value);
        }
        try
        {
            Store().Write(key, value);
        }
        catch (...)
        {
            // Keep the in-memory value; persistence failures are not fatal.
        }
    }

    void Settings::SetRaw(std::wstring_view key, std::wstring const& value)
    {
        if (ReadRaw(key) == value)
        {
            return;
        }
        StoreRaw(key, value);
        Changed(key);
    }

    bool Settings::Contains(std::wstring_view key)
    {
        return ReadRaw(key).has_value();
    }

    void Settings::Remove(std::wstring_view key)
    {
        bool const existed = ReadRaw(key).has_value();
        for (auto& [cachedKey, cachedValue] : m_cache)
        {
            if (cachedKey == key)
            {
                cachedValue.reset();
            }
        }
        try
        {
            Store().Remove(key);
        }
        catch (...)
        {
        }
        if (existed)
        {
            Changed(key);
        }
    }

    // ---- typed helpers (write the default on first read) ----

    bool Settings::ReadBool(std::wstring_view key, bool fallback)
    {
        auto raw = ReadRaw(key);
        if (!raw)
        {
            StoreRaw(key, FormatBool(fallback));
            return fallback;
        }
        return ParseBool(*raw).value_or(fallback);
    }

    void Settings::WriteBool(std::wstring_view key, bool value)
    {
        SetRaw(key, FormatBool(value));
    }

    std::int64_t Settings::ReadInt(std::wstring_view key, std::int64_t fallback)
    {
        auto raw = ReadRaw(key);
        if (!raw)
        {
            StoreRaw(key, FormatInt(fallback));
            return fallback;
        }
        return ParseInt(*raw).value_or(fallback);
    }

    void Settings::WriteInt(std::wstring_view key, std::int64_t value)
    {
        SetRaw(key, FormatInt(value));
    }

    double Settings::ReadDouble(std::wstring_view key, double fallback)
    {
        auto raw = ReadRaw(key);
        if (!raw)
        {
            StoreRaw(key, FormatDouble(fallback));
            return fallback;
        }
        return ParseDouble(*raw).value_or(fallback);
    }

    void Settings::WriteDouble(std::wstring_view key, double value)
    {
        if (std::isfinite(value))
        {
            SetRaw(key, FormatDouble(value));
        }
    }

    std::wstring Settings::ReadString(std::wstring_view key, std::wstring_view fallback)
    {
        auto raw = ReadRaw(key);
        if (!raw)
        {
            std::wstring value{ fallback };
            StoreRaw(key, value);
            return value;
        }
        return *raw;
    }

    void Settings::WriteString(std::wstring_view key, std::wstring_view value)
    {
        SetRaw(key, std::wstring{ value });
    }

    // ---- typed properties ----

#define EIKON_DEFINE_BOOL(Name, KeyText, Default)                                    \
    bool Settings::Name() { return ReadBool(k##Name, Default); }                     \
    void Settings::Name(bool value) { WriteBool(k##Name, value); }
#define EIKON_DEFINE_INT(Name, KeyText, Default)                                     \
    std::int64_t Settings::Name() { return ReadInt(k##Name, Default); }              \
    void Settings::Name(std::int64_t value) { WriteInt(k##Name, value); }
#define EIKON_DEFINE_STRING(Name, KeyText, Default)                                  \
    std::wstring Settings::Name() { return ReadString(k##Name, Default); }           \
    void Settings::Name(std::wstring const& value) { WriteString(k##Name, value); }

    EIKON_SETTINGS_BOOL(EIKON_DEFINE_BOOL)
    EIKON_SETTINGS_INT(EIKON_DEFINE_INT)
    EIKON_SETTINGS_STRING(EIKON_DEFINE_STRING)

#undef EIKON_DEFINE_BOOL
#undef EIKON_DEFINE_INT
#undef EIKON_DEFINE_STRING

    std::optional<std::wstring> Settings::DefaultFolderBookmark()
    {
        return ReadRaw(kDefaultFolderBookmark);
    }

    void Settings::DefaultFolderBookmark(std::optional<std::wstring> const& value)
    {
        if (value)
        {
            SetRaw(kDefaultFolderBookmark, *value);
        }
        else
        {
            Remove(kDefaultFolderBookmark);
        }
    }

    std::wstring Settings::Uid()
    {
        auto raw = ReadRaw(kUid);
        if (!raw || raw->empty())
        {
            std::wstring const uid = NewUuid();
            StoreRaw(kUid, uid);
            return uid;
        }
        return *raw;
    }

    void Settings::Uid(std::wstring const& value)
    {
        SetRaw(kUid, value);
    }

    // ---- hotkeys ----

    std::wstring Settings::ShortcutKey(HotKey hotKey)
    {
        return L"KeyboardShortcuts_" + std::wstring{ HotKeyName(hotKey) };
    }

    KeyShortcut Settings::DefaultShortcut(HotKey hotKey) noexcept
    {
        switch (hotKey)
        {
        case HotKey::Fullscreen: return { Modifiers::Ctrl | Modifiers::Shift, '1' };
        case HotKey::Area: return { Modifiers::Ctrl | Modifiers::Shift, '2' };
        case HotKey::Ocr: return { Modifiers::Ctrl | Modifiers::Alt | Modifiers::Shift, 'O' };
        default: return {};
        }
    }

    KeyShortcut Settings::Shortcut(HotKey hotKey)
    {
        auto const raw = ReadRaw(ShortcutKey(hotKey));
        if (!raw)
        {
            return DefaultShortcut(hotKey);
        }
        return KeyShortcut::Parse(*raw).value_or(DefaultShortcut(hotKey));
    }

    void Settings::Shortcut(HotKey hotKey, KeyShortcut const& value)
    {
        SetRaw(ShortcutKey(hotKey), value.IsEmpty() ? std::wstring{} : value.ToString());
    }
}
