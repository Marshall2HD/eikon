#include "pch.h"
#include "TestFramework.h"

#include "Core/KeyShortcut.h"

using namespace Eikon;

TEST(KeyShortcut_ToString_UsesWindowsNotation)
{
    CHECK_EQ((KeyShortcut{ Modifiers::Ctrl | Modifiers::Shift, 'C' }.ToString()), std::wstring{ L"Ctrl+Shift+C" });
    CHECK_EQ((KeyShortcut{ Modifiers::Alt, VK_DELETE }.ToString()), std::wstring{ L"Alt+Delete" });
    CHECK_EQ((KeyShortcut{ Modifiers::Win | Modifiers::Alt, 'V' }.ToString()), std::wstring{ L"Win+Alt+V" });
    CHECK_EQ((KeyShortcut{ Modifiers::Ctrl, VK_SPACE }.ToString()), std::wstring{ L"Ctrl+Space" });
    CHECK_EQ((KeyShortcut{ Modifiers::Ctrl, VK_OEM_COMMA }.ToString()), std::wstring{ L"Ctrl+," });
    CHECK_EQ((KeyShortcut{ Modifiers::Ctrl | Modifiers::Alt | Modifiers::Shift, VK_BACK }.ToString()),
             std::wstring{ L"Ctrl+Alt+Shift+Backspace" });
    CHECK_EQ((KeyShortcut{ Modifiers::None, VK_F5 }.ToString()), std::wstring{ L"F5" });
    CHECK_EQ(KeyShortcut{}.ToString(), std::wstring{});
}

TEST(KeyShortcut_Parse_KnownStrings)
{
    CHECK_EQ(KeyShortcut::Parse(L"Ctrl+Shift+C"), (std::optional<KeyShortcut>{ KeyShortcut{ Modifiers::Ctrl | Modifiers::Shift, 'C' } }));
    CHECK_EQ(KeyShortcut::Parse(L"Alt+Delete"), (std::optional<KeyShortcut>{ KeyShortcut{ Modifiers::Alt, VK_DELETE } }));
    CHECK_EQ(KeyShortcut::Parse(L"Win+Alt+V"), (std::optional<KeyShortcut>{ KeyShortcut{ Modifiers::Win | Modifiers::Alt, 'V' } }));
    CHECK_EQ(KeyShortcut::Parse(L"Alt+Win+V"), (std::optional<KeyShortcut>{ KeyShortcut{ Modifiers::Win | Modifiers::Alt, 'V' } }));
    CHECK_EQ(KeyShortcut::Parse(L"Ctrl+Space"), (std::optional<KeyShortcut>{ KeyShortcut{ Modifiers::Ctrl, VK_SPACE } }));
    CHECK_EQ(KeyShortcut::Parse(L"Ctrl+,"), (std::optional<KeyShortcut>{ KeyShortcut{ Modifiers::Ctrl, VK_OEM_COMMA } }));
    CHECK_EQ(KeyShortcut::Parse(L"ctrl+shift+c"), (std::optional<KeyShortcut>{ KeyShortcut{ Modifiers::Ctrl | Modifiers::Shift, 'C' } }));
    CHECK_EQ(KeyShortcut::Parse(L"Control+Del"), (std::optional<KeyShortcut>{ KeyShortcut{ Modifiers::Ctrl, VK_DELETE } }));
    CHECK_EQ(KeyShortcut::Parse(L" Ctrl + Q "), (std::optional<KeyShortcut>{ KeyShortcut{ Modifiers::Ctrl, 'Q' } }));
    CHECK_EQ(KeyShortcut::Parse(L"Ctrl+Num +"), (std::optional<KeyShortcut>{ KeyShortcut{ Modifiers::Ctrl, VK_ADD } }));
    CHECK_EQ(KeyShortcut::Parse(L"Ctrl++"), (std::optional<KeyShortcut>{ KeyShortcut{ Modifiers::Ctrl, VK_OEM_PLUS } }));
    CHECK_EQ(KeyShortcut::Parse(L"F24"), (std::optional<KeyShortcut>{ KeyShortcut{ Modifiers::None, VK_F24 } }));
    CHECK_EQ(KeyShortcut::Parse(L""), (std::optional<KeyShortcut>{ KeyShortcut{} }));
}

TEST(KeyShortcut_Parse_RejectsGarbage)
{
    CHECK(!KeyShortcut::Parse(L"Ctrl+").has_value());
    CHECK(!KeyShortcut::Parse(L"Ctrl").has_value());
    CHECK(!KeyShortcut::Parse(L"Foo+Bar").has_value());
    CHECK(!KeyShortcut::Parse(L"Ctrl+F25").has_value());
    CHECK(!KeyShortcut::Parse(L"Ctrl+Shift+Banana").has_value());
}

TEST(KeyShortcut_KeyName_IsLayoutIndependent)
{
    CHECK_EQ(KeyShortcut::KeyName('A'), std::wstring{ L"A" });
    CHECK_EQ(KeyShortcut::KeyName('7'), std::wstring{ L"7" });
    CHECK_EQ(KeyShortcut::KeyName(VK_F1), std::wstring{ L"F1" });
    CHECK_EQ(KeyShortcut::KeyName(VK_F12), std::wstring{ L"F12" });
    CHECK_EQ(KeyShortcut::KeyName(VK_RETURN), std::wstring{ L"Enter" });
    CHECK_EQ(KeyShortcut::KeyName(VK_ESCAPE), std::wstring{ L"Escape" });
    CHECK_EQ(KeyShortcut::KeyName(VK_TAB), std::wstring{ L"Tab" });
    CHECK_EQ(KeyShortcut::KeyName(VK_INSERT), std::wstring{ L"Insert" });
    CHECK_EQ(KeyShortcut::KeyName(VK_PRIOR), std::wstring{ L"PageUp" });
    CHECK_EQ(KeyShortcut::KeyName(VK_NEXT), std::wstring{ L"PageDown" });
    CHECK_EQ(KeyShortcut::KeyName(VK_HOME), std::wstring{ L"Home" });
    CHECK_EQ(KeyShortcut::KeyName(VK_END), std::wstring{ L"End" });
    CHECK_EQ(KeyShortcut::KeyName(VK_UP), std::wstring{ L"Up" });
    CHECK_EQ(KeyShortcut::KeyName(VK_LEFT), std::wstring{ L"Left" });
    CHECK_EQ(KeyShortcut::KeyName(VK_OEM_PERIOD), std::wstring{ L"." });
    CHECK_EQ(KeyShortcut::KeyName(VK_OEM_MINUS), std::wstring{ L"-" });
    CHECK_EQ(KeyShortcut::KeyName(VK_OEM_PLUS), std::wstring{ L"=" });
    CHECK_EQ(KeyShortcut::KeyName(VK_OEM_1), std::wstring{ L";" });
    CHECK_EQ(KeyShortcut::KeyName(VK_OEM_2), std::wstring{ L"/" });
    CHECK_EQ(KeyShortcut::KeyName(VK_OEM_3), std::wstring{ L"`" });
    CHECK_EQ(KeyShortcut::KeyName(VK_OEM_4), std::wstring{ L"[" });
    CHECK_EQ(KeyShortcut::KeyName(VK_OEM_5), std::wstring{ L"\\" });
    CHECK_EQ(KeyShortcut::KeyName(VK_OEM_6), std::wstring{ L"]" });
    CHECK_EQ(KeyShortcut::KeyName(VK_OEM_7), std::wstring{ L"'" });
    CHECK_EQ(KeyShortcut::KeyName(VK_NUMPAD3), std::wstring{ L"Num 3" });
    CHECK_EQ(KeyShortcut::KeyName(VK_MULTIPLY), std::wstring{ L"Num *" });
}

TEST(KeyShortcut_RoundTripsEveryVirtualKey)
{
    const Modifiers combinations[] = {
        Modifiers::None, Modifiers::Ctrl, Modifiers::Alt | Modifiers::Shift, Modifiers::Win | Modifiers::Alt,
        Modifiers::Ctrl | Modifiers::Alt | Modifiers::Shift | Modifiers::Win,
    };
    for (std::uint32_t vk = 1; vk < 0xFF; ++vk)
    {
        for (auto modifiers : combinations)
        {
            const KeyShortcut shortcut{ modifiers, vk };
            const auto text = shortcut.ToString();
            const auto parsed = KeyShortcut::Parse(text);
            if (!parsed || !(*parsed == shortcut))
            {
                CHECK_EQ(parsed, std::optional<KeyShortcut>{ shortcut });
            }
        }
    }
}

TEST(KeyShortcut_HasFlag)
{
    const Modifiers value = Modifiers::Ctrl | Modifiers::Shift;
    CHECK(HasFlag(value, Modifiers::Ctrl));
    CHECK(HasFlag(value, Modifiers::Shift));
    CHECK(!HasFlag(value, Modifiers::Alt));
    CHECK(!HasFlag(value, Modifiers::None));
}

