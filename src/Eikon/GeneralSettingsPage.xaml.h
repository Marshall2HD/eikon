#pragma once

#include "GeneralSettingsPage.g.h"

#include "Core/Event.h"

#include <cstdint>
#include <string_view>

namespace winrt::Eikon::implementation
{
    // Settings > General.
    struct GeneralSettingsPage : GeneralSettingsPageT<GeneralSettingsPage>
    {
        GeneralSettingsPage() = default;
        ~GeneralSettingsPage();

        void Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Help_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SaveFolderButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SaveFormat_Checked(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void DownscaleToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ScrollingMaxBox_KeyDown(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void ScrollingMaxBox_LostFocus(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ScrollingSpeedSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void AutostartToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void AfterGrab_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void AreaCrop_Checked(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ThumbnailClosingCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);

    private:
        void LoadValues();
        void BuildBackgroundTiles();
        void UpdateBackgroundTiles();
        void UpdateEnabled();
        void CommitScrollingMax();
        void CommitSolidColor();
        winrt::fire_and_forget LoadAutostart();
        void OnSettingChanged(std::wstring_view key);

        struct Tile
        {
            std::wstring value;
            winrt::Microsoft::UI::Xaml::Controls::Image image{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border fill{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border ring{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::TextBlock caption{ nullptr };
        };
        std::vector<Tile> m_tiles;
        winrt::Microsoft::UI::Xaml::Controls::TextBox m_solidBox{ nullptr };
        bool m_loading = false;
        std::uint64_t m_settingsToken = 0;
    };
}

namespace winrt::Eikon::factory_implementation
{
    struct GeneralSettingsPage : GeneralSettingsPageT<GeneralSettingsPage, implementation::GeneralSettingsPage>
    {
    };
}
