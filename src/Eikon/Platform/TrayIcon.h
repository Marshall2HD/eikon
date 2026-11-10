#pragma once

// Notification-area icon (Shell_NotifyIcon, NOTIFYICON_VERSION_4): status item
// (Windows mapping).
//
// Icons are template images (Assets\Tray\eikon-22*.png, black on transparent), tinted
// white or black to match the taskbar theme and re-rendered on theme and DPI changes. The menu is a
// Win32 popup menu (dark-mode aware) with 16-menu-* images (Assets\Menu\<name>.png) as item
// bitmaps, tinted the same way, and shortcut text right-aligned after a tab.
// The icon is re-added when Explorer restarts (TaskbarCreated).

#include "Core/Event.h"
#include "MessageWindow.h"

#include <windows.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Eikon
{
    class TrayIcon
    {
    public:
        struct MenuItem
        {
            std::wstring text;                 // empty text and no submenu = separator
            std::wstring shortcut;             // shown right-aligned ("Ctrl+Shift+1"); empty = none
            std::wstring icon;                 // asset name under Assets\Menu ("16-menu-area"); empty = none
            bool templateIcon = true;          // tinted for the menu theme (false for coloured images)
            std::function<void()> action;
            bool checked = false;
            bool enabled = true;
            std::vector<MenuItem> submenu;

            static MenuItem Separator() { return {}; }
        };

        explicit TrayIcon(MessageWindow& window);
        ~TrayIcon();

        void Show();
        void Hide();
        bool IsVisible() const noexcept { return m_visible; }
        void SetTooltip(std::wstring const& text);   // truncated to 127 chars

        // ---- Animated status icon ----
        // Busy: eikon-22-animated-1 … -5, one frame every 50 ms, until EndBusy.
        void BeginBusy();
        // Success: stop, eikon-22-ok for 1.5 s with the confetti puff, then eikon-22. Failure:
        // straight back to eikon-22.
        void EndBusy(bool success);
        // Shows eikon-22-ok for 1.5 s and the confetti (also after a capture that shows
        // nothing).
        void ShowOk();

        // Screen rectangle of the icon (Shell_NotifyIconGetRect), in physical pixels.
        std::optional<RECT> Rect() const;

        // Shows the menu from MenuProvider at `point` (screen pixels), or at the cursor.
        void ShowMenu(std::optional<POINT> point = std::nullopt);

        // Left click or keyboard select. AppController opens the menu (or the Library in library mode).
        Event<POINT> Invoked;
        // Builds the menu each time it opens (right click, context-menu key, or ShowMenu).
        std::function<std::vector<MenuItem>()> MenuProvider;

    private:
        enum class Frame { Normal, Ok, Busy1, Busy2, Busy3, Busy4, Busy5 };

        std::optional<LRESULT> OnMessage(UINT message, WPARAM wParam, LPARAM lParam);
        void SetFrame(Frame frame);
        HICON IconFor(Frame frame);
        void ClearIconCache();
        HBITMAP MenuBitmap(std::wstring const& name, bool templateIcon);
        void ClearMenuBitmaps();
        HMENU BuildMenu(std::vector<MenuItem> const& items, std::vector<std::function<void()>>& actions);
        bool Notify(DWORD message);
        void Add();

        MessageWindow& m_window;
        std::uint64_t m_handlerToken = 0;
        bool m_visible = false;
        std::wstring m_tooltip;
        Frame m_frame = Frame::Normal;
        std::map<Frame, HICON> m_icons;
        std::map<std::wstring, HBITMAP> m_menuBitmaps;
        UINT m_taskbarCreatedMessage = 0;
        ULONGLONG m_lastInvokeTick = 0;
        bool m_busy = false;
    };
}
