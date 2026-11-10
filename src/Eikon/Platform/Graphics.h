#pragma once

// Direct2D/DirectWrite/WIC helpers shared by Eikon's custom-drawn popups: the post-capture
// thumbnail, pins and the notification window.
// UI thread only.

#include "Core/Bitmap.h"
#include "Core/Geometry.h"
#include "Platform/Candy.h"

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite.h>
#include <wincodec.h>

#include <functional>
#include <string>
#include <string_view>

namespace Eikon::Gfx
{
    ID2D1Factory1* Factory();
    IDWriteFactory* DWrite();
    IWICImagingFactory* Wic();

    // EffectiveAppearance: dark when Windows' app mode is dark (AppsUseLightTheme = 0).
    bool IsDarkMode();
    // The monitor scale for `window` (GetDpiForWindow / 96).
    double ScaleFor(HWND window);
    double ScaleForMonitor(HMONITOR monitor);

    // Assets\Preview\<name>.png (or <name>-dark.png when `dark` and it exists), PBGRA, cached.
    winrt::com_ptr<IWICBitmapSource> Asset(std::wstring_view name, bool dark = false);
    // A WIC bitmap holding a copy of `bitmap`'s pixels (PBGRA).
    winrt::com_ptr<IWICBitmap> FromBitmap(Core::Bitmap const& bitmap);

    // "Segoe UI Variable Text" when installed (Windows 11), else "Segoe UI": system font.
    std::wstring const& SystemFontFamily();
    winrt::com_ptr<IDWriteTextFormat> TextFormat(float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);

    inline D2D1_COLOR_F Rgba(float r, float g, float b, float a) noexcept { return D2D1::ColorF(r, g, b, a); }
    inline D2D1_RECT_F ToD2D(Core::Rect const& r) noexcept
    {
        return D2D1::RectF(static_cast<float>(r.Left()), static_cast<float>(r.Top()), static_cast<float>(r.Right()),
                           static_cast<float>(r.Bottom()));
    }

    inline D2D1_COLOR_F FromArgb(Candy::Argb c, float alpha = 1.0f) noexcept
    {
        return D2D1::ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f * alpha);
    }
    // A Win32 system colour (GetSysColor index) for the high-contrast paths.
    D2D1_COLOR_F SystemColor(int index, float alpha = 1.0f);
    // "Segoe UI Variable Display" when installed, else "Segoe UI": Candy's display-weight headings.
    winrt::com_ptr<IDWriteTextFormat> DisplayTextFormat(float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_BOLD);

    // Candy's floating-card shadow (0 14 34 −14, #3B2A8A α 0.33 / black α 0.67 in dark) and its 1-px hairline ring.
    void DrawCandyShadow(ID2D1DeviceContext* context, D2D1_RECT_F rect, float radius, bool dark);
    // The colour fields of Candy's first-time surfaces: peach top-left, lilac top-right, mint at the bottom over the
    // base colour, clipped to the rounded rect.
    void FillCandyFields(ID2D1DeviceContext* context, D2D1_RECT_F rect, float radius, bool dark);

    void FillRoundedRect(ID2D1DeviceContext* context, D2D1_RECT_F rect, float radius, D2D1_COLOR_F color);
    // Stroke centred on the rect edge, inset by half its width.
    void StrokeRoundedRect(ID2D1DeviceContext* context, D2D1_RECT_F rect, float radius, D2D1_COLOR_F color, float width);
    // The shadow of a rounded rect: Gaussian blur with σ = radius / 2, offset in DIPs (y down).
    void DrawShadow(ID2D1DeviceContext* context, D2D1_RECT_F rect, float cornerRadius, D2D1_COLOR_F color, D2D1_POINT_2F offset,
                    float blurRadius);
    // Draws a WIC bitmap (asset) into `destination` (DIPs).
    void DrawAsset(ID2D1DeviceContext* context, IWICBitmapSource* source, D2D1_RECT_F destination, float opacity = 1.0f);

    // Software-rendered per-pixel-alpha content for a WS_EX_LAYERED window (UpdateLayeredWindow).
    // Drawing is in DIPs; `scale` maps them to physical pixels.
    class LayeredSurface
    {
    public:
        LayeredSurface() = default;
        LayeredSurface(LayeredSurface const&) = delete;
        LayeredSurface& operator=(LayeredSurface const&) = delete;
        ~LayeredSurface();

        // Renders `draw` into a `pixelSize` surface and shows it at `position` (screen pixels).
        void Render(HWND window, POINT position, SIZE pixelSize, double scale, BYTE alpha,
                    std::function<void(ID2D1DeviceContext*)> const& draw);
        // Re-shows the last frame with another constant alpha and/or position.
        void Update(HWND window, POINT position, BYTE alpha);

    private:
        void Release();

        HDC m_dc = nullptr;
        HBITMAP m_bitmap = nullptr;
        HGDIOBJ m_oldBitmap = nullptr;
        void* m_bits = nullptr;
        SIZE m_size{};
    };
}
