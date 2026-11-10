#include "pch.h"
#include "HotkeysSettingsPage.xaml.h"
#if __has_include("HotkeysSettingsPage.g.cpp")
#include "HotkeysSettingsPage.g.cpp"
#endif

#include "AppController.h"
#include "Platform/SystemIntegration.h"
#include "SettingsHelpers.h"
#include "Strings.h"

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
}

namespace winrt::Eikon::implementation
{
    using ::Eikon::Core::HotKey;
    using ::Eikon::Core::Settings;

    HotkeysSettingsPage::~HotkeysSettingsPage()
    {
        if (m_settingsToken)
        {
            Settings::Current().Changed.Remove(m_settingsToken);
        }
    }

    void HotkeysSettingsPage::InitializeComponent()
    {
        HotkeysSettingsPageT<HotkeysSettingsPage>::InitializeComponent();
        m_recorders = {
            { HotKey::Fullscreen, FullscreenRecorder() },
            { HotKey::Area, AreaRecorder() },
            { HotKey::RepeatArea, RepeatAreaRecorder() },
            { HotKey::AnyWindow, AnyWindowRecorder() },
            { HotKey::Window, WindowRecorder() },
            { HotKey::Scrolling, ScrollingRecorder() },
            { HotKey::Reopen, ReopenRecorder() },
            { HotKey::Ocr, OcrRecorder() },
            { HotKey::LoadClipboard, ClipboardRecorder() },
            { HotKey::Library, LibraryRecorder() },
        };
        // The row labels name the recorders for screen readers ("Area screenshot: Ctrl+Shift+2").
        wchar_t const* const labels[] = {
            L"Prefs_HotkeyFullscreen/Header", L"Prefs_HotkeyArea/Header", L"Prefs_HotkeyRepeatArea/Header",
            L"Prefs_HotkeyAnyWindow/Header", L"Prefs_HotkeyWindow/Header", L"Prefs_HotkeyScrolling/Header",
            L"Prefs_HotkeyReopen/Header", L"Prefs_HotkeyOcr/Header", L"Prefs_HotkeyClipboard/Header",
            L"Prefs_HotkeyLibrary/Header",
        };
        for (std::size_t i = 0; i < m_recorders.size(); ++i)
        {
            winrt::Automation::AutomationProperties::SetName(m_recorders[i].second, winrt::hstring{ ::Eikon::Strings::Get(labels[i]) });
        }
        auto handler = [weak = get_weak()](winrt::Eikon::ShortcutRecorder const& sender, winrt::Windows::Foundation::IInspectable const&) {
            if (auto self = weak.get())
            {
                self->OnShortcutChanged(sender);
            }
        };
        for (auto const& [hotKey, recorder] : m_recorders)
        {
            // A global shortcut needs Ctrl, Alt or Win, except function keys and Print Screen.
            recorder.RequireModifier(true);
            recorder.ShortcutChanged(handler);
        }
    }

    void HotkeysSettingsPage::Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        // viewWillAppear: other code stops nagging about hotkeys.
        Settings::Current().EverVisitedHotkeys(true);
        LoadShortcuts();
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

    void HotkeysSettingsPage::Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (m_settingsToken)
        {
            Settings::Current().Changed.Remove(m_settingsToken);
            m_settingsToken = 0;
        }
    }

    void HotkeysSettingsPage::OnSettingChanged(std::wstring_view key)
    {
        if (!m_loading && key.starts_with(L"KeyboardShortcuts_"))
        {
            LoadShortcuts();
        }
    }

    void HotkeysSettingsPage::LoadShortcuts()
    {
        auto& settings = Settings::Current();
        for (auto const& [hotKey, recorder] : m_recorders)
        {
            if (!recorder.IsRecording())
            {
                recorder.Shortcut(winrt::hstring{ settings.Shortcut(hotKey).ToString() });
            }
        }
    }

    void HotkeysSettingsPage::OnShortcutChanged(winrt::Eikon::ShortcutRecorder const& sender)
    {
        auto it = std::find_if(m_recorders.begin(), m_recorders.end(), [&](auto const& entry) { return entry.second == sender; });
        if (it == m_recorders.end())
        {
            return;
        }
        HotKey const hotKey = it->first;
        auto& settings = Settings::Current();
        auto& app = ::Eikon::AppController::Current();
        auto const previous = settings.Shortcut(hotKey);
        auto const parsed = ::Eikon::KeyShortcut::Parse(std::wstring_view{ sender.Shortcut() }).value_or(::Eikon::KeyShortcut{});

        // Storing the shortcut makes the AppController re-register every hotkey; a refusal from
        // RegisterHotKey (taken by Windows or another app) is reported through
        // HotKeyRegistrationFailed.
        bool failed = false;
        auto const token = app.HotKeyRegistrationFailed.Add([&](HotKey which) {
            if (which == hotKey)
            {
                failed = true;
            }
        });
        m_loading = true;
        settings.Shortcut(hotKey, parsed);
        app.HotKeyRegistrationFailed.Remove(token);
        if (failed)
        {
            settings.Shortcut(hotKey, previous);
            sender.Shortcut(winrt::hstring{ previous.ToString() });
        }
        m_loading = false;
        if (failed)
        {
            ::Eikon::SettingsUi::ShowAlert(XamlRoot(), ::Eikon::Strings::Get(L"Prefs_HotkeyTaken"), {});
        }
    }

    void HotkeysSettingsPage::OpenWindowsSettingsButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        // clickOpenPreferences: the page with "Use the Print screen key to open screen capture".
        ::Eikon::SystemIntegration::OpenUri(L"ms-settings:easeofaccess-keyboard");
    }
}
