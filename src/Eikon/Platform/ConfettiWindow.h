#pragma once

// The status icon's confetti (Windows mapping): a click-through, topmost layered
// window over the notification-area icon, drawn for one second from Core::Confetti.

#include <windows.h>

namespace Eikon::ConfettiWindow
{
    // `icon` is the icon's screen rectangle in physical pixels (Shell_NotifyIconGetRect). Black dots
    // on a light taskbar, white on a dark one. A new call restarts the puff.
    void Play(RECT const& icon, bool darkTaskbar);
}
