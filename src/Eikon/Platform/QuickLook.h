#pragma once

// The Library's Quick Look panel (QuickLookWinController): a borderless,
// non-activating, topmost window that shows the full image with rounded corners and a shadow, left
// of the Library window or centred on the screen, with an optional 20% black curtain behind it
// (libQLCurtain). Space and Esc are handled by the Library, which keeps the focus. UI thread only.

#include <windows.h>

#include <string>

namespace Eikon
{
    class QuickLookPanel
    {
    public:
        QuickLookPanel() = default;
        ~QuickLookPanel();
        QuickLookPanel(QuickLookPanel const&) = delete;
        QuickLookPanel& operator=(QuickLookPanel const&) = delete;

        // `pixelWidth`/`pixelHeight` and `dpr` give the image size in points;
        // `caller` is the Library window. Returns false when the image cannot be decoded.
        bool Show(std::wstring const& path, double pixelWidth, double pixelHeight, double dpr, HWND caller, bool curtain);
        void Hide();
        bool IsVisible() const noexcept;

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        void Paint(HWND hwnd);
        void EnsureWindows();
        void ReleaseBitmap();

        HWND m_hwnd = nullptr;
        HWND m_curtain = nullptr;
        HBITMAP m_bitmap = nullptr;
        int m_width = 0;
        int m_height = 0;
    };
}
