#pragma once

// Monitors, screen and window capture. The process is per-monitor DPI
// aware (v2), so every rectangle here is in physical pixels of the virtual desktop.

#include "Core/Bitmap.h"

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

namespace Eikon::ScreenCapture
{
    struct Monitor
    {
        HMONITOR handle = nullptr;
        RECT bounds{};          // physical pixels
        RECT workArea{};        // physical pixels, without the taskbar
        double scale = 1.0;     // effective DPI / 96: dpr
        bool primary = false;
        std::wstring device;    // \\.\DISPLAY1
    };

    // Every active monitor, in EnumDisplayMonitors order.
    std::vector<Monitor> EnumerateMonitors();
    // The primary monitor.
    std::optional<Monitor> PrimaryMonitor();
    // The monitor containing `point` (nearest when none does).
    std::optional<Monitor> MonitorFromPoint(POINT point);
    std::optional<Monitor> MonitorFromHandle(HMONITOR handle);
    // The main monitor: the foreground window's, else the cursor's.
    std::optional<Monitor> MainMonitor();

    // Copies the monitor's pixels (GDI BitBlt with CAPTUREBLT, so layered windows are included) into
    // an opaque BGRA8 bitmap with dpr = monitor scale and screenRect = monitor bounds. With
    // `includeCursor` the mouse pointer is drawn into the image (captureCursor "include"; "auto" and
    // "exclude" leave it out). Windows with
    // WDA_EXCLUDEFROMCAPTURE (Lucida's overlays) are left out. nullopt on failure.
    std::optional<Core::Bitmap> CaptureMonitor(Monitor const& monitor, bool includeCursor);
    // The same for any rectangle of the virtual desktop (Repeat Area; may span monitors), with
    // dpr = the scale of the monitor under its centre. Parts outside every monitor are black.
    std::optional<Core::Bitmap> CaptureRect(RECT const& rect);

    // ---- Windows ----

    struct WindowInfo
    {
        HWND handle = nullptr;
        RECT bounds{};          // DWMWA_EXTENDED_FRAME_BOUNDS, physical pixels
        std::wstring title;     // GetWindowText
        std::wstring app;       // the process's FileDescription, else its exe name (owner name)
        bool opaque = true;     // kCGWindowAlpha == 1: not a layered window with alpha < 255
        double scale = 1.0;     // scale of the window's monitor
    };

    // "layer 0, on screen" windows, front to back: visible, not cloaked, not minimised, not
    // a tool window, not topmost, not the desktop or the taskbar, not Lucida's capture overlays.
    // Owned windows (dialogs) are included.
    std::vector<WindowInfo> NormalWindows();
    // The active window: the first normal window larger than 70 × 70 pt.
    std::optional<WindowInfo> FrontmostWindow();
    // The window under `point` for Any Window capture: the first visible top-level window (topmost
    // ones included, Lucida's overlays and the desktop excluded) whose bounds contain it. Without
    // `withNames` the title and owner are left empty (cheap enough for every mouse move).
    std::optional<WindowInfo> WindowAtPoint(POINT point, bool withNames = true);
    std::optional<WindowInfo> DescribeWindow(HWND window);
    // Window attribution: owner and title of the first normal window overlapping `monitorBounds`
    // (the monitor centre lies in the window, or the window centre lies on the monitor).
    std::optional<WindowInfo> AttributeArea(RECT const& area);

    // The window's own pixels, unoccluded (PrintWindow PW_RENDERFULLCONTENT), cropped to its DWM
    // bounds, opaque; dpr = its monitor's scale. Falls back to the screen pixels under the bounds
    // when PrintWindow fails or draws nothing. nullopt on failure.
    std::optional<Core::Bitmap> CaptureWindow(WindowInfo const& window);

    // The desktop wallpaper of the primary monitor as the desktop shows it (IDesktopWallpaper path,
    // position mode and background colour), at that monitor's pixel size. Empty on failure.
    Core::Bitmap PrimaryWallpaper();

    // Overlay windows register here so window lookups and captures skip them.
    void RegisterOverlay(HWND window);
    void UnregisterOverlay(HWND window);
    bool IsOverlay(HWND window);
}
