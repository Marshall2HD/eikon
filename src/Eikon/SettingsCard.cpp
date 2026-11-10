#include "pch.h"
#include "SettingsCard.h"
#if __has_include("SettingsCard.g.cpp")
#include "SettingsCard.g.cpp"
#endif

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
}

namespace winrt::Eikon::implementation
{
    winrt::DependencyProperty SettingsCard::s_headerProperty{ nullptr };
    winrt::DependencyProperty SettingsCard::s_descriptionProperty{ nullptr };
    winrt::DependencyProperty SettingsCard::s_glyphProperty{ nullptr };
    winrt::DependencyProperty SettingsCard::s_headerIconProperty{ nullptr };

    SettingsCard::SettingsCard()
    {
        EnsureProperties();
        // The template parts only exist once the template is applied; Loaded comes after that.
        // Both handlers are on this object's own events, so capturing `this` is safe.
        Loaded([this](winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&) { UpdateVisuals(); });
        IsEnabledChanged([this](winrt::Windows::Foundation::IInspectable const&, winrt::DependencyPropertyChangedEventArgs const&) { UpdateVisuals(); });
        SizeChanged([this](winrt::Windows::Foundation::IInspectable const&, winrt::SizeChangedEventArgs const&) { UpdateWrap(); });
    }

    // Registered lazily (not at static-initialization time, before the XAML runtime is up).
    void SettingsCard::EnsureProperties()
    {
        if (s_headerProperty)
        {
            return;
        }
        auto const owner = winrt::xaml_typename<winrt::Eikon::SettingsCard>();
        winrt::PropertyChangedCallback const callback{ &SettingsCard::OnVisualPropertyChanged };
        s_headerProperty = winrt::DependencyProperty::Register(
            L"Header", winrt::xaml_typename<winrt::Windows::Foundation::IInspectable>(), owner,
            winrt::PropertyMetadata{ nullptr, callback });
        s_descriptionProperty = winrt::DependencyProperty::Register(
            L"Description", winrt::xaml_typename<winrt::Windows::Foundation::IInspectable>(), owner,
            winrt::PropertyMetadata{ nullptr, callback });
        s_glyphProperty = winrt::DependencyProperty::Register(
            L"Glyph", winrt::xaml_typename<winrt::hstring>(), owner,
            winrt::PropertyMetadata{ winrt::box_value(winrt::hstring{}), callback });
        s_headerIconProperty = winrt::DependencyProperty::Register(
            L"HeaderIcon", winrt::xaml_typename<winrt::IconElement>(), owner,
            winrt::PropertyMetadata{ nullptr, callback });
    }

    winrt::DependencyProperty SettingsCard::HeaderProperty()
    {
        EnsureProperties();
        return s_headerProperty;
    }

    winrt::DependencyProperty SettingsCard::DescriptionProperty()
    {
        EnsureProperties();
        return s_descriptionProperty;
    }

    winrt::DependencyProperty SettingsCard::GlyphProperty()
    {
        EnsureProperties();
        return s_glyphProperty;
    }

    winrt::DependencyProperty SettingsCard::HeaderIconProperty()
    {
        EnsureProperties();
        return s_headerIconProperty;
    }

    winrt::hstring SettingsCard::Glyph()
    {
        return winrt::unbox_value_or<winrt::hstring>(GetValue(GlyphProperty()), winrt::hstring{});
    }

    winrt::IconElement SettingsCard::HeaderIcon()
    {
        auto value = GetValue(HeaderIconProperty());
        return value ? value.try_as<winrt::IconElement>() : nullptr;
    }

    void SettingsCard::OnVisualPropertyChanged(
        winrt::DependencyObject const& sender, winrt::DependencyPropertyChangedEventArgs const&)
    {
        if (auto card = sender.try_as<winrt::Eikon::SettingsCard>())
        {
            winrt::get_self<SettingsCard>(card)->UpdateVisuals();
        }
    }

    // Narrow cards put the control under the header and description instead of squeezing the text
    // column to nothing (Windows Settings / Community Toolkit SettingsCard behaviour).
    void SettingsCard::UpdateWrap()
    {
        try
        {
            auto presenter = GetTemplateChild(L"ContentPresenter").try_as<winrt::FrameworkElement>();
            auto content = Content().try_as<winrt::UIElement>();
            if (!presenter || !content)
            {
                return;
            }
            constexpr double kMinTextWidth = 200;
            auto const padding = Padding();
            auto const border = BorderThickness();
            const double icon = (HeaderIcon() || !Glyph().empty()) ? 26.0 + 16.0 : 0.0;   // icon host + column spacing
            const double available = ActualWidth() - padding.Left - padding.Right - border.Left - border.Right - icon - 16.0;
            const bool wrap = available - content.DesiredSize().Width < kMinTextWidth;
            if (wrap == m_wrapped && m_wrapApplied)
            {
                return;
            }
            m_wrapped = wrap;
            m_wrapApplied = true;
            winrt::Grid::SetRow(presenter, wrap ? 1 : 0);
            winrt::Grid::SetColumn(presenter, wrap ? 1 : 2);
            winrt::Grid::SetColumnSpan(presenter, wrap ? 2 : 1);
            presenter.Margin(wrap ? winrt::Thickness{ 0, 8, 0, 0 } : winrt::Thickness{});
            presenter.HorizontalAlignment(wrap ? winrt::HorizontalAlignment::Left : HorizontalContentAlignment());
        }
        catch (...)
        {
        }
    }

    void SettingsCard::UpdateVisuals()
    {
        try
        {
            auto const hasIcon = HeaderIcon() != nullptr;
            auto const hasGlyph = !Glyph().empty();
            auto const description = Description();
            bool hasDescription = description != nullptr;
            if (hasDescription)
            {
                if (auto text = description.try_as<winrt::Windows::Foundation::IReference<winrt::hstring>>())
                {
                    hasDescription = !text.Value().empty();
                }
            }

            auto visibility = [](bool visible) { return visible ? winrt::Visibility::Visible : winrt::Visibility::Collapsed; };
            if (auto part = GetTemplateChild(L"IconHost").try_as<winrt::UIElement>())
            {
                part.Visibility(visibility(hasIcon || hasGlyph));
                part.Opacity(IsEnabled() ? 1.0 : 0.4);
            }
            if (auto part = GetTemplateChild(L"GlyphElement").try_as<winrt::UIElement>())
            {
                part.Visibility(visibility(!hasIcon && hasGlyph));
            }
            if (auto part = GetTemplateChild(L"HeaderIconPresenter").try_as<winrt::UIElement>())
            {
                part.Visibility(visibility(hasIcon));
            }
            if (auto part = GetTemplateChild(L"DescriptionPresenter").try_as<winrt::UIElement>())
            {
                part.Visibility(visibility(hasDescription));
            }
            if (auto part = GetTemplateChild(L"TextHost").try_as<winrt::UIElement>())
            {
                part.Opacity(IsEnabled() ? 1.0 : 0.4);
            }
        }
        catch (...)
        {
        }
    }
}
