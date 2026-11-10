#pragma once

// The post-capture floating thumbnail. One window for the app's lifetime, reused for every capture.
// A borderless, topmost, per-pixel-alpha Win32 window drawn with Direct2D: rounded panel, the image,
// the close button and grip ("manual" closing), the button row with its tooltip pill, keys
// and dragging the image out as a file. UI thread only.

#include "Core/Document.h"
#include "Core/Geometry.h"
#include "Core/PreviewLayout.h"
#include "Platform/Graphics.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Eikon
{
    class PreviewWindow
    {
    public:
        PreviewWindow() = default;
        PreviewWindow(PreviewWindow const&) = delete;
        PreviewWindow& operator=(PreviewWindow const&) = delete;
        ~PreviewWindow();

        // Loads `document`, sizes and places the window next to the mouse and
        // shows it as the key window. `captureRect` is the drawn capture rect in physical screen
        // pixels; without one (fullscreen, file, clipboard) a rect the image's size ending at the
        // mouse is used.
        void Show(std::shared_ptr<Core::Document> document, std::optional<Core::Rect> captureRect);
        // Closes the window (hide all, Esc, Ctrl+W). The image is kept until the next capture.
        void Hide();
        bool IsVisible() const noexcept;
        HWND Handle() const noexcept { return m_hwnd; }
        std::shared_ptr<Core::Document> const& Document() const noexcept { return m_document; }

    private:
        enum class Button { None, Close, Edit, Copy, Save, Upload, Ocr, Pin };
        struct ButtonInfo
        {
            Button id;
            std::wstring icon;      // asset name
            std::wstring tooltip;   // "Copy (Ctrl+C)"
            D2D1_RECT_F rect;       // DIPs
        };

        static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam);
        bool EnsureWindow();
        void Layout();
        void Render();
        void PrepareThumbnail();
        Button HitButton(POINT client) const;
        bool InImage(POINT client) const;
        bool InTopLeftArrowZone(POINT client) const;
        void Invoke(Button button);
        bool OnKey(UINT key);
        void StartDragOut();

        HWND m_hwnd = nullptr;
        Gfx::LayeredSurface m_surface;
        std::shared_ptr<Core::Document> m_document;
        winrt::com_ptr<IWICBitmap> m_thumbnail;   // the image scaled to the container, physical pixels
        Core::PreviewLayout::Thumbnail m_layout;
        std::vector<ButtonInfo> m_buttons;
        bool m_autoClosing = false;
        double m_scale = 1.0;
        POINT m_position{};
        SIZE m_pixelSize{};

        Button m_hover = Button::None;
        Button m_pressed = Button::None;
        bool m_tracking = false;
        bool m_imageDown = false;
        POINT m_imageDownPoint{};
    };
}
