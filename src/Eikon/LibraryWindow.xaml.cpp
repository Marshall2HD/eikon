#include "pch.h"
#include "LibraryWindow.xaml.h"
#if __has_include("LibraryWindow.g.cpp")
#include "LibraryWindow.g.cpp"
#endif

#include "AppController.h"
#include "Core/Color.h"
#include "Core/Settings.h"
#include "Platform/Candy.h"
#include "Platform/Exporter.h"
#include "Platform/ImageCodec.h"
#include "SettingsHelpers.h"
#include "Strings.h"

#include <shobjidl_core.h>

#include <cwctype>

namespace winrt
{
    using namespace Microsoft::UI::Windowing;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Controls::Primitives;
    using namespace Microsoft::UI::Xaml::Input;
    using namespace Microsoft::UI::Xaml::Media;
    using namespace Microsoft::UI::Xaml::Media::Imaging;
    using namespace Windows::Foundation;
    using namespace Windows::Graphics;
}

namespace
{
    using ::Eikon::LibraryIndex;
    using ::Eikon::Core::HotKey;
    using ::Eikon::Core::Settings;
    namespace Lib = ::Eikon::Core::Library;
    namespace LibLayout = ::Eikon::Core::LibraryLayout;

    constexpr double kScrollGutter = 15;                    // W = view width - (12 + 3) insets
    constexpr auto kDoubleClick = std::chrono::milliseconds{ 400 };
    constexpr auto kEmptySelectionDelay = std::chrono::milliseconds{ 40 };
    constexpr auto kTooltipDelay = std::chrono::milliseconds{ 250 };
    constexpr auto kSearchDelay = std::chrono::milliseconds{ 150 };
    constexpr double kMinimumReuse = 0.6;                  // a cached thumbnail >= 60% of the size is reused
    constexpr std::size_t kThumbnailCacheCap = 50;         // min(libLimit, 50)

    struct Tool
    {
        wchar_t const* id;
        wchar_t const* titleKey;
        wchar_t const* image;
        std::optional<HotKey> hotKey;
    };
    // The toolbar tools.
    Tool const kTools[] = {
        { L"show", L"Library_ToolShow", L"eikon", HotKey::Reopen },
        { L"fullscreen", L"Library_ToolFullscreen", L"fullscreen", HotKey::Fullscreen },
        { L"area", L"Library_ToolArea", L"area", HotKey::Area },
        { L"repeat", L"Library_ToolRepeat", L"repeat", HotKey::RepeatArea },
        { L"window", L"Library_ToolWindow", L"window", HotKey::Window },
        { L"scrolling", L"Library_ToolScrolling", L"scrolldown", HotKey::Scrolling },
        { L"scrollingup", L"Library_ToolScrollingUp", L"scrollup", std::nullopt },
        { L"ocr", L"Library_ToolOcr", L"ocr", HotKey::Ocr },
        { L"clipboard", L"Library_ToolClipboard", L"clipboard", HotKey::LoadClipboard },
    };

    struct LayoutChoice
    {
        wchar_t const* value;
        wchar_t const* titleKey;
        wchar_t const* icon;
    };
    LayoutChoice const kLayouts[] = {
        { L"list", L"Library_LayoutList", L"16-lib-layout-text" },
        { L"square80", L"Library_LayoutSmall", L"16-lib-layout-small" },
        { L"medium", L"Library_LayoutMedium", L"16-lib-layout-medium" },
        { L"large", L"Library_LayoutLarge", L"16-lib-layout-large" },
    };

    winrt::Uri AssetUri(std::wstring const& name)
    {
        return winrt::Uri{ L"ms-appx:///Assets/Library/" + name + L".png" };
    }

    winrt::Windows::UI::Color ParseColor(std::wstring const& hex, winrt::Windows::UI::Color fallback)
    {
        if (auto const color = ::Eikon::Core::Color::FromHex(hex))
        {
            return { 255, color->r, color->g, color->b };
        }
        return fallback;
    }

    // Candy's accent (#7B4BFF, #A98BFF in dark); the system highlight colour in high contrast.
    winrt::Windows::UI::Color AccentColor(bool dark)
    {
        if (::Eikon::Candy::HighContrast())
        {
            COLORREF const c = ::GetSysColor(COLOR_HIGHLIGHT);
            return { 255, GetRValue(c), GetGValue(c), GetBValue(c) };
        }
        auto const a = ::Eikon::Candy::For(dark).accent;
        return { a.a, a.r, a.g, a.b };
    }

    // DateComponentsFormatter-like "1h 2m", "3m 20s", "45s".
    std::wstring DurationText(double seconds)
    {
        auto total = static_cast<long long>(std::ceil((std::max)(0.0, seconds)));
        long long const h = total / 3600;
        long long const m = (total % 3600) / 60;
        long long const s = total % 60;
        std::wstring text;
        if (h > 0)
        {
            text = std::to_wstring(h) + L"h " + std::to_wstring(m) + L"m";
        }
        else if (m > 0)
        {
            text = std::to_wstring(m) + L"m " + std::to_wstring(s) + L"s";
        }
        else
        {
            text = std::to_wstring(s) + L"s";
        }
        return text;
    }

    bool FileExists(std::wstring const& path)
    {
        DWORD const attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    // Reveals the files in File Explorer, selected together when they share a folder.
    void RevealInExplorer(std::vector<std::wstring> const& paths)
    {
        if (paths.empty())
        {
            return;
        }
        auto const folderPath = std::filesystem::path{ paths.front() }.parent_path().wstring();
        PIDLIST_ABSOLUTE folder = ILCreateFromPathW(folderPath.c_str());
        if (!folder)
        {
            return;
        }
        std::vector<PIDLIST_ABSOLUTE> items;
        for (auto const& path : paths)
        {
            if (auto pidl = ILCreateFromPathW(path.c_str()))
            {
                items.push_back(pidl);
            }
        }
        std::vector<PCUITEMID_CHILD> children;
        for (auto const pidl : items)
        {
            children.push_back(ILFindLastID(pidl));
        }
        SHOpenFolderAndSelectItems(folder, static_cast<UINT>(children.size()), children.data(), 0);
        for (auto const pidl : items)
        {
            ILFree(pidl);
        }
        ILFree(folder);
    }

    // Copy as File(s): CF_HDROP with a copy drop effect, as File Explorer's Copy does.
    bool CopyFiles(std::vector<std::wstring> const& paths, HWND owner)
    {
        std::size_t chars = 1;
        for (auto const& path : paths)
        {
            chars += path.size() + 1;
        }
        HGLOBAL drop = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + chars * sizeof(wchar_t));
        if (!drop)
        {
            return false;
        }
        auto* header = static_cast<DROPFILES*>(GlobalLock(drop));
        header->pFiles = sizeof(DROPFILES);
        header->fWide = TRUE;
        auto* cursor = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(header) + sizeof(DROPFILES));
        for (auto const& path : paths)
        {
            std::memcpy(cursor, path.c_str(), path.size() * sizeof(wchar_t));
            cursor += path.size() + 1;
        }
        GlobalUnlock(drop);
        HGLOBAL effect = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
        if (effect)
        {
            *static_cast<DWORD*>(GlobalLock(effect)) = DROPEFFECT_COPY;
            GlobalUnlock(effect);
        }
        if (!OpenClipboard(owner))
        {
            GlobalFree(drop);
            if (effect)
            {
                GlobalFree(effect);
            }
            return false;
        }
        EmptyClipboard();
        bool const ok = SetClipboardData(CF_HDROP, drop) != nullptr;
        if (!ok)
        {
            GlobalFree(drop);
        }
        if (effect && !SetClipboardData(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT), effect))
        {
            GlobalFree(effect);
        }
        CloseClipboard();
        return ok;
    }

    // Move to Trash -> the Recycle Bin.
    std::vector<std::wstring> Recycle(std::vector<std::wstring> const& paths)
    {
        std::vector<std::wstring> recycled;
        try
        {
            auto operation = winrt::create_instance<IFileOperation>(CLSID_FileOperation);
            winrt::check_hresult(operation->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOFX_RECYCLEONDELETE));
            for (auto const& path : paths)
            {
                winrt::com_ptr<IShellItem> item;
                if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(item.put()))) &&
                    SUCCEEDED(operation->DeleteItem(item.get(), nullptr)))
                {
                    recycled.push_back(path);
                }
            }
            if (!recycled.empty())
            {
                operation->PerformOperations();
            }
        }
        catch (...)
        {
        }
        std::erase_if(recycled, [](auto const& path) { return FileExists(path); });
        return recycled;
    }
}

namespace winrt::Eikon::implementation
{
    LibraryWindow::~LibraryWindow()
    {
        if (m_indexToken)
        {
            LibraryIndex::Current().Changed.Remove(m_indexToken);
        }
        if (m_settingsToken)
        {
            Settings::Current().Changed.Remove(m_settingsToken);
        }
    }

    void LibraryWindow::InitializeComponent()
    {
        LibraryWindowT<LibraryWindow>::InitializeComponent();
        Title(winrt::hstring{ ::Eikon::Strings::Get(L"Library_WindowTitle") });
        SetUpWindow();
        BuildToolbar();

        auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        m_tooltipTimer = dispatcher.CreateTimer();
        m_tooltipTimer.IsRepeating(false);
        m_tooltipTimer.Interval(kTooltipDelay);
        m_tooltipTimer.Tick([weak = get_weak()](auto&&, auto&&) {
            if (auto self = weak.get())
            {
                self->ShowTooltip(self->m_hoverTool);
            }
        });
        m_statusTimer = dispatcher.CreateTimer();
        m_statusTimer.IsRepeating(false);
        m_statusTimer.Interval(kEmptySelectionDelay);
        m_statusTimer.Tick([weak = get_weak()](auto&&, auto&&) {
            if (auto self = weak.get())
            {
                self->UpdateStatus();
            }
        });
        m_searchTimer = dispatcher.CreateTimer();
        m_searchTimer.IsRepeating(false);
        m_searchTimer.Interval(kSearchDelay);
        m_searchTimer.Tick([weak = get_weak()](auto&&, auto&&) {
            if (auto self = weak.get())
            {
                self->RunSearch(std::wstring{ self->SearchBox().Text() });
            }
        });

        m_indexToken = LibraryIndex::Current().Changed.Add([weak = get_weak()] {
            if (auto self = weak.get())
            {
                self->OnIndexChanged();
            }
        });
        m_settingsToken = Settings::Current().Changed.Add([weak = get_weak()](std::wstring_view key) {
            if (auto self = weak.get())
            {
                if (key == Settings::kLibLayout || key == Settings::kLibPopoveerSize)
                {
                    self->UpdateLayoutButtons();
                    self->Relayout();
                }
            }
        });

        Activated([weak = get_weak()](auto&&, winrt::WindowActivatedEventArgs const& args) {
            if (auto self = weak.get())
            {
                // The popover closes when it loses focus.
                if (args.WindowActivationState() == winrt::WindowActivationState::Deactivated && self->m_open && !self->m_modal)
                {
                    self->HideLibrary();
                }
            }
        });
        Root().ActualThemeChanged([weak = get_weak()](auto&&, auto&&) {
            if (auto self = weak.get())
            {
                self->UpdateState();
                for (auto const& [index, view] : self->m_realized)
                {
                    self->BindView(*view, index);
                }
            }
        });

        UpdateLayoutButtons();
        SetView(LibraryIndex::Current().Items());
    }

    // ---- Window ----

    void LibraryWindow::SetUpWindow()
    {
        m_appWindow = AppWindow();
        if (auto native = this->try_as<::IWindowNative>())
        {
            native->get_WindowHandle(&m_hwnd);
        }
        try
        {
            if (auto presenter = m_appWindow.Presenter().try_as<winrt::OverlappedPresenter>())
            {
                presenter.SetBorderAndTitleBar(true, false);
                presenter.IsResizable(false);
                presenter.IsMaximizable(false);
                presenter.IsMinimizable(false);
                presenter.IsAlwaysOnTop(true);
            }
            m_appWindow.IsShownInSwitchers(false);
        }
        catch (...)
        {
        }
        if (m_hwnd)
        {
            DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
            DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        }
    }

    // Small 380 x 580, medium 560 x max(580, visible height - 100), anchored above the
    // notification-area icon (or at the pointer when the icon is hidden).
    void LibraryWindow::PlaceWindow()
    {
        if (!m_appWindow)
        {
            return;
        }
        POINT anchor{};
        GetCursorPos(&anchor);
        std::optional<RECT> icon;
        try
        {
            icon = ::Eikon::AppController::Current().TrayIcon().Rect();
        }
        catch (...)
        {
        }
        if (icon)
        {
            anchor = { (icon->left + icon->right) / 2, (icon->top + icon->bottom) / 2 };
        }
        HMONITOR const monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{ sizeof(info) };
        GetMonitorInfoW(monitor, &info);
        UINT dpiX = USER_DEFAULT_SCREEN_DPI;
        UINT dpiY = USER_DEFAULT_SCREEN_DPI;
        GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        double const scale = dpiX > 0 ? static_cast<double>(dpiX) / USER_DEFAULT_SCREEN_DPI : 1.0;
        RECT const work = info.rcWork;
        bool const medium = Settings::Current().LibPopoveerSize() == L"medium";
        double const workHeight = (work.bottom - work.top) / scale;
        double const widthDip = medium ? LibLayout::kMediumWidth : LibLayout::kSmallWidth;
        double const heightDip = (std::min)(medium ? LibLayout::MediumHeight(workHeight) : LibLayout::kSmallHeight, workHeight - 16);
        int const width = static_cast<int>(std::lround(widthDip * scale));
        int const height = static_cast<int>(std::lround(heightDip * scale));
        int const margin = static_cast<int>(std::lround(8 * scale));
        int x = anchor.x - width / 2;
        x = (std::max)(static_cast<int>(work.left) + margin, (std::min)(x, static_cast<int>(work.right) - width - margin));
        int y = static_cast<int>(work.bottom) - height - margin;
        if (icon && anchor.y < work.top)
        {
            y = work.top + margin;   // taskbar at the top
        }
        else if (!icon)
        {
            y = (std::max)(static_cast<int>(work.top) + margin, (std::min)(static_cast<int>(anchor.y) - height / 2, y));
        }
        m_appWindow.MoveAndResize(winrt::RectInt32{ x, y, width, height });
        // The outer rectangle includes the invisible resize frame; grow it so the visible window is
        // exactly the popover size.
        RECT outer{};
        RECT visible{};
        if (m_hwnd && GetWindowRect(m_hwnd, &outer) &&
            SUCCEEDED(DwmGetWindowAttribute(m_hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof(visible))))
        {
            int const left = visible.left - outer.left;
            int const top = visible.top - outer.top;
            int const extraWidth = (outer.right - outer.left) - (visible.right - visible.left);
            int const extraHeight = (outer.bottom - outer.top) - (visible.bottom - visible.top);
            if (extraWidth > 0 || extraHeight > 0)
            {
                m_appWindow.MoveAndResize(winrt::RectInt32{ x - left, y - top, width + extraWidth, height + extraHeight });
            }
        }
    }

    bool LibraryWindow::IsOpen() const
    {
        return m_open && m_appWindow && m_appWindow.IsVisible();
    }

    void LibraryWindow::Toggle()
    {
        if (IsOpen())
        {
            HideLibrary();
        }
        else
        {
            Open(std::nullopt);
        }
    }

    void LibraryWindow::Open(std::optional<std::wstring> const& search)
    {
        PlaceWindow();
        m_open = true;
        m_appWindow.Show(true);
        PlaceWindow();   // the visible-frame correction needs a shown window
        if (m_hwnd)
        {
            SetForegroundWindow(m_hwnd);
        }
        Activate();
        LibraryIndex::Current().Rescan();
        if (search)
        {
            SearchBox().Text(winrt::hstring{ *search });
            RunSearch(*search);
        }
        SearchBox().Focus(winrt::FocusState::Programmatic);
        Relayout();
    }

    void LibraryWindow::HideLibrary()
    {
        m_quickLook.Hide();
        m_open = false;
        TooltipBox().Visibility(winrt::Visibility::Collapsed);
        if (m_appWindow)
        {
            m_appWindow.Hide();
        }
    }

    // ---- Toolbar ----

    void LibraryWindow::BuildToolbar()
    {
        // Candy: a light toolbar in both themes; the tools are template glyphs in ink 2 on icon buttons with a
        // neutral wash on hover.
        auto style = winrt::Application::Current().Resources().Lookup(winrt::box_value(L"CandyIconButtonStyle")).as<winrt::Style>();
        for (auto const& tool : kTools)
        {
            winrt::Button button;
            button.Style(style);
            button.Width(32);
            button.Height(32);
            button.Tag(winrt::box_value(winrt::hstring{ tool.id }));
            winrt::BitmapIcon image;
            image.Width(28);
            image.Height(28);
            image.ShowAsMonochrome(true);
            image.UriSource(AssetUri(std::wstring{ L"lib-tool-28-" } + tool.image));
            button.Content(image);
            winrt::Automation::AutomationProperties::SetName(button, winrt::hstring{ ::Eikon::Strings::Get(tool.titleKey) });
            std::wstring const id = tool.id;
            button.Click([weak = get_weak(), id](auto&&, auto&&) {
                if (auto self = weak.get())
                {
                    self->RunTool(id);
                }
            });
            button.PointerEntered({ get_weak(), &LibraryWindow::Tool_PointerEntered });
            button.PointerExited({ get_weak(), &LibraryWindow::Tool_PointerExited });
            ToolbarPanel().Children().Append(button);
        }
        MoreButton().Tag(winrt::box_value(winrt::hstring{ L"more" }));
        winrt::Automation::AutomationProperties::SetName(MoreButton(), winrt::hstring{ ::Eikon::Strings::Get(L"Library_ToolMore") });
        winrt::Automation::AutomationProperties::SetName(SettingsButton(), winrt::hstring{ ::Eikon::Strings::Get(L"Library_Settings") });
        winrt::Automation::AutomationProperties::SetName(LayoutButton(), winrt::hstring{ ::Eikon::Strings::Get(L"Library_Layout") });
        winrt::Automation::AutomationProperties::SetName(SizeButton(), winrt::hstring{ ::Eikon::Strings::Get(L"Library_Size") });
    }

    void LibraryWindow::Tool_PointerEntered(winrt::IInspectable const& sender, winrt::PointerRoutedEventArgs const&)
    {
        auto const id = std::wstring{ winrt::unbox_value_or<winrt::hstring>(sender.as<winrt::FrameworkElement>().Tag(), L"") };
        if (id != L"more")
        {
            m_hoverTool = id;
            m_tooltipTimer.Stop();
            m_tooltipTimer.Start();
        }
    }

    void LibraryWindow::Tool_PointerExited(winrt::IInspectable const&, winrt::PointerRoutedEventArgs const&)
    {
        // Mouse-out hides the box.
        m_tooltipTimer.Stop();
        m_hoverTool.clear();
        TooltipBox().Visibility(winrt::Visibility::Collapsed);
    }

    // The title and the tool's current shortcut (dimmed).
    void LibraryWindow::ShowTooltip(std::wstring const& id)
    {
        auto const tool = std::find_if(std::begin(kTools), std::end(kTools), [&](auto const& t) { return t.id == id; });
        if (tool == std::end(kTools))
        {
            return;
        }
        TooltipText().Text(winrt::hstring{ ::Eikon::Strings::Get(tool->titleKey) });
        std::wstring hotkey;
        if (tool->hotKey)
        {
            auto const shortcut = Settings::Current().Shortcut(*tool->hotKey);
            if (!shortcut.IsEmpty())
            {
                hotkey = ::Eikon::Strings::ShortcutText(shortcut);
            }
        }
        TooltipHotkey().Text(winrt::hstring{ hotkey });
        TooltipBox().Visibility(winrt::Visibility::Visible);
    }

    // Closes the popover, then runs the status menu action.
    void LibraryWindow::RunTool(std::wstring const& id)
    {
        HideLibrary();
        auto timer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        timer.IsRepeating(false);
        // Let the window disappear before a capture starts.
        timer.Interval(std::chrono::milliseconds{ 150 });
        timer.Tick([id, timer](auto&&, auto&&) {
            timer.Stop();
            auto& app = ::Eikon::AppController::Current();
            if (id == L"show") app.Reopen(L"menu");
            else if (id == L"fullscreen") app.GrabScreenshot(L"fullscreen", L"menu");
            else if (id == L"area") app.GrabScreenshot(L"interactive", L"menu");
            else if (id == L"repeat") app.GrabScreenshot(L"interactive-repeat", L"menu");
            else if (id == L"window") app.GrabScreenshot(L"window", L"menu");
            else if (id == L"scrolling") app.GrabScrolling(-1, L"menu");
            else if (id == L"scrollingup") app.GrabScrolling(1, L"menu");
            else if (id == L"ocr") app.InstantOcr(L"menu");
            else if (id == L"clipboard") app.LoadFromClipboard(L"menu");
        });
        timer.Start();
    }

    void LibraryWindow::MoreButton_Click(winrt::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        // btnToolbarMore pops up the app's status menu under the button.
        auto const transform = MoreButton().TransformToVisual(nullptr);
        auto const point = transform.TransformPoint({ 0, static_cast<float>(MoreButton().ActualHeight()) });
        double const scale = Root().XamlRoot() ? Root().XamlRoot().RasterizationScale() : 1.0;
        POINT screen{ static_cast<LONG>(point.X * scale), static_cast<LONG>(point.Y * scale) };
        if (m_hwnd)
        {
            ClientToScreen(m_hwnd, &screen);
        }
        HideLibrary();
        ::Eikon::AppController::Current().TrayIcon().ShowMenu(screen);
    }

    void LibraryWindow::SettingsButton_Click(winrt::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        HideLibrary();
        ::Eikon::AppController::Current().OpenSettings();
    }

    void LibraryWindow::LayoutButton_Click(winrt::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        winrt::MenuFlyout menu;
        auto const current = Settings::Current().LibLayout();
        for (auto const& choice : kLayouts)
        {
            winrt::RadioMenuFlyoutItem item;
            item.Text(winrt::hstring{ ::Eikon::Strings::Get(choice.titleKey) });
            item.GroupName(L"layout");
            item.IsChecked(current == choice.value);
            winrt::BitmapIcon icon;
            icon.ShowAsMonochrome(true);
            icon.UriSource(AssetUri(choice.icon));
            item.Icon(icon);
            std::wstring const value = choice.value;
            item.Click([value](auto&&, auto&&) { Settings::Current().LibLayout(value); });
            menu.Items().Append(item);
        }
        menu.ShowAt(LayoutButton());
    }

    // changePopoverSize: small <-> medium; the gallery reloads after the resize.
    void LibraryWindow::SizeButton_Click(winrt::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        auto& settings = Settings::Current();
        settings.LibPopoveerSize(settings.LibPopoveerSize() == L"medium" ? L"small" : L"medium");
        PlaceWindow();
    }

    void LibraryWindow::UpdateLayoutButtons()
    {
        auto& settings = Settings::Current();
        auto const layout = settings.LibLayout();
        std::wstring icon = L"16-lib-layout-medium";
        for (auto const& choice : kLayouts)
        {
            if (layout == choice.value)
            {
                icon = choice.icon;
            }
        }
        LayoutIcon().UriSource(AssetUri(icon));
        SizeIcon().UriSource(AssetUri(settings.LibPopoveerSize() == L"medium" ? L"16-collapse" : L"16-expand"));
    }

    // ---- State ----

    void LibraryWindow::OnIndexChanged()
    {
        auto const& items = LibraryIndex::Current().Items();
        if (m_query.empty())
        {
            SetView(items);
        }
        else
        {
            RunSearch(m_query);
        }
        UpdateState();
    }

    void LibraryWindow::UpdateState()
    {
        try
        {
            auto& index = LibraryIndex::Current();
            auto& settings = Settings::Current();
            auto visible = [](bool on) { return on ? winrt::Visibility::Visible : winrt::Visibility::Collapsed; };
            bool const folderSet = !settings.DefaultFolder().empty();
            bool const hasRecords = !index.Items().empty();
            bool const indexing = index.IsIndexing();
            auto const phase = index.CurrentPhase();
            double const percent = index.Total() > 0 ? 100.0 * static_cast<double>(index.Done()) / static_cast<double>(index.Total()) : 0.0;
            std::wstring const left = index.SecondsLeft() >= 0 && phase == LibraryIndex::Phase::Indexing
                                          ? ::Eikon::Strings::Format(L"Library_TimeLeft", DurationText(index.SecondsLeft()))
                                          : std::wstring{};
            auto progressText = [&](bool message) {
                switch (phase)
                {
                case LibraryIndex::Phase::Preparing:
                    return ::Eikon::Strings::Get(message ? L"Library_PreparingMessage" : L"Library_Preparing");
                case LibraryIndex::Phase::Indexing:
                    return ::Eikon::Strings::Format(L"Library_Indexing", std::to_wstring(index.Done()), std::to_wstring(index.Total()));
                default:
                    return ::Eikon::Strings::Get(L"Library_Finishing");
                }
            };

            // The message holder replaces the gallery.
            std::wstring image;
            std::wstring title;
            std::wstring text;
            std::wstring button;
            std::wstring action;
            bool messageProgress = false;
            if (!folderSet)
            {
                image = L"40-empty-folder";
                title = ::Eikon::Strings::Get(L"Library_NoFolderTitle");
                text = ::Eikon::Strings::Get(L"Library_NoFolderMessage");
                button = ::Eikon::Strings::Get(L"Prefs_SelectFolder");
                action = L"folder";
            }
            else if (!hasRecords && index.Loaded())
            {
                if (indexing)
                {
                    image = L"40-empty-images";
                    title = ::Eikon::Strings::Get(L"Library_IndexingTitle");
                    text = progressText(true);
                    messageProgress = true;
                    if (phase != LibraryIndex::Phase::Finishing)
                    {
                        button = ::Eikon::Strings::Get(phase == LibraryIndex::Phase::Preparing ? L"Library_Cancel" : L"Library_Stop");
                        action = L"cancel";
                    }
                }
                else if (index.NewCount() > 0)
                {
                    image = L"40-empty-images";
                    title = ::Eikon::Strings::Get(L"Library_PreexistingTitle");
                    text = ::Eikon::Strings::Format(L"Library_PreexistingMessage", std::to_wstring(index.NewCount()));
                    button = ::Eikon::Strings::Get(L"Library_AddScreenshots");
                    action = L"add";
                }
                else
                {
                    image = L"40-empty-pinned";
                    title = ::Eikon::Strings::Get(L"Library_EmptyTitle");
                    text = ::Eikon::Strings::Get(L"Library_EmptyMessage");
                }
            }
            else if (hasRecords && !m_query.empty() && !m_searching && m_view.empty())
            {
                image = L"40-empty-notfound";
                title = ::Eikon::Strings::Get(L"Library_NothingFound");
                text = ::Eikon::Strings::Get(L"Library_NothingFoundMessage");
            }
            bool const message = !image.empty();
            MessageHolder().Visibility(visible(message));
            if (message)
            {
                MessageImage().UriSource(AssetUri(image));
                MessageTitle().Text(winrt::hstring{ title });
                MessageLabel().Text(winrt::hstring{ text });
                MessageLabel().Opacity(phase == LibraryIndex::Phase::Finishing && messageProgress ? 0.5 : 1.0);
                MessageProgress().Visibility(visible(messageProgress));
                MessageProgressBar().Value(percent);
                MessageProgressLeft().Text(winrt::hstring{ left });
                MessageAction().Visibility(visible(!button.empty()));
                MessageAction().Content(winrt::box_value(winrt::hstring{ button }));
                MessageAction().Tag(winrt::box_value(winrt::hstring{ action }));
            }

            // The progress holder sits above the gallery.
            bool const offerNew = !indexing && hasRecords && index.NewCount() > 0 && settings.ReadBool(L"libAllowPreexisting", false);
            bool const progress = hasRecords && (indexing || offerNew);
            ProgressHolder().Visibility(visible(progress));
            if (progress)
            {
                if (indexing)
                {
                    ProgressLabel().Text(winrt::hstring{ progressText(false) });
                    ProgressLabel().Opacity(phase == LibraryIndex::Phase::Finishing ? 0.5 : 1.0);
                    ProgressLeft().Text(winrt::hstring{ left });
                    ProgressBar().Visibility(winrt::Visibility::Visible);
                    ProgressBar().Value(percent);
                    ProgressAction().Visibility(visible(phase != LibraryIndex::Phase::Finishing));
                    ProgressAction().Content(winrt::box_value(winrt::hstring{ ::Eikon::Strings::Get(phase == LibraryIndex::Phase::Preparing ? L"Library_Cancel" : L"Library_Stop") }));
                    ProgressAction().Tag(winrt::box_value(winrt::hstring{ L"cancel" }));
                }
                else
                {
                    ProgressLabel().Text(winrt::hstring{ ::Eikon::Strings::Format(L"Library_NewImagesMessage", std::to_wstring(index.NewCount())) });
                    ProgressLabel().Opacity(1.0);
                    ProgressLeft().Text(L"");
                    ProgressBar().Visibility(winrt::Visibility::Collapsed);
                    ProgressAction().Visibility(winrt::Visibility::Visible);
                    ProgressAction().Content(winrt::box_value(winrt::hstring{ ::Eikon::Strings::Get(L"Library_AddFiles") }));
                    ProgressAction().Tag(winrt::box_value(winrt::hstring{ L"add" }));
                }
            }

            SearchHolder().Visibility(visible(hasRecords));
            bool const spinner = m_searching || (folderSet && !index.Loaded());
            SpinnerHolder().Visibility(visible(spinner));
            Spinner().IsActive(spinner);
            GalleryScroll().Visibility(visible(!message && hasRecords));
            UpdateStatus();
        }
        catch (...)
        {
        }
    }

    void LibraryWindow::ProgressAction_Click(winrt::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        auto const action = winrt::unbox_value_or<winrt::hstring>(ProgressAction().Tag(), L"");
        if (action == L"cancel")
        {
            LibraryIndex::Current().Cancel();
        }
        else if (action == L"add")
        {
            LibraryIndex::Current().AddNewFiles();
        }
    }

    void LibraryWindow::MessageAction_Click(winrt::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        auto const action = winrt::unbox_value_or<winrt::hstring>(MessageAction().Tag(), L"");
        if (action == L"folder")
        {
            m_modal = true;
            ::Eikon::SettingsUi::ChooseDefaultFolder(m_hwnd, Root().XamlRoot());
            m_modal = false;
            if (m_hwnd)
            {
                SetForegroundWindow(m_hwnd);
            }
        }
        else if (action == L"cancel")
        {
            LibraryIndex::Current().Cancel();
        }
        else if (action == L"add")
        {
            LibraryIndex::Current().AddNewFiles();
        }
        UpdateState();
    }

    void LibraryWindow::UpdateStatus()
    {
        try
        {
            auto const total = LibraryIndex::Current().Items().size();
            if (total == 0)
            {
                StatusText().Text(L"");
                return;
            }
            auto const selected = SelectedIndices();
            std::wstring text;
            if (selected.empty())
            {
                text = m_query.empty() ? Lib::CountText(total) : Lib::FoundText(m_view.size());
            }
            else if (selected.size() == 1)
            {
                auto const& item = m_view[selected.front()];
                text = Lib::SingleText(item->time, item->bytes);
            }
            else
            {
                text = Lib::SelectedText(selected.size());
            }
            StatusText().Text(winrt::hstring{ text });
        }
        catch (...)
        {
        }
    }

    // ---- Search ----

    void LibraryWindow::SearchBox_TextChanged(winrt::IInspectable const&, winrt::TextChangedEventArgs const&)
    {
        m_searchTimer.Stop();
        m_searchTimer.Start();
    }

    winrt::fire_and_forget LibraryWindow::RunSearch(std::wstring query)
    {
        auto weak = get_weak();
        auto const trimmed = Lib::TrimQuery(query);
        if (trimmed.empty())
        {
            if (!m_query.empty() || m_searching)
            {
                m_query.clear();
                m_searching = false;
                ++m_searchGeneration;
                SetView(LibraryIndex::Current().Items());
                UpdateState();
            }
            co_return;
        }
        m_query = trimmed;
        m_searching = true;
        auto const generation = ++m_searchGeneration;
        UpdateState();
        auto items = LibraryIndex::Current().Items();
        auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        co_await winrt::resume_background();
        std::vector<ItemPtr> result;
        for (auto const& item : items)
        {
            if (Lib::Matches(item->record, trimmed))
            {
                result.push_back(item);
            }
        }
        co_await wil::resume_foreground(dispatcher);
        auto self = weak.get();
        if (!self || generation != self->m_searchGeneration)
        {
            co_return;
        }
        self->m_searching = false;
        self->SetView(std::move(result));
        self->UpdateState();
    }

    void LibraryWindow::SetView(std::vector<ItemPtr> items)
    {
        // Keep the selection by file across reloads.
        std::set<std::wstring> selectedPaths;
        for (auto const index : m_selection)
        {
            if (index < m_view.size())
            {
                selectedPaths.insert(m_view[index]->path);
            }
        }
        std::optional<std::wstring> anchorPath;
        if (m_anchor && *m_anchor < m_view.size())
        {
            anchorPath = m_view[*m_anchor]->path;
        }
        std::optional<std::wstring> cursorPath;
        if (m_cursor && *m_cursor < m_view.size())
        {
            cursorPath = m_view[*m_cursor]->path;
        }
        m_view = std::move(items);
        m_selection.clear();
        m_anchor.reset();
        m_cursor.reset();
        for (std::size_t i = 0; i < m_view.size(); ++i)
        {
            if (selectedPaths.contains(m_view[i]->path))
            {
                m_selection.insert(i);
            }
            if (anchorPath && *anchorPath == m_view[i]->path)
            {
                m_anchor = i;
            }
            if (cursorPath && *cursorPath == m_view[i]->path)
            {
                m_cursor = i;
            }
        }
        Relayout();
        UpdateStatus();
        UpdateQuickLook();
    }

    // ---- Gallery ----

    void LibraryWindow::GalleryScroll_SizeChanged(winrt::IInspectable const&, winrt::SizeChangedEventArgs const&)
    {
        Relayout();
    }

    void LibraryWindow::GalleryScroll_ViewChanged(winrt::IInspectable const&, winrt::ScrollViewerViewChangedEventArgs const&)
    {
        Realize();
    }

    void LibraryWindow::Relayout()
    {
        try
        {
            auto& settings = Settings::Current();
            double const viewWidth = GalleryScroll().ActualWidth();
            if (viewWidth <= kScrollGutter)
            {
                return;
            }
            m_kind = LibLayout::KindFor(settings.LibLayout(), settings.LibPopoveerSize());
            std::vector<LibLayout::Size> images;
            images.reserve(m_view.size());
            for (auto const& item : m_view)
            {
                // Until the analysis knows the size, an item lays out as a square.
                images.push_back(item->width > 0 && item->height > 0 ? LibLayout::Size{ item->width, item->height } : LibLayout::Size{ 1000, 1000 });
            }
            m_layoutWidth = viewWidth - kScrollGutter;
            m_layout = LibLayout::Compute(m_kind, m_layoutWidth, images);
            m_flow = LibLayout::Place(m_layout.sizes, viewWidth, m_layout.lineSpacing);
            GalleryCanvas().Width(viewWidth);
            GalleryCanvas().Height(m_flow.height);
            for (auto const& [index, view] : m_realized)
            {
                view->index = SIZE_MAX;
                view->path.clear();
                view->root.Visibility(winrt::Visibility::Collapsed);
            }
            m_realized.clear();
            Realize();
        }
        catch (...)
        {
        }
    }

    void LibraryWindow::Realize()
    {
        double const offset = GalleryScroll().VerticalOffset();
        double const height = (std::max)(GalleryScroll().ViewportHeight(), GalleryScroll().ActualHeight());
        double const top = offset - height * 0.5;
        double const bottom = offset + height * 1.5;
        // Recycle what left the window.
        for (auto it = m_realized.begin(); it != m_realized.end();)
        {
            auto const& frame = it->first < m_flow.frames.size() ? m_flow.frames[it->first] : LibLayout::Rect{};
            if (it->first >= m_view.size() || frame.y + frame.height < top || frame.y > bottom)
            {
                it->second->index = SIZE_MAX;
                it->second->path.clear();
                it->second->root.Visibility(winrt::Visibility::Collapsed);
                it = m_realized.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (std::size_t i = 0; i < m_flow.frames.size() && i < m_view.size(); ++i)
        {
            auto const& frame = m_flow.frames[i];
            if (frame.y + frame.height < top || frame.y > bottom || m_realized.contains(i))
            {
                continue;
            }
            auto& view = AcquireView();
            m_realized[i] = &view;
            BindView(view, i);
        }
    }

    LibraryWindow::ItemView& LibraryWindow::AcquireView()
    {
        for (auto& view : m_views)
        {
            if (view->index == SIZE_MAX)
            {
                return *view;
            }
        }
        auto view = std::make_unique<ItemView>();
        view->root = winrt::Grid{};
        view->root.IsHitTestVisible(false);
        // Candy: 10-px tiles.
        view->root.CornerRadius(winrt::CornerRadius{ 10, 10, 10, 10 });
        view->tile = winrt::Border{};
        view->tile.CornerRadius(winrt::CornerRadius{ 10, 10, 10, 10 });
        view->tile.BorderThickness(winrt::Thickness{ 0.5, 0.5, 0.5, 0.5 });
        view->image = winrt::Image{};
        view->image.Stretch(winrt::Stretch::UniformToFill);
        view->image.HorizontalAlignment(winrt::HorizontalAlignment::Center);
        view->image.VerticalAlignment(winrt::VerticalAlignment::Center);
        view->tile.Child(view->image);
        view->root.Children().Append(view->tile);
        view->fileIcon = winrt::Image{};
        view->fileIcon.Width(20);
        view->fileIcon.Height(20);
        view->fileIcon.Margin(winrt::Thickness{ 2, 0, 0, 0 });
        view->fileIcon.HorizontalAlignment(winrt::HorizontalAlignment::Left);
        view->fileIcon.VerticalAlignment(winrt::VerticalAlignment::Center);
        view->root.Children().Append(view->fileIcon);
        view->name = winrt::TextBlock{};
        view->name.Margin(winrt::Thickness{ 25, 0, 0, 0 });
        view->name.VerticalAlignment(winrt::VerticalAlignment::Center);
        view->name.TextTrimming(winrt::TextTrimming::CharacterEllipsis);
        view->name.TextWrapping(winrt::TextWrapping::NoWrap);
        view->name.FontSize(13);
        view->root.Children().Append(view->name);
        view->selection = winrt::Border{};
        view->selection.CornerRadius(winrt::CornerRadius{ 10, 10, 10, 10 });
        view->selection.BorderThickness(winrt::Thickness{ 2, 2, 2, 2 });
        view->root.Children().Append(view->selection);
        view->gap = winrt::Border{};
        view->gap.Margin(winrt::Thickness{ 2, 2, 2, 2 });
        view->gap.CornerRadius(winrt::CornerRadius{ 8, 8, 8, 8 });
        view->gap.BorderThickness(winrt::Thickness{ 1, 1, 1, 1 });
        view->root.Children().Append(view->gap);
        GalleryCanvas().Children().Append(view->root);
        m_views.push_back(std::move(view));
        return *m_views.back();
    }

    void LibraryWindow::BindView(ItemView& view, std::size_t index)
    {
        auto const& item = m_view[index];
        auto const& frame = m_flow.frames[index];
        bool const dark = Root().ActualTheme() == winrt::ElementTheme::Dark;
        bool const list = m_kind == LibLayout::Kind::List;
        view.index = index;
        bool const samePath = view.path == item->path;
        view.path = item->path;
        view.root.Visibility(winrt::Visibility::Visible);
        winrt::Canvas::SetLeft(view.root, frame.x);
        winrt::Canvas::SetTop(view.root, frame.y);
        view.root.Width(frame.width);
        view.root.Height(frame.height);

        view.tile.Visibility(list ? winrt::Visibility::Collapsed : winrt::Visibility::Visible);
        view.fileIcon.Visibility(list ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);
        view.name.Visibility(list ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);
        if (list)
        {
            auto const extension = std::filesystem::path{ item->name }.extension().wstring();
            bool const png = CompareStringOrdinal(extension.c_str(), -1, L".png", -1, TRUE) == CSTR_EQUAL;
            // -dark: a light outline on the dark background.
            std::wstring const badge = std::wstring{ png ? L"20-lib-file-png" : L"20-lib-file-jpeg" } + (dark ? L"-dark" : L"");
            view.fileIcon.Source(winrt::BitmapImage{ AssetUri(badge) });
            view.name.Text(winrt::hstring{ item->name });
            view.name.Width((std::max)(0.0, m_layoutWidth - 24));
            view.name.Foreground(winrt::SolidColorBrush{ dark ? winrt::Windows::UI::Color{ 255, 255, 255, 255 } : winrt::Windows::UI::Color{ 255, 0, 0, 0 } });
            view.image.Source(nullptr);
        }
        else
        {
            // Unselected: 0.5 pt, white (dark) or black (light) at 0.4; filled with basecolor until the
            // thumbnail arrives.
            view.tile.BorderBrush(winrt::SolidColorBrush{ dark ? winrt::Windows::UI::Color{ 102, 255, 255, 255 } : winrt::Windows::UI::Color{ 102, 0, 0, 0 } });
            view.tile.Background(winrt::SolidColorBrush{ ParseColor(Lib::Field(item->record, Lib::kBaseColor), dark ? winrt::Windows::UI::Color{ 255, 64, 64, 64 } : winrt::Windows::UI::Color{ 255, 230, 230, 230 }) });
            if (!samePath)
            {
                view.image.Source(nullptr);
            }
            RequestThumbnail(view);
        }
        UpdateSelectionVisual(view);
    }

    void LibraryWindow::UpdateSelectionVisual(ItemView& view)
    {
        bool const selected = view.index != SIZE_MAX && m_selection.contains(view.index);
        bool const dark = Root().ActualTheme() == winrt::ElementTheme::Dark;
        bool const list = m_kind == LibLayout::Kind::List;
        auto accent = AccentColor(dark);
        double alpha = m_galleryFocused ? 0.25 : 0.15;
        if (dark)
        {
            alpha *= 1.75;
        }
        auto fill = accent;
        fill.A = static_cast<std::uint8_t>(std::lround(alpha * 255));
        view.root.Background(selected ? winrt::Brush{ winrt::SolidColorBrush{ fill } } : nullptr);
        bool const ring = selected && !list;
        view.selection.Visibility(ring ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);
        view.gap.Visibility(ring ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);
        if (ring)
        {
            view.selection.BorderBrush(winrt::SolidColorBrush{ accent });
            // The gap between the tile and the accent ring: the Library's base colour.
            auto const base = ::Eikon::Candy::For(dark).base;
            view.gap.BorderBrush(winrt::SolidColorBrush{ winrt::Windows::UI::Color{ base.a, base.r, base.g, base.b } });
        }
    }

    void LibraryWindow::UpdateAllSelectionVisuals()
    {
        for (auto const& [index, view] : m_realized)
        {
            UpdateSelectionVisual(*view);
        }
    }

    // Thumbnails and their cache: the cell's aspect-fill size x backing scale.
    void LibraryWindow::RequestThumbnail(ItemView& view)
    {
        if (view.index >= m_layout.uncropped.size())
        {
            return;
        }
        double const scale = Root().XamlRoot() ? Root().XamlRoot().RasterizationScale() : 1.0;
        auto const size = m_layout.uncropped[view.index];
        int const width = (std::max)(1, static_cast<int>(std::lround(size.width * scale)));
        int const height = (std::max)(1, static_cast<int>(std::lround(size.height * scale)));
        if (auto it = m_thumbnails.find(view.path); it != m_thumbnails.end())
        {
            view.image.Source(it->second.source);
            m_lru.remove(view.path);
            m_lru.push_front(view.path);
            if (it->second.width >= kMinimumReuse * width)
            {
                return;
            }
        }
        if (!m_loading.contains(view.path))
        {
            m_loading.insert(view.path);
            LoadThumbnail(view.path, width, height);
        }
    }

    winrt::fire_and_forget LibraryWindow::LoadThumbnail(std::wstring path, int width, int height)
    {
        auto weak = get_weak();
        auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        co_await winrt::resume_background();
        auto pixels = ::Eikon::ImageCodec::DecodeFileScaled(path.c_str(), static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), true);
        co_await wil::resume_foreground(dispatcher);
        auto self = weak.get();
        if (!self)
        {
            co_return;
        }
        self->m_loading.erase(path);
        if (pixels.empty())
        {
            co_return;
        }
        winrt::WriteableBitmap bitmap{ width, height };
        auto buffer = bitmap.PixelBuffer();
        std::memcpy(buffer.data(), pixels.data(), (std::min)(static_cast<std::size_t>(buffer.Capacity()), pixels.size()));
        bitmap.Invalidate();
        self->m_thumbnails[path] = Thumbnail{ bitmap, width, height };
        self->m_lru.remove(path);
        self->m_lru.push_front(path);
        auto const limit = static_cast<std::size_t>((std::clamp<std::int64_t>)(Settings::Current().LibLimit(), 1, static_cast<std::int64_t>(kThumbnailCacheCap)));
        while (self->m_lru.size() > limit)
        {
            self->m_thumbnails.erase(self->m_lru.back());
            self->m_lru.pop_back();
        }
        for (auto const& [index, view] : self->m_realized)
        {
            if (view->path == path)
            {
                view->image.Source(bitmap);
            }
        }
    }

    std::optional<std::size_t> LibraryWindow::HitTest(winrt::Point point) const
    {
        for (std::size_t i = 0; i < m_flow.frames.size() && i < m_view.size(); ++i)
        {
            auto const& frame = m_flow.frames[i];
            if (point.X >= frame.x && point.X < frame.x + frame.width && point.Y >= frame.y && point.Y < frame.y + frame.height)
            {
                return i;
            }
        }
        return std::nullopt;
    }

    void LibraryWindow::GalleryCanvas_PointerPressed(winrt::IInspectable const&, winrt::PointerRoutedEventArgs const& args)
    {
        auto const point = args.GetCurrentPoint(GalleryCanvas());
        auto const properties = point.Properties();
        auto const index = HitTest(point.Position());
        GalleryScroll().Focus(winrt::FocusState::Pointer);
        args.Handled(true);
        auto const modifiers = ::Eikon::CurrentModifiers();
        bool const ctrl = ::Eikon::HasFlag(modifiers, ::Eikon::Modifiers::Ctrl);
        bool const shift = ::Eikon::HasFlag(modifiers, ::Eikon::Modifiers::Shift);
        if (properties.IsRightButtonPressed())
        {
            // Right-click selects the clicked item first, unless it is already selected.
            if (index && !m_selection.contains(*index))
            {
                SelectOnly(*index);
            }
            if (index)
            {
                ShowContextMenu(point.Position());
            }
            return;
        }
        if (!properties.IsLeftButtonPressed())
        {
            return;
        }
        if (!index)
        {
            if (!ctrl && !shift && !m_selection.empty())
            {
                m_selection.clear();
                SelectionChanged();
            }
            return;
        }
        auto const now = std::chrono::steady_clock::now();
        bool const doubleClick = modifiers == ::Eikon::Modifiers::None && m_lastDownIndex == index && now - m_lastDown < kDoubleClick;
        m_lastDown = now;
        m_lastDownIndex = index;
        if (doubleClick)
        {
            // The default action libAction.
            m_lastDownIndex.reset();
            RunAction(Settings::Current().LibAction(), *index);
            return;
        }
        Select(*index, shift, ctrl);
    }

    void LibraryWindow::GalleryScroll_FocusChanged(winrt::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        bool const focused = GalleryScroll().FocusState() != winrt::FocusState::Unfocused;
        if (focused != m_galleryFocused)
        {
            m_galleryFocused = focused;
            UpdateAllSelectionVisuals();
        }
    }

    // An alphanumeric, punctuation or whitespace key (not Space, no Control) moves focus to the search
    // field and types the character there.
    void LibraryWindow::GalleryScroll_CharacterReceived(winrt::UIElement const&, winrt::CharacterReceivedRoutedEventArgs const& args)
    {
        wchar_t const c = args.Character();
        if (c == L' ' || c < 0x20 || c == 0x7F || ::Eikon::HasFlag(::Eikon::CurrentModifiers(), ::Eikon::Modifiers::Ctrl))
        {
            return;
        }
        if (!(std::iswalnum(c) || std::iswpunct(c) || std::iswspace(c)))
        {
            return;
        }
        args.Handled(true);
        auto box = SearchBox();
        box.Focus(winrt::FocusState::Keyboard);
        box.Text(box.Text() + winrt::hstring{ std::wstring(1, c) });
        box.SelectionStart(static_cast<int32_t>(box.Text().size()));
    }

    void LibraryWindow::Select(std::size_t index, bool extend, bool toggle)
    {
        if (index >= m_view.size())
        {
            return;
        }
        if (toggle)
        {
            if (!m_selection.erase(index))
            {
                m_selection.insert(index);
            }
            m_anchor = index;
            m_cursor = index;
        }
        else if (extend && m_anchor)
        {
            m_selection.clear();
            for (std::size_t i = (std::min)(*m_anchor, index); i <= (std::max)(*m_anchor, index); ++i)
            {
                m_selection.insert(i);
            }
            m_cursor = index;
        }
        else
        {
            SelectOnly(index);
            return;
        }
        ScrollIntoView(index);
        SelectionChanged();
    }

    void LibraryWindow::SelectOnly(std::size_t index)
    {
        if (index >= m_view.size())
        {
            return;
        }
        m_selection = { index };
        m_anchor = index;
        m_cursor = index;
        ScrollIntoView(index);
        SelectionChanged();
    }

    void LibraryWindow::ScrollIntoView(std::size_t index)
    {
        if (index >= m_flow.frames.size())
        {
            return;
        }
        auto const& frame = m_flow.frames[index];
        double const offset = GalleryScroll().VerticalOffset();
        double const height = GalleryScroll().ViewportHeight();
        if (frame.y < offset)
        {
            GalleryScroll().ChangeView(nullptr, frame.y - LibLayout::kInsetTop, nullptr, true);
        }
        else if (frame.y + frame.height > offset + height)
        {
            GalleryScroll().ChangeView(nullptr, frame.y + frame.height - height + 12, nullptr, true);
        }
    }

    void LibraryWindow::SelectionChanged()
    {
        UpdateAllSelectionVisuals();
        // A change to an empty selection is debounced by 0.04 s.
        m_statusTimer.Stop();
        if (m_selection.empty())
        {
            m_statusTimer.Start();
        }
        else
        {
            UpdateStatus();
        }
        UpdateQuickLook();
    }

    std::vector<std::size_t> LibraryWindow::SelectedIndices() const
    {
        std::vector<std::size_t> indices;
        for (auto const index : m_selection)
        {
            if (index < m_view.size())
            {
                indices.push_back(index);
            }
        }
        return indices;
    }

    std::optional<std::size_t> LibraryWindow::FirstSelected() const
    {
        if (m_cursor && m_selection.contains(*m_cursor) && *m_cursor < m_view.size())
        {
            return m_cursor;
        }
        auto const indices = SelectedIndices();
        if (indices.empty())
        {
            return std::nullopt;
        }
        return indices.front();
    }

    // ---- Keyboard ----

    void LibraryWindow::Root_PreviewKeyDown(winrt::IInspectable const&, winrt::KeyRoutedEventArgs const& args)
    {
        using VK = winrt::Windows::System::VirtualKey;
        auto const key = args.Key();
        auto const modifiers = ::Eikon::CurrentModifiers();
        bool const ctrl = ::Eikon::HasFlag(modifiers, ::Eikon::Modifiers::Ctrl);
        bool const shift = ::Eikon::HasFlag(modifiers, ::Eikon::Modifiers::Shift);

        if (ctrl && shift && key == VK::R)
        {
            LibraryIndex::Current().Rescan();
            args.Handled(true);
            return;
        }
        if (key == VK::Escape)
        {
            if (m_quickLook.IsVisible())
            {
                m_quickLook.Hide();
            }
            else
            {
                HideLibrary();
            }
            args.Handled(true);
            return;
        }
        if (!m_galleryFocused)
        {
            // Down with nothing selected selects item 0 and focuses the gallery.
            if (key == VK::Down && !m_view.empty() && GalleryScroll().Visibility() == winrt::Visibility::Visible)
            {
                if (m_selection.empty())
                {
                    SelectOnly(0);
                }
                GalleryScroll().Focus(winrt::FocusState::Keyboard);
                args.Handled(true);
            }
            return;
        }
        if (m_view.empty())
        {
            return;
        }
        std::size_t const last = m_view.size() - 1;
        auto move = [&](std::optional<std::size_t> target) {
            if (!target)
            {
                return;
            }
            std::size_t const index = (std::min)(*target, last);
            if (shift)
            {
                if (!m_anchor)
                {
                    m_anchor = FirstSelected().value_or(index);
                }
                Select(index, true, false);
            }
            else
            {
                SelectOnly(index);
            }
        };
        std::size_t const current = m_cursor.value_or(FirstSelected().value_or(0));
        bool const none = m_selection.empty();
        std::size_t const row = m_kind == LibLayout::Kind::List ? 1 : static_cast<std::size_t>((std::max)(1, m_layout.itemsPerRow));
        bool handled = true;
        switch (key)
        {
        case VK::Space:
            ToggleQuickLook();
            break;
        case VK::Enter:
            RunAction(Settings::Current().LibAction());
            break;
        case VK::C:
            if (ctrl) RunAction(L"copy");
            else handled = false;
            break;
        case VK::P:
            if (ctrl) RunAction(L"pin");
            else handled = false;
            break;
        case VK::I:
            if (ctrl) RunAction(L"info");
            else handled = false;
            break;
        case VK::A:
            if (ctrl)
            {
                for (std::size_t i = 0; i < m_view.size(); ++i)
                {
                    m_selection.insert(i);
                }
                SelectionChanged();
            }
            else handled = false;
            break;
        case VK::Delete:
            RunAction(L"delete");
            break;
        case VK::Left:
            move(none ? 0 : (current == 0 ? 0 : current - 1));
            break;
        case VK::Right:
            move(none ? 0 : current + 1);
            break;
        case VK::Up:
            move(ctrl ? 0 : (none ? 0 : (current >= row ? current - row : current)));
            break;
        case VK::Down:
            move(ctrl ? last : (none ? 0 : (current + row <= last ? current + row : current)));
            break;
        case VK::Home:
            move(0);
            break;
        case VK::End:
            move(last);
            break;
        default:
            handled = false;
            break;
        }
        if (handled)
        {
            args.Handled(true);
        }
    }

    // ---- Context menu and actions ----

    void LibraryWindow::ShowContextMenu(winrt::Point point)
    {
        auto const count = SelectedIndices().size();
        struct Entry
        {
            std::wstring title;
            wchar_t const* accelerator;
            wchar_t const* action;
        };
        std::vector<Entry> const entries = {
            { ::Eikon::Strings::Get(L"Library_MenuQuickLook"), L"Space", L"quicklook" },
            { ::Eikon::Strings::Get(L"Library_MenuEdit"), L"Enter", L"eikon" },
            { ::Eikon::Strings::Get(L"Library_MenuOpen"), nullptr, L"open" },
            { ::Eikon::Strings::Get(L"Library_MenuExplorer"), nullptr, L"finder" },
            { ::Eikon::Strings::Get(L"Library_MenuCopy"), L"Ctrl+C", L"copy" },
            { count >= 2 ? ::Eikon::Strings::Format(L"Library_MenuCopyFiles", std::to_wstring(count)) : ::Eikon::Strings::Get(L"Library_MenuCopyFile"), nullptr, L"copyFiles" },
            { ::Eikon::Strings::Get(L"Library_MenuPin"), L"Ctrl+P", L"pin" },
            // menuWillOpen retitles Delete with the count.
            { ::Eikon::Strings::Format(L"Library_MenuDeleteCount", std::to_wstring(count)), L"Delete", L"delete" },
        };
        winrt::MenuFlyout menu;
        for (auto const& entry : entries)
        {
            winrt::MenuFlyoutItem item;
            item.Text(winrt::hstring{ entry.title });
            if (entry.accelerator)
            {
                item.KeyboardAcceleratorTextOverride(entry.accelerator);
            }
            std::wstring const action = entry.action;
            item.Click([weak = get_weak(), action](auto&&, auto&&) {
                if (auto self = weak.get())
                {
                    self->RunAction(action);
                }
            });
            menu.Items().Append(item);
        }
        winrt::FlyoutShowOptions options;
        options.Position(point);
        menu.ShowAt(GalleryCanvas(), options);
    }

    bool LibraryWindow::CheckExists(ItemPtr const& item)
    {
        if (FileExists(item->path))
        {
            return true;
        }
        ::Eikon::AppController::Current().Notify({ ::Eikon::Strings::Get(L"Library_GoneTitle"),
                                                    ::Eikon::Strings::Format(L"Library_GoneMessage", item->name), true, 5.0, true });
        return false;
    }

    void LibraryWindow::RunAction(std::wstring const& action, std::optional<std::size_t> clicked)
    {
        auto const index = clicked ? clicked : FirstSelected();
        if (!index || *index >= m_view.size())
        {
            return;
        }
        auto const item = m_view[*index];
        if (!CheckExists(item))
        {
            return;
        }
        // Ctrl+I goes through the same dispatcher as Quick Look.
        if (action == L"quicklook" || action == L"info")
        {
            ToggleQuickLook();
            return;
        }
        std::vector<std::wstring> selectedPaths;
        for (auto const i : SelectedIndices())
        {
            selectedPaths.push_back(m_view[i]->path);
        }
        if (selectedPaths.empty())
        {
            selectedPaths.push_back(item->path);
        }
        if (action == L"delete")
        {
            DeleteSelected();
            HideLibrary();
            return;
        }
        HideLibrary();
        auto& app = ::Eikon::AppController::Current();
        double const dpr = std::wcstod(Lib::Field(item->record, Lib::kDpr).c_str(), nullptr);
        if (action == L"eikon")
        {
            if (!app.OpenImageFile(item->path, dpr))
            {
                app.Notify({ ::Eikon::Strings::Get(L"Library_CantOpenTitle"), ::Eikon::Strings::Format(L"Library_CantOpenMessage", item->name), true, 5.0, true });
            }
        }
        else if (action == L"open")
        {
            ShellExecuteW(nullptr, L"open", item->path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        else if (action == L"finder")
        {
            // lib:action-finder reveals every selected file together.
            RevealInExplorer(selectedPaths);
        }
        else if (action == L"copy")
        {
            if (auto bitmap = ::Eikon::LibraryFiles::Decode(item->path, dpr))
            {
                if (::Eikon::Exporter::CopyToClipboard(*bitmap, app.MessageWindowHandle()))
                {
                    app.Notify({ ::Eikon::Strings::Get(L"Library_CopiedTitle"), ::Eikon::Strings::Get(L"Library_CopiedMessage") });
                }
            }
        }
        else if (action == L"copyFiles")
        {
            CopyFiles(selectedPaths, app.MessageWindowHandle());
        }
        else if (action == L"pin")
        {
            if (auto bitmap = ::Eikon::LibraryFiles::Decode(item->path, dpr))
            {
                app.PinImage(std::make_shared<::Eikon::Core::Bitmap const>(std::move(*bitmap)));
            }
        }
    }

    // Moves the selected files to the Recycle Bin, drops their records and selects the
    // item now at the first deleted index.
    void LibraryWindow::DeleteSelected()
    {
        auto const indices = SelectedIndices();
        if (indices.empty())
        {
            return;
        }
        std::vector<std::wstring> paths;
        for (auto const i : indices)
        {
            paths.push_back(m_view[i]->path);
        }
        std::size_t const first = indices.front();
        auto const recycled = Recycle(paths);
        m_selection.clear();
        LibraryIndex::Current().Remove(recycled);
        if (!m_view.empty())
        {
            SelectOnly((std::min)(first, m_view.size() - 1));
        }
        else
        {
            SelectionChanged();
        }
    }

    void LibraryWindow::ToggleQuickLook()
    {
        if (m_quickLook.IsVisible())
        {
            m_quickLook.Hide();
            m_quickLookPath.clear();
            return;
        }
        m_quickLookPath.clear();
        auto const index = FirstSelected();
        if (!index)
        {
            return;
        }
        auto const& item = m_view[*index];
        double width = item->width;
        double height = item->height;
        if (width <= 0 || height <= 0)
        {
            auto const [w, h] = ::Eikon::LibraryFiles::PixelSize(item->path);
            width = w;
            height = h;
        }
        double const dpr = std::wcstod(Lib::Field(item->record, Lib::kDpr).c_str(), nullptr);
        if (m_quickLook.Show(item->path, width, height, dpr > 0 ? dpr : 1.0, m_hwnd, Settings::Current().LibQLCurtain()))
        {
            m_quickLookPath = item->path;
        }
    }

    // While Quick Look is open, moving the selection replaces the image.
    void LibraryWindow::UpdateQuickLook()
    {
        if (!m_quickLook.IsVisible())
        {
            return;
        }
        auto const index = FirstSelected();
        if (!index)
        {
            m_quickLook.Hide();
            m_quickLookPath.clear();
            return;
        }
        if (m_view[*index]->path != m_quickLookPath)
        {
            m_quickLook.Hide();
            ToggleQuickLook();
        }
    }
}
