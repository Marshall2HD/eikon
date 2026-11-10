#include "pch.h"
#include "SettingsHelpers.h"

#include "Core/Settings.h"
#include "Platform/LibraryIndex.h"
#include "Strings.h"

#include <shobjidl_core.h>
#include <winrt/Microsoft.UI.Content.h>

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Controls::Primitives;
}

namespace Eikon::SettingsUi
{
    void PrepareDialog(winrt::ContentDialog const& dialog, winrt::XamlRoot const& root)
    {
        dialog.XamlRoot(root);
        if (auto style = FindResource(L"DefaultContentDialogStyle").try_as<winrt::Style>())
        {
            dialog.Style(style);
        }
    }

    winrt::Windows::Foundation::IInspectable FindResource(wchar_t const* key)
    {
        try
        {
            // Lookup also searches merged dictionaries; it throws when the key is missing.
            return winrt::Application::Current().Resources().Lookup(winrt::box_value(winrt::hstring{ key }));
        }
        catch (...)
        {
        }
        return nullptr;
    }

    void ShowHelp(winrt::FrameworkElement const& anchor, std::wstring const& text)
    {
        try
        {
            winrt::TextBlock block;
            block.Text(text);
            block.TextWrapping(winrt::TextWrapping::Wrap);
            block.TextAlignment(winrt::TextAlignment::Center);
            block.MaxWidth(300);
            // Line spacing 4 on the 13 pt system font.
            block.LineHeight(20);
            block.LineStackingStrategy(winrt::LineStackingStrategy::BlockLineHeight);
            winrt::Flyout flyout;
            flyout.Content(block);
            flyout.Placement(winrt::FlyoutPlacementMode::Bottom);
            winrt::Style presenter{ winrt::xaml_typename<winrt::FlyoutPresenter>() };
            presenter.Setters().Append(winrt::Setter{ winrt::Control::PaddingProperty(), winrt::box_value(winrt::Thickness{ 20, 20, 20, 20 }) });
            flyout.FlyoutPresenterStyle(presenter);
            flyout.ShowAt(anchor);
        }
        catch (...)
        {
        }
    }

    winrt::Windows::Foundation::IAsyncAction ShowAlert(winrt::XamlRoot root, std::wstring title, std::wstring message)
    {
        if (!root)
        {
            co_return;
        }
        try
        {
            winrt::ContentDialog dialog;
            PrepareDialog(dialog, root);
            dialog.Title(winrt::box_value(winrt::hstring{ title }));
            if (!message.empty())
            {
                winrt::TextBlock body;
                body.Text(message);
                body.TextWrapping(winrt::TextWrapping::Wrap);
                dialog.Content(body);
            }
            dialog.CloseButtonText(winrt::hstring{ Strings::Get(L"Prefs_Ok") });
            dialog.DefaultButton(winrt::ContentDialogButton::Close);
            co_await dialog.ShowAsync();
        }
        catch (...)
        {
        }
    }

    HWND WindowOf(winrt::UIElement const& element)
    {
        try
        {
            if (auto root = element.XamlRoot())
            {
                auto const id = root.ContentIslandEnvironment().AppWindowId();
                return winrt::Microsoft::UI::GetWindowFromWindowId(id);
            }
        }
        catch (...)
        {
        }
        return nullptr;
    }

    std::optional<std::wstring> ChooseDefaultFolder(HWND owner, winrt::XamlRoot const& root)
    {
        if (LibraryIndex::Current().IsIndexing())
        {
            ShowAlert(root, Strings::Get(L"Prefs_IndexingTitle"), Strings::Get(L"Prefs_IndexingMessage"));
            return std::nullopt;
        }
        try
        {
            auto dialog = winrt::create_instance<IFileOpenDialog>(CLSID_FileOpenDialog);
            FILEOPENDIALOGOPTIONS options{};
            winrt::check_hresult(dialog->GetOptions(&options));
            winrt::check_hresult(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST));
            auto const title = Strings::Get(L"Prefs_ChooseFolderTitle");
            auto const button = Strings::Get(L"Prefs_SelectFolder");
            dialog->SetTitle(title.c_str());
            dialog->SetOkButtonLabel(button.c_str());
            auto const current = Core::Settings::Current().DefaultFolder();
            if (!current.empty())
            {
                winrt::com_ptr<IShellItem> folder;
                if (SUCCEEDED(SHCreateItemFromParsingName(current.c_str(), nullptr, IID_PPV_ARGS(folder.put()))))
                {
                    dialog->SetFolder(folder.get());
                }
            }
            if (dialog->Show(owner) != S_OK)
            {
                return std::nullopt;
            }
            winrt::com_ptr<IShellItem> result;
            winrt::check_hresult(dialog->GetResult(result.put()));
            wil::unique_cotaskmem_string path;
            winrt::check_hresult(result->GetDisplayName(SIGDN_FILESYSPATH, path.put()));
            std::wstring folder{ path.get() };
            // Setting defaultFolder makes the Library reload (LibraryIndex listens to it).
            Core::Settings::Current().DefaultFolder(folder);
            return folder;
        }
        catch (...)
        {
            OutputDebugStringW(L"Eikon: the folder picker failed\n");
        }
        return std::nullopt;
    }
}
