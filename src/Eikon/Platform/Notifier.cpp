#include "pch.h"
#include "Platform/Notifier.h"

#include "Core/Settings.h"
#include "Platform/Graphics.h"
#include "Strings.h"

#include <windowsx.h>

#include <winrt/Microsoft.Windows.AppNotifications.h>
#include <winrt/Microsoft.Windows.AppNotifications.Builder.h>

#include <chrono>
#include <cmath>

namespace Eikon::Notifier
{
    namespace
    {
        using namespace winrt::Microsoft::Windows::AppNotifications;
        using namespace winrt::Microsoft::Windows::AppNotifications::Builder;
        using Clock = std::chrono::steady_clock;

        // ---- constants (DIPs) ----
        constexpr double kWindowWidth = 422;
        constexpr double kWindowHeight = 122;
        constexpr D2D1_RECT_F kCard{ 31, 24, 391, 78 };       // viewNotification 360×54, insets 31/24
        constexpr double kStartAbove = 59;                    // origin maxY − 63 (y up): top 59 pt above the edge
        constexpr auto kSlideDuration = std::chrono::milliseconds(115); // for 86 pt
        constexpr auto kFadeDuration = std::chrono::milliseconds(200);
        constexpr auto kSnapBackDuration = std::chrono::milliseconds(150);
        constexpr double kHoverExitHide = 0.8;                // Mouse out -> hide after 0.8 s
        constexpr double kCloseThreshold = 30;                // closeThreshold
        constexpr double kMinAlphaDuringDrag = 0.05;          // minAlphaDuringDrag
        constexpr double kActiveScreenWindow = 2.0;           // lastActiveTime within 2 s

        constexpr UINT_PTR kTimerHide = 1;
        constexpr UINT_PTR kTimerAnimation = 2;

        bool g_registered = false;
        std::function<void()> g_toastAction;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue g_dispatcher{ nullptr };
        HMONITOR g_lastActiveMonitor = nullptr;
        Clock::time_point g_lastActiveTime{};

        void Log(std::wstring const& text)
        {
            OutputDebugStringW((L"[Eikon] " + text + L"\n").c_str());
        }

        double EaseInOut(double t) noexcept
        {
            return t < 0.5 ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2;
        }

        // ---- The custom window (WinNotifier + Notifier's custom* views) ----
        class Window
        {
        public:
            static Window& Current()
            {
                static Window window;
                return window;
            }

            void Show(Notification notification);
            void Hide();
            void Destroy();

        private:
            enum class State { Hidden, Shown, Closing };
            enum class Animation { None, Slide, Fade, SnapBack };

            static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
            LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam);
            bool EnsureWindow();
            void Draw();
            void Push() { m_surface.Update(m_hwnd, { m_x, m_top }, static_cast<BYTE>(std::lround(m_alpha * 255))); }
            void StartAnimation(Animation kind, std::chrono::milliseconds duration, int toTop, double toAlpha);
            void StepAnimation();
            void ScheduleHide(double seconds);
            void BeginClosing();
            D2D1_RECT_F ButtonRect() const;
            bool InCard(POINT client) const;
            bool InButton(POINT client) const;
            float ButtonWidth() const;

            HWND m_hwnd = nullptr;
            Gfx::LayeredSurface m_surface;
            Notification m_current;
            State m_state = State::Hidden;
            double m_scale = 1.0;
            int m_x = 0;
            int m_top = 0;
            int m_restTop = 0;
            double m_alpha = 1.0;
            HMONITOR m_monitor = nullptr;

            Animation m_animation = Animation::None;
            Clock::time_point m_animationStart{};
            std::chrono::milliseconds m_animationDuration{};
            int m_fromTop = 0;
            int m_toTop = 0;
            double m_fromAlpha = 1.0;
            double m_toAlpha = 1.0;

            bool m_hover = false;
            bool m_tracking = false;
            bool m_buttonPressed = false;
            bool m_dragging = false;
            POINT m_dragStart{};
            int m_dragStartTop = 0;
            double m_originalAlpha = 1.0;
            double m_totalDelta = 0; // DIPs
        };

        bool Window::EnsureWindow()
        {
            if (m_hwnd)
            {
                return true;
            }
            static ATOM const atom = [] {
                WNDCLASSEXW wc{ sizeof(wc) };
                wc.lpfnWndProc = &Window::Proc;
                wc.hInstance = GetModuleHandleW(nullptr);
                wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                wc.lpszClassName = L"EikonNotification";
                return RegisterClassExW(&wc);
            }();
            if (!atom)
            {
                return false;
            }
            // Level 101 (pop-up menu level), all Spaces, not activating -> topmost tool window that
            // never takes focus.
            m_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"EikonNotification", L"Lucida",
                                     WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), this);
            return m_hwnd != nullptr;
        }

        float Window::ButtonWidth() const
        {
            // Hugging button: title width + the CtrlButton's 10 pt padding on each side; 45 when
            // there is no title. A hidden button keeps its constraints, so
            // the text column is the same width with or without it.
            std::wstring const& title = m_current.buttonTitle;
            if (title.empty())
            {
                return 45.0f;
            }
            auto format = Gfx::TextFormat(12.0f, DWRITE_FONT_WEIGHT_BOLD);
            winrt::com_ptr<IDWriteTextLayout> layout;
            if (!format || FAILED(Gfx::DWrite()->CreateTextLayout(title.c_str(), static_cast<UINT32>(title.size()), format.get(), 1000, 100,
                                                                   layout.put())))
            {
                return 45.0f;
            }
            DWRITE_TEXT_METRICS metrics{};
            layout->GetMetrics(&metrics);
            return (std::max)(45.0f, std::ceil(metrics.widthIncludingTrailingWhitespace) + 24.0f);
        }

        D2D1_RECT_F Window::ButtonRect() const
        {
            float const w = ButtonWidth();
            // trailing 14, top 13, height 28 inside the card.
            return { kCard.right - 14 - w, kCard.top + 13, kCard.right - 14, kCard.top + 41 };
        }

        bool Window::InCard(POINT client) const
        {
            double const x = client.x / m_scale;
            double const y = client.y / m_scale;
            return x >= kCard.left && x < kCard.right && y >= kCard.top && y < kCard.bottom;
        }

        bool Window::InButton(POINT client) const
        {
            if (m_current.buttonTitle.empty())
            {
                return false;
            }
            D2D1_RECT_F const r = ButtonRect();
            double const x = client.x / m_scale;
            double const y = client.y / m_scale;
            return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
        }

        void Window::Draw()
        {
            bool const dark = Gfx::IsDarkMode();
            SIZE const size{ static_cast<LONG>(std::lround(kWindowWidth * m_scale)), static_cast<LONG>(std::lround(kWindowHeight * m_scale)) };
            m_surface.Render(m_hwnd, { m_x, m_top }, size, m_scale, static_cast<BYTE>(std::lround(m_alpha * 255)), [&](ID2D1DeviceContext* dc) {
                // Candy: a floating 16-px card on the surface colour with the Candy shadow. High contrast: the window colours.
                bool const contrast = Candy::HighContrast();
                auto const& candy = Candy::For(dark);
                D2D1_RECT_F const card = kCard;
                if (contrast)
                {
                    Gfx::FillRoundedRect(dc, card, Candy::kRadiusFloat, Gfx::SystemColor(COLOR_WINDOW));
                    Gfx::StrokeRoundedRect(dc, card, Candy::kRadiusFloat, Gfx::SystemColor(COLOR_WINDOWTEXT), 1);
                }
                else
                {
                    Gfx::DrawCandyShadow(dc, card, Candy::kRadiusFloat, dark);
                    Gfx::FillRoundedRect(dc, card, Candy::kRadiusFloat, Gfx::FromArgb(candy.surface));
                }

                // The app mark: the gradient tile (24-notification-logo), 34×34 at (12, 10); a colour swatch the same
                // size; the warning icon at 24×24 in the middle of that slot.
                D2D1_RECT_F const slot{ card.left + 12, card.top + 10, card.left + 46, card.top + 44 };
                if (m_current.swatch)
                {
                    std::uint32_t const rgb = *m_current.swatch;
                    D2D1_COLOR_F const color = Gfx::Rgba(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1);
                    Gfx::FillRoundedRect(dc, slot, 10, color);
                    Gfx::StrokeRoundedRect(dc, slot, 10, contrast ? Gfx::SystemColor(COLOR_WINDOWTEXT) : Gfx::FromArgb(candy.line), 1);
                }
                else if (m_current.warning)
                {
                    Gfx::DrawAsset(dc, Gfx::Asset(L"24-notification-warning", dark).get(), { slot.left + 5, slot.top + 5, slot.right - 5, slot.bottom - 5 });
                }
                else
                {
                    Gfx::DrawAsset(dc, Gfx::Asset(L"24-notification-logo", false).get(), slot);
                }

                // Title (13 px bold, ink) at (58, 9), message (12 px, ink 2) under it, one line each; the column
                // ends 12 before the button.
                float const textRight = card.right - 14 - ButtonWidth() - 12;
                D2D1_COLOR_F const ink = contrast ? Gfx::SystemColor(COLOR_WINDOWTEXT) : Gfx::FromArgb(candy.ink);
                D2D1_COLOR_F const ink2 = contrast ? Gfx::SystemColor(COLOR_WINDOWTEXT) : Gfx::FromArgb(candy.ink2);
                winrt::com_ptr<ID2D1SolidColorBrush> label;
                dc->CreateSolidColorBrush(ink, label.put());
                auto drawText = [&](std::wstring const& text, float size, DWRITE_FONT_WEIGHT weight, D2D1_RECT_F box, bool wordTrim, D2D1_COLOR_F color) {
                    auto format = Gfx::TextFormat(size, weight);
                    if (!format || text.empty())
                    {
                        return;
                    }
                    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                    if (wordTrim)
                    {
                        // Single-line word wrapping: cut after the last whole word, no ellipsis.
                        DWRITE_TRIMMING trimming{ DWRITE_TRIMMING_GRANULARITY_WORD, 0, 0 };
                        format->SetTrimming(&trimming, nullptr);
                    }
                    // Single-line text fields: a line break shows as a space.
                    std::wstring line = text;
                    std::replace(line.begin(), line.end(), L'\n', L' ');
                    label->SetColor(color);
                    dc->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED);
                    dc->DrawText(line.c_str(), static_cast<UINT32>(line.size()), format.get(), box, label.get());
                    dc->PopAxisAlignedClip();
                };
                drawText(m_current.title, 13.0f, DWRITE_FONT_WEIGHT_BOLD, { card.left + 58, card.top + 9, textRight, card.top + 27 }, false, ink);
                drawText(m_current.message, 12.0f, DWRITE_FONT_WEIGHT_NORMAL, { card.left + 58, card.top + 27, textRight, card.top + 44 }, true, ink2);

                // Candy's secondary (ghost) button: surface α 0.7 with a 1-px line ring, radius 8, 12-px bold ink text;
                // surface 2 while pressed.
                if (!m_current.buttonTitle.empty())
                {
                    D2D1_RECT_F const button = ButtonRect();
                    if (contrast)
                    {
                        Gfx::FillRoundedRect(dc, button, Candy::kRadiusControl, Gfx::SystemColor(COLOR_BTNFACE));
                        Gfx::StrokeRoundedRect(dc, button, Candy::kRadiusControl, Gfx::SystemColor(COLOR_BTNTEXT), 1);
                    }
                    else
                    {
                        Gfx::FillRoundedRect(dc, button, Candy::kRadiusControl,
                                             m_buttonPressed ? Gfx::FromArgb(candy.surface2) : Gfx::FromArgb(candy.surface, 0.7f));
                        Gfx::StrokeRoundedRect(dc, button, Candy::kRadiusControl, Gfx::FromArgb(candy.line), 1);
                    }
                    label->SetColor(contrast ? Gfx::SystemColor(COLOR_BTNTEXT) : ink);
                    auto format = Gfx::TextFormat(12.0f, DWRITE_FONT_WEIGHT_BOLD);
                    if (format)
                    {
                        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                        dc->DrawText(m_current.buttonTitle.c_str(), static_cast<UINT32>(m_current.buttonTitle.size()), format.get(), button,
                                     label.get());
                    }
                }
            });
        }

        void Window::Show(Notification notification)
        {
            if (!EnsureWindow())
            {
                return;
            }
            KillTimer(m_hwnd, kTimerHide);
            KillTimer(m_hwnd, kTimerAnimation);
            m_animation = Animation::None;
            m_dragging = false;
            m_buttonPressed = false;
            if (GetCapture() == m_hwnd)
            {
                ReleaseCapture();
            }
            m_current = std::move(notification);

            // Monitor: where the editor was hidden if that was within 2 s, else where this window
            // last was, else the primary monitor.
            HMONITOR monitor = nullptr;
            double const sinceActive = std::chrono::duration<double>(Clock::now() - g_lastActiveTime).count();
            if (g_lastActiveMonitor && sinceActive < kActiveScreenWindow)
            {
                monitor = g_lastActiveMonitor;
            }
            else if (m_monitor)
            {
                monitor = m_monitor;
            }
            MONITORINFO info{ sizeof(info) };
            if (!monitor || !GetMonitorInfoW(monitor, &info))
            {
                monitor = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
                GetMonitorInfoW(monitor, &info);
            }
            m_monitor = monitor;
            m_scale = Gfx::ScaleForMonitor(monitor);

            // Centred horizontally; slides down from 59 pt above the edge to the work-area top.
            int const width = static_cast<int>(std::lround(kWindowWidth * m_scale));
            m_x = (info.rcMonitor.left + info.rcMonitor.right) / 2 - width / 2;
            m_restTop = info.rcWork.top;
            m_top = m_restTop - static_cast<int>(std::lround(kStartAbove * m_scale));
            m_alpha = 1.0;
            m_state = State::Shown;
            Draw();
            SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            StartAnimation(Animation::Slide, kSlideDuration, m_restTop, 1.0);
            if (m_current.seconds > 0 && !m_hover)
            {
                ScheduleHide(m_current.seconds);
            }
        }

        void Window::ScheduleHide(double seconds)
        {
            // Only while shown; marks "closing" and fades after `seconds`.
            if (!m_hwnd || m_state == State::Hidden)
            {
                return;
            }
            KillTimer(m_hwnd, kTimerHide);
            SetTimer(m_hwnd, kTimerHide, static_cast<UINT>(std::lround(seconds * 1000)), nullptr);
        }

        void Window::BeginClosing()
        {
            KillTimer(m_hwnd, kTimerHide);
            m_state = State::Closing;
            StartAnimation(Animation::Fade, kFadeDuration, m_top, 0.0);
        }

        void Window::Hide()
        {
            if (!m_hwnd)
            {
                return;
            }
            KillTimer(m_hwnd, kTimerHide);
            KillTimer(m_hwnd, kTimerAnimation);
            m_animation = Animation::None;
            m_state = State::Hidden;
            m_dragging = false;
            m_hover = false;
            if (GetCapture() == m_hwnd)
            {
                ReleaseCapture();
            }
            ShowWindow(m_hwnd, SW_HIDE);
            m_alpha = 1.0;
        }

        void Window::Destroy()
        {
            if (m_hwnd)
            {
                DestroyWindow(m_hwnd);
                m_hwnd = nullptr;
            }
        }

        void Window::StartAnimation(Animation kind, std::chrono::milliseconds duration, int toTop, double toAlpha)
        {
            m_animation = kind;
            m_animationStart = Clock::now();
            m_animationDuration = duration;
            m_fromTop = m_top;
            m_toTop = toTop;
            m_fromAlpha = m_alpha;
            m_toAlpha = toAlpha;
            SetTimer(m_hwnd, kTimerAnimation, 10, nullptr);
            StepAnimation();
        }

        void Window::StepAnimation()
        {
            if (m_animation == Animation::None)
            {
                KillTimer(m_hwnd, kTimerAnimation);
                return;
            }
            double const elapsed = std::chrono::duration<double, std::milli>(Clock::now() - m_animationStart).count();
            double const t = m_animationDuration.count() > 0 ? (std::min)(1.0, elapsed / m_animationDuration.count()) : 1.0;
            double const e = EaseInOut(t);
            m_top = m_fromTop + static_cast<int>(std::lround((m_toTop - m_fromTop) * e));
            m_alpha = m_fromAlpha + (m_toAlpha - m_fromAlpha) * e;
            Push();
            if (t >= 1.0)
            {
                Animation const finished = m_animation;
                m_animation = Animation::None;
                KillTimer(m_hwnd, kTimerAnimation);
                if (finished == Animation::Fade)
                {
                    Hide(); // Close, alpha back to 1, "hidden"
                }
            }
        }

        LRESULT CALLBACK Window::Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == WM_NCCREATE)
            {
                auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            }
            auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self && self->m_hwnd == hwnd)
            {
                try
                {
                    return self->OnMessage(message, wParam, lParam);
                }
                catch (...)
                {
                    Log(L"notification window: exception in a message handler");
                }
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        LRESULT Window::OnMessage(UINT message, WPARAM wParam, LPARAM lParam)
        {
            switch (message)
            {
            case WM_MOUSEACTIVATE:
                return MA_NOACTIVATE;
            case WM_TIMER:
                if (wParam == kTimerHide)
                {
                    KillTimer(m_hwnd, kTimerHide);
                    if (m_state != State::Hidden)
                    {
                        BeginClosing();
                    }
                }
                else if (wParam == kTimerAnimation)
                {
                    StepAnimation();
                }
                return 0;
            case WM_MOUSEMOVE:
            {
                POINT const client{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                if (!m_tracking)
                {
                    TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, m_hwnd, 0 };
                    m_tracking = TrackMouseEvent(&track) != FALSE;
                }
                if (m_dragging)
                {
                    POINT screen{};
                    GetCursorPos(&screen);
                    int const deltaPx = screen.y - m_dragStart.y;
                    m_totalDelta = deltaPx / m_scale;
                    m_top = m_dragStartTop + deltaPx;
                    double const progress = (std::min)(std::abs(m_totalDelta) / kCloseThreshold, 1.0);
                    m_alpha = (std::max)(kMinAlphaDuringDrag, m_originalAlpha - progress * (m_originalAlpha - kMinAlphaDuringDrag));
                    Push();
                    if (std::abs(m_totalDelta) >= kCloseThreshold)
                    {
                        Hide();
                    }
                    return 0;
                }
                bool const inside = InCard(client);
                if (inside != m_hover)
                {
                    m_hover = inside;
                    if (inside)
                    {
                        // CtrlMouseListener: inside -> the hide timer stops. A fade in progress is
                        // undone so the card doesn't stay invisible under the pointer.
                        KillTimer(m_hwnd, kTimerHide);
                        if (m_animation == Animation::Fade)
                        {
                            m_animation = Animation::None;
                            KillTimer(m_hwnd, kTimerAnimation);
                            m_alpha = 1.0;
                            m_state = State::Shown;
                            Push();
                        }
                    }
                    else if (m_state != State::Hidden)
                    {
                        ScheduleHide(kHoverExitHide);
                    }
                }
                return 0;
            }
            case WM_MOUSELEAVE:
                m_tracking = false;
                if (m_hover && !m_dragging)
                {
                    m_hover = false;
                    if (m_state != State::Hidden)
                    {
                        ScheduleHide(kHoverExitHide);
                    }
                }
                return 0;
            case WM_LBUTTONDOWN:
            {
                POINT const client{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                SetCapture(m_hwnd);
                if (InButton(client))
                {
                    m_buttonPressed = true;
                    Draw();
                    return 0;
                }
                // sendEvent: a mouse down outside the button starts a vertical drag.
                m_dragging = true;
                GetCursorPos(&m_dragStart);
                m_dragStartTop = m_top;
                m_totalDelta = 0;
                m_originalAlpha = m_alpha;
                if (m_animation == Animation::Slide)
                {
                    m_animation = Animation::None;
                    KillTimer(m_hwnd, kTimerAnimation);
                    m_top = m_restTop;
                    m_dragStartTop = m_top;
                    Push();
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
                if (m_buttonPressed)
                {
                    m_buttonPressed = false;
                    if (InButton(client))
                    {
                        // customBtnClick: hide first, then run the action.
                        auto action = m_current.action;
                        Hide();
                        if (action)
                        {
                            action();
                        }
                    }
                    else
                    {
                        Draw();
                    }
                    return 0;
                }
                if (m_dragging)
                {
                    m_dragging = false;
                    if (std::abs(m_totalDelta) >= kCloseThreshold)
                    {
                        Hide();
                    }
                    else
                    {
                        // Snap back to the start position and alpha over 0.15 s.
                        StartAnimation(Animation::SnapBack, kSnapBackDuration, m_dragStartTop, m_originalAlpha);
                    }
                }
                return 0;
            }
            case WM_CAPTURECHANGED:
                if (m_dragging && reinterpret_cast<HWND>(lParam) != m_hwnd)
                {
                    m_dragging = false;
                    StartAnimation(Animation::SnapBack, kSnapBackDuration, m_dragStartTop, m_originalAlpha);
                }
                return 0;
            case WM_SETTINGCHANGE:
                if (m_state != State::Hidden && lParam && std::wstring_view{ reinterpret_cast<wchar_t const*>(lParam) } == L"ImmersiveColorSet")
                {
                    Draw();
                }
                break;
            default:
                break;
            }
            return DefWindowProcW(m_hwnd, message, wParam, lParam);
        }

        bool SystemNotificationsAllowed()
        {
            if (!g_registered)
            {
                return false;
            }
            try
            {
                return AppNotificationManager::Default().Setting() == AppNotificationSetting::Enabled;
            }
            catch (...)
            {
                return false;
            }
        }

        bool ShowToast(Notification& notification)
        {
            try
            {
                AppNotificationBuilder builder;
                builder.AddText(notification.title);
                if (!notification.message.empty())
                {
                    builder.AddText(notification.message);
                }
                if (!notification.buttonTitle.empty())
                {
                    builder.AddButton(AppNotificationButton(notification.buttonTitle).AddArgument(L"action", L"button"));
                }
                builder.AddArgument(L"action", L"body");
                auto toast = builder.BuildNotification();
                toast.Expiration(winrt::clock::now() + std::chrono::milliseconds(static_cast<std::int64_t>((std::max)(notification.seconds, 1.0) * 1000)));
                g_toastAction = notification.action;
                AppNotificationManager::Default().Show(toast);
                return true;
            }
            catch (...)
            {
                Log(L"showing a system notification failed");
                return false;
            }
        }
    }

    void Initialize()
    {
        try
        {
            g_dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
            // Must be subscribed before Register() so clicks reach this process.
            AppNotificationManager::Default().NotificationInvoked([](auto&&, AppNotificationActivatedEventArgs const&) {
                if (g_dispatcher)
                {
                    g_dispatcher.TryEnqueue([] {
                        if (auto action = g_toastAction)
                        {
                            try
                            {
                                action();
                            }
                            catch (...)
                            {
                            }
                        }
                    });
                }
            });
            AppNotificationManager::Default().Register();
            g_registered = true;
        }
        catch (...)
        {
            Log(L"app notifications are unavailable");
        }
    }

    void Shutdown()
    {
        Window::Current().Destroy();
        if (!g_registered)
        {
            return;
        }
        try
        {
            AppNotificationManager::Default().Unregister();
        }
        catch (...)
        {
        }
        g_registered = false;
    }

    void Show(Notification notification)
    {
        if (notification.beep)
        {
            MessageBeep(MB_ICONWARNING);
        }
        std::wstring const type = Core::Settings::Current().NotificationType();
        Log(L"notification (" + type + L"): " + notification.title + L" / " + notification.message);
        if (type == L"none")
        {
            return;
        }
        if (type == L"system")
        {
            if (SystemNotificationsAllowed() && ShowToast(notification))
            {
                return;
            }
            notification.message += L"\n" + Strings::Get(L"Notification_NoSystemPermission");
        }
        Window::Current().Show(std::move(notification));
    }

    void Hide()
    {
        Window::Current().Hide();
    }

    void SetLastActiveMonitor(HMONITOR monitor)
    {
        g_lastActiveMonitor = monitor;
        g_lastActiveTime = Clock::now();
    }
}
