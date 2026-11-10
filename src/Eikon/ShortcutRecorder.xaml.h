#pragma once

#include "ShortcutRecorder.g.h"

#include "Core/KeyShortcut.h"

namespace winrt::Eikon::implementation
{
    struct ShortcutRecorder : ShortcutRecorderT<ShortcutRecorder>
    {
        ShortcutRecorder() = default;
        ~ShortcutRecorder();

        void InitializeComponent();

        winrt::hstring Shortcut() const { return m_shortcut; }
        void Shortcut(winrt::hstring const& value);
        bool RequireModifier() const noexcept { return m_requireModifier; }
        void RequireModifier(bool value) noexcept { m_requireModifier = value; }
        bool IsRecording() const noexcept { return m_recording; }

        winrt::event_token ShortcutChanged(
            winrt::Windows::Foundation::TypedEventHandler<winrt::Eikon::ShortcutRecorder, winrt::Windows::Foundation::IInspectable> const& handler);
        void ShortcutChanged(winrt::event_token const& token) noexcept;

        // XAML event handlers.
        void EditButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void EditButton_LostFocus(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ClearButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Root_PreviewKeyDown(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void Root_PreviewKeyUp(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void Recorder_Unloaded(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void StartRecording();
        void StopRecording();
        void Commit(std::wstring const& shortcut);
        void UpdateView();
        void AddKeycap(std::wstring const& text, bool pending);
        void SuspendGlobalHotKeys();
        void ResumeGlobalHotKeys() noexcept;

        winrt::hstring m_shortcut;
        bool m_requireModifier = false;
        bool m_recording = false;
        ::Eikon::Modifiers m_pendingModifiers = ::Eikon::Modifiers::None;
        std::wstring m_hint;

        // Global hotkeys temporarily unregistered while recording, so pressing one records it
        // instead of capturing.
        bool m_hotKeysSuspended = false;

        winrt::event<winrt::Windows::Foundation::TypedEventHandler<winrt::Eikon::ShortcutRecorder, winrt::Windows::Foundation::IInspectable>> m_shortcutChanged;
    };
}

namespace winrt::Eikon::factory_implementation
{
    struct ShortcutRecorder : ShortcutRecorderT<ShortcutRecorder, implementation::ShortcutRecorder>
    {
    };
}
