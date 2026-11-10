#include "pch.h"
#include "Platform/QuickLook.h"

#include "Platform/ImageCodec.h"

namespace Eikon
{
    namespace
    {
        constexpr wchar_t kPanelClass[] = L"EikonQuickLook";
        constexpr wchar_t kCurtainClass[] = L"EikonQuickLookCurtain";
        constexpr double kMaxWidth = 1200;     // points
        constexpr double kSideMargin = 40;     // available width - 40
        constexpr double kVerticalMargin = 50; // visible height - 50
        constexpr double kGap = 16;            // caller minX - width - 16
        constexpr double kTopOffset = 30;      // top at caller maxY - 30
        constexpr BYTE kCurtainAlpha = 51;     // black at 0.2

        double ScaleOf(HMONITOR monitor)
        {
            UINT dpiX = USER_DEFAULT_SCREEN_DPI;
            UINT dpiY = USER_DEFAULT_SCREEN_DPI;
            if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) || dpiX == 0)
            {
                return 1.0;
            }
            return static_cast<double>(dpiX) / USER_DEFAULT_SCREEN_DPI;
        }

        void RegisterClasses()
        {
            static bool registered = false;
            if (registered)
            {
                return;
            }
            registered = true;
            WNDCLASSEXW panel{ sizeof(panel) };
            panel.style = CS_DROPSHADOW;
            panel.lpfnWndProc = [](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
                if (message == WM_NCCREATE)
                {
                    auto const* create = reinterpret_cast<CREATESTRUCTW const*>(lParam);
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
                }
                return DefWindowProcW(hwnd, message, wParam, lParam);
            };
            panel.hInstance = GetModuleHandleW(nullptr);
            panel.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            panel.lpszClassName = kPanelClass;
            RegisterClassExW(&panel);

            WNDCLASSEXW curtain{ sizeof(curtain) };
            curtain.lpfnWndProc = DefWindowProcW;
            curtain.hInstance = GetModuleHandleW(nullptr);
            curtain.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
            curtain.lpszClassName = kCurtainClass;
            RegisterClassExW(&curtain);
        }
    }

    QuickLookPanel::~QuickLookPanel()
    {
        if (m_hwnd)
        {
            DestroyWindow(m_hwnd);
        }
        if (m_curtain)
        {
            DestroyWindow(m_curtain);
        }
        ReleaseBitmap();
    }

    void QuickLookPanel::ReleaseBitmap()
    {
        if (m_bitmap)
        {
            DeleteObject(m_bitmap);
            m_bitmap = nullptr;
        }
    }

    void QuickLookPanel::EnsureWindows()
    {
        RegisterClasses();
        if (!m_hwnd)
        {
            m_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kPanelClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr,
                                     nullptr, GetModuleHandleW(nullptr), this);
            if (m_hwnd)
            {
                SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&QuickLookPanel::WndProc));
                // Rounded corners and the system shadow.
                DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
                DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
            }
        }
        if (!m_curtain)
        {
            m_curtain = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT, kCurtainClass,
                                        L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (m_curtain)
            {
                SetLayeredWindowAttributes(m_curtain, 0, kCurtainAlpha, LWA_ALPHA);
            }
        }
    }

    bool QuickLookPanel::Show(std::wstring const& path, double pixelWidth, double pixelHeight, double dpr, HWND caller, bool curtain)
    {
        if (pixelWidth <= 0 || pixelHeight <= 0)
        {
            return false;
        }
        EnsureWindows();
        if (!m_hwnd)
        {
            return false;
        }
        RECT callerRect{};
        GetWindowRect(caller, &callerRect);
        HMONITOR const monitor = MonitorFromWindow(caller, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{ sizeof(info) };
        GetMonitorInfoW(monitor, &info);
        double const scale = ScaleOf(monitor);
        RECT const work = info.rcWork;
        double const workWidth = (work.right - work.left) / scale;
        double const workHeight = (work.bottom - work.top) / scale;

        // Width = min(image width in points, available width - 40, 1200); the height follows the aspect
        // ratio and is reduced, with the width, to the visible height - 50.
        double const aspect = pixelHeight / pixelWidth;
        double width = (std::min)({ pixelWidth / (dpr > 0 ? dpr : 1.0), workWidth - kSideMargin, kMaxWidth });
        double height = width * aspect;
        if (height > workHeight - kVerticalMargin)
        {
            height = workHeight - kVerticalMargin;
            width = height / aspect;
        }
        int const w = (std::max)(1, static_cast<int>(std::lround(width * scale)));
        int const h = (std::max)(1, static_cast<int>(std::lround(height * scale)));

        auto pixels = ImageCodec::DecodeFileScaled(path.c_str(), static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), true);
        if (pixels.empty())
        {
            return false;
        }
        // Composite over the window background (premultiplied), so transparent screenshots show the
        // panel colour rather than black.
        COLORREF const background = GetSysColor(COLOR_WINDOW);
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
        {
            unsigned const inverse = 255u - pixels[i + 3];
            pixels[i + 0] = static_cast<std::uint8_t>(pixels[i + 0] + GetBValue(background) * inverse / 255u);
            pixels[i + 1] = static_cast<std::uint8_t>(pixels[i + 1] + GetGValue(background) * inverse / 255u);
            pixels[i + 2] = static_cast<std::uint8_t>(pixels[i + 2] + GetRValue(background) * inverse / 255u);
            pixels[i + 3] = 255;
        }
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HDC screen = GetDC(nullptr);
        HBITMAP bitmap = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (!bitmap || !bits)
        {
            return false;
        }
        std::memcpy(bits, pixels.data(), pixels.size());
        ReleaseBitmap();
        m_bitmap = bitmap;
        m_width = w;
        m_height = h;

        // Left of the caller when it fits there, otherwise centred on the screen.
        int x = callerRect.left - w - static_cast<int>(std::lround(kGap * scale));
        int y = callerRect.top + static_cast<int>(std::lround(kTopOffset * scale));
        if (x < work.left)
        {
            x = work.left + (work.right - work.left - w) / 2;
            y = work.top + (work.bottom - work.top - h) / 2;
        }
        y = (std::max)(static_cast<int>(work.top), (std::min)(y, static_cast<int>(work.bottom) - h));

        if (curtain && m_curtain)
        {
            RECT const full = info.rcMonitor;
            // Behind the Library, which stays above it.
            SetWindowPos(m_curtain, caller, full.left, full.top, full.right - full.left, full.bottom - full.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        else if (m_curtain)
        {
            ShowWindow(m_curtain, SW_HIDE);
        }
        SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return true;
    }

    void QuickLookPanel::Hide()
    {
        if (m_hwnd)
        {
            ShowWindow(m_hwnd, SW_HIDE);
        }
        if (m_curtain)
        {
            ShowWindow(m_curtain, SW_HIDE);
        }
        ReleaseBitmap();
    }

    bool QuickLookPanel::IsVisible() const noexcept
    {
        return m_hwnd && IsWindowVisible(m_hwnd);
    }

    void QuickLookPanel::Paint(HWND hwnd)
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        if (m_bitmap)
        {
            HDC memory = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(memory, m_bitmap);
            BitBlt(dc, 0, 0, m_width, m_height, memory, 0, 0, SRCCOPY);
            SelectObject(memory, old);
            DeleteDC(memory);
        }
        EndPaint(hwnd, &paint);
    }

    LRESULT CALLBACK QuickLookPanel::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<QuickLookPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (message)
        {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if (self)
            {
                self->Paint(hwnd);
                return 0;
            }
            break;
        default:
            break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
