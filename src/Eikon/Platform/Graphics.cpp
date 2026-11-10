#include "pch.h"
#include "Platform/Graphics.h"

#include <d2d1effects.h>

#include <map>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dxguid.lib") // CLSID_D2D1Shadow, CLSID_D2D1GaussianBlur

namespace Eikon::Gfx
{
    namespace
    {
        std::wstring ExeDirectory()
        {
            std::wstring path(MAX_PATH, L'\0');
            while (true)
            {
                DWORD const length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
                if (length == 0)
                {
                    return {};
                }
                if (length < path.size())
                {
                    path.resize(length);
                    break;
                }
                path.resize(path.size() * 2);
            }
            auto const slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
        }

        winrt::com_ptr<IWICBitmapSource> LoadPng(std::wstring const& path)
        {
            auto* wic = Wic();
            if (!wic || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                return nullptr;
            }
            winrt::com_ptr<IWICBitmapDecoder> decoder;
            winrt::com_ptr<IWICBitmapFrameDecode> frame;
            winrt::com_ptr<IWICBitmapSource> converted;
            winrt::com_ptr<IWICBitmap> cached;
            if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.put())) ||
                FAILED(decoder->GetFrame(0, frame.put())) ||
                FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppPBGRA, frame.get(), converted.put())) ||
                FAILED(wic->CreateBitmapFromSource(converted.get(), WICBitmapCacheOnLoad, cached.put())))
            {
                return nullptr;
            }
            return cached;
        }
    }

    ID2D1Factory1* Factory()
    {
        static winrt::com_ptr<ID2D1Factory1> factory = [] {
            winrt::com_ptr<ID2D1Factory1> f;
            D2D1_FACTORY_OPTIONS options{};
            D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, f.put_void());
            return f;
        }();
        return factory.get();
    }

    IDWriteFactory* DWrite()
    {
        static winrt::com_ptr<IDWriteFactory> factory = [] {
            winrt::com_ptr<IDWriteFactory> f;
            DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(f.put()));
            return f;
        }();
        return factory.get();
    }

    IWICImagingFactory* Wic()
    {
        static winrt::com_ptr<IWICImagingFactory> factory = [] {
            winrt::com_ptr<IWICImagingFactory> f;
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(f.put()));
            return f;
        }();
        return factory.get();
    }

    bool IsDarkMode()
    {
        DWORD value = 1;
        DWORD size = sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                         RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        {
            return false;
        }
        return value == 0;
    }

    double ScaleFor(HWND window)
    {
        UINT const dpi = window ? GetDpiForWindow(window) : 0;
        return dpi ? dpi / 96.0 : 1.0;
    }

    double ScaleForMonitor(HMONITOR monitor)
    {
        UINT dpiX = 96;
        UINT dpiY = 96;
        if (monitor && SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX > 0)
        {
            return dpiX / 96.0;
        }
        return 1.0;
    }

    winrt::com_ptr<IWICBitmapSource> Asset(std::wstring_view name, bool dark)
    {
        static std::map<std::wstring, winrt::com_ptr<IWICBitmapSource>, std::less<>> cache;
        std::wstring const key = std::wstring{ name } + (dark ? L"-dark" : L"");
        if (auto it = cache.find(key); it != cache.end())
        {
            return it->second;
        }
        std::wstring const folder = ExeDirectory() + L"\\Assets\\Preview\\";
        winrt::com_ptr<IWICBitmapSource> source;
        if (dark)
        {
            source = LoadPng(folder + key + L".png");
        }
        if (!source)
        {
            source = LoadPng(folder + std::wstring{ name } + L".png");
        }
        if (!source)
        {
            OutputDebugStringW((L"Eikon: missing asset " + key + L"\n").c_str());
        }
        cache.emplace(key, source);
        return source;
    }

    winrt::com_ptr<IWICBitmap> FromBitmap(Core::Bitmap const& bitmap)
    {
        winrt::com_ptr<IWICBitmap> result;
        if (bitmap.IsEmpty() || !Wic())
        {
            return result;
        }
        Wic()->CreateBitmapFromMemory(static_cast<UINT>(bitmap.width), static_cast<UINT>(bitmap.height), GUID_WICPixelFormat32bppPBGRA,
                                      static_cast<UINT>(bitmap.Stride()), static_cast<UINT>(bitmap.pixels.size()),
                                      const_cast<BYTE*>(bitmap.pixels.data()), result.put());
        return result;
    }

    std::wstring const& SystemFontFamily()
    {
        static std::wstring const family = [] {
            winrt::com_ptr<IDWriteFontCollection> fonts;
            if (DWrite() && SUCCEEDED(DWrite()->GetSystemFontCollection(fonts.put())))
            {
                UINT32 index = 0;
                BOOL exists = FALSE;
                if (SUCCEEDED(fonts->FindFamilyName(L"Segoe UI Variable Text", &index, &exists)) && exists)
                {
                    return std::wstring{ L"Segoe UI Variable Text" };
                }
            }
            return std::wstring{ L"Segoe UI" };
        }();
        return family;
    }

    winrt::com_ptr<IDWriteTextFormat> TextFormat(float size, DWRITE_FONT_WEIGHT weight)
    {
        winrt::com_ptr<IDWriteTextFormat> format;
        if (DWrite())
        {
            DWrite()->CreateTextFormat(SystemFontFamily().c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                       L"en-us", format.put());
        }
        return format;
    }

    D2D1_COLOR_F SystemColor(int index, float alpha)
    {
        COLORREF const c = ::GetSysColor(index);
        return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, alpha);
    }

    winrt::com_ptr<IDWriteTextFormat> DisplayTextFormat(float size, DWRITE_FONT_WEIGHT weight)
    {
        static std::wstring const family = [] {
            winrt::com_ptr<IDWriteFontCollection> fonts;
            if (DWrite() && SUCCEEDED(DWrite()->GetSystemFontCollection(fonts.put())))
            {
                UINT32 index = 0;
                BOOL exists = FALSE;
                if (SUCCEEDED(fonts->FindFamilyName(L"Segoe UI Variable Display", &index, &exists)) && exists)
                {
                    return std::wstring{ L"Segoe UI Variable Display" };
                }
            }
            return std::wstring{ L"Segoe UI" };
        }();
        winrt::com_ptr<IDWriteTextFormat> format;
        if (DWrite())
        {
            DWrite()->CreateTextFormat(family.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us",
                                       format.put());
        }
        return format;
    }

    void DrawCandyShadow(ID2D1DeviceContext* context, D2D1_RECT_F rect, float radius, bool dark)
    {
        // box-shadow: 0 14px 34px −14px: the shape shrunk by 14, 14 down, blur 34 (σ 17).
        D2D1_RECT_F const spread{ rect.left + 14, rect.top + 14, rect.right - 14, rect.bottom - 14 };
        DrawShadow(context, spread, (std::max)(0.0f, radius - 14), FromArgb(dark ? Candy::kShadowDark : Candy::kShadowLight), { 0, 14 }, 34);
        // 0 0 0 1px ring: ink α 0.08 (white α 0.08 in dark), just outside the card.
        D2D1_RECT_F const ring{ rect.left - 1, rect.top - 1, rect.right + 1, rect.bottom + 1 };
        StrokeRoundedRect(context, ring, radius + 1, dark ? D2D1::ColorF(1, 1, 1, 0.08f) : D2D1::ColorF(0.10f, 0.08f, 0.19f, 0.08f), 1);
    }

    void FillCandyFields(ID2D1DeviceContext* context, D2D1_RECT_F rect, float radius, bool dark)
    {
        auto const& candy = Candy::For(dark);
        FillRoundedRect(context, rect, radius, FromArgb(candy.base));
        winrt::com_ptr<ID2D1RoundedRectangleGeometry> clip;
        if (FAILED(Factory()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect, radius, radius), clip.put())))
        {
            return;
        }
        float const w = rect.right - rect.left;
        float const h = rect.bottom - rect.top;
        struct Field
        {
            Candy::Argb color;
            float cx, cy, rx, ry;
        };
        // radial-gradient(rx ry at cx cy, colour 0%, transparent 62%).
        Field const fields[] = { { candy.peach, 0.08f, 0.08f, 0.55f, 0.75f },
                                 { candy.lilac, 0.94f, 0.14f, 0.55f, 0.75f },
                                 { candy.mint, 0.55f, 1.12f, 0.70f, 0.80f } };
        for (auto const& field : fields)
        {
            D2D1_GRADIENT_STOP const stops[] = { { 0.0f, FromArgb(field.color) }, { 0.62f, FromArgb(field.color, 0.0f) } };
            winrt::com_ptr<ID2D1GradientStopCollection> collection;
            winrt::com_ptr<ID2D1RadialGradientBrush> brush;
            if (FAILED(context->CreateGradientStopCollection(stops, 2, collection.put())))
            {
                continue;
            }
            D2D1_POINT_2F const center{ rect.left + field.cx * w, rect.top + field.cy * h };
            if (SUCCEEDED(context->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(center, { 0, 0 }, field.rx * w, field.ry * h),
                                                             collection.get(), brush.put())))
            {
                context->FillGeometry(clip.get(), brush.get());
            }
        }
    }

    void FillRoundedRect(ID2D1DeviceContext* context, D2D1_RECT_F rect, float radius, D2D1_COLOR_F color)
    {
        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        if (SUCCEEDED(context->CreateSolidColorBrush(color, brush.put())))
        {
            context->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.get());
        }
    }

    void StrokeRoundedRect(ID2D1DeviceContext* context, D2D1_RECT_F rect, float radius, D2D1_COLOR_F color, float width)
    {
        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        if (SUCCEEDED(context->CreateSolidColorBrush(color, brush.put())))
        {
            float const half = width / 2;
            D2D1_RECT_F const inset{ rect.left + half, rect.top + half, rect.right - half, rect.bottom - half };
            float const r = (std::max)(0.0f, radius - half);
            context->DrawRoundedRectangle(D2D1::RoundedRect(inset, r, r), brush.get(), width);
        }
    }

    void DrawShadow(ID2D1DeviceContext* context, D2D1_RECT_F rect, float cornerRadius, D2D1_COLOR_F color, D2D1_POINT_2F offset,
                    float blurRadius)
    {
        // The shape goes into an offscreen bitmap the size of the whole target (in DIPs), which
        // feeds the shadow effect.
        D2D1_SIZE_F const size = context->GetSize();
        winrt::com_ptr<ID2D1BitmapRenderTarget> shape;
        if (FAILED(context->CreateCompatibleRenderTarget(size, shape.put())))
        {
            return;
        }
        shape->BeginDraw();
        shape->Clear(D2D1::ColorF(0, 0, 0, 0));
        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        if (SUCCEEDED(shape->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), brush.put())))
        {
            shape->FillRoundedRectangle(D2D1::RoundedRect(rect, cornerRadius, cornerRadius), brush.get());
        }
        if (FAILED(shape->EndDraw()))
        {
            return;
        }
        winrt::com_ptr<ID2D1Bitmap> bitmap;
        winrt::com_ptr<ID2D1Effect> effect;
        if (FAILED(shape->GetBitmap(bitmap.put())) || FAILED(context->CreateEffect(CLSID_D2D1Shadow, effect.put())))
        {
            return;
        }
        effect->SetInput(0, bitmap.get());
        effect->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, (std::max)(0.01f, blurRadius / 2));
        effect->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(color.r, color.g, color.b, color.a));
        context->DrawImage(effect.get(), offset);
    }

    void DrawAsset(ID2D1DeviceContext* context, IWICBitmapSource* source, D2D1_RECT_F destination, float opacity)
    {
        if (!source)
        {
            return;
        }
        winrt::com_ptr<ID2D1Bitmap> bitmap;
        if (SUCCEEDED(context->CreateBitmapFromWicBitmap(source, nullptr, bitmap.put())))
        {
            context->DrawBitmap(bitmap.get(), destination, opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        }
    }

    // ---- LayeredSurface ----

    LayeredSurface::~LayeredSurface()
    {
        Release();
    }

    void LayeredSurface::Release()
    {
        if (m_dc)
        {
            SelectObject(m_dc, m_oldBitmap);
            DeleteDC(m_dc);
            m_dc = nullptr;
        }
        if (m_bitmap)
        {
            DeleteObject(m_bitmap);
            m_bitmap = nullptr;
        }
        m_bits = nullptr;
        m_size = {};
    }

    void LayeredSurface::Render(HWND window, POINT position, SIZE pixelSize, double scale, BYTE alpha,
                                std::function<void(ID2D1DeviceContext*)> const& draw)
    {
        if (pixelSize.cx <= 0 || pixelSize.cy <= 0 || !Factory())
        {
            return;
        }
        if (!m_dc || m_size.cx != pixelSize.cx || m_size.cy != pixelSize.cy)
        {
            Release();
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = pixelSize.cx;
            info.bmiHeader.biHeight = -pixelSize.cy; // top-down
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            m_dc = CreateCompatibleDC(nullptr);
            m_bitmap = CreateDIBSection(m_dc, &info, DIB_RGB_COLORS, &m_bits, nullptr, 0);
            if (!m_dc || !m_bitmap)
            {
                Release();
                return;
            }
            m_oldBitmap = SelectObject(m_dc, m_bitmap);
            m_size = pixelSize;
        }
        std::memset(m_bits, 0, static_cast<std::size_t>(m_size.cx) * m_size.cy * 4);

        float const dpi = static_cast<float>(96.0 * scale);
        D2D1_RENDER_TARGET_PROPERTIES const properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
        winrt::com_ptr<ID2D1DCRenderTarget> target;
        if (FAILED(Factory()->CreateDCRenderTarget(&properties, target.put())))
        {
            return;
        }
        RECT const bounds{ 0, 0, m_size.cx, m_size.cy };
        if (FAILED(target->BindDC(m_dc, &bounds)))
        {
            return;
        }
        auto context = target.try_as<ID2D1DeviceContext>();
        if (!context)
        {
            return;
        }
        context->BeginDraw();
        context->Clear(D2D1::ColorF(0, 0, 0, 0));
        context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE); // ClearType needs an opaque background
        try
        {
            draw(context.get());
        }
        catch (...)
        {
            OutputDebugStringW(L"Eikon: drawing a layered window failed\n");
        }
        context->EndDraw();
        GdiFlush();
        Update(window, position, alpha);
    }

    void LayeredSurface::Update(HWND window, POINT position, BYTE alpha)
    {
        if (!m_dc || !window)
        {
            return;
        }
        POINT source{ 0, 0 };
        BLENDFUNCTION blend{ AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
        UpdateLayeredWindow(window, nullptr, &position, &m_size, m_dc, &source, 0, &blend, ULW_ALPHA);
    }
}
