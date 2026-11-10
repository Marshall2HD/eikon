#pragma once

#include <optional>
#include <string>

// Small helpers shared by the Settings window pages and the Library (UI thread only).
namespace Eikon::SettingsUi
{
    // Applies the ContentDialog default style (needed for dialogs created in code in WinUI 3).
    void PrepareDialog(winrt::Microsoft::UI::Xaml::Controls::ContentDialog const& dialog,
                       winrt::Microsoft::UI::Xaml::XamlRoot const& root);

    // Looks a resource up in the application resources (merged dictionaries included); nullptr when
    // it is missing.
    winrt::Windows::Foundation::IInspectable FindResource(wchar_t const* key);

    // A transient flyout below
    // the "?" button with the text centred, 20 px padding and 4 px line spacing.
    void ShowHelp(winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor, std::wstring const& text);

    // An alert with one "OK" button.
    winrt::Windows::Foundation::IAsyncAction ShowAlert(winrt::Microsoft::UI::Xaml::XamlRoot root, std::wstring title, std::wstring message);

    // The HWND of the window that hosts `element` (nullptr when unknown).
    HWND WindowOf(winrt::Microsoft::UI::Xaml::UIElement const& element);

    // The folder picker, title "Choose default folder", button "Select Folder". On
    // success it stores defaultFolder and tells the Library; returns the folder, or nullopt
    // when cancelled. While the Library is indexing it shows "Can't change folder while indexing"
    // instead (`root` may be null: no alert then).
    std::optional<std::wstring> ChooseDefaultFolder(HWND owner, winrt::Microsoft::UI::Xaml::XamlRoot const& root);
}
