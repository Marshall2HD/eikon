#pragma once

// A screenshot pinned to the screen. A borderless topmost Win32 window with the DWM shadow and rounded corners,
// drawn with Direct2D. Aspect-locked resizing with the 0.5×/1×/2× snaps, wheel and key zoom,
// transparency, the context menu and the hover toolbar. Any number
// can exist; AppController owns them. UI thread only.

#include "Core/Bitmap.h"
#include "Core/Geometry.h"
#include "Platform/Graphics.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace Eikon
{
    class PinWindow
    {
    public:
        // A pin of `image` centred on `center` (screen pixels), ordered front and made key.
        // `closed` runs (once) after the window is destroyed; the owner may delete the pin then.
        PinWindow(std::shared_ptr<Core::Bitmap const> image, POINT center, std::function<void(PinWindow*)> closed);
        PinWindow(PinWindow const&) = delete;
        PinWindow& operator=(PinWindow const&) = delete;
        ~PinWindow();

        HWND Handle() const noexcept { return m_hwnd; }
        // Closes the pin.
        void Close();

    private:
        enum class Control { None, Close, Dragger, Edit, Copy };

        static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam);
        void Render();
        bool EnsureTarget();
        void ApplyFrame();

        // DIP geometry of the window's client area (y down).
        D2D1_SIZE_F ClientDips() const;
        D2D1_RECT_F ToolbarRect() const;
        D2D1_RECT_F ControlRect(Control control) const;
        Control HitControl(POINT client) const;
        double ScreenWidthDips() const;

        void SetZoom(std::int64_t percent);
        void ZoomStep(bool zoomIn);
        void ResizeAboutCentre(double widthDips);
        void UpdateZoom();
        void SetTransparency(int percent);
        void ShowToolbar(bool show);
        void ShowContextMenu(POINT screen);
        void Edit();
        void Copy();
        void DragOut();

        HWND m_hwnd = nullptr;
        std::shared_ptr<Core::Bitmap const> m_image;
        std::function<void(PinWindow*)> m_closed;
        Core::Size m_original;            // the 1:1 size in DIPs (image pixels / monitor scale)
        double m_scale = 1.0;             // monitor scale of the window
        std::int64_t m_zoom = 100;        // currentZoom
        int m_transparency = 100;
        bool m_toolbarVisible = false;
        bool m_tracking = false;
        Control m_hover = Control::None;
        Control m_pressed = Control::None;
        bool m_draggerDown = false;
        POINT m_draggerDownPoint{};
        int m_wheelAccumulator = 0;
        std::chrono::steady_clock::time_point m_opened = std::chrono::steady_clock::now();

        winrt::com_ptr<ID2D1HwndRenderTarget> m_target;
        winrt::com_ptr<ID2D1Bitmap> m_bitmap;    // the image on m_target
    };
}
