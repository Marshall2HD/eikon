#include "pch.h"
#include "Platform/FirstCaptureCard.h"

#include "Platform/Candy.h"
#include "Platform/Graphics.h"
#include "Strings.h"

#include <windowsx.h>

#include <cmath>

namespace Eikon::FirstCaptureCard
{
    namespace
    {
        // ---- Layout (DIPs): a 344-wide card with 18 px padding, 24 px around it for the shadow ----
        constexpr float kMargin = 32;
        constexpr float kCardWidth = 344;
        constexpr float kPadding = 18;
        constexpr float kTitleTop = kPadding;               // 18 px display bold, line 24
        constexpr float kSubtitleTop = kTitleTop + 28;      // 12 px ink 2, line 16
        constexpr float kChoicesTop = kSubtitleTop + 16 + 14;
        constexpr float kChoiceHeight = 106;                 // 10 + preview 62 + 8 + label 16 + 10
        constexpr float kChoiceGap = 10;
        constexpr float kButtonTop = kChoicesTop + kChoiceHeight + 14;
        constexpr float kButtonHeight = 34;
        constexpr float kCardHeight = kButtonTop + kButtonHeight + kPadding;
        constexpr float kWindowWidth = kCardWidth + 2 * kMargin;
        constexpr float kWindowHeight = kCardHeight + 2 * kMargin;

        enum class Part { None, Preview, Editor, Continue };

        class Card
        {
        public:
            static Card& Current()
            {
                static Card card;
                return card;
            }

            void Ask(std::function<void(std::wstring const&)> done);
            bool IsOpen() const noexcept { return m_hwnd && IsWindowVisible(m_hwnd); }

        private:
            static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
            LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam);
            bool EnsureWindow();
            void Draw();
            void Finish(bool preview);
            D2D1_RECT_F CardRect() const { return { kMargin, kMargin, kMargin + kCardWidth, kMargin + kCardHeight }; }
            D2D1_RECT_F ChoiceRect(bool preview) const;
            D2D1_RECT_F ButtonRect() const;
            float ButtonWidth() const;
            Part HitTest(POINT client) const;

            HWND m_hwnd = nullptr;
            Gfx::LayeredSurface m_surface;
            std::function<void(std::wstring const&)> m_done;
            double m_scale = 1.0;
            POINT m_position{};
            bool m_preview = false;    // "Open the editor" is the default
            Part m_hover = Part::None;
            Part m_pressed = Part::None;
            bool m_tracking = false;
        };

        D2D1_RECT_F Card::ChoiceRect(bool preview) const
        {
            float const left = kMargin + kPadding;
            float const width = (kCardWidth - 2 * kPadding - kChoiceGap) / 2;
            float const x = preview ? left : left + width + kChoiceGap;
            return { x, kMargin + kChoicesTop, x + width, kMargin + kChoicesTop + kChoiceHeight };
        }

        float Card::ButtonWidth() const
        {
            std::wstring const text = Strings::Get(L"Capture_AreaCardContinue");
            auto format = Gfx::TextFormat(13.0f, DWRITE_FONT_WEIGHT_BOLD);
            winrt::com_ptr<IDWriteTextLayout> layout;
            if (!format || FAILED(Gfx::DWrite()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.get(), 1000, 100, layout.put())))
            {
                return 96;
            }
            DWRITE_TEXT_METRICS metrics{};
            layout->GetMetrics(&metrics);
            return std::ceil(metrics.widthIncludingTrailingWhitespace) + 32;   // padding 8 × 16
        }

        D2D1_RECT_F Card::ButtonRect() const
        {
            float const right = kMargin + kCardWidth - kPadding;
            float const top = kMargin + kButtonTop;
            return { right - ButtonWidth(), top, right, top + kButtonHeight };
        }

        Part Card::HitTest(POINT client) const
        {
            float const x = static_cast<float>(client.x / m_scale);
            float const y = static_cast<float>(client.y / m_scale);
            auto const inside = [x, y](D2D1_RECT_F const& r) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; };
            if (inside(ChoiceRect(true))) return Part::Preview;
            if (inside(ChoiceRect(false))) return Part::Editor;
            if (inside(ButtonRect())) return Part::Continue;
            return Part::None;
        }

        bool Card::EnsureWindow()
        {
            if (m_hwnd)
            {
                return true;
            }
            static ATOM const atom = [] {
                WNDCLASSEXW wc{ sizeof(wc) };
                wc.lpfnWndProc = &Card::Proc;
                wc.hInstance = GetModuleHandleW(nullptr);
                wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                wc.lpszClassName = L"EikonFirstCapture";
                return RegisterClassExW(&wc);
            }();
            if (!atom)
            {
                return false;
            }
            // A topmost tool window that takes focus (the keyboard drives it), off the taskbar.
            m_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"EikonFirstCapture", L"", WS_POPUP, 0, 0, 1, 1, nullptr,
                                     nullptr, GetModuleHandleW(nullptr), this);
            return m_hwnd != nullptr;
        }

        void Card::Ask(std::function<void(std::wstring const&)> done)
        {
            if (!EnsureWindow())
            {
                done(L"editor");
                return;
            }
            m_done = std::move(done);
            m_preview = false;
            m_hover = Part::None;
            m_pressed = Part::None;
            // The title is the window's text, so screen readers and the switcher name it.
            SetWindowTextW(m_hwnd, Strings::Get(L"Capture_AreaCardTitle").c_str());

            POINT cursor{};
            GetCursorPos(&cursor);
            HMONITOR const monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO info{ sizeof(info) };
            GetMonitorInfoW(monitor, &info);
            m_scale = Gfx::ScaleForMonitor(monitor);
            int const width = static_cast<int>(std::lround(kWindowWidth * m_scale));
            int const height = static_cast<int>(std::lround(kWindowHeight * m_scale));
            m_position = { (info.rcWork.left + info.rcWork.right - width) / 2, (info.rcWork.top + info.rcWork.bottom - height) / 2 };
            Draw();
            SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetForegroundWindow(m_hwnd);
            SetFocus(m_hwnd);
        }

        void Card::Finish(bool preview)
        {
            ShowWindow(m_hwnd, SW_HIDE);
            if (GetCapture() == m_hwnd)
            {
                ReleaseCapture();
            }
            auto done = std::move(m_done);
            m_done = nullptr;
            if (done)
            {
                done(preview ? L"preview" : L"editor");
            }
        }

        void Card::Draw()
        {
            bool const dark = Gfx::IsDarkMode();
            bool const contrast = Candy::HighContrast();
            auto const& candy = Candy::For(dark);
            SIZE const size{ static_cast<LONG>(std::lround(kWindowWidth * m_scale)), static_cast<LONG>(std::lround(kWindowHeight * m_scale)) };
            m_surface.Render(m_hwnd, m_position, size, m_scale, 255, [&](ID2D1DeviceContext* dc) {
                D2D1_RECT_F const card = CardRect();
                auto const ink = contrast ? Gfx::SystemColor(COLOR_WINDOWTEXT) : Gfx::FromArgb(candy.ink);
                auto const ink2 = contrast ? Gfx::SystemColor(COLOR_WINDOWTEXT) : Gfx::FromArgb(candy.ink2);
                auto const accent = contrast ? Gfx::SystemColor(COLOR_HIGHLIGHT) : Gfx::FromArgb(candy.accent);
                if (contrast)
                {
                    Gfx::FillRoundedRect(dc, card, Candy::kRadiusFloat, Gfx::SystemColor(COLOR_WINDOW));
                    Gfx::StrokeRoundedRect(dc, card, Candy::kRadiusFloat, ink, 1);
                }
                else
                {
                    Gfx::DrawCandyShadow(dc, card, Candy::kRadiusFloat, dark);
                    Gfx::FillCandyFields(dc, card, Candy::kRadiusFloat, dark);
                }

                winrt::com_ptr<ID2D1SolidColorBrush> brush;
                dc->CreateSolidColorBrush(ink, brush.put());
                auto text = [&](std::wstring const& value, IDWriteTextFormat* format, D2D1_RECT_F box, D2D1_COLOR_F color) {
                    if (!format)
                    {
                        return;
                    }
                    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                    brush->SetColor(color);
                    dc->DrawText(value.c_str(), static_cast<UINT32>(value.size()), format, box, brush.get());
                };
                float const left = card.left + kPadding;
                float const right = card.right - kPadding;
                text(Strings::Get(L"Capture_AreaCardTitle"), Gfx::DisplayTextFormat(18.0f).get(), { left, card.top + kTitleTop, right, card.top + kTitleTop + 26 },
                     ink);
                text(Strings::Get(L"Capture_AreaCardSubtitle"), Gfx::TextFormat(12.0f).get(),
                     { left, card.top + kSubtitleTop, right, card.top + kSubtitleTop + 16 }, ink2);

                // The two choices: radius 12, a 1-px line ring, or the lilac fill with a 2-px accent ring when selected.
                for (bool const preview : { true, false })
                {
                    D2D1_RECT_F const r = ChoiceRect(preview);
                    bool const selected = preview == m_preview;
                    bool const hover = m_hover == (preview ? Part::Preview : Part::Editor);
                    if (selected)
                    {
                        Gfx::FillRoundedRect(dc, r, Candy::kRadiusCard, contrast ? Gfx::SystemColor(COLOR_WINDOW) : Gfx::FromArgb(candy.accentSoft));
                        Gfx::StrokeRoundedRect(dc, r, Candy::kRadiusCard, accent, 2);
                    }
                    else
                    {
                        if (hover && !contrast)
                        {
                            Gfx::FillRoundedRect(dc, r, Candy::kRadiusCard, Gfx::FromArgb(candy.surface, 0.5f));
                        }
                        Gfx::StrokeRoundedRect(dc, r, Candy::kRadiusCard,
                                               contrast ? ink : (hover ? Gfx::FromArgb(candy.muted, 0.6f) : Gfx::FromArgb(candy.line)), 1);
                    }
                    // The picture: a surface-2 frame (radius 8) with a white "screenshot".
                    D2D1_RECT_F const picture{ r.left + 10, r.top + 10, r.right - 10, r.top + 72 };
                    Gfx::FillRoundedRect(dc, picture, 8, contrast ? Gfx::SystemColor(COLOR_WINDOW) : Gfx::FromArgb(candy.surface2));
                    if (contrast)
                    {
                        Gfx::StrokeRoundedRect(dc, picture, 8, ink, 1);
                    }
                    D2D1_RECT_F const shot = preview ? D2D1_RECT_F{ picture.right - 52, picture.bottom - 38, picture.right - 8, picture.bottom - 8 }
                                                     : D2D1_RECT_F{ picture.left + 10, picture.top + 8, picture.right - 10, picture.bottom - 8 };
                    if (!contrast)
                    {
                        Gfx::DrawShadow(dc, { shot.left + 2, shot.top + 2, shot.right - 2, shot.bottom - 2 }, 4, Gfx::Rgba(0, 0, 0, 0.3f), { 0, 4 }, 10);
                    }
                    Gfx::FillRoundedRect(dc, shot, 6, contrast ? Gfx::SystemColor(COLOR_WINDOW) : Gfx::Rgba(1, 1, 1, 1));
                    if (contrast)
                    {
                        Gfx::StrokeRoundedRect(dc, shot, 6, ink, 1);
                    }
                    auto label = Gfx::TextFormat(12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
                    text(Strings::Get(preview ? L"Capture_AreaCardPreview" : L"Capture_AreaCardEditor"), label.get(),
                         { r.left + 10, picture.bottom + 8, r.right - 6, picture.bottom + 26 }, ink);
                }

                // Continue: the ink button, radius 8, base-coloured bold text.
                D2D1_RECT_F const button = ButtonRect();
                float const fade = m_pressed == Part::Continue ? 0.75f : (m_hover == Part::Continue ? 0.88f : 1.0f);
                if (contrast)
                {
                    Gfx::FillRoundedRect(dc, button, Candy::kRadiusControl, Gfx::SystemColor(COLOR_BTNFACE));
                    Gfx::StrokeRoundedRect(dc, button, Candy::kRadiusControl, Gfx::SystemColor(COLOR_BTNTEXT), 2);
                }
                else
                {
                    Gfx::FillRoundedRect(dc, button, Candy::kRadiusControl, Gfx::FromArgb(candy.ink, fade));
                }
                if (auto format = Gfx::TextFormat(13.0f, DWRITE_FONT_WEIGHT_BOLD))
                {
                    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    text(Strings::Get(L"Capture_AreaCardContinue"), format.get(), button,
                         contrast ? Gfx::SystemColor(COLOR_BTNTEXT) : Gfx::FromArgb(candy.onInk));
                }
            });
        }

        LRESULT CALLBACK Card::Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == WM_NCCREATE)
            {
                auto const* create = reinterpret_cast<CREATESTRUCTW const*>(lParam);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            }
            auto* self = reinterpret_cast<Card*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self && self->m_hwnd == hwnd)
            {
                return self->OnMessage(message, wParam, lParam);
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        LRESULT Card::OnMessage(UINT message, WPARAM wParam, LPARAM lParam)
        {
            switch (message)
            {
            case WM_MOUSEMOVE:
            {
                if (!m_tracking)
                {
                    TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, m_hwnd, 0 };
                    m_tracking = TrackMouseEvent(&track) != FALSE;
                }
                Part const hover = HitTest({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
                if (hover != m_hover)
                {
                    m_hover = hover;
                    Draw();
                }
                return 0;
            }
            case WM_MOUSELEAVE:
                m_tracking = false;
                if (m_hover != Part::None)
                {
                    m_hover = Part::None;
                    Draw();
                }
                return 0;
            case WM_LBUTTONDOWN:
            {
                Part const part = HitTest({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
                if (part == Part::Preview || part == Part::Editor)
                {
                    m_preview = part == Part::Preview;
                }
                m_pressed = part;
                SetCapture(m_hwnd);
                Draw();
                return 0;
            }
            case WM_LBUTTONDBLCLK:
                return 0;
            case WM_LBUTTONUP:
            {
                Part const pressed = m_pressed;
                m_pressed = Part::None;
                if (GetCapture() == m_hwnd)
                {
                    ReleaseCapture();
                }
                if (pressed == Part::Continue && HitTest({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }) == Part::Continue)
                {
                    Finish(m_preview);
                    return 0;
                }
                Draw();
                return 0;
            }
            case WM_KEYDOWN:
                switch (wParam)
                {
                case VK_LEFT:
                case VK_UP:
                    m_preview = true;
                    Draw();
                    return 0;
                case VK_RIGHT:
                case VK_DOWN:
                    m_preview = false;
                    Draw();
                    return 0;
                case VK_TAB:
                    m_preview = !m_preview;
                    Draw();
                    return 0;
                case VK_RETURN:
                case VK_SPACE:
                    Finish(m_preview);
                    return 0;
                case VK_ESCAPE:
                    Finish(false);   // the default: open the editor
                    return 0;
                default:
                    break;
                }
                break;
            case WM_CLOSE:
                Finish(false);
                return 0;
            case WM_MOUSEACTIVATE:
                return MA_ACTIVATE;
            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE:
                if (IsWindowVisible(m_hwnd))
                {
                    Draw();
                }
                break;
            default:
                break;
            }
            return DefWindowProcW(m_hwnd, message, wParam, lParam);
        }
    }

    void Ask(std::function<void(std::wstring const& mode)> done)
    {
        Card::Current().Ask(std::move(done));
    }

    bool IsOpen() noexcept
    {
        return Card::Current().IsOpen();
    }
}
