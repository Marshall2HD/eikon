#pragma once

#include "SettingsWindow.g.h"

namespace winrt::Eikon::implementation
{
    struct SettingsWindow : SettingsWindowT<SettingsWindow>
    {
        SettingsWindow() = default;

        void InitializeComponent();

        void ShowPage(winrt::hstring const& tag);

        void NavView_SelectionChanged(
            winrt::Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            winrt::Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);

    private:
        void Navigate(winrt::hstring const& tag);
        void UpdateNavItems();
        winrt::Microsoft::UI::Xaml::Controls::NavigationViewItem FindItem(winrt::hstring const& tag);

        winrt::hstring m_currentTag;
    };
}

namespace winrt::Eikon::factory_implementation
{
    struct SettingsWindow : SettingsWindowT<SettingsWindow, implementation::SettingsWindow>
    {
    };
}
