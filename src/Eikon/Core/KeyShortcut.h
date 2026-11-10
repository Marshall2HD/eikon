#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Eikon
{
    // Modifier bit flags shared by global hot keys, the popup's key handling and the
    // shortcut recorder control.
    enum class Modifiers : std::uint8_t
    {
        None = 0,
        Ctrl = 1 << 0,
        Alt = 1 << 1,
        Shift = 1 << 2,
        Win = 1 << 3,
    };

    constexpr Modifiers operator|(Modifiers a, Modifiers b) noexcept
    {
        return static_cast<Modifiers>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }
    constexpr Modifiers operator&(Modifiers a, Modifiers b) noexcept
    {
        return static_cast<Modifiers>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
    }
    constexpr Modifiers& operator|=(Modifiers& a, Modifiers b) noexcept { return a = a | b; }
    constexpr bool HasFlag(Modifiers value, Modifiers flag) noexcept { return (value & flag) == flag && flag != Modifiers::None; }

    // A key plus modifiers, e.g. Ctrl+Shift+C. `key` is a Win32 virtual-key code (VK_*), which is
    // also the numeric value of winrt::Windows::System::VirtualKey.
    struct KeyShortcut
    {
        Modifiers modifiers = Modifiers::None;
        std::uint32_t key = 0;

        bool IsEmpty() const noexcept { return key == 0; }
        bool operator==(KeyShortcut const&) const = default;

        // Serialized form used in settings, e.g. "Ctrl+Shift+C", "Alt+Delete", "Win+Alt+V".
        // Modifier order is always Win, Ctrl, Alt, Shift (Windows convention); Parse accepts any
        // order and is case-insensitive. Empty shortcut <-> empty string. Key names come from
        // KeyName (layout independent); unnamed keys are written as "0xNN".
        std::wstring ToString() const;
        static std::optional<KeyShortcut> Parse(std::wstring_view text);

        // Human readable name of a single key for display ("C", "Delete", "Space", "F5", ",").
        static std::wstring KeyName(std::uint32_t virtualKey);
    };

    // Reads the live keyboard state (GetKeyState) and returns the currently held modifiers.
    Modifiers CurrentModifiers() noexcept;
}
