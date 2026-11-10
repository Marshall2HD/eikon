#include "pch.h"
#include "Platform/ScreenCapture.h"

#include "Platform/ImageLoader.h"

#include <wil/com.h>
#include <shobjidl_core.h>

#include <cmath>
#include <cstring>
#include <set>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "version.lib")

namespace Eikon::ScreenCapture
{
    namespace
    {
        void Log(std::wstring_view message)
        {
            std::wstring line{ L"[Eikon] " };
            line.append(message);
            line.push_back(L'\n');
            ::OutputDebugStringW(line.c_str());
        }

        std::set<HWND>& Overlays()
        {
            static std::set<HWND> overlays;
            return overlays;
        }

        Monitor Describe(HMONITOR handle)
        {
            Monitor monitor;
            monitor.handle = handle;
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(handle, &info))
            {
                monitor.bounds = info.rcMonitor;
                monitor.workArea = info.rcWork;
                monitor.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
                monitor.device = info.szDevice;
            }
            UINT dpiX = USER_DEFAULT_SCREEN_DPI;
            UINT dpiY = USER_DEFAULT_SCREEN_DPI;
            if (SUCCEEDED(GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX > 0)
            {
                monitor.scale = static_cast<double>(dpiX) / USER_DEFAULT_SCREEN_DPI;
            }
            return monitor;
        }

        BOOL CALLBACK Collect(HMONITOR handle, HDC, LPRECT, LPARAM data)
        {
            reinterpret_cast<std::vector<Monitor>*>(data)->push_back(Describe(handle));
            return TRUE;
        }

        double ScaleAt(POINT point)
        {
            HMONITOR const handle = ::MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
            return handle ? Describe(handle).scale : 1.0;
        }

        void DrawCursor(HDC dc, RECT const& bounds)
        {
            CURSORINFO cursor{};
            cursor.cbSize = sizeof(cursor);
            if (!GetCursorInfo(&cursor) || !(cursor.flags & CURSOR_SHOWING) || !cursor.hCursor)
            {
                return;
            }
            ICONINFO icon{};
            if (!GetIconInfo(cursor.hCursor, &icon))
            {
                return;
            }
            wil::unique_hbitmap mask{ icon.hbmMask };
            wil::unique_hbitmap color{ icon.hbmColor };
            int const x = cursor.ptScreenPos.x - static_cast<int>(icon.xHotspot) - bounds.left;
            int const y = cursor.ptScreenPos.y - static_cast<int>(icon.yHotspot) - bounds.top;
            DrawIconEx(dc, x, y, cursor.hCursor, 0, 0, 0, nullptr, DI_NORMAL | DI_DEFAULTSIZE);
        }

        // A top-down 32bpp DIB section selected into a memory DC.
        struct Dib
        {
            wil::unique_hdc dc;
            wil::unique_hbitmap bitmap;
            HGDIOBJ previous = nullptr;
            void* bits = nullptr;
            int width = 0;
            int height = 0;

            ~Dib()
            {
                if (dc && previous)
                {
                    SelectObject(dc.get(), previous);
                }
            }

            bool Create(HDC reference, int w, int h)
            {
                width = w;
                height = h;
                BITMAPINFO info{};
                info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth = w;
                info.bmiHeader.biHeight = -h;
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = 32;
                info.bmiHeader.biCompression = BI_RGB;
                bitmap.reset(CreateDIBSection(reference, &info, DIB_RGB_COLORS, &bits, nullptr, 0));
                dc.reset(CreateCompatibleDC(reference));
                if (!bitmap || !bits || !dc)
                {
                    return false;
                }
                previous = SelectObject(dc.get(), bitmap.get());
                return true;
            }

            // Copies the sub-rectangle into an opaque bitmap (GDI leaves alpha undefined).
            Core::Bitmap Extract(int x, int y, int w, int h, double dpr) const
            {
                Core::Bitmap out;
                out.width = w;
                out.height = h;
                out.dpr = dpr;
                out.pixels.resize(out.Stride() * static_cast<std::size_t>(h));
                auto const* source = static_cast<std::uint8_t const*>(bits);
                for (int row = 0; row < h; ++row)
                {
                    std::memcpy(out.pixels.data() + static_cast<std::size_t>(row) * out.Stride(),
                                source + (static_cast<std::size_t>(y + row) * width + x) * 4, out.Stride());
                }
                for (std::size_t i = 3; i < out.pixels.size(); i += 4)
                {
                    out.pixels[i] = 0xFF;
                }
                return out;
            }
        };

        std::optional<Core::Bitmap> Blit(RECT const& rect, bool includeCursor, double dpr)
        {
            int const width = rect.right - rect.left;
            int const height = rect.bottom - rect.top;
            if (width <= 0 || height <= 0)
            {
                return std::nullopt;
            }
            HDC const screen = GetDC(nullptr);
            if (!screen)
            {
                return std::nullopt;
            }
            auto releaseScreen = wil::scope_exit([&] { ReleaseDC(nullptr, screen); });
            Dib dib;
            if (!dib.Create(screen, width, height))
            {
                return std::nullopt;
            }
            BOOL const copied = BitBlt(dib.dc.get(), 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT);
            if (!copied)
            {
                Log(L"BitBlt from the screen failed");
                return std::nullopt;
            }
            if (includeCursor)
            {
                DrawCursor(dib.dc.get(), rect);
            }
            GdiFlush();
            Core::Bitmap bitmap = dib.Extract(0, 0, width, height, dpr);
            bitmap.screenRect = Core::Rect{ static_cast<double>(rect.left), static_cast<double>(rect.top), static_cast<double>(width), static_cast<double>(height) };
            return bitmap;
        }

        bool IsCloaked(HWND window)
        {
            DWORD cloaked = 0;
            return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
        }

        RECT FrameBounds(HWND window)
        {
            RECT bounds{};
            if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))))
            {
                GetWindowRect(window, &bounds);
            }
            return bounds;
        }

        std::wstring ClassName(HWND window)
        {
            wchar_t name[64]{};
            GetClassNameW(window, name, ARRAYSIZE(name));
            return name;
        }

        bool IsShellWindow(HWND window)
        {
            auto const name = ClassName(window);
            return name == L"Progman" || name == L"WorkerW" || name == L"Shell_TrayWnd" || name == L"Shell_SecondaryTrayWnd";
        }

        // Visible, on screen and not one of ours: the common part of every window filter.
        bool IsOnScreen(HWND window)
        {
            if (!IsWindowVisible(window) || IsIconic(window) || IsCloaked(window) || Overlays().contains(window) || IsShellWindow(window))
            {
                return false;
            }
            auto const exStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
            if ((exStyle & WS_EX_TOOLWINDOW) != 0)
            {
                return false;
            }
            // Click-through layered windows (overlays of other apps) are not windows a user picks.
            if ((exStyle & WS_EX_LAYERED) != 0 && (exStyle & WS_EX_TRANSPARENT) != 0)
            {
                return false;
            }
            RECT const bounds = FrameBounds(window);
            return bounds.right > bounds.left && bounds.bottom > bounds.top;
        }

        bool IsTopmost(HWND window)
        {
            return (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        }

        std::wstring WindowTitle(HWND window)
        {
            int const length = GetWindowTextLengthW(window);
            std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
            int const copied = GetWindowTextW(window, title.data(), length + 1);
            title.resize(static_cast<std::size_t>((std::max)(copied, 0)));
            return title;
        }

        // kCGWindowOwnerName: the FileDescription of the process, else the exe name without extension.
        std::wstring OwnerName(HWND window)
        {
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            wil::unique_handle process{ OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) };
            if (!process)
            {
                return {};
            }
            std::wstring path(MAX_PATH, L'\0');
            DWORD size = static_cast<DWORD>(path.size());
            if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &size))
            {
                return {};
            }
            path.resize(size);
            DWORD handle = 0;
            DWORD const infoSize = GetFileVersionInfoSizeW(path.c_str(), &handle);
            if (infoSize > 0)
            {
                std::vector<std::uint8_t> info(infoSize);
                if (GetFileVersionInfoW(path.c_str(), 0, infoSize, info.data()))
                {
                    struct Translation
                    {
                        WORD language;
                        WORD codePage;
                    };
                    Translation* translations = nullptr;
                    UINT length = 0;
                    if (VerQueryValueW(info.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &length) && length >= sizeof(Translation))
                    {
                        wchar_t key[64]{};
                        swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", translations[0].language, translations[0].codePage);
                        wchar_t* description = nullptr;
                        UINT descriptionLength = 0;
                        if (VerQueryValueW(info.data(), key, reinterpret_cast<void**>(&description), &descriptionLength) && description && descriptionLength > 1)
                        {
                            return std::wstring{ description };
                        }
                    }
                }
            }
            return std::filesystem::path{ path }.stem().wstring();
        }

        WindowInfo Basic(HWND window)
        {
            WindowInfo info;
            info.handle = window;
            info.bounds = FrameBounds(window);
            auto const exStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
            if ((exStyle & WS_EX_LAYERED) != 0)
            {
                BYTE alpha = 255;
                DWORD flags = 0;
                if (GetLayeredWindowAttributes(window, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) != 0 && alpha < 255)
                {
                    info.opaque = false;
                }
            }
            HMONITOR const monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            info.scale = monitor ? Describe(monitor).scale : 1.0;
            return info;
        }

        WindowInfo& Complete(WindowInfo& info)
        {
            info.title = WindowTitle(info.handle);
            info.app = OwnerName(info.handle);
            return info;
        }

        std::vector<HWND> TopLevelWindows()
        {
            std::vector<HWND> windows;
            EnumWindows(
                [](HWND window, LPARAM data) -> BOOL {
                    reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&windows));
            return windows;
        }

        bool Contains(RECT const& rect, POINT point)
        {
            return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
        }

        POINT Centre(RECT const& rect)
        {
            return { rect.left + (rect.right - rect.left) / 2, rect.top + (rect.bottom - rect.top) / 2 };
        }

        // Every pixel black: PrintWindow drew nothing (some GPU-rendered windows).
        bool IsBlank(Core::Bitmap const& bitmap)
        {
            for (std::size_t i = 0; i < bitmap.pixels.size(); i += 4)
            {
                if (bitmap.pixels[i] != 0 || bitmap.pixels[i + 1] != 0 || bitmap.pixels[i + 2] != 0)
                {
                    return false;
                }
            }
            return true;
        }

        // PrintWindow leaves the 1 px frame DWM draws around a Windows 11 window black; where the
        // window is on top, take those pixels from the screen so the edge looks as it does there.
        void RestoreBorder(Core::Bitmap& image, WindowInfo const& window)
        {
            int const w = image.width;
            int const h = image.height;
            if (w < 3 || h < 3)
            {
                return;
            }
            auto const screen = Blit(window.bounds, false, window.scale);
            if (!screen || screen->width != w || screen->height != h)
            {
                return;
            }
            auto fix = [&](int x, int y) {
                std::uint8_t* p = image.pixels.data() + (static_cast<std::size_t>(y) * w + x) * 4;
                if (p[0] != 0 || p[1] != 0 || p[2] != 0)
                {
                    return;
                }
                POINT const point{ window.bounds.left + x, window.bounds.top + y };
                HWND const hit = WindowFromPoint(point);
                if (hit && GetAncestor(hit, GA_ROOT) == window.handle)
                {
                    std::memcpy(p, screen->pixels.data() + (static_cast<std::size_t>(y) * w + x) * 4, 4);
                }
            };
            for (int x = 0; x < w; ++x)
            {
                fix(x, 0);
                fix(x, h - 1);
            }
            for (int y = 1; y < h - 1; ++y)
            {
                fix(0, y);
                fix(w - 1, y);
            }
        }

        // Draws `image` into `canvas` at (x, y), clipped (opaque copy).
        void Paste(Core::Bitmap& canvas, Core::Bitmap const& image, int x, int y)
        {
            for (int row = 0; row < image.height; ++row)
            {
                int const ty = y + row;
                if (ty < 0 || ty >= canvas.height)
                {
                    continue;
                }
                int const left = (std::max)(0, x);
                int const right = (std::min)(canvas.width, x + image.width);
                if (right <= left)
                {
                    return;
                }
                std::memcpy(canvas.pixels.data() + (static_cast<std::size_t>(ty) * canvas.width + left) * 4,
                            image.pixels.data() + (static_cast<std::size_t>(row) * image.width + (left - x)) * 4,
                            static_cast<std::size_t>(right - left) * 4);
            }
        }
    }

    std::vector<Monitor> EnumerateMonitors()
    {
        std::vector<Monitor> monitors;
        EnumDisplayMonitors(nullptr, nullptr, &Collect, reinterpret_cast<LPARAM>(&monitors));
        return monitors;
    }

    std::optional<Monitor> PrimaryMonitor()
    {
        HMONITOR const handle = ::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        if (!handle)
        {
            return std::nullopt;
        }
        return Describe(handle);
    }

    std::optional<Monitor> MonitorFromPoint(POINT point)
    {
        HMONITOR const handle = ::MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
        if (!handle)
        {
            return std::nullopt;
        }
        return Describe(handle);
    }

    std::optional<Monitor> MonitorFromHandle(HMONITOR handle)
    {
        if (!handle)
        {
            return std::nullopt;
        }
        return Describe(handle);
    }

    std::optional<Monitor> MainMonitor()
    {
        HWND const foreground = GetForegroundWindow();
        if (foreground && IsWindowVisible(foreground) && !Overlays().contains(foreground))
        {
            if (HMONITOR const handle = MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL))
            {
                return Describe(handle);
            }
        }
        POINT cursor{};
        GetCursorPos(&cursor);
        return MonitorFromPoint(cursor);
    }

    std::optional<Core::Bitmap> CaptureMonitor(Monitor const& monitor, bool includeCursor)
    {
        return Blit(monitor.bounds, includeCursor, monitor.scale);
    }

    std::optional<Core::Bitmap> CaptureRect(RECT const& rect)
    {
        return Blit(rect, false, ScaleAt(Centre(rect)));
    }

    std::vector<WindowInfo> NormalWindows()
    {
        std::vector<WindowInfo> windows;
        for (HWND window : TopLevelWindows())
        {
            if (IsOnScreen(window) && !IsTopmost(window))
            {
                windows.push_back(Basic(window));
            }
        }
        return windows;
    }

    std::optional<WindowInfo> FrontmostWindow()
    {
        for (auto& window : NormalWindows())
        {
            double const width = (window.bounds.right - window.bounds.left) / window.scale;
            double const height = (window.bounds.bottom - window.bounds.top) / window.scale;
            if (width > 70 && height > 70)
            {
                return Complete(window);
            }
        }
        return std::nullopt;
    }

    std::optional<WindowInfo> WindowAtPoint(POINT point, bool withNames)
    {
        for (HWND window : TopLevelWindows())
        {
            if (IsOnScreen(window) && Contains(FrameBounds(window), point))
            {
                WindowInfo info = Basic(window);
                return withNames ? Complete(info) : info;
            }
        }
        return std::nullopt;
    }

    std::optional<WindowInfo> DescribeWindow(HWND window)
    {
        if (!window || !IsWindow(window))
        {
            return std::nullopt;
        }
        WindowInfo info = Basic(window);
        return Complete(info);
    }

    std::optional<WindowInfo> AttributeArea(RECT const& area)
    {
        POINT const areaCentre = Centre(area);
        for (auto& window : NormalWindows())
        {
            if (!window.opaque)
            {
                continue;
            }
            if (Contains(window.bounds, areaCentre) || Contains(area, Centre(window.bounds)))
            {
                return Complete(window);
            }
        }
        return std::nullopt;
    }

    std::optional<Core::Bitmap> CaptureWindow(WindowInfo const& window)
    {
        RECT windowRect{};
        if (!GetWindowRect(window.handle, &windowRect))
        {
            return std::nullopt;
        }
        RECT const& bounds = window.bounds;
        int const width = windowRect.right - windowRect.left;
        int const height = windowRect.bottom - windowRect.top;
        int const x = bounds.left - windowRect.left;
        int const y = bounds.top - windowRect.top;
        int const w = bounds.right - bounds.left;
        int const h = bounds.bottom - bounds.top;
        if (width > 0 && height > 0 && w > 0 && h > 0 && x >= 0 && y >= 0 && x + w <= width && y + h <= height)
        {
            HDC const screen = GetDC(nullptr);
            Dib dib;
            bool const created = screen && dib.Create(screen, width, height);
            if (screen)
            {
                ReleaseDC(nullptr, screen);
            }
            if (created && PrintWindow(window.handle, dib.dc.get(), PW_RENDERFULLCONTENT))
            {
                GdiFlush();
                Core::Bitmap bitmap = dib.Extract(x, y, w, h, window.scale);
                if (!IsBlank(bitmap))
                {
                    RestoreBorder(bitmap, window);
                    bitmap.screenRect = Core::Rect{ static_cast<double>(bounds.left), static_cast<double>(bounds.top), static_cast<double>(w), static_cast<double>(h) };
                    return bitmap;
                }
            }
            Log(L"PrintWindow gave nothing; using the screen pixels");
        }
        auto bitmap = Blit(bounds, false, window.scale);
        if (bitmap)
        {
            bitmap->dpr = window.scale;
        }
        return bitmap;
    }

    Core::Bitmap PrimaryWallpaper()
    {
        auto const primary = PrimaryMonitor();
        if (!primary)
        {
            Log(L"Can't identify desktop");
            return {};
        }
        int const width = primary->bounds.right - primary->bounds.left;
        int const height = primary->bounds.bottom - primary->bounds.top;
        wil::com_ptr<IDesktopWallpaper> desktop;
        if (FAILED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(desktop.put()))))
        {
            Log(L"Can't identify desktop");
            return {};
        }
        // The wallpaper of the monitor whose rectangle is the primary's.
        std::wstring path;
        UINT count = 0;
        desktop->GetMonitorDevicePathCount(&count);
        for (UINT i = 0; i < count; ++i)
        {
            wil::unique_cotaskmem_string id;
            RECT rect{};
            if (FAILED(desktop->GetMonitorDevicePathAt(i, &id)) || FAILED(desktop->GetMonitorRECT(id.get(), &rect)))
            {
                continue;
            }
            if (rect.left == primary->bounds.left && rect.top == primary->bounds.top)
            {
                wil::unique_cotaskmem_string file;
                if (SUCCEEDED(desktop->GetWallpaper(id.get(), &file)) && file)
                {
                    path = file.get();
                }
                break;
            }
        }
        if (path.empty())
        {
            wil::unique_cotaskmem_string file;
            if (SUCCEEDED(desktop->GetWallpaper(nullptr, &file)) && file)
            {
                path = file.get();
            }
        }
        COLORREF background = 0;
        desktop->GetBackgroundColor(&background);
        DESKTOP_WALLPAPER_POSITION position = DWPOS_FILL;
        desktop->GetPosition(&position);

        Core::Bitmap canvas = Core::Bitmap::Create(width, height, primary->scale);
        for (std::size_t i = 0; i < canvas.pixels.size(); i += 4)
        {
            canvas.pixels[i] = GetBValue(background);
            canvas.pixels[i + 1] = GetGValue(background);
            canvas.pixels[i + 2] = GetRValue(background);
        }
        if (path.empty())
        {
            return canvas;   // a solid-colour desktop
        }
        auto image = ImageLoader::DecodeFile(path);
        if (!image)
        {
            Log(L"Wallpaper file fallback failed");
            return {};
        }
        double const fx = static_cast<double>(width) / image->width;
        double const fy = static_cast<double>(height) / image->height;
        switch (position)
        {
        case DWPOS_TILE:
            for (int y = 0; y < height; y += image->height)
            {
                for (int x = 0; x < width; x += image->width)
                {
                    Paste(canvas, *image, x, y);
                }
            }
            break;
        case DWPOS_CENTER:
            Paste(canvas, *image, (width - image->width) / 2, (height - image->height) / 2);
            break;
        case DWPOS_STRETCH:
            Paste(canvas, ImageLoader::Scale(*image, width, height), 0, 0);
            break;
        case DWPOS_FIT:
        {
            double const f = (std::min)(fx, fy);
            int const w = (std::max)(1, static_cast<int>(std::lround(image->width * f)));
            int const h = (std::max)(1, static_cast<int>(std::lround(image->height * f)));
            Paste(canvas, ImageLoader::Scale(*image, w, h), (width - w) / 2, (height - h) / 2);
            break;
        }
        default:   // DWPOS_FILL, DWPOS_SPAN (approximated as fill on this monitor)
        {
            double const f = (std::max)(fx, fy);
            int const w = (std::max)(width, static_cast<int>(std::lround(image->width * f)));
            int const h = (std::max)(height, static_cast<int>(std::lround(image->height * f)));
            Paste(canvas, ImageLoader::Scale(*image, w, h), (width - w) / 2, (height - h) / 2);
            break;
        }
        }
        return canvas;
    }

    void RegisterOverlay(HWND window)
    {
        Overlays().insert(window);
    }

    void UnregisterOverlay(HWND window)
    {
        Overlays().erase(window);
    }

    bool IsOverlay(HWND window)
    {
        return Overlays().contains(window);
    }
}
