#include "pch.h"
#include "AdvancedSettingsPage.xaml.h"
#if __has_include("AdvancedSettingsPage.g.cpp")
#include "AdvancedSettingsPage.g.cpp"
#endif

#include "Core/Preferences.h"
#include "Core/Settings.h"
#include "Platform/Secrets.h"
#include "SettingsHelpers.h"
#include "Strings.h"

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
}

namespace winrt::Eikon::implementation
{
    using ::Eikon::Core::Settings;
    namespace Prefs = ::Eikon::Core::Preferences;

    AdvancedSettingsPage::~AdvancedSettingsPage()
    {
        if (m_settingsToken)
        {
            Settings::Current().Changed.Remove(m_settingsToken);
        }
    }

    void AdvancedSettingsPage::Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (OcrLanguageCombo().Items().Size() == 0)
        {
            for (auto const& language : Prefs::OcrLanguages())
            {
                winrt::ComboBoxItem item;
                item.Content(winrt::box_value(winrt::hstring{ language.name }));
                item.Tag(winrt::box_value(winrt::hstring{ language.code }));
                OcrLanguageCombo().Items().Append(item);
            }
        }
        LoadValues();
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

    void AdvancedSettingsPage::Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (m_settingsToken)
        {
            Settings::Current().Changed.Remove(m_settingsToken);
            m_settingsToken = 0;
        }
    }

    void AdvancedSettingsPage::OnSettingChanged(std::wstring_view key)
    {
        if (!m_loading && (key == Settings::kSaveOnEsc || key == Settings::kCopyOnEsc || key == Settings::kAlwaysOnTop || key == Settings::kShowMenubarIcon))
        {
            LoadValues();
        }
    }

    void AdvancedSettingsPage::LoadValues()
    {
        auto& settings = Settings::Current();
        m_loading = true;
        try
        {
            auto const language = settings.PrimaryOCRLang();
            int index = -1;
            auto const& languages = Prefs::OcrLanguages();
            for (std::size_t i = 0; i < languages.size(); ++i)
            {
                if (languages[i].code == language)
                {
                    index = static_cast<int>(i);
                }
            }
            OcrLanguageCombo().SelectedIndex(index);
            OcrBreaksToggle().IsOn(settings.OcrRemoveBreaks());
            // Checked means 0.
            HideTrayToggle().IsOn(settings.ShowMenubarIcon() == 0);
            HideSplashToggle().IsOn(settings.ShowIntro() == 0);
            AlwaysOnTopToggle().IsOn(settings.AlwaysOnTop());
            ZoomDirectionToggle().IsOn(settings.AltZoomDirection());
            ReverseAutoscrollToggle().IsOn(settings.ScrollingReverseAutoscroll());
            PreferLargeToggle().IsOn(settings.PreferLargeWindow());
            EscCopyCheck().IsChecked(settings.CopyOnEsc());
            EscSaveCheck().IsChecked(settings.SaveOnEsc());
            auto const type = settings.NotificationType();
            ConfirmationCombo().SelectedIndex(type == L"system" ? 1 : type == L"none" ? 2 : 0);
            DeeplinksToggle().IsOn(::Eikon::Secrets::Current().AllowDeeplinks());
        }
        catch (...)
        {
        }
        m_loading = false;
    }

    void AdvancedSettingsPage::Help_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::RoutedEventArgs const&)
    {
        if (auto button = sender.try_as<winrt::FrameworkElement>())
        {
            ::Eikon::SettingsUi::ShowHelp(button, ::Eikon::Strings::Get(winrt::unbox_value_or<winrt::hstring>(button.Tag(), L"")));
        }
    }

    void AdvancedSettingsPage::OcrLanguageCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::SelectionChangedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        if (auto item = OcrLanguageCombo().SelectedItem().try_as<winrt::ComboBoxItem>())
        {
            Settings::Current().PrimaryOCRLang(std::wstring{ winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"en-US") });
        }
    }

    void AdvancedSettingsPage::Toggle_Toggled(winrt::Windows::Foundation::IInspectable const& sender, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        auto& settings = Settings::Current();
        auto toggle = sender.as<winrt::ToggleSwitch>();
        bool const on = toggle.IsOn();
        if (toggle == OcrBreaksToggle()) settings.OcrRemoveBreaks(on);
        else if (toggle == HideTrayToggle()) settings.ShowMenubarIcon(on ? 0 : 1);
        else if (toggle == HideSplashToggle()) settings.ShowIntro(on ? 0 : 1);
        // The editor applies alwaysOnTop as soon as it changes.
        else if (toggle == AlwaysOnTopToggle()) settings.AlwaysOnTop(on);
        else if (toggle == ZoomDirectionToggle()) settings.AltZoomDirection(on);
        else if (toggle == ReverseAutoscrollToggle()) settings.ScrollingReverseAutoscroll(on);
        else if (toggle == PreferLargeToggle()) settings.PreferLargeWindow(on);
        // Kept in the encrypted store so another app cannot switch it on.
        else if (toggle == DeeplinksToggle()) ::Eikon::Secrets::Current().AllowDeeplinks(on);
    }

    void AdvancedSettingsPage::EscCheck_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::RoutedEventArgs const&)
    {
        auto& settings = Settings::Current();
        if (sender == EscCopyCheck())
        {
            settings.CopyOnEsc(EscCopyCheck().IsChecked().Value());
            return;
        }
        bool const save = EscSaveCheck().IsChecked().Value();
        settings.SaveOnEsc(save);
        // Checking Save Image with no save folder opens the folder picker.
        if (save && settings.DefaultFolder().empty())
        {
            ::Eikon::SettingsUi::ChooseDefaultFolder(::Eikon::SettingsUi::WindowOf(*this), XamlRoot());
        }
    }

    void AdvancedSettingsPage::ConfirmationCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::SelectionChangedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        if (auto item = ConfirmationCombo().SelectedItem().try_as<winrt::ComboBoxItem>())
        {
            Settings::Current().NotificationType(std::wstring{ winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"custom") });
        }
    }
}
