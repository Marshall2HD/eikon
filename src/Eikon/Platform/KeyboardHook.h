#pragma once

#include "Core/Event.h"
#include "Core/KeyShortcut.h"

#include <vector>
#include <windows.h>

namespace Eikon
{
    // Low-level keyboard hook (WH_KEYBOARD_LL), installed on the UI thread, used for two things
    // RegisterHotKey cannot do:
    //  * Win-key shortcuts that Windows keeps for itself (Win+V, Win+Shift+C, ...): each shortcut
    //    set with SetShortcuts is swallowed (and the Start menu suppressed on Win key release by
    //    injecting a dummy VK 0xE8 key) and ShortcutPressed is raised instead, so Lucida opens
    //    instead of the Windows feature. Shortcuts set with SetBlockedShortcuts are swallowed the
    //    same way without raising anything (Block Win+C).
    //  * Paste detection for the paste stack: PasteDetected is raised when the user presses Ctrl+V
    //    or Shift+Insert anywhere (raised on key up). Injected input (LLKHF_INJECTED,
    //    i.e. Lucida's own paste) is ignored.
    // The hook is only installed while at least one feature is enabled. The callback must stay
    // fast; events are posted to the UI thread's queue rather than raised inside the hook.
    class KeyboardHook
    {
    public:
        static KeyboardHook& Instance();

        // Win-key shortcuts to intercept (empty = none). Each must include Modifiers::Win.
        void SetShortcuts(std::vector<KeyShortcut> shortcuts);
        void SetBlockedShortcuts(std::vector<KeyShortcut> shortcuts);
        void EnablePasteDetection(bool enable);

        Event<> ShortcutPressed;
        Event<> PasteDetected;

    private:
        KeyboardHook() = default;
        void UpdateHook();
        static LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam);

        HHOOK m_hook = nullptr;
        std::vector<KeyShortcut> m_shortcuts;
        std::vector<KeyShortcut> m_blocked;
        bool m_detectPaste = false;
    };
}
