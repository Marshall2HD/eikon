#include "pch.h"
#include "Platform/KeyboardHook.h"

#pragma comment(lib, "user32.lib")

namespace Eikon
{
    namespace
    {
        constexpr DWORD VkV = 'V';
        // Unassigned virtual key. Injected while Win is held so that releasing Win does not open Start.
        constexpr WORD DummyKey = 0xE8;

        // Hook state. The hook runs on the UI thread (the thread that installed it), so plain
        // variables are sufficient.
        // Intentionally leaked so that no COM release runs during static destruction.
        winrt::Microsoft::UI::Dispatching::DispatcherQueue& Queue()
        {
            // winrt projected types delete operator new; wrap the handle in a plain struct.
            struct Holder
            {
                winrt::Microsoft::UI::Dispatching::DispatcherQueue queue{ nullptr };
            };
            static auto* holder = new Holder{};
            return holder->queue;
        }
        bool g_leftWinDown = false;
        bool g_rightWinDown = false;
        DWORD g_swallowingKey = 0;       // main key of an intercepted shortcut; swallow its repeats and key-up
        bool g_suppressStart = false;    // a Win shortcut was handled; keep the Win key-up from opening Start
        bool g_pasteVDown = false;       // Ctrl+V down seen; raise PasteDetected on V up
        bool g_pasteInsertDown = false;  // Shift+Insert down seen; raise on Insert up

        bool IsDown(int vk) noexcept
        {
            return (GetAsyncKeyState(vk) & 0x8000) != 0;
        }

        void InjectDummyKey() noexcept
        {
            INPUT inputs[2]{};
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = DummyKey;
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = DummyKey;
            inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, inputs, sizeof(INPUT));
        }

        enum class HookEvent
        {
            Shortcut,
            Paste,
        };

        void RaiseLater(HookEvent which) noexcept
        {
            auto raise = [which]() {
                try
                {
                    auto& hook = KeyboardHook::Instance();
                    if (which == HookEvent::Shortcut)
                    {
                        hook.ShortcutPressed();
                    }
                    else
                    {
                        hook.PasteDetected();
                    }
                }
                catch (...)
                {
                    OutputDebugStringW(L"Eikon: exception in a keyboard hook event handler\n");
                }
            };

            try
            {
                if (auto const& queue = Queue(); queue && queue.TryEnqueue(raise))
                {
                    return;
                }
            }
            catch (...)
            {
            }
            // No dispatcher queue (should not happen): raise synchronously.
            raise();
        }
    }

    KeyboardHook& KeyboardHook::Instance()
    {
        static KeyboardHook instance;
        return instance;
    }

    void KeyboardHook::SetBlockedShortcuts(std::vector<KeyShortcut> shortcuts)
    {
        if (m_blocked == shortcuts)
        {
            return;
        }
        m_blocked = std::move(shortcuts);
        UpdateHook();
    }

    void KeyboardHook::SetShortcuts(std::vector<KeyShortcut> shortcuts)
    {
        if (m_shortcuts == shortcuts)
        {
            return;
        }
        m_shortcuts = std::move(shortcuts);
        g_swallowingKey = 0;
        if (m_shortcuts.empty())
        {
            g_suppressStart = false;
        }
        UpdateHook();
    }

    void KeyboardHook::EnablePasteDetection(bool enable)
    {
        if (m_detectPaste == enable)
        {
            return;
        }
        m_detectPaste = enable;
        g_pasteVDown = false;
        g_pasteInsertDown = false;
        UpdateHook();
    }

    void KeyboardHook::UpdateHook()
    {
        const bool needed = !m_shortcuts.empty() || !m_blocked.empty() || m_detectPaste;
        if (needed && !m_hook)
        {
            try
            {
                Queue() = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
            }
            catch (...)
            {
                Queue() = nullptr;
            }
            g_leftWinDown = IsDown(VK_LWIN);
            g_rightWinDown = IsDown(VK_RWIN);
            m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, &KeyboardHook::HookProc, GetModuleHandleW(nullptr), 0);
            if (!m_hook)
            {
                OutputDebugStringW(L"Eikon: SetWindowsHookExW(WH_KEYBOARD_LL) failed\n");
            }
        }
        else if (!needed && m_hook)
        {
            UnhookWindowsHookEx(m_hook);
            m_hook = nullptr;
            g_swallowingKey = 0;
            g_suppressStart = false;
            g_pasteVDown = false;
            g_pasteInsertDown = false;
        }
    }

    LRESULT CALLBACK KeyboardHook::HookProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HC_ACTION)
        {
            try
            {
                auto const* info = reinterpret_cast<KBDLLHOOKSTRUCT const*>(lParam);
                auto const& self = Instance();
                const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
                const bool up = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
                const bool injected = (info->flags & LLKHF_INJECTED) != 0;
                const DWORD vk = info->vkCode;

                // Injected input still counts for shortcuts (remote desktop tools such as Parsec inject
                // the user's keys); only our own dummy key is skipped. Paste detection below ignores all
                // injected input so that Eikon's own Ctrl+V does not count as a user paste.
                if (!(injected && vk == DummyKey))
                {
                    if (vk == VK_LWIN && (down || up))
                    {
                        g_leftWinDown = down;
                    }
                    else if (vk == VK_RWIN && (down || up))
                    {
                        g_rightWinDown = down;
                    }

                    // ---- Win-key shortcuts Windows keeps for itself (Win+V, Win+Shift+C, ...) ----
                    if (vk == g_swallowingKey && (down || up))
                    {
                        if (up)
                        {
                            g_swallowingKey = 0;
                        }
                        return 1; // auto-repeat or release of a swallowed shortcut key
                    }
                    if (down && (!self.m_shortcuts.empty() || !self.m_blocked.empty()))
                    {
                        const bool win = g_leftWinDown || g_rightWinDown || IsDown(VK_LWIN) || IsDown(VK_RWIN);
                        if (win)
                        {
                            const bool ctrl = IsDown(VK_CONTROL);
                            const bool alt = IsDown(VK_MENU);
                            const bool shift = IsDown(VK_SHIFT);
                            auto matches = [&](KeyShortcut const& shortcut) {
                                return shortcut.key == vk &&
                                       HasFlag(shortcut.modifiers, Modifiers::Ctrl) == ctrl &&
                                       HasFlag(shortcut.modifiers, Modifiers::Alt) == alt &&
                                       HasFlag(shortcut.modifiers, Modifiers::Shift) == shift;
                            };
                            auto swallow = [&](bool raise) {
                                g_swallowingKey = vk;
                                g_suppressStart = true;
                                InjectDummyKey(); // while Win is still held: Start stays closed on release
                                if (raise)
                                {
                                    RaiseLater(HookEvent::Shortcut);
                                }
                            };
                            for (auto const& shortcut : self.m_shortcuts)
                            {
                                if (matches(shortcut))
                                {
                                    swallow(true);
                                    return 1;
                                }
                            }
                            for (auto const& shortcut : self.m_blocked)
                            {
                                if (matches(shortcut))
                                {
                                    swallow(false);
                                    return 1;
                                }
                            }
                        }
                    }
                    if ((vk == VK_LWIN || vk == VK_RWIN) && up && g_suppressStart)
                    {
                        if (!g_leftWinDown && !g_rightWinDown)
                        {
                            g_suppressStart = false;
                            InjectDummyKey();
                        }
                    }

                    // ---- Paste detection (Ctrl+V / Shift+Insert, raised on key up) ----
                    if (self.m_detectPaste && !injected)
                    {
                        if (vk == VkV)
                        {
                            if (down)
                            {
                                const bool ctrl = IsDown(VK_CONTROL);
                                const bool others = IsDown(VK_MENU) || IsDown(VK_SHIFT) || IsDown(VK_LWIN) || IsDown(VK_RWIN);
                                if (ctrl && !others)
                                {
                                    g_pasteVDown = true;
                                }
                            }
                            else if (up && g_pasteVDown)
                            {
                                g_pasteVDown = false;
                                RaiseLater(HookEvent::Paste);
                            }
                        }
                        else if (vk == VK_INSERT)
                        {
                            if (down)
                            {
                                const bool shift = IsDown(VK_SHIFT);
                                const bool others = IsDown(VK_CONTROL) || IsDown(VK_MENU) || IsDown(VK_LWIN) || IsDown(VK_RWIN);
                                if (shift && !others)
                                {
                                    g_pasteInsertDown = true;
                                }
                            }
                            else if (up && g_pasteInsertDown)
                            {
                                g_pasteInsertDown = false;
                                RaiseLater(HookEvent::Paste);
                            }
                        }
                    }
                }
            }
            catch (...)
            {
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
}
