#include "pch.h"
#include "GeneralSettingsPage.xaml.h"
#if __has_include("GeneralSettingsPage.g.cpp")
#include "GeneralSettingsPage.g.cpp"
#endif

#include "AppController.h"
#include "Core/Color.h"
#include "Core/Preferences.h"
#include "Core/Settings.h"
#include "Platform/Candy.h"
#include "Platform/SystemIntegration.h"
#include "SettingsHelpers.h"
#include "Strings.h"

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Media;
    using namespace Microsoft::UI::Xaml::Media::Imaging;
}

namespace
{
    using ::Eikon::Core::Settings;
    namespace Prefs = ::Eikon::Core::Preferences;

    struct TileSpec
    {
        wchar_t const* value;     // windowShadow
        wchar_t const* image;     // Assets/Prefs/<image>.png (+ "-dark")
        wchar_t const* caption;   // resource key
    };
    // riWinBackground: tags 1-4.
    constexpr TileSpec kTiles[] = {
        { L"wallpaper", L"pref-window-wallpaper", L"Prefs_BgWallpaper" },
        { L"transparent", L"pref-window-transparent", L"Prefs_BgTransparent" },
        { L"solid", L"pref-window-solid", L"Prefs_BgSolid" },
        { L"trimmed", L"pref-window-trimmed", L"Prefs_BgTrimmed" },
    };

    // A stored colour that does not parse shows as sRGB (0.96, 0.28, 0.14).
    winrt::Windows::UI::Color SolidColor()
    {
        auto const parsed = ::Eikon::Core::Color::FromHex(Settings::Current().WindowSolidColor());
        if (!parsed)
        {
            return winrt::Windows::UI::Color{ 255, 245, 71, 36 };
        }
        return winrt::Windows::UI::Color{ 255, parsed->r, parsed->g, parsed->b };
    }
}

namespace winrt::Eikon::implementation
{
    GeneralSettingsPage::~GeneralSettingsPage()
    {
        if (m_settingsToken)
        {
            Settings::Current().Changed.Remove(m_settingsToken);
        }
    }

    void GeneralSettingsPage::Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            if (m_tiles.empty())
            {
                BuildBackgroundTiles();
                ActualThemeChanged([weak = get_weak()](auto&&, auto&&) {
                    if (auto self = weak.get())
                    {
                        self->UpdateBackgroundTiles();
                    }
                });
            }
            LoadValues();
            LoadAutostart();
            if (!m_settingsToken)
            {
                m_settingsToken = Settings::Current().Changed.Add([weak = get_weak()](std::wstring_view key) {
                    if (auto self = weak.get())
                    {
                        self->OnSettingChanged(key);
                    }
                });
            }
        }
        catch (...)
        {
        }
    }

    void GeneralSettingsPage::Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        // updateScrollingMax also runs on viewWillDisappear.
        CommitScrollingMax();
        if (m_settingsToken)
        {
            Settings::Current().Changed.Remove(m_settingsToken);
            m_settingsToken = 0;
        }
    }

    void GeneralSettingsPage::OnSettingChanged(std::wstring_view key)
    {
        if (m_loading)
        {
            return;
        }
        if (key == Settings::kDefaultFolder || key == Settings::kAfterGrabSave || key == Settings::kAfterGrabShow ||
            key == Settings::kAfterGrabCopy || key == Settings::kWindowShadow || key == Settings::kWindowSolidColor)
        {
            LoadValues();
        }
    }

    void GeneralSettingsPage::LoadValues()
    {
        auto& settings = Settings::Current();
        m_loading = true;
        try
        {
            UpdateBackgroundTiles();
            auto const folder = settings.DefaultFolder();
            SaveFolderText().Text(folder.empty() ? winrt::hstring{ ::Eikon::Strings::Get(L"Prefs_SelectFolder") } : winrt::hstring{ folder });
            winrt::ToolTipService::SetToolTip(SaveFolderButton(), folder.empty() ? nullptr : winrt::box_value(winrt::hstring{ folder }));
            auto const format = settings.SaveFormat();
            SaveFormatPng().IsChecked(format == L"PNG");
            SaveFormatAuto().IsChecked(format != L"PNG");
            DownscaleToggle().IsOn(settings.DownscaleOnSave());
            ScrollingMaxBox().Text(winrt::to_hstring(settings.ScrollingMax()));
            ScrollingSpeedSlider().Value(static_cast<double>(std::clamp<std::int64_t>(settings.ScrollingSpeed(), 0, 3)));
            AfterShowCheck().IsChecked(settings.AfterGrabShow());
            AfterCopyCheck().IsChecked(settings.AfterGrabCopy());
            AfterSaveCheck().IsChecked(settings.AfterGrabSave());
            // The default "none" displays as Editor.
            bool const thumbnail = settings.AreaCaptureMode() == L"preview";
            AreaThumbnailRadio().IsChecked(thumbnail);
            AreaEditorRadio().IsChecked(!thumbnail);
            ThumbnailClosingCombo().SelectedIndex(settings.ThumbnailClosing() == L"auto" ? 0 : 1);
            UpdateEnabled();
        }
        catch (...)
        {
        }
        m_loading = false;
    }

    // Thumbnail/Editor need Show; the closing popup needs areaCaptureMode == "preview" as well.
    void GeneralSettingsPage::UpdateEnabled()
    {
        auto& settings = Settings::Current();
        bool const show = settings.AfterGrabShow();
        AreaCropCard().IsEnabled(show);
        ThumbnailClosingCard().IsEnabled(show && settings.AreaCaptureMode() == L"preview");
    }

    winrt::fire_and_forget GeneralSettingsPage::LoadAutostart()
    {
        auto weak = get_weak();
        bool const enabled = co_await ::Eikon::SystemIntegration::IsLaunchAtLoginEnabled();
        if (auto self = weak.get())
        {
            self->m_loading = true;
            self->AutostartToggle().IsOn(enabled);
            self->m_loading = false;
        }
    }

    void GeneralSettingsPage::BuildBackgroundTiles()
    {
        auto panel = BackgroundPanel();
        for (auto const& spec : kTiles)
        {
            Tile tile;
            tile.value = spec.value;

            // Candy: the 85x60 preview in an 8-px rounded clip, inside a 2-px accent ring with a 2-px gap
            // (radius 12), so the ring never covers the picture. The ring is transparent when unselected.
            winrt::Grid picture;
            picture.Width(85);
            picture.Height(60);
            tile.fill = winrt::Border{};
            picture.Children().Append(tile.fill);
            tile.image = winrt::Image{};
            tile.image.Stretch(winrt::Stretch::Fill);
            picture.Children().Append(tile.image);
            winrt::Border clip;
            clip.CornerRadius(winrt::CornerRadius{ 8, 8, 8, 8 });
            clip.Child(picture);
            tile.ring = winrt::Border{};
            tile.ring.CornerRadius(winrt::CornerRadius{ 12, 12, 12, 12 });
            tile.ring.BorderThickness(winrt::Thickness{ 2, 2, 2, 2 });
            tile.ring.Padding(winrt::Thickness{ 2, 2, 2, 2 });
            tile.ring.Child(clip);
            auto face = tile.ring;

            winrt::Button button;
            button.Padding(winrt::Thickness{});
            button.BorderThickness(winrt::Thickness{});
            button.Background(winrt::SolidColorBrush{ winrt::Microsoft::UI::Colors::Transparent() });
            button.CornerRadius(winrt::CornerRadius{ 12, 12, 12, 12 });
            button.Content(face);
            auto const caption = ::Eikon::Strings::Get(spec.caption);
            winrt::Automation::AutomationProperties::SetName(button, winrt::hstring{ caption });
            std::wstring const value = spec.value;
            button.Click([weak = get_weak(), value](auto&&, auto&&) {
                if (auto self = weak.get())
                {
                    Settings::Current().WindowShadow(value);
                    self->UpdateBackgroundTiles();
                    if (value == L"solid" && self->m_solidBox)
                    {
                        self->m_solidBox.Focus(winrt::FocusState::Programmatic);
                    }
                }
            });

            tile.caption = winrt::TextBlock{};
            tile.caption.Text(caption);
            tile.caption.FontSize(12);
            tile.caption.HorizontalAlignment(winrt::HorizontalAlignment::Center);

            winrt::Grid captionHost;
            captionHost.Height(24);
            captionHost.Children().Append(tile.caption);
            if (value == L"solid")
            {
                // tfSolidColor replaces the caption while Solid is selected.
                m_solidBox = winrt::TextBox{};
                m_solidBox.Width(85);
                m_solidBox.MinWidth(0);
                m_solidBox.MinHeight(0);
                m_solidBox.Height(24);
                m_solidBox.Padding(winrt::Thickness{ 6, 2, 6, 2 });
                m_solidBox.FontSize(12);
                m_solidBox.TextAlignment(winrt::TextAlignment::Center);
                winrt::Automation::AutomationProperties::SetName(m_solidBox, winrt::hstring{ ::Eikon::Strings::Get(L"Prefs_SolidColorField") });
                m_solidBox.KeyDown([weak = get_weak()](auto&&, winrt::Input::KeyRoutedEventArgs const& args) {
                    if (args.Key() == winrt::Windows::System::VirtualKey::Enter)
                    {
                        if (auto self = weak.get())
                        {
                            self->CommitSolidColor();
                        }
                        args.Handled(true);
                    }
                });
                m_solidBox.LostFocus([weak = get_weak()](auto&&, auto&&) {
                    if (auto self = weak.get())
                    {
                        self->CommitSolidColor();
                    }
                });
                captionHost.Children().Append(m_solidBox);
            }

            winrt::StackPanel column;
            column.Spacing(6);
            column.Children().Append(button);
            column.Children().Append(captionHost);
            panel.Children().Append(column);
            m_tiles.push_back(std::move(tile));
        }
    }

    void GeneralSettingsPage::UpdateBackgroundTiles()
    {
        try
        {
            auto& settings = Settings::Current();
            auto const selected = settings.WindowShadow();
            bool const dark = ActualTheme() == winrt::ElementTheme::Dark;
            auto const& candy = ::Eikon::Candy::For(dark);
            auto const accent = ::Eikon::Candy::HighContrast()
                ? winrt::Brush{ winrt::SolidColorBrush{ winrt::Windows::UI::Color{ 255, GetRValue(GetSysColor(COLOR_HIGHLIGHT)), GetGValue(GetSysColor(COLOR_HIGHLIGHT)), GetBValue(GetSysColor(COLOR_HIGHLIGHT)) } } }
                : winrt::Brush{ winrt::SolidColorBrush{ winrt::Windows::UI::Color{ candy.accent.a, candy.accent.r, candy.accent.g, candy.accent.b } } };
            winrt::SolidColorBrush const none{ winrt::Microsoft::UI::Colors::Transparent() };
            for (std::size_t i = 0; i < m_tiles.size(); ++i)
            {
                auto& tile = m_tiles[i];
                std::wstring uri = L"ms-appx:///Assets/Prefs/";
                uri += kTiles[i].image;
                uri += dark ? L"-dark.png" : L".png";
                tile.image.Source(winrt::BitmapImage{ winrt::Windows::Foundation::Uri{ uri } });
                bool const on = tile.value == selected;
                tile.ring.BorderBrush(on ? accent : winrt::Brush{ none });
                // The solid radio is drawn on the stored colour.
                tile.fill.Background(tile.value == L"solid" ? winrt::Brush{ winrt::SolidColorBrush{ SolidColor() } } : nullptr);
                bool const field = tile.value == L"solid" && on && m_solidBox;
                tile.caption.Visibility(field ? winrt::Visibility::Collapsed : winrt::Visibility::Visible);
                if (tile.value == L"solid" && m_solidBox)
                {
                    m_solidBox.Visibility(field ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);
                    if (m_solidBox.FocusState() == winrt::FocusState::Unfocused)
                    {
                        m_solidBox.Text(winrt::hstring{ settings.WindowSolidColor() });
                    }
                }
            }
        }
        catch (...)
        {
        }
    }

    void GeneralSettingsPage::CommitSolidColor()
    {
        if (!m_solidBox)
        {
            return;
        }
        auto& settings = Settings::Current();
        if (auto const color = Prefs::NormalizeSolidColor(std::wstring_view{ m_solidBox.Text() }))
        {
            settings.WindowSolidColor(*color);
        }
        m_solidBox.Text(winrt::hstring{ settings.WindowSolidColor() });
        UpdateBackgroundTiles();
    }

    void GeneralSettingsPage::Help_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::RoutedEventArgs const&)
    {
        if (auto button = sender.try_as<winrt::FrameworkElement>())
        {
            ::Eikon::SettingsUi::ShowHelp(button, ::Eikon::Strings::Get(winrt::unbox_value_or<winrt::hstring>(button.Tag(), L"")));
        }
    }

    void GeneralSettingsPage::SaveFolderButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        ::Eikon::SettingsUi::ChooseDefaultFolder(::Eikon::SettingsUi::WindowOf(*this), XamlRoot());
        LoadValues();
    }

    void GeneralSettingsPage::SaveFormat_Checked(winrt::Windows::Foundation::IInspectable const& sender, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        Settings::Current().SaveFormat(sender == SaveFormatPng() ? L"PNG" : L"Auto");
    }

    void GeneralSettingsPage::DownscaleToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (!m_loading)
        {
            Settings::Current().DownscaleOnSave(DownscaleToggle().IsOn());
        }
    }

    void GeneralSettingsPage::ScrollingMaxBox_KeyDown(winrt::Windows::Foundation::IInspectable const&, winrt::Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() == winrt::Windows::System::VirtualKey::Enter)
        {
            CommitScrollingMax();
            args.Handled(true);
        }
    }

    void GeneralSettingsPage::ScrollingMaxBox_LostFocus(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        CommitScrollingMax();
    }

    void GeneralSettingsPage::CommitScrollingMax()
    {
        try
        {
            auto const value = Prefs::ParseScrollingMax(std::wstring_view{ ScrollingMaxBox().Text() });
            Settings::Current().ScrollingMax(value);
            ScrollingMaxBox().Text(winrt::to_hstring(value));
        }
        catch (...)
        {
        }
    }

    void GeneralSettingsPage::ScrollingSpeedSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Primitives::RangeBaseValueChangedEventArgs const& args)
    {
        if (!m_loading)
        {
            Settings::Current().ScrollingSpeed(static_cast<std::int64_t>(std::lround(args.NewValue())));
        }
    }

    void GeneralSettingsPage::AutostartToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        [](winrt::weak_ref<GeneralSettingsPage> weak, bool enable) -> winrt::fire_and_forget {
            bool const enabled = co_await ::Eikon::SystemIntegration::SetLaunchAtLoginEnabled(enable);
            ::Eikon::AppController::Current().RefreshLaunchAtStartup();
            if (auto self = weak.get())
            {
                self->m_loading = true;
                self->AutostartToggle().IsOn(enabled);
                self->m_loading = false;
            }
        }(get_weak(), AutostartToggle().IsOn());
    }

    void GeneralSettingsPage::AfterGrab_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::RoutedEventArgs const&)
    {
        auto& settings = Settings::Current();
        Prefs::AfterGrab state{ settings.AfterGrabShow(), settings.AfterGrabCopy(), settings.AfterGrabSave() };
        bool const wasSave = state.save;
        if (sender == AfterShowCheck())
        {
            state = Prefs::SetShow(state, AfterShowCheck().IsChecked().Value());
        }
        else if (sender == AfterCopyCheck())
        {
            state = Prefs::SetCopy(state, AfterCopyCheck().IsChecked().Value());
        }
        else
        {
            state = Prefs::SetSave(state, AfterSaveCheck().IsChecked().Value());
        }
        m_loading = true;
        settings.AfterGrabShow(state.show);
        settings.AfterGrabCopy(state.copy);
        settings.AfterGrabSave(state.save);
        m_loading = false;
        LoadValues();
        // Checking Save with no folder opens the picker; cancelling it keeps Save checked.
        if (state.save && !wasSave && settings.DefaultFolder().empty())
        {
            ::Eikon::SettingsUi::ChooseDefaultFolder(::Eikon::SettingsUi::WindowOf(*this), XamlRoot());
            LoadValues();
        }
    }

    void GeneralSettingsPage::AreaCrop_Checked(winrt::Windows::Foundation::IInspectable const& sender, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        Settings::Current().AreaCaptureMode(sender == AreaThumbnailRadio() ? L"preview" : L"editor");
        UpdateEnabled();
    }

    void GeneralSettingsPage::ThumbnailClosingCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::SelectionChangedEventArgs const&)
    {
        if (m_loading || ThumbnailClosingCombo().SelectedIndex() < 0)
        {
            return;
        }
        Settings::Current().ThumbnailClosing(ThumbnailClosingCombo().SelectedIndex() == 0 ? L"auto" : L"manual");
    }
}
