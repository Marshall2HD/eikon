#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace Eikon
{
    // Hidden top-level (not message-only, so it receives the TaskbarCreated broadcast) Win32 window
    // created on the UI thread. Platform services hang their Win32 messages off it: clipboard
    // updates, WM_HOTKEY, tray icon callbacks.
    class MessageWindow
    {
    public:
        using Handler = std::function<std::optional<LRESULT>(UINT message, WPARAM wParam, LPARAM lParam)>;

        MessageWindow();
        ~MessageWindow();
        MessageWindow(MessageWindow const&) = delete;
        MessageWindow& operator=(MessageWindow const&) = delete;

        HWND Handle() const noexcept { return m_hwnd; }

        // Handlers are invoked in registration order; the first one returning a value wins.
        std::uint64_t AddHandler(Handler handler);
        void RemoveHandler(std::uint64_t token);

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        HWND m_hwnd = nullptr;
        std::vector<std::pair<std::uint64_t, Handler>> m_handlers;
        std::uint64_t m_lastToken = 0;
    };
}
