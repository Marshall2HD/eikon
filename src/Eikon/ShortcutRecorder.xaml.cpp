#include "pch.h"
#include "ShortcutRecorder.xaml.h"
#if __has_include("ShortcutRecorder.g.cpp")
#include "ShortcutRecorder.g.cpp"
#endif

#include <winrt/Windows.UI.Core.h>

#include "AppController.h"
#include "SettingsHelpers.h"
#include "Strings.h"

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Input;
    using namespace Microsoft::UI::Xaml::Automation;
}

namespace
{
    bool IsModifierKey(std::uint32_t vk) noexcept
    {
        switch (vk)
        {
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_LWIN:
        case VK_RWIN:
            return true;
        default:
            return false;
        }
    }

    bool IsFunctionKey(std::uint32_t vk) noexcept
    {
        return vk >= VK_F1 && vk <= VK_F24;
    }

    // Keys that work as a global shortcut on their own: function keys and Print Screen (the key
    // the Hotkeys pane offers to take from the Snipping Tool).
    bool IsStandaloneKey(std::uint32_t vk) noexcept
    {
        return IsFunctionKey(vk) || vk == VK_SNAPSHOT;
    }

    // Shift alone is not a usable modifier for a global shortcut (it would swallow typing).
    bool HasRequiredModifier(Eikon::Modifiers modifiers) noexcept
    {
        return Eikon::HasFlag(modifiers, Eikon::Modifiers::Ctrl) || Eikon::HasFlag(modifiers, Eikon::Modifiers::Alt) ||
               Eikon::HasFlag(modifiers, Eikon::Modifiers::Win);
    }

    std::vector<std::wstring> ModifierNames(Eikon::Modifiers modifiers)
    {
        std::vector<std::wstring> names;
        // Windows order (and KeyShortcut::ToString's): Win, Ctrl, Alt, Shift.
        for (auto const flag : { Eikon::Modifiers::Win, Eikon::Modifiers::Ctrl, Eikon::Modifiers::Alt, Eikon::Modifiers::Shift })
        {
            if (Eikon::HasFlag(modifiers, flag))
            {
                names.push_back(Eikon::Strings::ModifierName(flag));
            }
        }
        return names;
    }
}

namespace winrt::Eikon::implementation
{
    ShortcutRecorder::~ShortcutRecorder()
    {
        ResumeGlobalHotKeys();
    }

    void ShortcutRecorder::InitializeComponent()
    {
        ShortcutRecorderT<ShortcutRecorder>::InitializeComponent();
        UpdateView();
        // The page names the recorder after construction; refresh the spoken "<label>: <shortcut>".
        Loaded([weak = get_weak()](auto&&, auto&&) {
            if (auto self = weak.get())
            {
                self->UpdateView();
            }
        });
    }

    void ShortcutRecorder::Shortcut(winrt::hstring const& value)
    {
        if (m_shortcut == value)
        {
            return;
        }
        m_shortcut = value;
        UpdateView();
    }

    winrt::event_token ShortcutRecorder::ShortcutChanged(
        winrt::Windows::Foundation::TypedEventHandler<winrt::Eikon::ShortcutRecorder, winrt::Windows::Foundation::IInspectable> const& handler)
    {
        return m_shortcutChanged.add(handler);
    }

    void ShortcutRecorder::ShortcutChanged(winrt::event_token const& token) noexcept
    {
        m_shortcutChanged.remove(token);
    }

    void ShortcutRecorder::EditButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            if (m_recording)
            {
                StopRecording();
            }
            else
            {
                StartRecording();
            }
        }
        catch (...)
        {
        }
    }

    void ShortcutRecorder::EditButton_LostFocus(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            if (m_recording)
            {
                StopRecording();
            }
        }
        catch (...)
        {
        }
    }

    void ShortcutRecorder::ClearButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            Commit(L"");
            EditButton().Focus(winrt::FocusState::Programmatic);
        }
        catch (...)
        {
        }
    }

    void ShortcutRecorder::Recorder_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            if (m_recording)
            {
                StopRecording();
            }
        }
        catch (...)
        {
        }
        ResumeGlobalHotKeys();
    }

    void ShortcutRecorder::Root_PreviewKeyDown(winrt::Windows::Foundation::IInspectable const&, winrt::KeyRoutedEventArgs const& args)
    {
        if (!m_recording)
        {
            return;
        }
        try
        {
            auto const vk = static_cast<std::uint32_t>(args.Key());
            auto const modifiers = ::Eikon::CurrentModifiers();

            // Plain Tab leaves the control (keyboard navigation keeps working).
            if (vk == VK_TAB && modifiers == ::Eikon::Modifiers::None)
            {
                StopRecording();
                return;
            }
            args.Handled(true);
            if (args.KeyStatus().WasKeyDown)
            {
                return; // auto-repeat
            }
            if (vk == 0 || vk == VK_PACKET || vk == VK_PROCESSKEY || IsModifierKey(vk))
            {
                m_pendingModifiers = modifiers;
                m_hint.clear();
                UpdateView();
                return;
            }
            if (modifiers == ::Eikon::Modifiers::None)
            {
                if (vk == VK_ESCAPE)
                {
                    StopRecording();
                    return;
                }
                if (vk == VK_BACK || vk == VK_DELETE)
                {
                    Commit(L"");
                    return;
                }
            }
            if (m_requireModifier && !HasRequiredModifier(modifiers) && !IsStandaloneKey(vk))
            {
                m_pendingModifiers = modifiers;
                m_hint = ::Eikon::Strings::Format(L"Settings_IncludeModifier", ::Eikon::Strings::ModifierName(::Eikon::Modifiers::Ctrl),
                                                  ::Eikon::Strings::ModifierName(::Eikon::Modifiers::Alt),
                                                  ::Eikon::Strings::ModifierName(::Eikon::Modifiers::Win));
                UpdateView();
                return;
            }
            ::Eikon::KeyShortcut shortcut;
            shortcut.modifiers = modifiers;
            shortcut.key = vk;
            Commit(shortcut.ToString());
        }
        catch (...)
        {
        }
    }

    void ShortcutRecorder::Root_PreviewKeyUp(winrt::Windows::Foundation::IInspectable const&, winrt::KeyRoutedEventArgs const& args)
    {
        if (!m_recording)
        {
            return;
        }
        try
        {
            args.Handled(true);
            auto const modifiers = ::Eikon::CurrentModifiers();
            // Windows consumes Print Screen's key-down (screen capture), so only its key-up arrives.
            if (static_cast<std::uint32_t>(args.Key()) == VK_SNAPSHOT)
            {
                ::Eikon::KeyShortcut shortcut;
                shortcut.modifiers = modifiers;
                shortcut.key = VK_SNAPSHOT;
                Commit(shortcut.ToString());
                return;
            }
            if (modifiers != m_pendingModifiers)
            {
                m_pendingModifiers = modifiers;
                UpdateView();
            }
        }
        catch (...)
        {
        }
    }

    void ShortcutRecorder::StartRecording()
    {
        m_recording = true;
        m_pendingModifiers = ::Eikon::Modifiers::None;
        m_hint.clear();
        SuspendGlobalHotKeys();
        EditButton().Focus(winrt::FocusState::Programmatic);
        UpdateView();
    }

    void ShortcutRecorder::StopRecording()
    {
        m_recording = false;
        m_pendingModifiers = ::Eikon::Modifiers::None;
        m_hint.clear();
        ResumeGlobalHotKeys();
        UpdateView();
    }

    void ShortcutRecorder::Commit(std::wstring const& shortcut)
    {
        // Restore the global hot keys first; the page then stores the new value, and the settings
        // change makes AppController::ApplyHotKeys register it.
        StopRecording();
        winrt::hstring const value{ shortcut };
        if (value == m_shortcut)
        {
            return;
        }
        m_shortcut = value;
        UpdateView();
        m_shortcutChanged(*this, nullptr);
    }

    void ShortcutRecorder::AddKeycap(std::wstring const& text, bool pending)
    {
        winrt::Border border;
        winrt::TextBlock block;
        block.Text(text);
        auto borderStyle = ::Eikon::SettingsUi::FindResource(pending ? L"SettingsKeycapPendingBorderStyle" : L"SettingsKeycapBorderStyle");
        auto textStyle = ::Eikon::SettingsUi::FindResource(pending ? L"SettingsKeycapPendingTextStyle" : L"SettingsKeycapTextStyle");
        if (borderStyle && textStyle)
        {
            border.Style(borderStyle.as<winrt::Style>());
            block.Style(textStyle.as<winrt::Style>());
        }
        else
        {
            border.Padding(winrt::Thickness{ 8, 3, 8, 4 });
            border.MinWidth(30);
            border.BorderThickness(winrt::Thickness{ 1, 1, 1, 1 });
            border.CornerRadius(winrt::CornerRadius{ 4, 4, 4, 4 });
            block.HorizontalAlignment(winrt::HorizontalAlignment::Center);
        }
        border.Child(block);
        KeysPanel().Children().Append(border);
    }

    void ShortcutRecorder::UpdateView()
    {
        try
        {
            KeysPanel().Children().Clear();
            std::wstring status;
            std::wstring spoken;

            if (m_recording)
            {
                for (auto const& name : ModifierNames(m_pendingModifiers))
                {
                    AddKeycap(name, true);
                }
                if (!m_hint.empty())
                {
                    status = m_hint;
                }
                else if (m_pendingModifiers == ::Eikon::Modifiers::None)
                {
                    status = ::Eikon::Strings::Get(L"Settings_PressShortcut");
                }
                spoken = ::Eikon::Strings::Get(L"Settings_Recording");
            }
            else
            {
                auto const parsed = ::Eikon::KeyShortcut::Parse(std::wstring_view{ m_shortcut });
                if (parsed && !parsed->IsEmpty())
                {
                    for (auto const& name : ModifierNames(parsed->modifiers))
                    {
                        AddKeycap(name, false);
                    }
                    AddKeycap(::Eikon::Strings::KeyText(parsed->key), false);
                    spoken = ::Eikon::Strings::ShortcutText(*parsed);
                }
                else
                {
                    status = ::Eikon::Strings::Get(L"Settings_NotSet");
                    spoken = status;
                }
            }

            StatusText().Text(status);
            StatusText().Visibility(status.empty() ? winrt::Visibility::Collapsed : winrt::Visibility::Visible);
            KeysPanel().Visibility(KeysPanel().Children().Size() == 0 ? winrt::Visibility::Collapsed : winrt::Visibility::Visible);
            EditGlyph().Glyph(m_recording ? L"" : L""); // Record / Edit
            ClearButton().Visibility(!m_recording && !m_shortcut.empty() ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);

            if (m_recording)
            {
                if (auto accent = ::Eikon::SettingsUi::FindResource(L"AccentFillColorDefaultBrush").try_as<winrt::Microsoft::UI::Xaml::Media::Brush>())
                {
                    EditButton().BorderBrush(accent);
                }
            }
            else
            {
                EditButton().ClearValue(winrt::Control::BorderBrushProperty());
            }

            // Screen readers: "<label set by the page>: Ctrl+Alt+V".
            std::wstring label;
            if (auto self = this->try_as<winrt::DependencyObject>())
            {
                label = winrt::AutomationProperties::GetName(self);
            }
            if (label.empty())
            {
                label = ::Eikon::Strings::Get(L"Settings_Shortcut");
            }
            winrt::AutomationProperties::SetName(EditButton(), winrt::hstring{ ::Eikon::Strings::Format(L"Settings_NameValue", label, spoken) });
            winrt::AutomationProperties::SetName(ClearButton(), winrt::hstring{ ::Eikon::Strings::Format(L"Settings_ClearNamed", label) });
        }
        catch (...)
        {
        }
    }

    void ShortcutRecorder::SuspendGlobalHotKeys()
    {
        if (m_hotKeysSuspended)
        {
            return;
        }
        m_hotKeysSuspended = true;
        // Unregisters the global hotkeys without touching the saved settings, so the recorder sees
        // every key combination.
        ::Eikon::AppController::Current().SuspendHotKeys(true);
    }

    void ShortcutRecorder::ResumeGlobalHotKeys() noexcept
    {
        if (!m_hotKeysSuspended)
        {
            return;
        }
        m_hotKeysSuspended = false;
        try
        {
            ::Eikon::AppController::Current().SuspendHotKeys(false);
        }
        catch (...)
        {
        }
    }
}
