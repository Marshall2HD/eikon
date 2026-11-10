#include "pch.h"
#include "Core/KeyShortcut.h"

#include <span>
#include <string>
#include <string_view>

namespace Eikon
{
    namespace
    {
        struct NamedKey
        {
            std::uint32_t key;
            std::wstring_view name;
        };

        // Canonical display/serialization names. Deliberately independent of the active keyboard
        // layout so that settings round-trip deterministically.
        constexpr NamedKey kNamedKeys[] = {
            { VK_SPACE, L"Space" },
            { VK_RETURN, L"Enter" },
            { VK_DELETE, L"Delete" },
            { VK_BACK, L"Backspace" },
            { VK_TAB, L"Tab" },
            { VK_ESCAPE, L"Escape" },
            { VK_INSERT, L"Insert" },
            { VK_HOME, L"Home" },
            { VK_END, L"End" },
            { VK_PRIOR, L"PageUp" },
            { VK_NEXT, L"PageDown" },
            { VK_UP, L"Up" },
            { VK_DOWN, L"Down" },
            { VK_LEFT, L"Left" },
            { VK_RIGHT, L"Right" },
            { VK_PAUSE, L"Pause" },
            { VK_SNAPSHOT, L"PrintScreen" },
            { VK_APPS, L"Menu" },
            { VK_CAPITAL, L"CapsLock" },
            { VK_NUMLOCK, L"NumLock" },
            { VK_SCROLL, L"ScrollLock" },
            { VK_CLEAR, L"Clear" },
            { VK_OEM_COMMA, L"," },
            { VK_OEM_PERIOD, L"." },
            { VK_OEM_MINUS, L"-" },
            { VK_OEM_PLUS, L"=" },
            { VK_OEM_1, L";" },
            { VK_OEM_2, L"/" },
            { VK_OEM_3, L"`" },
            { VK_OEM_4, L"[" },
            { VK_OEM_5, L"\\" },
            { VK_OEM_6, L"]" },
            { VK_OEM_7, L"'" },
            { VK_OEM_102, L"OEM102" },
            { VK_MULTIPLY, L"Num *" },
            { VK_ADD, L"Num +" },
            { VK_SEPARATOR, L"Num Separator" },
            { VK_SUBTRACT, L"Num -" },
            { VK_DECIMAL, L"Num ." },
            { VK_DIVIDE, L"Num /" },
        };

        // Additional spellings accepted by Parse (never produced by ToString).
        constexpr NamedKey kKeyAliases[] = {
            { VK_RETURN, L"Return" },
            { VK_ESCAPE, L"Esc" },
            { VK_DELETE, L"Del" },
            { VK_INSERT, L"Ins" },
            { VK_PRIOR, L"PgUp" },
            { VK_NEXT, L"PgDn" },
            { VK_PRIOR, L"Page Up" },
            { VK_NEXT, L"Page Down" },
            { VK_UP, L"ArrowUp" },
            { VK_DOWN, L"ArrowDown" },
            { VK_LEFT, L"ArrowLeft" },
            { VK_RIGHT, L"ArrowRight" },
            { VK_SNAPSHOT, L"PrtSc" },
            { VK_APPS, L"Apps" },
            { VK_OEM_COMMA, L"Comma" },
            { VK_OEM_PERIOD, L"Period" },
            { VK_OEM_MINUS, L"Minus" },
            { VK_OEM_PLUS, L"Plus" },
            { VK_OEM_PLUS, L"+" },
        };

        struct NamedModifier
        {
            Modifiers flag;
            std::wstring_view name;
        };

        constexpr NamedModifier kModifierNames[] = {
            { Modifiers::Win, L"Win" },
            { Modifiers::Ctrl, L"Ctrl" },
            { Modifiers::Alt, L"Alt" },
            { Modifiers::Shift, L"Shift" },
        };

        constexpr NamedModifier kModifierAliases[] = {
            { Modifiers::Win, L"Windows" },
            { Modifiers::Win, L"Meta" },
            { Modifiers::Ctrl, L"Control" },
        };

        wchar_t AsciiLower(wchar_t c) noexcept
        {
            return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
        }

        bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept
        {
            if (a.size() != b.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                if (AsciiLower(a[i]) != AsciiLower(b[i]))
                {
                    return false;
                }
            }
            return true;
        }

        bool StartsWithIgnoreCase(std::wstring_view text, std::wstring_view prefix) noexcept
        {
            return text.size() >= prefix.size() && EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
        }

        std::wstring_view TrimSpaces(std::wstring_view text) noexcept
        {
            while (!text.empty() && text.front() == L' ')
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && text.back() == L' ')
            {
                text.remove_suffix(1);
            }
            return text;
        }

        bool ParseUnsigned(std::wstring_view text, int base, std::uint32_t& value) noexcept
        {
            if (text.empty() || text.size() > 8)
            {
                return false;
            }
            std::uint32_t result = 0;
            for (wchar_t c : text)
            {
                std::uint32_t digit = 0;
                if (c >= L'0' && c <= L'9')
                {
                    digit = static_cast<std::uint32_t>(c - L'0');
                }
                else if (base == 16 && c >= L'a' && c <= L'f')
                {
                    digit = static_cast<std::uint32_t>(c - L'a' + 10);
                }
                else if (base == 16 && c >= L'A' && c <= L'F')
                {
                    digit = static_cast<std::uint32_t>(c - L'A' + 10);
                }
                else
                {
                    return false;
                }
                result = result * static_cast<std::uint32_t>(base) + digit;
            }
            value = result;
            return true;
        }

        std::optional<std::uint32_t> KeyFromName(std::wstring_view name)
        {
            if (name.empty())
            {
                return std::nullopt;
            }
            if (name.size() == 1)
            {
                const wchar_t c = name[0];
                if (c >= L'a' && c <= L'z')
                {
                    return static_cast<std::uint32_t>(c - L'a' + L'A');
                }
                if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))
                {
                    return static_cast<std::uint32_t>(c);
                }
            }
            for (auto const& entry : kNamedKeys)
            {
                if (EqualsIgnoreCase(name, entry.name))
                {
                    return entry.key;
                }
            }
            for (auto const& entry : kKeyAliases)
            {
                if (EqualsIgnoreCase(name, entry.name))
                {
                    return entry.key;
                }
            }
            // Function keys F1..F24.
            if (name.size() >= 2 && (name[0] == L'F' || name[0] == L'f'))
            {
                std::uint32_t number = 0;
                if (ParseUnsigned(name.substr(1), 10, number) && number >= 1 && number <= 24)
                {
                    return static_cast<std::uint32_t>(VK_F1) + number - 1;
                }
            }
            // Numpad digits: "Num 0".."Num 9" (also "Numpad0").
            for (std::wstring_view prefix : { std::wstring_view{ L"Num " }, std::wstring_view{ L"Numpad" }, std::wstring_view{ L"Num" } })
            {
                if (StartsWithIgnoreCase(name, prefix) && name.size() == prefix.size() + 1)
                {
                    const wchar_t c = name[prefix.size()];
                    if (c >= L'0' && c <= L'9')
                    {
                        return static_cast<std::uint32_t>(VK_NUMPAD0) + static_cast<std::uint32_t>(c - L'0');
                    }
                }
            }
            // Fallback form produced by KeyName for unnamed keys: "0xNN".
            if (name.size() > 2 && name[0] == L'0' && (name[1] == L'x' || name[1] == L'X'))
            {
                std::uint32_t code = 0;
                if (ParseUnsigned(name.substr(2), 16, code) && code > 0 && code <= 0xFF)
                {
                    return code;
                }
            }
            return std::nullopt;
        }
    }

    std::wstring KeyShortcut::KeyName(std::uint32_t virtualKey)
    {
        if ((virtualKey >= 'A' && virtualKey <= 'Z') || (virtualKey >= '0' && virtualKey <= '9'))
        {
            return std::wstring(1, static_cast<wchar_t>(virtualKey));
        }
        if (virtualKey >= VK_F1 && virtualKey <= VK_F24)
        {
            return L"F" + std::to_wstring(virtualKey - VK_F1 + 1);
        }
        if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9)
        {
            return L"Num " + std::wstring(1, static_cast<wchar_t>(L'0' + (virtualKey - VK_NUMPAD0)));
        }
        for (auto const& entry : kNamedKeys)
        {
            if (entry.key == virtualKey)
            {
                return std::wstring{ entry.name };
            }
        }
        if (virtualKey == 0)
        {
            return {};
        }
        static constexpr wchar_t kHex[] = L"0123456789ABCDEF";
        std::wstring result = L"0x";
        result += kHex[(virtualKey >> 4) & 0xF];
        result += kHex[virtualKey & 0xF];
        return result;
    }

    std::wstring KeyShortcut::ToString() const
    {
        if (IsEmpty())
        {
            return {};
        }
        std::wstring result;
        for (auto const& modifier : kModifierNames)
        {
            if (HasFlag(modifiers, modifier.flag))
            {
                result += modifier.name;
                result += L'+';
            }
        }
        result += KeyName(key);
        return result;
    }

    std::optional<KeyShortcut> KeyShortcut::Parse(std::wstring_view text)
    {
        text = TrimSpaces(text);
        if (text.empty())
        {
            return KeyShortcut{};
        }

        KeyShortcut shortcut;
        // Consume "Modifier+" prefixes; whatever remains is the key name. This keeps names that
        // contain '+' (e.g. "Num +") parseable.
        bool consumed = true;
        while (consumed)
        {
            consumed = false;
            for (std::span<const NamedModifier> table : { std::span<const NamedModifier>{ kModifierNames }, std::span<const NamedModifier>{ kModifierAliases } })
            {
                for (auto const& modifier : table)
                {
                    if (StartsWithIgnoreCase(text, modifier.name) && text.size() > modifier.name.size())
                    {
                        std::wstring_view rest = TrimSpaces(text.substr(modifier.name.size()));
                        if (rest.size() > 1 && rest.front() == L'+')
                        {
                            shortcut.modifiers |= modifier.flag;
                            text = TrimSpaces(rest.substr(1));
                            consumed = true;
                            break;
                        }
                    }
                }
                if (consumed)
                {
                    break;
                }
            }
        }

        auto key = KeyFromName(text);
        if (!key)
        {
            return std::nullopt;
        }
        shortcut.key = *key;
        return shortcut;
    }

    Modifiers CurrentModifiers() noexcept
    {
        Modifiers result = Modifiers::None;
        auto down = [](int vk) noexcept { return (static_cast<unsigned short>(GetKeyState(vk)) & 0x8000u) != 0; };
        if (down(VK_CONTROL))
        {
            result |= Modifiers::Ctrl;
        }
        if (down(VK_MENU))
        {
            result |= Modifiers::Alt;
        }
        if (down(VK_SHIFT))
        {
            result |= Modifiers::Shift;
        }
        if (down(VK_LWIN) || down(VK_RWIN))
        {
            result |= Modifiers::Win;
        }
        return result;
    }
}
