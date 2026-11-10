#pragma once

#include "AdvancedSettingsPage.g.h"

#include <cstdint>
#include <string_view>

namespace winrt::Eikon::implementation
{
    // Settings > Advanced.
    struct AdvancedSettingsPage : AdvancedSettingsPageT<AdvancedSettingsPage>
    {
        AdvancedSettingsPage() = default;
        ~AdvancedSettingsPage();

        void Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Help_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OcrLanguageCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void Toggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void EscCheck_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ConfirmationCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);

    private:
        void LoadValues();
        void OnSettingChanged(std::wstring_view key);

        bool m_loading = false;
        std::uint64_t m_settingsToken = 0;
    };
}

namespace winrt::Eikon::factory_implementation
{
    struct AdvancedSettingsPage : AdvancedSettingsPageT<AdvancedSettingsPage, implementation::AdvancedSettingsPage>
    {
    };
}
