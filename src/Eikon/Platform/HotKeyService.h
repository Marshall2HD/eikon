#pragma once

#include "Core/KeyShortcut.h"
#include "MessageWindow.h"

#include <functional>
#include <map>

namespace Eikon
{
    // System-wide hot keys via RegisterHotKey on the MessageWindow (WM_HOTKEY).
    class HotKeyService
    {
    public:
        explicit HotKeyService(MessageWindow& window);
        ~HotKeyService();

        // Registers (or re-registers) `id`. Returns false if Windows refused (shortcut taken).
        // An empty shortcut just unregisters. MOD_NOREPEAT is always added.
        bool Register(int id, KeyShortcut const& shortcut, std::function<void()> callback);
        void Unregister(int id);

        static constexpr int PopupHotKeyId = 1;

    private:
        MessageWindow& m_window;
        std::uint64_t m_handlerToken = 0;
        std::map<int, std::function<void()>> m_callbacks;
    };
}
