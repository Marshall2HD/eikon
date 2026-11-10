#include "pch.h"
#include "Platform/TrayIcon.h"

#include "Platform/ConfettiWindow.h"
#include "Platform/ImageCodec.h"

#include <cstring>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace Eikon
{
    namespace
    {
        constexpr UINT CallbackMessage = WM_APP + 1;
        constexpr UINT IconId = 1;
        constexpr UINT_PTR BusyTimerId = 0xE1C0;
        constexpr UINT_PTR OkTimerId = 0xE1C1;
        constexpr UINT BusyFrameMs = 50;     // One frame every 0.05 s
        constexpr UINT OkMs = 1500;          // eikon-22-ok for 1.5 s

        std::wstring ExeDirectory()
        {
            std::wstring path(MAX_PATH, L'\0');
            for (;;)
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
            auto const separator = path.find_last_of(L"\\/");
            return separator == std::wstring::npos ? std::wstring{} : path.substr(0, separator);
        }

        bool ReadThemeFlag(wchar_t const* name) noexcept
        {
            DWORD value = 1;
            DWORD size = sizeof(value);
            LSTATUS const status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                name, RRF_RT_REG_DWORD, nullptr, &value, &size);
            return status != ERROR_SUCCESS || value != 0;
        }

        bool TaskbarUsesLightTheme() noexcept { return ReadThemeFlag(L"SystemUsesLightTheme"); }
        bool AppsUseLightTheme() noexcept { return ReadThemeFlag(L"AppsUseLightTheme"); }

        // Whether the icon is actually on screen: its centre must be over the taskbar (or the open
        // overflow flyout). Windows still reports a rect while the taskbar is auto-hidden or covered
        // by a full-screen window, and the confetti must not puff over another app there.
        bool IconOnScreen(RECT const& rect) noexcept
        {
            POINT const centre{ (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2 };
            HWND const root = GetAncestor(WindowFromPoint(centre), GA_ROOT);
            wchar_t name[64]{};
            if (!root || !GetClassNameW(root, name, ARRAYSIZE(name)))
            {
                return false;
            }
            return wcscmp(name, L"Shell_TrayWnd") == 0 || wcscmp(name, L"Shell_SecondaryTrayWnd") == 0 ||
                wcscmp(name, L"NotifyIconOverflowWindow") == 0 || wcscmp(name, L"TopLevelWindowForOverflowXamlIsland") == 0;
        }

        UINT CurrentDpi(HWND hwnd) noexcept
        {
            UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 0;
            if (dpi == 0)
            {
                dpi = GetDpiForSystem();
            }
            return dpi == 0 ? 96 : dpi;
        }

        int SmallIconSize(HWND hwnd) noexcept
        {
            UINT const dpi = CurrentDpi(hwnd);
            int size = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
            return size > 0 ? size : MulDiv(16, static_cast<int>(dpi), 96);
        }

        // Creates an icon from top-down, straight-alpha BGRA pixels.
        HICON IconFromBgra(int size, std::vector<std::uint8_t> const& pixels) noexcept
        {
            BITMAPV5HEADER header{};
            header.bV5Size = sizeof(header);
            header.bV5Width = size;
            header.bV5Height = -size;
            header.bV5Planes = 1;
            header.bV5BitCount = 32;
            header.bV5Compression = BI_BITFIELDS;
            header.bV5RedMask = 0x00FF0000;
            header.bV5GreenMask = 0x0000FF00;
            header.bV5BlueMask = 0x000000FF;
            header.bV5AlphaMask = 0xFF000000;

            void* bits = nullptr;
            HDC screen = GetDC(nullptr);
            wil::unique_hbitmap color{ CreateDIBSection(screen, reinterpret_cast<BITMAPINFO const*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0) };
            ReleaseDC(nullptr, screen);
            if (!color || !bits)
            {
                return nullptr;
            }
            std::memcpy(bits, pixels.data(), (std::min)(pixels.size(), static_cast<std::size_t>(size) * size * 4));
            GdiFlush();

            std::vector<std::uint8_t> maskBits(static_cast<std::size_t>((size + 15) / 16) * 2 * size, 0);
            wil::unique_hbitmap mask{ CreateBitmap(size, size, 1, 1, maskBits.data()) };
            if (!mask)
            {
                return nullptr;
            }
            ICONINFO info{};
            info.fIcon = TRUE;
            info.hbmMask = mask.get();
            info.hbmColor = color.get();
            return CreateIconIndirect(&info);
        }

        // Template image: keep the shape (alpha), replace the colour.
        void Tint(std::vector<std::uint8_t>& pixels, std::uint8_t value, bool premultiplied) noexcept
        {
            for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
            {
                std::uint8_t const alpha = pixels[i + 3];
                std::uint8_t const channel = premultiplied ? static_cast<std::uint8_t>((value * alpha + 127) / 255) : value;
                pixels[i] = pixels[i + 1] = pixels[i + 2] = channel;
            }
        }

        wchar_t const* FrameAsset(int index) noexcept
        {
            static constexpr wchar_t const* kNames[] = {
                L"eikon-22", L"eikon-22-ok", L"eikon-22-animated-1", L"eikon-22-animated-2",
                L"eikon-22-animated-3", L"eikon-22-animated-4", L"eikon-22-animated-5",
            };
            return kNames[index];
        }

        void EnableDarkContextMenus() noexcept
        {
            static bool const once = [] {
                // Undocumented but stable since Windows 10 1903: uxtheme ordinal 135 is
                // SetPreferredAppMode, 136 FlushMenuThemes. Best effort.
                HMODULE const uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
                if (uxtheme)
                {
                    using SetPreferredAppModeFn = int(WINAPI*)(int);
                    using FlushMenuThemesFn = void(WINAPI*)();
                    if (auto setMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135))))
                    {
                        setMode(1); // AllowDark
                    }
                    if (auto flush = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136))))
                    {
                        flush();
                    }
                }
                return true;
            }();
            (void)once;
        }
    }

    TrayIcon::TrayIcon(MessageWindow& window) : m_window(window)
    {
        m_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
        if (m_taskbarCreatedMessage != 0 && m_window.Handle())
        {
            ChangeWindowMessageFilterEx(m_window.Handle(), m_taskbarCreatedMessage, MSGFLT_ALLOW, nullptr);
        }
        EnableDarkContextMenus();
        m_handlerToken = m_window.AddHandler([this](UINT message, WPARAM wParam, LPARAM lParam) {
            return OnMessage(message, wParam, lParam);
        });
    }

    TrayIcon::~TrayIcon()
    {
        KillTimer(m_window.Handle(), BusyTimerId);
        KillTimer(m_window.Handle(), OkTimerId);
        if (m_visible)
        {
            Notify(NIM_DELETE);
            m_visible = false;
        }
        m_window.RemoveHandler(m_handlerToken);
        ClearIconCache();
        ClearMenuBitmaps();
    }

    void TrayIcon::Show()
    {
        if (m_visible)
        {
            Notify(NIM_MODIFY);
        }
        else
        {
            Add();
        }
    }

    void TrayIcon::Hide()
    {
        if (m_visible)
        {
            Notify(NIM_DELETE);
            m_visible = false;
        }
    }

    void TrayIcon::SetTooltip(std::wstring const& text)
    {
        m_tooltip = text.substr(0, 127);
        if (m_visible)
        {
            Notify(NIM_MODIFY);
        }
    }

    // ---- animation ----

    void TrayIcon::BeginBusy()
    {
        KillTimer(m_window.Handle(), OkTimerId);
        m_busy = true;
        SetFrame(Frame::Busy1);
        SetTimer(m_window.Handle(), BusyTimerId, BusyFrameMs, nullptr);
    }

    void TrayIcon::EndBusy(bool success)
    {
        m_busy = false;
        KillTimer(m_window.Handle(), BusyTimerId);
        if (success)
        {
            ShowOk();
        }
        else
        {
            KillTimer(m_window.Handle(), OkTimerId);
            SetFrame(Frame::Normal);
        }
    }

    void TrayIcon::ShowOk()
    {
        if (m_busy)
        {
            return;
        }
        SetFrame(Frame::Ok);
        // The confetti puff around the icon.
        if (auto const rect = Rect(); rect && IconOnScreen(*rect))
        {
            ConfettiWindow::Play(*rect, !TaskbarUsesLightTheme());
        }
        SetTimer(m_window.Handle(), OkTimerId, OkMs, nullptr);
    }

    void TrayIcon::SetFrame(Frame frame)
    {
        m_frame = frame;
        if (m_visible)
        {
            Notify(NIM_MODIFY);
        }
    }

    HICON TrayIcon::IconFor(Frame frame)
    {
        if (auto it = m_icons.find(frame); it != m_icons.end())
        {
            return it->second;
        }
        int const size = SmallIconSize(m_window.Handle());
        std::wstring const path = ExeDirectory() + L"\\Assets\\Tray\\" + FrameAsset(static_cast<int>(frame)) + L".png";
        auto pixels = ImageCodec::DecodeFileScaled(path.c_str(), static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size), false);
        HICON icon = nullptr;
        if (!pixels.empty())
        {
            Tint(pixels, TaskbarUsesLightTheme() ? 0x00 : 0xFF, false);
            icon = IconFromBgra(size, pixels);
        }
        if (!icon)
        {
            std::wstring const fallback = ExeDirectory() + L"\\Assets\\Eikon.ico";
            icon = static_cast<HICON>(LoadImageW(nullptr, fallback.c_str(), IMAGE_ICON, size, size, LR_LOADFROMFILE));
        }
        m_icons[frame] = icon;
        return icon;
    }

    void TrayIcon::ClearIconCache()
    {
        for (auto const& [frame, icon] : m_icons)
        {
            if (icon)
            {
                DestroyIcon(icon);
            }
        }
        m_icons.clear();
    }

    HBITMAP TrayIcon::MenuBitmap(std::wstring const& name, bool templateIcon)
    {
        if (name.empty())
        {
            return nullptr;
        }
        if (auto it = m_menuBitmaps.find(name); it != m_menuBitmaps.end())
        {
            return it->second;
        }
        int const size = SmallIconSize(m_window.Handle());
        std::wstring const path = ExeDirectory() + L"\\Assets\\Menu\\" + name + L".png";
        auto pixels = ImageCodec::DecodeFileScaled(path.c_str(), static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size), true);
        HBITMAP bitmap = nullptr;
        if (!pixels.empty())
        {
            if (templateIcon && !AppsUseLightTheme())
            {
                Tint(pixels, 0xFF, true); // dark menus: white template images
            }
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = size;
            info.bmiHeader.biHeight = -size;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (bitmap && bits)
            {
                // Menus alpha-blend 32bpp item bitmaps, which needs premultiplied pixels.
                std::memcpy(bits, pixels.data(), pixels.size());
                GdiFlush();
            }
        }
        m_menuBitmaps[name] = bitmap;
        return bitmap;
    }

    void TrayIcon::ClearMenuBitmaps()
    {
        for (auto const& [name, bitmap] : m_menuBitmaps)
        {
            if (bitmap)
            {
                DeleteObject(bitmap);
            }
        }
        m_menuBitmaps.clear();
    }

    // ---- Shell_NotifyIcon ----

    std::optional<RECT> TrayIcon::Rect() const
    {
        if (!m_visible)
        {
            return std::nullopt;
        }
        NOTIFYICONIDENTIFIER identifier{};
        identifier.cbSize = sizeof(identifier);
        identifier.hWnd = m_window.Handle();
        identifier.uID = IconId;
        RECT rect{};
        if (FAILED(Shell_NotifyIconGetRect(&identifier, &rect)))
        {
            return std::nullopt;
        }
        return rect;
    }

    void TrayIcon::Add()
    {
        m_visible = true; // desired state; re-added on TaskbarCreated if Explorer isn't ready yet
        if (!Notify(NIM_ADD))
        {
            // A stale icon from a previous instance may still be registered.
            Notify(NIM_DELETE);
            if (!Notify(NIM_ADD))
            {
                OutputDebugStringW(L"Eikon: Shell_NotifyIcon(NIM_ADD) failed\n");
                return;
            }
        }
        Notify(NIM_SETVERSION);
    }

    bool TrayIcon::Notify(DWORD message)
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = m_window.Handle();
        data.uID = IconId;
        if (!data.hWnd)
        {
            return false;
        }
        if (message == NIM_SETVERSION)
        {
            data.uVersion = NOTIFYICON_VERSION_4;
        }
        else if (message != NIM_DELETE)
        {
            data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
            data.uCallbackMessage = CallbackMessage;
            data.hIcon = IconFor(m_frame);
            std::wstring const tip = m_tooltip.empty() ? std::wstring{ L"Lucida" } : m_tooltip;
            wcsncpy_s(data.szTip, std::size(data.szTip), tip.c_str(), _TRUNCATE);
        }
        return Shell_NotifyIconW(message, &data) != FALSE;
    }

    std::optional<LRESULT> TrayIcon::OnMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        try
        {
            if (message == CallbackMessage)
            {
                // NOTIFYICON_VERSION_4: LOWORD(lParam) = event, wParam = anchor coordinates.
                POINT const anchor{ static_cast<short>(LOWORD(wParam)), static_cast<short>(HIWORD(wParam)) };
                switch (LOWORD(lParam))
                {
                case NIN_SELECT:
                case NIN_KEYSELECT:
                {
                    // NIN_KEYSELECT can arrive twice for one Enter press; ignore immediate repeats.
                    ULONGLONG const now = GetTickCount64();
                    if (now - m_lastInvokeTick < 150)
                    {
                        break;
                    }
                    m_lastInvokeTick = now;
                    Invoked(anchor);
                    break;
                }
                case WM_CONTEXTMENU:
                    ShowMenu(anchor);
                    break;
                default:
                    break;
                }
                return LRESULT{ 0 };
            }

            if (m_taskbarCreatedMessage != 0 && message == m_taskbarCreatedMessage)
            {
                if (m_visible)
                {
                    ClearIconCache();
                    Add();
                }
                return std::nullopt;
            }

            switch (message)
            {
            case WM_TIMER:
                if (wParam == BusyTimerId)
                {
                    // 1 → 2 → 3 → 4 → 5 → 1 …
                    int const next = m_frame >= Frame::Busy1 && m_frame < Frame::Busy5 ? static_cast<int>(m_frame) + 1
                                                                                     : static_cast<int>(Frame::Busy1);
                    SetFrame(static_cast<Frame>(next));
                    return LRESULT{ 0 };
                }
                if (wParam == OkTimerId)
                {
                    KillTimer(m_window.Handle(), OkTimerId);
                    if (!m_busy)
                    {
                        SetFrame(Frame::Normal);
                    }
                    return LRESULT{ 0 };
                }
                break;
            case WM_SETTINGCHANGE:
                if (lParam && CompareStringOrdinal(reinterpret_cast<wchar_t const*>(lParam), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL)
                {
                    ClearIconCache();
                    ClearMenuBitmaps();
                    if (m_visible)
                    {
                        Notify(NIM_MODIFY);
                    }
                }
                break;
            case WM_DPICHANGED:
            case WM_DISPLAYCHANGE:
                ClearIconCache();
                ClearMenuBitmaps();
                if (m_visible)
                {
                    Notify(NIM_MODIFY);
                }
                break;
            default:
                break;
            }
        }
        catch (...)
        {
            OutputDebugStringW(L"Eikon: exception in the tray icon handler\n");
        }
        return std::nullopt;
    }

    // ---- menu ----

    HMENU TrayIcon::BuildMenu(std::vector<MenuItem> const& items, std::vector<std::function<void()>>& actions)
    {
        HMENU const menu = CreatePopupMenu();
        if (!menu)
        {
            return nullptr;
        }
        UINT position = 0;
        for (auto const& item : items)
        {
            MENUITEMINFOW info{};
            info.cbSize = sizeof(info);
            if (item.text.empty() && item.submenu.empty())
            {
                info.fMask = MIIM_FTYPE;
                info.fType = MFT_SEPARATOR;
                InsertMenuItemW(menu, position++, TRUE, &info);
                continue;
            }
            std::wstring text = item.text;
            if (!item.shortcut.empty())
            {
                text += L'\t';
                text += item.shortcut;
            }
            info.fMask = MIIM_STRING | MIIM_FTYPE | MIIM_STATE | MIIM_ID;
            info.fType = MFT_STRING;
            info.dwTypeData = text.data();
            info.fState = (item.checked ? MFS_CHECKED : MFS_UNCHECKED) | (item.enabled ? MFS_ENABLED : MFS_DISABLED);
            if (!item.submenu.empty())
            {
                info.fMask |= MIIM_SUBMENU;
                info.hSubMenu = BuildMenu(item.submenu, actions);
                info.wID = 0;
            }
            else
            {
                actions.push_back(item.action);
                info.wID = static_cast<UINT>(actions.size()); // 1-based command id
            }
            if (HBITMAP const bitmap = MenuBitmap(item.icon, item.templateIcon))
            {
                info.fMask |= MIIM_BITMAP;
                info.hbmpItem = bitmap;
            }
            InsertMenuItemW(menu, position++, TRUE, &info);
        }
        return menu;
    }

    void TrayIcon::ShowMenu(std::optional<POINT> point)
    {
        if (!MenuProvider)
        {
            return;
        }
        std::vector<MenuItem> const items = MenuProvider();
        if (items.empty())
        {
            return;
        }
        std::vector<std::function<void()>> actions;
        wil::unique_hmenu menu{ BuildMenu(items, actions) };
        if (!menu)
        {
            return;
        }
        POINT anchor{};
        if (point)
        {
            anchor = *point;
        }
        else
        {
            GetCursorPos(&anchor);
        }

        HWND const hwnd = m_window.Handle();
        // Required so the menu closes when the user clicks elsewhere.
        SetForegroundWindow(hwnd);
        UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_BOTTOMALIGN;
        flags |= GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
        UINT const command = static_cast<UINT>(TrackPopupMenuEx(menu.get(), flags, anchor.x, anchor.y, hwnd, nullptr));
        PostMessageW(hwnd, WM_NULL, 0, 0);

        if (command >= 1 && command <= actions.size())
        {
            if (auto const& action = actions[command - 1])
            {
                try
                {
                    action();
                }
                catch (...)
                {
                    OutputDebugStringW(L"Eikon: exception in a tray menu action\n");
                }
            }
        }
    }
}
