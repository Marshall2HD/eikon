#pragma once

// User-visible text. Strings live in Strings/en-US/Resources.resw. XAML uses x:Uid; code uses
// Get and Format with the same resource keys.

#include "Core/KeyShortcut.h"
#include "Core/Localization.h"

#include <string>
#include <string_view>

namespace Eikon::Strings
{
    // Startup, before any window is created: lets Core look strings up.
    void Initialize();

    // The string for a resource key (the key itself if it is missing, to make the gap visible).
    // Uses one cached MRT Core ResourceLoader.
    std::wstring Get(std::wstring_view key);

    // Get(key) with {0}, {1}, ... replaced by the arguments (see Localization::Format).
    template <typename... Args>
    std::wstring Format(std::wstring_view key, Args const&... args)
    {
        return Eikon::Localization::Format(Get(key), { std::wstring_view{ args }... });
    }

    // Shortcut / key names for display ("Ctrl+Shift+1"). Never use these for the serialized form in
    // settings: that is KeyShortcut::ToString.
    std::wstring ShortcutText(KeyShortcut const& shortcut);
    std::wstring ModifiersText(Modifiers modifiers);      // "Ctrl+Shift", in Win, Ctrl, Alt, Shift order
    std::wstring ModifierName(Modifiers modifier);        // a single flag
    std::wstring KeyText(std::uint32_t virtualKey);
}
