#include "pch.h"
#include "UploadSettingsPage.xaml.h"
#if __has_include("UploadSettingsPage.g.cpp")
#include "UploadSettingsPage.g.cpp"
#endif

#include "AppController.h"
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

    namespace
    {
        void SelectByTag(winrt::ComboBox const& combo, std::wstring_view tag, int fallback)
        {
            int index = fallback;
            for (std::uint32_t i = 0; i < combo.Items().Size(); ++i)
            {
                if (auto item = combo.Items().GetAt(i).try_as<winrt::ComboBoxItem>())
                {
                    if (winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"") == tag)
                    {
                        index = static_cast<int>(i);
                    }
                }
            }
            combo.SelectedIndex(index);
        }

        std::wstring SelectedTag(winrt::ComboBox const& combo)
        {
            if (auto item = combo.SelectedItem().try_as<winrt::ComboBoxItem>())
            {
                return std::wstring{ winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"") };
            }
            return {};
        }
    }

    void UploadSettingsPage::Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        LoadValues();
    }

    void UploadSettingsPage::Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
    }

    void UploadSettingsPage::LoadValues()
    {
        auto& settings = Settings::Current();
        auto& secrets = ::Eikon::Secrets::Current();
        m_loading = true;
        try
        {
            // "cloud" is not offered; it shows as disabled without being rewritten.
            SelectByTag(UploadModeCombo(), settings.UploadMode(), 0);
            AccessBox().Text(winrt::hstring{ secrets.S3Access() });
            SecretBox().Password(winrt::hstring{ secrets.S3Secret() });
            BucketBox().Text(winrt::hstring{ settings.UploadS3Bucket() });
            auto const region = settings.UploadS3Region();
            auto const endpoint = settings.UploadS3Endpoint();
            bool const other = !endpoint.empty() || region == L"auto";
            AmazonRadio().IsChecked(!other);
            OtherRadio().IsChecked(other);
            RegionBox().Text(winrt::hstring{ region });
            EndpointBox().Text(winrt::hstring{ endpoint });
            PrefixBox().Text(winrt::hstring{ settings.UploadS3Prefix() });
            SelectByTag(SharingCombo(), settings.UploadS3SharingMode(), 0);
            ExpirationSlider().Value(Prefs::ExpirationIndex(settings.UploadS3PresignExpiration()));
            ExpirationLabel().Text(winrt::hstring{ ::Eikon::Strings::Get(Prefs::Expirations()[Prefs::ExpirationIndex(settings.UploadS3PresignExpiration())].labelKey) });
            UpdateVisibility();
        }
        catch (...)
        {
        }
        m_loading = false;
    }

    // viewDisabled for none, boxS3 for s3; the Service radios pick Region or Endpoint;
    // the sharing mode shows the expiration slider or the public URL.
    void UploadSettingsPage::UpdateVisibility()
    {
        auto visible = [](bool on) { return on ? winrt::Visibility::Visible : winrt::Visibility::Collapsed; };
        bool const s3 = SelectedTag(UploadModeCombo()) == L"s3";
        DisabledText().Visibility(visible(!s3));
        S3Panel().Visibility(visible(s3));
        bool const other = OtherRadio().IsChecked().Value();
        RegionCard().Visibility(visible(!other));
        EndpointCard().Visibility(visible(other));
        auto const sharing = SelectedTag(SharingCombo());
        ExpirationCard().Visibility(visible(sharing == L"presign"));
        PublicUrlCard().Visibility(visible(sharing == L"public"));
    }

    void UploadSettingsPage::UploadModeCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::SelectionChangedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        Settings::Current().UploadMode(SelectedTag(UploadModeCombo()));
        UpdateVisibility();
    }

    void UploadSettingsPage::Field_KeyDown(winrt::Windows::Foundation::IInspectable const& sender, winrt::Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() == winrt::Windows::System::VirtualKey::Enter)
        {
            Commit(sender);
        }
    }

    void UploadSettingsPage::Field_LostFocus(winrt::Windows::Foundation::IInspectable const& sender, winrt::RoutedEventArgs const&)
    {
        Commit(sender);
    }

    // The editS3* actions: keys go to the encrypted store, bucket/region/prefix are trimmed, the
    // endpoint and public URL are normalised and written back to their fields.
    void UploadSettingsPage::Commit(winrt::Windows::Foundation::IInspectable const& field)
    {
        if (m_loading)
        {
            return;
        }
        auto& settings = Settings::Current();
        auto& secrets = ::Eikon::Secrets::Current();
        try
        {
            if (field == SecretBox())
            {
                secrets.S3Secret(std::wstring{ SecretBox().Password() });
                return;
            }
            auto box = field.try_as<winrt::TextBox>();
            if (!box)
            {
                return;
            }
            std::wstring_view const text{ box.Text() };
            std::wstring value;
            if (box == AccessBox())
            {
                value = Prefs::TrimField(text);
                secrets.S3Access(value);
            }
            else if (box == BucketBox())
            {
                value = Prefs::TrimField(text);
                settings.UploadS3Bucket(value);
            }
            else if (box == RegionBox())
            {
                value = Prefs::TrimField(text);
                settings.UploadS3Region(value);
            }
            else if (box == EndpointBox())
            {
                value = Prefs::NormalizeEndpoint(text);
                settings.UploadS3Endpoint(value);
            }
            else if (box == PrefixBox())
            {
                value = Prefs::TrimField(text);
                settings.UploadS3Prefix(value);
            }
            else if (box == PublicUrlBox())
            {
                value = Prefs::NormalizePublicUrl(text);
                settings.UploadS3PublicUrl(value);
            }
            else
            {
                return;
            }
            if (std::wstring_view{ box.Text() } != value)
            {
                box.Text(winrt::hstring{ value });
            }
        }
        catch (...)
        {
        }
    }

    // pickS3RegionOrEndpoint: Amazon clears the endpoint and the region; Other sets region "auto".
    void UploadSettingsPage::Service_Checked(winrt::Windows::Foundation::IInspectable const& sender, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        auto& settings = Settings::Current();
        if (sender == AmazonRadio())
        {
            settings.UploadS3Endpoint(L"");
            settings.UploadS3Region(L"");
        }
        else
        {
            settings.UploadS3Region(L"auto");
        }
        m_loading = true;
        RegionBox().Text(winrt::hstring{ settings.UploadS3Region() });
        EndpointBox().Text(winrt::hstring{ settings.UploadS3Endpoint() });
        m_loading = false;
        UpdateVisibility();
    }

    void UploadSettingsPage::SharingCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::SelectionChangedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        Settings::Current().UploadS3SharingMode(SelectedTag(SharingCombo()));
        UpdateVisibility();
    }

    // Snap to an index, show its label, store its seconds.
    void UploadSettingsPage::ExpirationSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Primitives::RangeBaseValueChangedEventArgs const& args)
    {
        auto const index = Prefs::SnapExpiration(args.NewValue());
        auto const& entry = Prefs::Expirations()[static_cast<std::size_t>(index)];
        ExpirationLabel().Text(winrt::hstring{ ::Eikon::Strings::Get(entry.labelKey) });
        if (!m_loading)
        {
            Settings::Current().UploadS3PresignExpiration(entry.seconds);
        }
    }

    void UploadSettingsPage::TestButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (!m_testing)
        {
            RunTest();
        }
    }

    // clickS3Test: spinner + "Checking connection…", then "Good to go!" or "Test Failed" with the
    // "S3 Connection Test Failed" alert. The request itself belongs to the uploader.
    winrt::fire_and_forget UploadSettingsPage::RunTest()
    {
        auto weak = get_weak();
        // Commit whatever field still has focus first.
        for (auto const& field : { winrt::Windows::Foundation::IInspectable{ AccessBox() }, winrt::Windows::Foundation::IInspectable{ SecretBox() },
                                   winrt::Windows::Foundation::IInspectable{ BucketBox() }, winrt::Windows::Foundation::IInspectable{ RegionBox() },
                                   winrt::Windows::Foundation::IInspectable{ EndpointBox() }, winrt::Windows::Foundation::IInspectable{ PrefixBox() } })
        {
            Commit(field);
        }
        auto& settings = Settings::Current();
        auto const root = XamlRoot();
        if (Prefs::HasUppercase(settings.UploadS3Bucket()))
        {
            co_await ::Eikon::SettingsUi::ShowAlert(root, ::Eikon::Strings::Get(L"Prefs_S3UppercaseTitle"), ::Eikon::Strings::Get(L"Prefs_S3UppercaseMessage"));
        }
        auto self = weak.get();
        if (!self)
        {
            co_return;
        }
        self->m_testing = true;
        self->TestButton().IsEnabled(false);
        self->TestRing().Visibility(winrt::Visibility::Visible);
        self->TestRing().IsActive(true);
        self->TestStatus().Text(winrt::hstring{ ::Eikon::Strings::Get(L"Prefs_S3Checking") });

        std::wstring error;
        if (settings.UploadS3Region().empty() && settings.UploadS3Endpoint().empty())
        {
            error = ::Eikon::Strings::Get(L"Prefs_S3RegionRequired");
        }
        else
        {
            try
            {
                error = std::wstring{ co_await ::Eikon::AppController::Current().TestS3Connection() };
            }
            catch (winrt::hresult_error const& e)
            {
                error = std::wstring{ e.message() };
            }
        }

        self = weak.get();
        if (!self)
        {
            co_return;
        }
        self->TestRing().IsActive(false);
        self->TestRing().Visibility(winrt::Visibility::Collapsed);
        self->TestButton().IsEnabled(true);
        self->TestStatus().Text(winrt::hstring{ ::Eikon::Strings::Get(error.empty() ? L"Prefs_S3Good" : L"Prefs_S3Failed") });
        self->m_testing = false;
        if (!error.empty())
        {
            co_await ::Eikon::SettingsUi::ShowAlert(self->XamlRoot(), ::Eikon::Strings::Get(L"Prefs_S3FailedTitle"), error);
        }
    }
}
