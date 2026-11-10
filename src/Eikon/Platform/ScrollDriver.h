#pragma once

// Input side of scrolling capture: cursor parking,
// synthesised wheel notches (SendInput), the UI Automation fallback for views whose single notch
// moves too far, and the low-level hooks that stop a capture on a real key or click.
// UI thread only.

#include <windows.h>

#include <atomic>
#include <optional>
#include <thread>

namespace Eikon::ScrollDriver
{
    // Cursor position and move (physical pixels).
    std::optional<POINT> CursorPosition();
    void MoveCursor(POINT point);
    // Where the cursor is parked between scrolls: (main display right - 2, top + 100 pt).
    POINT ParkingPoint();

    // One wheel event per notch at the cursor. direction -1 scrolls down (mouseData -120, the sign
    // of direction * step); `reverse` (scrollingReverseAutoscroll) flips it.
    void PostWheel(int direction, int notches, bool reverse);

    // ScrollPattern.Scroll(NoAmount, SmallIncrement/SmallDecrement) on the nearest scrollable
    // element under `point`, `count` times; VK_DOWN / VK_UP when UI Automation has none.
    void PostAutomationScroll(POINT point, int direction, int count);

    // WH_MOUSE_LL + WH_KEYBOARD_LL while alive, on a thread of their own: a low-level hook runs
    // on the thread that installed it, and the UI thread both calls SendInput (which would then
    // wait for its own hook, letting timers re-enter) and does work that must not delay
    // system-wide input. AnyKeyOrClick: a key down or left/right button down that was not
    // injected (Lucida's own wheel and keys are, during auto-scroll). Escape: any VK_ESCAPE
    // key down (manual mode). Nothing is swallowed; the owner polls Triggered().
    class InputMonitor
    {
    public:
        enum class Trigger
        {
            AnyKeyOrClick,
            Escape,
        };

        explicit InputMonitor(Trigger trigger);
        ~InputMonitor();
        InputMonitor(InputMonitor const&) = delete;
        InputMonitor& operator=(InputMonitor const&) = delete;

        bool Triggered() const noexcept { return m_triggered.load(std::memory_order_relaxed); }

    private:
        static LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam);

        Trigger m_trigger;
        std::atomic<bool> m_triggered{ false };
        std::thread m_thread;
        DWORD m_threadId = 0;
    };
}
