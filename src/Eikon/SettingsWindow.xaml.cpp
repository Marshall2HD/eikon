#include "pch.h"
#include "SettingsWindow.xaml.h"
#if __has_include("SettingsWindow.g.cpp")
#include "SettingsWindow.g.cpp"
#endif

#include "Platform/Candy.h"
#include "Strings.h"

namespace winrt
{
    using namespace Microsoft::UI::Windowing;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Media::Animation;
    using namespace Windows::Graphics;
}

namespace
{
    // Panes are 520 pt wide; the navigation pane adds 200.
    constexpr double kInitialWidth = 900;   // DIPs
    constexpr double kInitialHeight = 700;
    constexpr int kMinimumWidth = 640;
    constexpr int kMinimumHeight = 480;

    std::wstring IconPath()
    {
        wchar_t buffer[MAX_PATH]{};
        DWORD const length = GetModuleFileNameW(nullptr, buffer, ARRAYSIZE(buffer));
        std::wstring path{ buffer, length };
        auto const slash = path.find_last_of(L'\\');
        path = slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash + 1);
        return path + L"Assets\\Eikon.ico";
    }
}

namespace winrt::Eikon::implementation
{
    void SettingsWindow::InitializeComponent()
    {
        SettingsWindowT<SettingsWindow>::InitializeComponent();
        Title(winrt::hstring{ ::Eikon::Strings::Get(L"Settings_WindowTitle") });
        try
        {
            ExtendsContentIntoTitleBar(true);
            SetTitleBar(AppTitleBar());
        }
        catch (...)
        {
        }

        // Candy's sidebar: rounded (10) items, the selected one raised with an accent icon.
        RootGrid().ActualThemeChanged([this](auto&&, auto&&) { UpdateNavItems(); });

        // Release the pages when the window closes, so a recorder that is still listening resumes the
        // global hotkeys right away.
        Closed([this](auto&&, auto&&) {
            try
            {
                ContentFrame().Content(nullptr);
            }
            catch (...)
            {
            }
        });

        // General unless the AppController selects another pane right after construction.
        auto weak = get_weak();
        if (auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread())
        {
            dispatcher.TryEnqueue([weak]() {
                if (auto self = weak.get())
                {
                    if (self->m_currentTag.empty())
                    {
                        self->ShowPage(L"general");
                    }
                }
            });
        }

        auto appWindow = AppWindow();
        if (!appWindow)
        {
            return;
        }
        try
        {
            appWindow.TitleBar().PreferredHeightOption(winrt::TitleBarHeightOption::Tall);
            appWindow.SetIcon(IconPath());
        }
        catch (...)
        {
        }
        try
        {
            // Centred on the monitor under the mouse (where the tray menu was).
            POINT cursor{};
            GetCursorPos(&cursor);
            UINT dpiX = USER_DEFAULT_SCREEN_DPI;
            UINT dpiY = USER_DEFAULT_SCREEN_DPI;
            double scale = 1.0;
            if (SUCCEEDED(GetDpiForMonitor(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX > 0)
            {
                scale = static_cast<double>(dpiX) / USER_DEFAULT_SCREEN_DPI;
            }
            auto const work = winrt::DisplayArea::GetFromPoint(winrt::PointInt32{ cursor.x, cursor.y }, winrt::DisplayAreaFallback::Primary).WorkArea();
            int const width = (std::min)(static_cast<int>(kInitialWidth * scale), work.Width);
            int const height = (std::min)(static_cast<int>(kInitialHeight * scale), work.Height);
            appWindow.MoveAndResize(winrt::RectInt32{ work.X + (work.Width - width) / 2, work.Y + (work.Height - height) / 2, width, height });
            if (auto presenter = appWindow.Presenter().try_as<winrt::OverlappedPresenter>())
            {
                presenter.PreferredMinimumWidth(static_cast<int>(kMinimumWidth * scale));
                presenter.PreferredMinimumHeight(static_cast<int>(kMinimumHeight * scale));
            }
        }
        catch (...)
        {
            OutputDebugStringW(L"Eikon: failed to position the Settings window\n");
        }
    }

    void SettingsWindow::ShowPage(winrt::hstring const& tag)
    {
        try
        {
            winrt::hstring target = tag;
            auto item = FindItem(target);
            if (!item)
            {
                target = L"general";
                item = FindItem(target);
            }
            Navigate(target);
            if (item && NavView().SelectedItem() != item)
            {
                NavView().SelectedItem(item);
            }
        }
        catch (...)
        {
            OutputDebugStringW(L"Eikon: failed to show a settings page\n");
        }
    }

    void SettingsWindow::NavView_SelectionChanged(winrt::NavigationView const&, winrt::NavigationViewSelectionChangedEventArgs const& args)
    {
        try
        {
            if (auto container = args.SelectedItemContainer())
            {
                Navigate(winrt::unbox_value_or<winrt::hstring>(container.Tag(), L"general"));
            }
        }
        catch (...)
        {
        }
    }

    winrt::NavigationViewItem SettingsWindow::FindItem(winrt::hstring const& tag)
    {
        for (auto const& entry : NavView().MenuItems())
        {
            if (auto item = entry.try_as<winrt::NavigationViewItem>())
            {
                if (winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"") == tag)
                {
                    return item;
                }
            }
        }
        return nullptr;
    }

    void SettingsWindow::Navigate(winrt::hstring const& tag)
    {
        if (tag == m_currentTag)
        {
            return;
        }
        winrt::Windows::UI::Xaml::Interop::TypeName pageType{};
        if (tag == L"general") pageType = winrt::xaml_typename<winrt::Eikon::GeneralSettingsPage>();
        else if (tag == L"hotkeys") pageType = winrt::xaml_typename<winrt::Eikon::HotkeysSettingsPage>();
        else if (tag == L"uploading") pageType = winrt::xaml_typename<winrt::Eikon::UploadSettingsPage>();
        else if (tag == L"advanced") pageType = winrt::xaml_typename<winrt::Eikon::AdvancedSettingsPage>();
        else return;
        m_currentTag = tag;
        ContentFrame().Navigate(pageType, nullptr, winrt::EntranceNavigationTransitionInfo{});
        UpdateNavItems();
    }

    void SettingsWindow::UpdateNavItems()
    {
        try
        {
            bool const dark = RootGrid().ActualTheme() == winrt::ElementTheme::Dark;
            bool const contrast = ::Eikon::Candy::HighContrast();
            auto const& candy = ::Eikon::Candy::For(dark);
            auto const brush = [](::Eikon::Candy::Argb c) { return winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{ { c.a, c.r, c.g, c.b } }; };
            for (auto const& entry : NavView().MenuItems())
            {
                auto item = entry.try_as<winrt::NavigationViewItem>();
                if (!item)
                {
                    continue;
                }
                bool const selected = winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"") == m_currentTag;
                item.CornerRadius(winrt::CornerRadius{ 10, 10, 10, 10 });
                item.FontWeight(selected ? winrt::Windows::UI::Text::FontWeights::SemiBold() : winrt::Windows::UI::Text::FontWeights::Normal());
                if (auto icon = item.Icon().try_as<winrt::BitmapIcon>(); icon && !contrast)
                {
                    icon.Foreground(brush(selected ? candy.accent : candy.ink2));
                }
            }
        }
        catch (...)
        {
        }
    }
}
