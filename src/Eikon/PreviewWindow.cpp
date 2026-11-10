#include "pch.h"
#include "PreviewWindow.h"

#include "AppController.h"
#include "Core/Settings.h"
#include "Platform/FileDrag.h"
#include "Strings.h"

#include <windowsx.h>

namespace Eikon
{
    namespace
    {
        using Core::PreviewLayout::kThumbnailWidth;

        // Candy: the card floats with a shadow, so the layered window is this much larger on every side
        // (DIPs); layout, hit tests and placement stay in card coordinates.
        constexpr double kShadowMargin = 24;

        void Log(std::wstring const& text)
        {
            OutputDebugStringW((L"[Eikon] preview: " + text + L"\n").c_str());
        }

        bool Contains(D2D1_RECT_F const& r, double x, double y) noexcept
        {
            return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
        }

        D2D1_RECT_F AspectFit(D2D1_RECT_F box, double imageWidth, double imageHeight) noexcept
        {
            double const bw = box.right - box.left;
            double const bh = box.bottom - box.top;
            if (imageWidth <= 0 || imageHeight <= 0 || bw <= 0 || bh <= 0)
            {
                return box;
            }
            double const s = (std::min)(bw / imageWidth, bh / imageHeight);
            double const w = imageWidth * s;
            double const h = imageHeight * s;
            float const x = static_cast<float>(box.left + (bw - w) / 2);
            float const y = static_cast<float>(box.top + (bh - h) / 2);
            return { x, y, static_cast<float>(x + w), static_cast<float>(y + h) };
        }
    }

    PreviewWindow::~PreviewWindow()
    {
        if (m_hwnd)
        {
            DestroyWindow(m_hwnd);
        }
    }

    bool PreviewWindow::EnsureWindow()
    {
        if (m_hwnd)
        {
            return true;
        }
        static ATOM const atom = [] {
            WNDCLASSEXW wc{ sizeof(wc) };
            wc.style = CS_DBLCLKS;
            wc.lpfnWndProc = &PreviewWindow::Proc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.lpszClassName = L"EikonPreview";
            return RegisterClassExW(&wc);
        }();
        if (!atom)
        {
            return false;
        }
        // Borderless, clear, no system shadow, level 1000 (above nearly everything), kept off the
        // taskbar; it takes keys (KeyableWindow).
        m_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"EikonPreview", L"Eikon", WS_POPUP, 0, 0, 1, 1, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), this);
        return m_hwnd != nullptr;
    }

    bool PreviewWindow::IsVisible() const noexcept
    {
        return m_hwnd && IsWindowVisible(m_hwnd);
    }

    void PreviewWindow::Layout()
    {
        using namespace Core::PreviewLayout;
        auto const& image = m_document->Image();
        m_layout = ThumbnailFor(image.PointSize(), m_autoClosing);

        // Button stack: 7 pt insets, 32 pt high, 32×32 buttons with equal spacing; Upload only when
        // uploadMode isn't "none".
        struct Spec
        {
            Button id;
            wchar_t const* icon;
            wchar_t const* tooltip;
        };
        std::vector<Spec> specs{
            { Button::Edit, L"ico-16-edit-light", L"Preview_EditTooltip" },
            { Button::Copy, L"ico-16-copy-light", L"Preview_CopyTooltip" },
            { Button::Save, L"ico-16-save-light", L"Preview_SaveTooltip" },
        };
        if (Core::Settings::Current().UploadMode() != L"none")
        {
            specs.push_back({ Button::Upload, L"ico-16-upload-light", L"Preview_UploadTooltip" });
        }
        specs.push_back({ Button::Ocr, L"ico-16-ocr-light", L"Preview_OcrTooltip" });
        specs.push_back({ Button::Pin, L"ico-16-pin-light", L"Preview_PinTooltip" });

        m_buttons.clear();
        double const height = m_layout.window.height;
        float const top = static_cast<float>(height - 7 - 32);
        double const available = kThumbnailWidth - 14;
        std::size_t const n = specs.size();
        double const spacing = n > 1 ? (available - 32.0 * n) / (n - 1) : 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            float const left = static_cast<float>(7 + i * (32 + spacing));
            m_buttons.push_back({ specs[i].id, specs[i].icon, Strings::Get(specs[i].tooltip), { left, top, left + 32, top + 32 } });
        }
        if (!m_autoClosing)
        {
            m_buttons.push_back({ Button::Close, L"ico-16-close", {}, { 4, 4, 24, 24 } });
        }
    }

    void PreviewWindow::PrepareThumbnail()
    {
        // The image layer is resizeAspect inside the container: scale once to the device size.
        m_thumbnail = nullptr;
        auto const& image = m_document->Image();
        D2D1_RECT_F const box = Gfx::ToD2D(m_layout.image);
        D2D1_RECT_F const fit = AspectFit(box, image.width, image.height);
        UINT const w = (std::max)(1u, static_cast<UINT>(std::lround((fit.right - fit.left) * m_scale)));
        UINT const h = (std::max)(1u, static_cast<UINT>(std::lround((fit.bottom - fit.top) * m_scale)));
        auto source = Gfx::FromBitmap(image);
        winrt::com_ptr<IWICBitmapScaler> scaler;
        winrt::com_ptr<IWICBitmap> scaled;
        if (source && Gfx::Wic() && SUCCEEDED(Gfx::Wic()->CreateBitmapScaler(scaler.put())) &&
            SUCCEEDED(scaler->Initialize(source.get(), w, h, WICBitmapInterpolationModeHighQualityCubic)) &&
            SUCCEEDED(Gfx::Wic()->CreateBitmapFromSource(scaler.get(), WICBitmapCacheOnLoad, scaled.put())))
        {
            m_thumbnail = scaled;
        }
    }

    void PreviewWindow::Show(std::shared_ptr<Core::Document> document, std::optional<Core::Rect> captureRect)
    {
        if (!document || document->Image().IsEmpty() || !EnsureWindow())
        {
            return;
        }
        m_document = std::move(document);
        m_autoClosing = Core::Settings::Current().ThumbnailClosing() == L"auto";
        m_hover = Button::None; // every show resets mouseState to "normal"
        m_pressed = Button::None;
        m_imageDown = false;

        // Next to the corner of the selection where the drag ended, inside the monitor.
        POINT mouse{};
        GetCursorPos(&mouse);
        HMONITOR const monitor = MonitorFromPoint(mouse, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{ sizeof(info) };
        GetMonitorInfoW(monitor, &info);
        m_scale = Gfx::ScaleForMonitor(monitor);
        Layout();
        SIZE const cardPixels{ static_cast<LONG>(std::lround(m_layout.window.width * m_scale)),
                               static_cast<LONG>(std::lround(m_layout.window.height * m_scale)) };
        LONG const margin = static_cast<LONG>(std::lround(kShadowMargin * m_scale));
        m_pixelSize = { cardPixels.cx + 2 * margin, cardPixels.cy + 2 * margin };
        Core::Point const mousePoint{ static_cast<double>(mouse.x), static_cast<double>(mouse.y) };
        Core::Size const imageSize{ m_document->Image().PointSize().width * m_scale, m_document->Image().PointSize().height * m_scale };
        Core::Rect const rect = captureRect && !captureRect->IsEmpty() ? *captureRect : Core::PreviewLayout::RectEndingAtMouse(mousePoint, imageSize);
        Core::Rect const screen{ static_cast<double>(info.rcMonitor.left), static_cast<double>(info.rcMonitor.top),
                                 static_cast<double>(info.rcMonitor.right - info.rcMonitor.left),
                                 static_cast<double>(info.rcMonitor.bottom - info.rcMonitor.top) };
        Core::Point const origin = Core::PreviewLayout::ThumbnailOrigin(
            mousePoint, rect, { static_cast<double>(cardPixels.cx), static_cast<double>(cardPixels.cy) }, screen);
        m_position = { static_cast<LONG>(std::lround(origin.x)) - margin, static_cast<LONG>(std::lround(origin.y)) - margin };
        Log(L"preview " + std::to_wstring(m_pixelSize.cx) + L"x" + std::to_wstring(m_pixelSize.cy));

        PrepareThumbnail();
        Render();
        SetWindowPos(m_hwnd, HWND_TOPMOST, m_position.x, m_position.y, m_pixelSize.cx, m_pixelSize.cy, SWP_SHOWWINDOW);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
    }

    void PreviewWindow::Hide()
    {
        if (!m_hwnd || !IsWindowVisible(m_hwnd))
        {
            return;
        }
        if (GetCapture() == m_hwnd)
        {
            ReleaseCapture();
        }
        m_hover = Button::None;
        m_pressed = Button::None;
        ShowWindow(m_hwnd, SW_HIDE);
    }

    void PreviewWindow::Render()
    {
        if (!m_document)
        {
            return;
        }
        bool const dark = Gfx::IsDarkMode();
        bool const contrast = Candy::HighContrast();
        auto const& candy = Candy::For(dark);
        m_surface.Render(m_hwnd, m_position, m_pixelSize, m_scale, 255, [&](ID2D1DeviceContext* dc) {
            float const w = static_cast<float>(m_layout.window.width);
            float const h = static_cast<float>(m_layout.window.height);
            D2D1_RECT_F const bounds{ 0, 0, w, h };
            // The shadow in window coordinates (its blur reaches into the margin), then everything else in card
            // coordinates, inside the margin.
            float const m = static_cast<float>(kShadowMargin);
            if (!contrast)
            {
                Gfx::DrawCandyShadow(dc, { m, m, m + w, m + h }, Candy::kRadiusFloat, dark);
            }
            D2D1_MATRIX_3X2_F base{};
            dc->GetTransform(&base);
            dc->SetTransform(D2D1::Matrix3x2F::Translation(m, m) * base);

            // Candy: a floating 16-px card on the surface colour with the Candy shadow. High contrast: the window colours.
            if (contrast)
            {
                Gfx::FillRoundedRect(dc, bounds, Candy::kRadiusFloat, Gfx::SystemColor(COLOR_WINDOW));
                Gfx::StrokeRoundedRect(dc, bounds, Candy::kRadiusFloat, Gfx::SystemColor(COLOR_WINDOWTEXT), 1);
            }
            else
            {
                Gfx::FillRoundedRect(dc, bounds, Candy::kRadiusFloat, Gfx::FromArgb(candy.surface));
            }

            // Image container: radius 6, shadow black α0.1 radius 1 offset 0.5 down; image aspect-fit.
            auto const& image = m_document->Image();
            D2D1_RECT_F const fit = AspectFit(Gfx::ToD2D(m_layout.image), image.width, image.height);
            Gfx::DrawShadow(dc, fit, 6, Gfx::Rgba(0, 0, 0, 0.1f), { 0, 0.5f }, 1);
            if (m_thumbnail)
            {
                winrt::com_ptr<ID2D1Bitmap> bitmap;
                if (SUCCEEDED(dc->CreateBitmapFromWicBitmap(m_thumbnail.get(), nullptr, bitmap.put())))
                {
                    winrt::com_ptr<ID2D1RoundedRectangleGeometry> clip;
                    Gfx::Factory()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(fit, 6, 6), clip.put());
                    winrt::com_ptr<ID2D1Layer> layer;
                    dc->CreateLayer(nullptr, layer.put());
                    dc->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.get()), layer.get());
                    dc->DrawBitmap(bitmap.get(), fit, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
                    dc->PopLayer();
                }
            }

            // Grip (manual mode): 32×12, top 7, centred; it takes no clicks.
            if (!m_autoClosing)
            {
                float const gx = (w - 32) / 2;
                Gfx::DrawAsset(dc, Gfx::Asset(L"preview-grip", dark).get(), { gx, 7, gx + 32, 19 });
            }

            // Buttons: 32×32 radius 10, the close button 20×20 radius 9; Candy's neutral wash (surface 2) on hover,
            // a shade darker while pressed.
            for (auto const& button : m_buttons)
            {
                bool const active = button.id == m_hover || button.id == m_pressed;
                if (active)
                {
                    bool const close = button.id == Button::Close;
                    D2D1_COLOR_F const wash = contrast ? Gfx::SystemColor(COLOR_HIGHLIGHT)
                                                       : (button.id == m_pressed ? Gfx::FromArgb(candy.accentSoft) : Gfx::FromArgb(candy.surface2));
                    Gfx::FillRoundedRect(dc, button.rect, close ? 9.0f : 10.0f, wash);
                }
                float const cx = (button.rect.left + button.rect.right) / 2;
                float const cy = (button.rect.top + button.rect.bottom) / 2;
                Gfx::DrawAsset(dc, Gfx::Asset(button.icon, dark).get(), { cx - 8, cy - 8, cx + 8, cy + 8 });
            }

            // The tooltip over the image: the text split at "(": the label and the key part, 8 apart, padding
            // 16/8/16/10. Candy: an ink rounded rectangle (radius 8), base-coloured 13-px text, the key at 60 %.
            auto const hovered = std::find_if(m_buttons.begin(), m_buttons.end(), [&](auto const& b) { return b.id == m_hover; });
            if (hovered != m_buttons.end() && !hovered->tooltip.empty())
            {
                std::wstring const& tip = hovered->tooltip;
                std::size_t const paren = tip.find(L'(');
                std::wstring label = tip.substr(0, paren);
                while (!label.empty() && label.back() == L' ')
                {
                    label.pop_back();
                }
                std::wstring const key = paren == std::wstring::npos ? std::wstring{} : tip.substr(paren);
                auto format = Gfx::TextFormat(13.0f);
                if (format)
                {
                    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                    auto measure = [&](std::wstring const& text) {
                        winrt::com_ptr<IDWriteTextLayout> layout;
                        DWRITE_TEXT_METRICS metrics{};
                        if (SUCCEEDED(Gfx::DWrite()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.get(), 1000, 100, layout.put())))
                        {
                            layout->GetMetrics(&metrics);
                        }
                        return metrics;
                    };
                    DWRITE_TEXT_METRICS const a = measure(label);
                    DWRITE_TEXT_METRICS const b = key.empty() ? DWRITE_TEXT_METRICS{} : measure(key);
                    float const textHeight = (std::max)(a.height, 17.0f);
                    float const pillWidth = 16 + a.widthIncludingTrailingWhitespace + (key.empty() ? 0 : 8 + b.widthIncludingTrailingWhitespace) + 16;
                    float const pillHeight = 7 + textHeight + 7;
                    float const cx = static_cast<float>(m_layout.image.Center().x);
                    float const cy = static_cast<float>(m_layout.image.Center().y);
                    D2D1_RECT_F const pill{ cx - pillWidth / 2, cy - pillHeight / 2, cx + pillWidth / 2, cy + pillHeight / 2 };
                    Gfx::DrawShadow(dc, pill, Candy::kRadiusControl, Gfx::Rgba(0, 0, 0, 0.18f), { 0, 2 }, 8);
                    D2D1_COLOR_F const fill = contrast ? Gfx::SystemColor(COLOR_INFOBK) : Gfx::FromArgb(candy.ink);
                    D2D1_COLOR_F const text = contrast ? Gfx::SystemColor(COLOR_INFOTEXT) : Gfx::FromArgb(candy.onInk);
                    Gfx::FillRoundedRect(dc, pill, Candy::kRadiusControl, fill);
                    if (contrast)
                    {
                        Gfx::StrokeRoundedRect(dc, pill, Candy::kRadiusControl, text, 1);
                    }
                    winrt::com_ptr<ID2D1SolidColorBrush> primary;
                    winrt::com_ptr<ID2D1SolidColorBrush> secondary;
                    dc->CreateSolidColorBrush(text, primary.put());
                    dc->CreateSolidColorBrush({ text.r, text.g, text.b, 0.6f }, secondary.put());
                    float const textTop = pill.top + 7;
                    dc->DrawText(label.c_str(), static_cast<UINT32>(label.size()), format.get(),
                                 { pill.left + 16, textTop, pill.left + 16 + a.widthIncludingTrailingWhitespace + 1, textTop + textHeight }, primary.get());
                    if (!key.empty())
                    {
                        float const kx = pill.left + 16 + a.widthIncludingTrailingWhitespace + 8;
                        dc->DrawText(key.c_str(), static_cast<UINT32>(key.size()), format.get(),
                                     { kx, textTop, kx + b.widthIncludingTrailingWhitespace + 1, textTop + textHeight }, secondary.get());
                    }
                }
            }
            dc->SetTransform(base);
        });
    }

    PreviewWindow::Button PreviewWindow::HitButton(POINT client) const
    {
        double const x = client.x / m_scale - kShadowMargin;
        double const y = client.y / m_scale - kShadowMargin;
        for (auto const& button : m_buttons)
        {
            if (Contains(button.rect, x, y))
            {
                return button.id;
            }
        }
        return Button::None;
    }

    bool PreviewWindow::InImage(POINT client) const
    {
        return m_layout.image.Contains({ client.x / m_scale - kShadowMargin, client.y / m_scale - kShadowMargin });
    }

    bool PreviewWindow::InTopLeftArrowZone(POINT client) const
    {
        // CtrlDragOutView: wider than 100 pt -> the top-left 15×15 shows the arrow.
        if (m_layout.image.width <= 100)
        {
            return false;
        }
        double const x = client.x / m_scale - kShadowMargin - m_layout.image.x;
        double const y = client.y / m_scale - kShadowMargin - m_layout.image.y;
        return x >= 0 && x < 15 && y >= 0 && y < 15;
    }

    void PreviewWindow::Invoke(Button button)
    {
        auto& app = AppController::Current();
        auto document = m_document;
        if (!document)
        {
            return;
        }
        switch (button)
        {
        case Button::Edit:
            Hide();
            app.OpenInEditor(document);
            break;
        case Button::Copy:
            // Copy, then hide all (closes the preview).
            app.CopyImage();
            app.HideAll();
            break;
        case Button::Save:
            app.SaveImage();
            break;
        case Button::Upload:
            app.UploadImage();
            break;
        case Button::Ocr:
            app.RecognizeText(document->ImagePtr(), L"preview");
            Hide();
            break;
        case Button::Pin:
            app.PinImage(document->ImagePtr());
            Hide();
            break;
        case Button::Close:
            Hide();
            m_document.reset();
            m_thumbnail = nullptr;
            break;
        case Button::None:
            break;
        }
    }

    bool PreviewWindow::OnKey(UINT key)
    {
        auto& app = AppController::Current();
        bool const ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool const shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool const alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (alt)
        {
            return false;
        }
        if (ctrl)
        {
            switch (key)
            {
            case 'S':
                if (shift)
                {
                    app.HideAll(); // Save As: hide, then the save dialog
                    app.SaveImageAs();
                }
                else
                {
                    Invoke(Button::Save);
                }
                return true;
            case 'C': Invoke(Button::Copy); return true;
            case 'Q':
            case 'W': Hide(); return true;
            case 'E': Invoke(Button::Upload); return true;
            case 'O': Invoke(Button::Ocr); return true;
            case 'P': Invoke(Button::Pin); return true;
            default: return false;
            }
        }
        switch (key)
        {
        case VK_F2:
            Invoke(Button::Upload);
            return true;
        case VK_RETURN:
            Invoke(Button::Edit);
            return true;
        case VK_ESCAPE:
        {
            auto& settings = Core::Settings::Current();
            if (settings.CopyOnEsc())
            {
                app.CopyImage();
            }
            if (settings.SaveOnEsc())
            {
                app.SaveImage();
            }
            Hide();
            return true;
        }
        default:
            return false;
        }
    }

    void PreviewWindow::StartDragOut()
    {
        if (!m_document)
        {
            Log(L"No image to save");
            return;
        }
        auto const document = m_document;
        auto const time = document->metadata.screenshotDate.time_since_epoch().count() ? document->metadata.screenshotDate
                                                                                       : std::chrono::system_clock::now();
        if (FileDrag::DragImage(document->Image(), FileDrag::DefaultFileName(time), m_hwnd))
        {
            // When the drag ends, hiding all closes the preview.
            AppController::Current().HideAll();
        }
    }

    LRESULT CALLBACK PreviewWindow::Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* self = reinterpret_cast<PreviewWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && self->m_hwnd == hwnd)
        {
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

    LRESULT PreviewWindow::OnMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_NCHITTEST:
        {
            // isMovableByWindowBackground: everything that isn't a control or the image drags the
            // window (the grip included: it takes no hits).
            POINT client{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(m_hwnd, &client);
            if (HitButton(client) != Button::None || InImage(client))
            {
                return HTCLIENT;
            }
            return HTCAPTION;
        }
        case WM_NCLBUTTONDBLCLK:
            return 0; // no maximise on a caption double click
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT)
            {
                POINT client{};
                GetCursorPos(&client);
                ScreenToClient(m_hwnd, &client);
                if (InImage(client) && !InTopLeftArrowZone(client))
                {
                    // Open hand over the image, closed while the mouse is down (Windows' nearest: the
                    // hand and the move cursor).
                    SetCursor(LoadCursorW(nullptr, m_imageDown ? IDC_SIZEALL : IDC_HAND));
                    return TRUE;
                }
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_MOUSEMOVE:
        {
            POINT const client{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (!m_tracking)
            {
                TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, m_hwnd, 0 };
                m_tracking = TrackMouseEvent(&track) != FALSE;
            }
            if (m_imageDown && (wParam & MK_LBUTTON))
            {
                int const dx = std::abs(client.x - m_imageDownPoint.x);
                int const dy = std::abs(client.y - m_imageDownPoint.y);
                if (dx > GetSystemMetrics(SM_CXDRAG) || dy > GetSystemMetrics(SM_CYDRAG))
                {
                    m_imageDown = false;
                    if (GetCapture() == m_hwnd)
                    {
                        ReleaseCapture();
                    }
                    StartDragOut();
                }
                return 0;
            }
            Button const hover = HitButton(client);
            if (hover != m_hover)
            {
                m_hover = hover;
                Render();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            m_tracking = false;
            if (m_hover != Button::None)
            {
                m_hover = Button::None;
                Render();
            }
            return 0;
        case WM_LBUTTONDOWN:
        {
            POINT const client{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            Button const button = HitButton(client);
            SetCapture(m_hwnd);
            if (button != Button::None)
            {
                m_pressed = button;
                Render();
            }
            else if (InImage(client))
            {
                m_imageDown = true;
                m_imageDownPoint = client;
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
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
            m_imageDown = false;
            Button const pressed = m_pressed;
            m_pressed = Button::None;
            if (pressed != Button::None)
            {
                Render();
                if (HitButton(client) == pressed)
                {
                    Invoke(pressed);
                }
            }
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (OnKey(static_cast<UINT>(wParam)))
            {
                return 0;
            }
            break;
        case WM_ACTIVATE:
            // "auto": closes as soon as it is no longer the key window ("blur").
            if (LOWORD(wParam) == WA_INACTIVE && m_autoClosing && IsWindowVisible(m_hwnd))
            {
                Log(L"hidePreview (blur)");
                Hide();
            }
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_ACTIVATE;
        case WM_CLOSE:
            // Alt+F4 or a WM_CLOSE from outside hides the thumbnail; destroying it would leave m_hwnd dangling
            // and every later thumbnail silently unshown.
            Log(L"hidePreview (close)");
            Hide();
            return 0;
        case WM_SETTINGCHANGE:
            if (IsWindowVisible(m_hwnd) && lParam && std::wstring_view{ reinterpret_cast<wchar_t const*>(lParam) } == L"ImmersiveColorSet")
            {
                Render(); // reruns on appearance changes
            }
            break;
        default:
            break;
        }
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    }
}
