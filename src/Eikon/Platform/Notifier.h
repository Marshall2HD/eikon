#pragma once

// Notifications in three presentations (notificationType):
//   "custom" (default)  Eikon's own notification window: a 360×54 card that slides
//                       down at the top centre of a monitor, pauses while hovered, fades out after
//                       `seconds`, and can be dragged away.
//   "system"            a Windows app notification (toast); when Windows won't show them, the custom
//                       window with "\nP.S. Eikon doesn't have permission to show System Notifications".
//   "none"              nothing (the beep still sounds).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <windows.h>

namespace Eikon
{
    struct Notification
    {
        std::wstring title;
        std::wstring message;
        bool warning = false;          // 24-notification-warning icon instead of the logo
        double seconds = 3.0;          // default; errors and URL messages pass 5
        bool beep = false;             // MessageBeep(MB_ICONWARNING), also with "none"
        std::wstring buttonTitle;      // optional action button
        std::function<void()> action;  // notificationAction (the button; a toast click with "system")
        std::optional<std::uint32_t> swatch; // 0xRRGGBB colour shown instead of the icon (colour copies)
    };

    namespace Notifier
    {
        // Registers for app notifications (call once at startup, on the UI thread).
        void Initialize();
        void Shutdown();
        void Show(Notification notification);
        // Hides the custom window at once.
        void Hide();
        // The monitor of the editor when Eikon last hid
        // it. A notification within 2 s of that appears on that monitor.
        void SetLastActiveMonitor(HMONITOR monitor);
    }
}
