#pragma once

#include "SettingsCard.g.h"

namespace winrt::Eikon::implementation
{
    struct SettingsCard : SettingsCardT<SettingsCard>
    {
        SettingsCard();

        winrt::Windows::Foundation::IInspectable Header() { return GetValue(HeaderProperty()); }
        void Header(winrt::Windows::Foundation::IInspectable const& value) { SetValue(HeaderProperty(), value); }
        winrt::Windows::Foundation::IInspectable Description() { return GetValue(DescriptionProperty()); }
        void Description(winrt::Windows::Foundation::IInspectable const& value) { SetValue(DescriptionProperty(), value); }
        winrt::hstring Glyph();
        void Glyph(winrt::hstring const& value) { SetValue(GlyphProperty(), winrt::box_value(value)); }
        winrt::Microsoft::UI::Xaml::Controls::IconElement HeaderIcon();
        void HeaderIcon(winrt::Microsoft::UI::Xaml::Controls::IconElement const& value) { SetValue(HeaderIconProperty(), value); }

        static winrt::Microsoft::UI::Xaml::DependencyProperty HeaderProperty();
        static winrt::Microsoft::UI::Xaml::DependencyProperty DescriptionProperty();
        static winrt::Microsoft::UI::Xaml::DependencyProperty GlyphProperty();
        static winrt::Microsoft::UI::Xaml::DependencyProperty HeaderIconProperty();

    private:
        static void EnsureProperties();
        static void OnVisualPropertyChanged(
            winrt::Microsoft::UI::Xaml::DependencyObject const& sender,
            winrt::Microsoft::UI::Xaml::DependencyPropertyChangedEventArgs const& args);
        void UpdateVisuals();
        void UpdateWrap();
        bool m_wrapped = false;
        bool m_wrapApplied = false;

        static winrt::Microsoft::UI::Xaml::DependencyProperty s_headerProperty;
        static winrt::Microsoft::UI::Xaml::DependencyProperty s_descriptionProperty;
        static winrt::Microsoft::UI::Xaml::DependencyProperty s_glyphProperty;
        static winrt::Microsoft::UI::Xaml::DependencyProperty s_headerIconProperty;
    };
}

namespace winrt::Eikon::factory_implementation
{
    struct SettingsCard : SettingsCardT<SettingsCard, implementation::SettingsCard>
    {
    };
}
