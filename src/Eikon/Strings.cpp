#include "pch.h"
#include "Strings.h"

namespace Eikon::Strings
{
    namespace
    {
        using winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceLoader;

        ResourceLoader const& Loader()
        {
            static ResourceLoader const loader = []() -> ResourceLoader {
                try
                {
                    return ResourceLoader{};
                }
                catch (...)
                {
                    OutputDebugStringW(L"Eikon: resources.pri not available\n");
                    return nullptr;
                }
            }();
            return loader;
        }

        // Core's lookup: empty when missing, so Core falls back to its English text.
        std::wstring Lookup(std::wstring_view key)
        {
            if (auto const& loader = Loader())
            {
                return std::wstring{ loader.GetString(winrt::hstring{ key }) };
            }
            return {};
        }

        // Resource key of a key name that is printed differently on keyboards in other languages.
        wchar_t const* KeyResource(std::uint32_t virtualKey) noexcept
        {
            switch (virtualKey)
            {
            case VK_SPACE: return L"Key_Space";
            case VK_RETURN: return L"Key_Enter";
            case VK_DELETE: return L"Key_Delete";
            case VK_BACK: return L"Key_Backspace";
            case VK_TAB: return L"Key_Tab";
            case VK_ESCAPE: return L"Key_Escape";
            case VK_INSERT: return L"Key_Insert";
            case VK_HOME: return L"Key_Home";
            case VK_END: return L"Key_End";
            case VK_PRIOR: return L"Key_PageUp";
            case VK_NEXT: return L"Key_PageDown";
            case VK_UP: return L"Key_Up";
            case VK_DOWN: return L"Key_Down";
            case VK_LEFT: return L"Key_Left";
            case VK_RIGHT: return L"Key_Right";
            case VK_SNAPSHOT: return L"Key_PrintScreen";
            default: return nullptr;
            }
        }
    }

    void Initialize()
    {
        Localization::SetLookup(&Lookup);
    }

    std::wstring Get(std::wstring_view key)
    {
        try
        {
            if (auto text = Lookup(key); !text.empty())
            {
                return text;
            }
        }
        catch (...)
        {
        }
        return std::wstring{ key };
    }

    std::wstring ModifierName(Modifiers modifier)
    {
        switch (modifier)
        {
        case Modifiers::Ctrl: return Get(L"Key_Ctrl");
        case Modifiers::Alt: return Get(L"Key_Alt");
        case Modifiers::Shift: return Get(L"Key_Shift");
        case Modifiers::Win: return Get(L"Key_Win");
        default: return {};
        }
    }

    std::wstring ModifiersText(Modifiers modifiers)
    {
        std::wstring text;
        for (auto const flag : { Modifiers::Win, Modifiers::Ctrl, Modifiers::Alt, Modifiers::Shift })
        {
            if (HasFlag(modifiers, flag))
            {
                if (!text.empty())
                {
                    text += L'+';
                }
                text += ModifierName(flag);
            }
        }
        return text;
    }

    std::wstring KeyText(std::uint32_t virtualKey)
    {
        if (auto const resource = KeyResource(virtualKey))
        {
            return Get(resource);
        }
        return KeyShortcut::KeyName(virtualKey);
    }

    std::wstring ShortcutText(KeyShortcut const& shortcut)
    {
        if (shortcut.IsEmpty())
        {
            return {};
        }
        std::wstring text = ModifiersText(shortcut.modifiers);
        if (!text.empty())
        {
            text += L'+';
        }
        return text + KeyText(shortcut.key);
    }
}
