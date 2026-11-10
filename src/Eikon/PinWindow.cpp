#include "pch.h"
#include "PinWindow.h"

#include "AppController.h"
#include "Core/Document.h"
#include "Core/PreviewLayout.h"
#include "Core/Settings.h"
#include "Platform/Exporter.h"
#include "Platform/FileDrag.h"
#include "Strings.h"

#include <d2d1effects.h>
#include <windowsx.h>

namespace Eikon
{
    namespace
    {
        constexpr UINT_PTR kHideToolbarTimer = 1;
        constexpr UINT kHideToolbarDelayMs = 400;   // Hides 0.4 s after mouseExited
        constexpr double kResizeBand = 6.0;         // DIPs of edge that resize the window

        enum MenuId : UINT
        {
            kMenuEdit = 1,
            kMenuCopy,
            kMenu50,
            kMenu100,
            kMenu200,
            kMenuTransparency,
            kMenuClose,
        };

        void Log(std::wstring const& text)
        {
            OutputDebugStringW((L"[Eikon] pin: " + text + L"\n").c_str());
        }

        bool Contains(D2D1_RECT_F const& r, double x, double y) noexcept
        {
            return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
        }

        MONITORINFO MonitorInfoFor(HMONITOR monitor)
        {
            MONITORINFO info{ sizeof(info) };
            GetMonitorInfoW(monitor, &info);
            return info;
        }
    }

    PinWindow::PinWindow(std::shared_ptr<Core::Bitmap const> image, POINT center, std::function<void(PinWindow*)> closed)
        : m_image(std::move(image)), m_closed(std::move(closed))
    {
        static ATOM const atom = [] {
            WNDCLASSEXW wc{ sizeof(wc) };
            wc.lpfnWndProc = &PinWindow::Proc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.lpszClassName = L"EikonPin";
            return RegisterClassExW(&wc);
        }();
        if (!atom || !m_image || m_image->IsEmpty())
        {
            return;
        }

        // Image pixels / display scale (1:1 physical pixels); start at 0.5 when it would cover
        // ≥ 75 % of the screen's width or height.
        HMONITOR const monitor = MonitorFromPoint(center, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO const info = MonitorInfoFor(monitor);
        m_scale = Gfx::ScaleForMonitor(monitor);
        m_original = { m_image->width / m_scale, m_image->height / m_scale };
        Core::Size const screen{ (info.rcMonitor.right - info.rcMonitor.left) / m_scale, (info.rcMonitor.bottom - info.rcMonitor.top) / m_scale };
        double const start = Core::PreviewLayout::PinStartScale(m_original, screen);
        m_zoom = static_cast<std::int64_t>(start * 100);
        int const width = (std::max)(1, static_cast<int>(std::lround(m_original.width * start * m_scale)));
        int const height = (std::max)(1, static_cast<int>(std::lround(m_original.height * start * m_scale)));

        // Level 3 (floating) -> topmost; kept off the taskbar; takes keys.
        m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"EikonPin", L"Lucida", WS_POPUP | WS_THICKFRAME, center.x - width / 2,
                                 center.y - height / 2, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), this);
        if (!m_hwnd)
        {
            return;
        }
        ApplyFrame();
        SetWindowPos(m_hwnd, HWND_TOPMOST, center.x - width / 2, center.y - height / 2, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
        Log(L"pin " + std::to_wstring(width) + L"x" + std::to_wstring(height));
    }

    PinWindow::~PinWindow()
    {
        if (m_hwnd)
        {
            m_closed = nullptr;
            DestroyWindow(m_hwnd);
        }
    }

    void PinWindow::Close()
    {
        if (m_hwnd)
        {
            auto const seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_opened).count();
            Log(L"close after " + std::to_wstring(seconds) + L" s");
            DestroyWindow(m_hwnd);
        }
    }

    void PinWindow::ApplyFrame()
    {
        // Borderless with the system shadow and Windows 11's 8 px rounded corners (mainView radius 8);
        // the 1 px accent border is turned off.
        DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
        DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
        COLORREF border = DWMWA_COLOR_NONE;
        DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
        MARGINS const margins{ 0, 0, 0, 1 };
        DwmExtendFrameIntoClientArea(m_hwnd, &margins);
        DWMNCRENDERINGPOLICY policy = m_transparency > 89 ? DWMNCRP_ENABLED : DWMNCRP_DISABLED;
        DwmSetWindowAttribute(m_hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    }

    D2D1_SIZE_F PinWindow::ClientDips() const
    {
        RECT client{};
        GetClientRect(m_hwnd, &client);
        return { static_cast<float>(client.right / m_scale), static_cast<float>(client.bottom / m_scale) };
    }

    D2D1_RECT_F PinWindow::ToolbarRect() const
    {
        // viewToolbar: insets 4, height 36, along the top.
        D2D1_SIZE_F const size = ClientDips();
        return { 4, 4, (std::max)(4.0f, size.width - 4), 40 };
    }

    D2D1_RECT_F PinWindow::ControlRect(Control control) const
    {
        D2D1_RECT_F const bar = ToolbarRect();
        // btnClose at (12, 12) in the toolbar; stackButtons at x 30 (6 after the dot), left inset 4,
        // no spacing: dragger 32×28, edit and copy 24×24, all centred on the toolbar.
        float const stack = bar.left + 30 + 4;
        switch (control)
        {
        case Control::Close: return { bar.left + 12, bar.top + 12, bar.left + 24, bar.top + 24 };
        case Control::Dragger: return { stack, bar.top + 4, stack + 32, bar.top + 32 };
        case Control::Edit: return { stack + 32, bar.top + 6, stack + 56, bar.top + 30 };
        case Control::Copy: return { stack + 56, bar.top + 6, stack + 80, bar.top + 30 };
        case Control::None: break;
        }
        return {};
    }

    PinWindow::Control PinWindow::HitControl(POINT client) const
    {
        if (!m_toolbarVisible)
        {
            return Control::None;
        }
        double const x = client.x / m_scale;
        double const y = client.y / m_scale;
        D2D1_RECT_F const bar = ToolbarRect();
        if (!Contains(bar, x, y))
        {
            return Control::None;
        }
        for (Control c : { Control::Close, Control::Dragger, Control::Edit, Control::Copy })
        {
            if (Contains(ControlRect(c), x, y))
            {
                return c;
            }
        }
        return Control::None;
    }

    double PinWindow::ScreenWidthDips() const
    {
        if (!m_hwnd)
        {
            return 1920; // 3840 / 2 with no screen
        }
        MONITORINFO const info = MonitorInfoFor(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST));
        return (info.rcMonitor.right - info.rcMonitor.left) / m_scale;
    }

    bool PinWindow::EnsureTarget()
    {
        if (m_target)
        {
            return true;
        }
        RECT client{};
        GetClientRect(m_hwnd, &client);
        float const dpi = static_cast<float>(96.0 * m_scale);
        auto const properties =
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
        auto const hwndProperties = D2D1::HwndRenderTargetProperties(
            m_hwnd, D2D1::SizeU(static_cast<UINT32>((std::max)(1L, client.right)), static_cast<UINT32>((std::max)(1L, client.bottom))));
        if (!Gfx::Factory() || FAILED(Gfx::Factory()->CreateHwndRenderTarget(properties, hwndProperties, m_target.put())))
        {
            return false;
        }
        auto const bitmapProperties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        m_target->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(m_image->width), static_cast<UINT32>(m_image->height)), m_image->pixels.data(),
                               static_cast<UINT32>(m_image->Stride()), bitmapProperties, m_bitmap.put());
        return true;
    }

    void PinWindow::Render()
    {
        if (!m_hwnd || !EnsureTarget())
        {
            return;
        }
        auto context = m_target.try_as<ID2D1DeviceContext>();
        if (!context)
        {
            return;
        }
        D2D1_SIZE_F const size = ClientDips();
        D2D1_RECT_F const bounds{ 0, 0, size.width, size.height };
        context->BeginDraw();
        // mainView's black α0.4 sublayer shows through transparent pixels; with an opaque window it is
        // drawn over a mid grey (the desktop behind can't show through).
        context->Clear(D2D1::ColorF(0.3f, 0.3f, 0.3f, 1.0f));
        if (m_bitmap)
        {
            // Nearest-neighbour at ≥ 200 % so enlarged pins show crisp pixels; else the closest
            // to trilinear.
            auto const mode = m_zoom >= 200 ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC;
            context->DrawBitmap(m_bitmap.get(), bounds, 1.0f, mode, nullptr, nullptr);
        }

        if (m_toolbarVisible)
        {
            D2D1_RECT_F const bar = ToolbarRect();
            winrt::com_ptr<ID2D1RoundedRectangleGeometry> clip;
            // Candy: a rounded bar (radius 8, never a capsule).
            Gfx::Factory()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(bar, Candy::kRadiusControl, Candy::kRadiusControl), clip.put());
            winrt::com_ptr<ID2D1Layer> layer;
            context->CreateLayer(nullptr, layer.put());
            context->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.get()), layer.get());

            // The toolbar: blending within the window, dark material: the
            // image under it blurred, a dark tint, then viewToolbarCurtain black α0.25.
            winrt::com_ptr<ID2D1Effect> blur;
            if (m_bitmap && SUCCEEDED(context->CreateEffect(CLSID_D2D1GaussianBlur, blur.put())))
            {
                blur->SetInput(0, m_bitmap.get());
                blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 10.0f * static_cast<float>(m_image->width) / size.width);
                blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
                D2D1_MATRIX_3X2_F old{};
                context->GetTransform(&old);
                // The bitmap is 96 DPI, so one image pixel is one DIP: scale it onto the window.
                context->SetTransform(D2D1::Matrix3x2F::Scale(size.width / m_image->width, size.height / m_image->height) * old);
                context->DrawImage(blur.get());
                context->SetTransform(old);
            }
            // Candy: the blurred image under frosted ink (#1A1530 α 0.8) instead of dark tint.
            Gfx::FillRoundedRect(context.get(), bar, Candy::kRadiusControl, Gfx::FromArgb(Candy::kFrostedInk));

            // btnClose: Candy's coral dot (#FF5A36, darker while pressed; traffic-light red
            // sRGB(1, 0.37, 0.34)); 8-close on hover.
            D2D1_RECT_F const dot = ControlRect(Control::Close);
            bool const pressed = m_pressed == Control::Close && m_hover == Control::Close;
            Gfx::FillRoundedRect(context.get(), dot, 6, pressed ? Gfx::Rgba(0.78f, 0.27f, 0.16f, 1) : Gfx::FromArgb(Candy::kCoral));
            if (m_hover == Control::Close)
            {
                float const cx = (dot.left + dot.right) / 2;
                float const cy = (dot.top + dot.bottom) / 2;
                Gfx::DrawAsset(context.get(), Gfx::Asset(L"8-close").get(), { cx - 4, cy - 4, cx + 4, cy + 4 });
            }

            // Each image at its size in points: 16 × 16, the dragger's ico-16-drag-white 28 × 16.
            auto drawIcon = [&](Control control, wchar_t const* name, float width = 16) {
                D2D1_RECT_F const r = ControlRect(control);
                float const cx = (r.left + r.right) / 2;
                float const cy = (r.top + r.bottom) / 2;
                float const opacity = m_pressed == control && m_hover == control ? 0.6f : 1.0f;
                Gfx::DrawAsset(context.get(), Gfx::Asset(name).get(), { cx - width / 2, cy - 8, cx + width / 2, cy + 8 }, opacity);
            };
            drawIcon(Control::Dragger, L"ico-16-drag-white", 28);
            drawIcon(Control::Edit, L"ico-16-edit-white");
            drawIcon(Control::Copy, L"ico-16-copy-white");

            // textZoom: "<N>%", 13 pt, white α0.8, 4 pt into its 24 pt high box after the copy button.
            if (auto format = Gfx::TextFormat(13.0f))
            {
                format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                std::wstring const text = std::to_wstring(m_zoom) + L"%";
                winrt::com_ptr<ID2D1SolidColorBrush> brush;
                context->CreateSolidColorBrush(Gfx::Rgba(1, 1, 1, 0.8f), brush.put());
                float const x = ControlRect(Control::Copy).right + 4;
                context->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format.get(), { x, bar.top + 10, (std::max)(x, bar.right - 12), bar.top + 26 },
                                  brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            context->PopLayer();
        }

        if (context->EndDraw() == D2DERR_RECREATE_TARGET)
        {
            m_bitmap = nullptr;
            m_target = nullptr;
        }
    }

    void PinWindow::UpdateZoom()
    {
        // zoom = Int(max(w·100/origW, h·100/origH)).
        RECT client{};
        GetClientRect(m_hwnd, &client);
        Core::Size const current{ client.right / m_scale, client.bottom / m_scale };
        m_zoom = Core::PreviewLayout::PinZoomPercent(current, m_original);
    }

    void PinWindow::ResizeAboutCentre(double widthDips)
    {
        Core::Size const size = Core::PreviewLayout::PinResize({ widthDips, widthDips * m_original.height / m_original.width }, m_original, ScreenWidthDips());
        RECT window{};
        GetWindowRect(m_hwnd, &window);
        int const w = (std::max)(1, static_cast<int>(std::lround(size.width * m_scale)));
        int const h = (std::max)(1, static_cast<int>(std::lround(size.height * m_scale)));
        int const cx = (window.left + window.right) / 2;
        int const cy = (window.top + window.bottom) / 2;
        SetWindowPos(m_hwnd, nullptr, cx - w / 2, cy - h / 2, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void PinWindow::SetZoom(std::int64_t percent)
    {
        ResizeAboutCentre(m_original.width * static_cast<double>(percent) / 100.0);
        // The window is the rounding of exactly this zoom; keep the number rather than re-deriving it
        // from whole pixels (which could read 124 for 125).
        m_zoom = percent;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void PinWindow::ZoomStep(bool zoomIn)
    {
        if (auto const next = Core::PreviewLayout::PinZoomStep(m_zoom, zoomIn, m_original.width, ScreenWidthDips()))
        {
            SetZoom(*next);
        }
    }

    void PinWindow::SetTransparency(int percent)
    {
        // Opacity t/100; shadow only when t ≥ 90.
        m_transparency = percent;
        LONG_PTR const ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
        if (percent < 100)
        {
            SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
            SetLayeredWindowAttributes(m_hwnd, 0, static_cast<BYTE>(std::lround(percent * 255.0 / 100.0)), LWA_ALPHA);
        }
        else if (ex & WS_EX_LAYERED)
        {
            SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
        }
        ApplyFrame();
        RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
    }

    void PinWindow::ShowToolbar(bool show)
    {
        if (show)
        {
            KillTimer(m_hwnd, kHideToolbarTimer);
        }
        if (m_toolbarVisible != show)
        {
            m_toolbarVisible = show;
            if (!show)
            {
                m_hover = Control::None;
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

    void PinWindow::ShowContextMenu(POINT screen)
    {
        // No key equivalents.
        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }
        AppendMenuW(menu, MF_STRING, kMenuEdit, Strings::Get(L"Pin_MenuEdit").c_str());
        AppendMenuW(menu, MF_STRING, kMenuCopy, Strings::Get(L"Pin_MenuCopy").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenu50, Strings::Get(L"Pin_Menu50").c_str());
        AppendMenuW(menu, MF_STRING, kMenu100, Strings::Get(L"Pin_Menu100").c_str());
        AppendMenuW(menu, MF_STRING, kMenu200, Strings::Get(L"Pin_Menu200").c_str());
        AppendMenuW(menu, MF_STRING | (m_transparency < 100 ? MF_CHECKED : 0), kMenuTransparency, Strings::Get(L"Pin_MenuSemiTransparent").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuClose, Strings::Get(L"Pin_MenuClose").c_str());
        SetForegroundWindow(m_hwnd);
        UINT const id = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, m_hwnd, nullptr));
        DestroyMenu(menu);
        switch (id)
        {
        case kMenuEdit: Edit(); break;
        case kMenuCopy: Copy(); break;
        case kMenu50: SetZoom(50); break;
        case kMenu100: SetZoom(100); break;
        case kMenu200: SetZoom(200); break;
        case kMenuTransparency: SetTransparency(m_transparency < 100 ? 100 : 50); break;
        case kMenuClose: Close(); break;
        default: break;
        }
    }

    void PinWindow::Edit()
    {
        // The pin's image into the editor (metadata from a pin), show it, close the pin.
        auto document = std::make_shared<Core::Document>(*m_image);
        document->metadata.mode = L"pin";
        document->metadata.screenshotDate = std::chrono::system_clock::now();
        AppController::Current().OpenInEditor(document);
        Close();
    }

    void PinWindow::Copy()
    {
        if (!Exporter::CopyToClipboard(*m_image, AppController::Current().MessageWindowHandle()))
        {
            Log(L"copying the image failed");
        }
    }

    void PinWindow::DragOut()
    {
        // The grip: same as the thumbnail's drag-out, which ends by hiding the windows.
        if (FileDrag::DragImage(*m_image, FileDrag::DefaultFileName(std::chrono::system_clock::now()), m_hwnd))
        {
            AppController::Current().HideAll();
        }
    }

    LRESULT CALLBACK PinWindow::Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto* self = static_cast<PinWindow*>(create->lpCreateParams);
            self->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        auto* self = reinterpret_cast<PinWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && self->m_hwnd == hwnd)
        {
            if (message == WM_NCDESTROY)
            {
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                self->m_hwnd = nullptr;
                self->m_target = nullptr;
                self->m_bitmap = nullptr;
                if (auto closed = std::move(self->m_closed))
                {
                    closed(self);
                }
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }
            try
            {
                return self->OnMessage(message, wParam, lParam);
            }
            catch (...)
            {
                Log(L"exception in a message handler");
            }
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT PinWindow::OnMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_NCCALCSIZE:
            if (wParam)
            {
                return 0; // the client area is the whole window: no visible frame
            }
            break;
        case WM_NCACTIVATE:
            return DefWindowProcW(m_hwnd, message, wParam, -1); // don't repaint a frame
        case WM_NCHITTEST:
        {
            POINT client{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(m_hwnd, &client);
            RECT rect{};
            GetClientRect(m_hwnd, &rect);
            int const band = static_cast<int>(std::lround(kResizeBand * m_scale));
            bool const left = client.x < band;
            bool const right = client.x >= rect.right - band;
            bool const top = client.y < band;
            bool const bottom = client.y >= rect.bottom - band;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO:
        {
            // The real limits are applied in WM_SIZING; let the window grow to 2 × screen width.
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            double const maxWidth = 2.0 * ScreenWidthDips() * m_scale + 2;
            double const aspect = m_original.width > 0 ? m_original.height / m_original.width : 1.0;
            info->ptMinTrackSize = { 1, 1 };
            info->ptMaxTrackSize = { static_cast<LONG>(maxWidth), static_cast<LONG>((std::max)(maxWidth * aspect, maxWidth)) };
            return 0;
        }
        case WM_SIZING:
        {
            // Aspect kept, 0.5×/1×/2× snaps, ≤ 2 × screen width, ≥ 44.
            auto* r = reinterpret_cast<RECT*>(lParam);
            double const aspect = m_original.height / m_original.width;
            double const w = (r->right - r->left) / m_scale;
            double const h = (r->bottom - r->top) / m_scale;
            RECT current{};
            GetWindowRect(m_hwnd, &current);
            double const currentWidth = (current.right - current.left) / m_scale;
            double proposed = w;
            if (wParam == WMSZ_TOP || wParam == WMSZ_BOTTOM)
            {
                proposed = h / aspect;
            }
            else if (wParam != WMSZ_LEFT && wParam != WMSZ_RIGHT && std::abs(h / aspect - currentWidth) > std::abs(w - currentWidth))
            {
                proposed = h / aspect; // a corner moved mostly vertically
            }
            Core::Size const size = Core::PreviewLayout::PinResize({ proposed, proposed * aspect }, m_original, ScreenWidthDips());
            LONG const pw = (std::max)(1L, static_cast<LONG>(std::lround(size.width * m_scale)));
            LONG const ph = (std::max)(1L, static_cast<LONG>(std::lround(size.height * m_scale)));
            bool const fromLeft = wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT;
            bool const fromTop = wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT;
            if (fromLeft)
            {
                r->left = r->right - pw;
            }
            else
            {
                r->right = r->left + pw;
            }
            if (fromTop)
            {
                r->top = r->bottom - ph;
            }
            else
            {
                r->bottom = r->top + ph;
            }
            return TRUE;
        }
        case WM_SIZE:
            if (m_target)
            {
                m_target->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
            }
            UpdateZoom();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED:
        {
            m_scale = HIWORD(wParam) / 96.0;
            m_target = nullptr;
            m_bitmap = nullptr;
            auto const* suggested = reinterpret_cast<RECT const*>(lParam);
            SetWindowPos(m_hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(m_hwnd, &ps);
            Render();
            EndPaint(m_hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            POINT const client{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (!m_tracking)
            {
                TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, m_hwnd, 0 };
                m_tracking = TrackMouseEvent(&track) != FALSE;
            }
            ShowToolbar(true);
            if (m_draggerDown && (wParam & MK_LBUTTON))
            {
                if (std::abs(client.x - m_draggerDownPoint.x) > GetSystemMetrics(SM_CXDRAG) ||
                    std::abs(client.y - m_draggerDownPoint.y) > GetSystemMetrics(SM_CYDRAG))
                {
                    m_draggerDown = false;
                    if (GetCapture() == m_hwnd)
                    {
                        ReleaseCapture();
                    }
                    DragOut();
                }
                return 0;
            }
            Control const hover = HitControl(client);
            if (hover != m_hover)
            {
                m_hover = hover;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            m_tracking = false;
            if (m_hover != Control::None)
            {
                m_hover = Control::None;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            SetTimer(m_hwnd, kHideToolbarTimer, kHideToolbarDelayMs, nullptr);
            return 0;
        case WM_TIMER:
            if (wParam == kHideToolbarTimer)
            {
                KillTimer(m_hwnd, kHideToolbarTimer);
                POINT cursor{};
                GetCursorPos(&cursor);
                if (WindowFromPoint(cursor) != m_hwnd || SendMessageW(m_hwnd, WM_NCHITTEST, 0, MAKELPARAM(cursor.x, cursor.y)) != HTCLIENT)
                {
                    ShowToolbar(false);
                }
                return 0;
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE)
            {
                ShowToolbar(false); // hides when the window resigns key
            }
            break;
        case WM_LBUTTONDOWN:
        {
            POINT const client{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            Control const control = HitControl(client);
            if (control == Control::Dragger)
            {
                m_draggerDown = true;
                m_draggerDownPoint = client;
                SetCapture(m_hwnd);
            }
            else if (control != Control::None)
            {
                m_pressed = control;
                m_hover = control;
                SetCapture(m_hwnd);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            else
            {
                // isMovableByWindowBackground: drag anywhere to move.
                POINT screen = client;
                ClientToScreen(m_hwnd, &screen);
                ReleaseCapture();
                SendMessageW(m_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(screen.x, screen.y));
            }
            return 0;
        }
        case WM_LBUTTONUP:
        {
            POINT const client{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (GetCapture() == m_hwnd)
            {
                ReleaseCapture();
            }
            if (m_draggerDown)
            {
                m_draggerDown = false;
                Log(L"draggerClick");
                return 0;
            }
            Control const pressed = m_pressed;
            m_pressed = Control::None;
            if (pressed != Control::None)
            {
                InvalidateRect(m_hwnd, nullptr, FALSE);
                if (HitControl(client) == pressed)
                {
                    switch (pressed)
                    {
                    case Control::Close: Close(); break;
                    case Control::Edit: Edit(); break;
                    case Control::Copy: Copy(); break;
                    default: break;
                    }
                }
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK:
            return 0;
        case WM_CAPTURECHANGED:
            m_draggerDown = false;
            if (m_pressed != Control::None)
            {
                m_pressed = Control::None;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_CONTEXTMENU:
        {
            POINT screen{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (lParam == -1)
            {
                RECT window{};
                GetWindowRect(m_hwnd, &window);
                screen = { (window.left + window.right) / 2, (window.top + window.bottom) / 2 };
            }
            ShowContextMenu(screen);
            return 0;
        }
        case WM_MOUSEWHEEL:
        {
            // One step per wheel notch (WHEEL_DELTA); precision touchpads accumulate to ±120.
            // Positive zooms in; altZoomDirection reverses it.
            m_wheelAccumulator += GET_WHEEL_DELTA_WPARAM(wParam);
            bool const reverse = Core::Settings::Current().AltZoomDirection();
            while (std::abs(m_wheelAccumulator) >= WHEEL_DELTA && m_hwnd)
            {
                bool const up = m_wheelAccumulator > 0;
                m_wheelAccumulator += up ? -WHEEL_DELTA : WHEEL_DELTA;
                ZoomStep(up != reverse);
            }
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            // Esc is not handled: it doesn't close a pin.
            bool const ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool const shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            UINT const key = static_cast<UINT>(wParam);
            if (ctrl)
            {
                if (key == 'Q' || key == 'W')
                {
                    Close();
                    return 0;
                }
                break;
            }
            if (message == WM_SYSKEYDOWN)
            {
                break;
            }
            int const step = static_cast<int>(std::lround((shift ? 10 : 1) * m_scale));
            RECT window{};
            GetWindowRect(m_hwnd, &window);
            switch (key)
            {
            case '5':
            case VK_NUMPAD5: SetTransparency(50); return 0;
            case '0':
            case VK_NUMPAD0: SetTransparency(100); return 0;
            case VK_OEM_PLUS:
            case VK_ADD: ZoomStep(true); return 0;
            case VK_OEM_MINUS:
            case VK_SUBTRACT: ZoomStep(false); return 0;
            case VK_LEFT: SetWindowPos(m_hwnd, nullptr, window.left - step, window.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER); return 0;
            case VK_RIGHT: SetWindowPos(m_hwnd, nullptr, window.left + step, window.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER); return 0;
            case VK_UP: SetWindowPos(m_hwnd, nullptr, window.left, window.top - step, 0, 0, SWP_NOSIZE | SWP_NOZORDER); return 0;
            case VK_DOWN: SetWindowPos(m_hwnd, nullptr, window.left, window.top + step, 0, 0, SWP_NOSIZE | SWP_NOZORDER); return 0;
            default: break;
            }
            break;
        }
        default:
            break;
        }
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    }
}
