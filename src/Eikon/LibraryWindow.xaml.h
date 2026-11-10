#pragma once

#include "LibraryWindow.g.h"

#include "Core/LibraryLayout.h"
#include "Platform/LibraryIndex.h"
#include "Platform/QuickLook.h"

#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace winrt::Eikon::implementation
{
    // The Library window: the gallery, its items and the search.
    struct LibraryWindow : LibraryWindowT<LibraryWindow>
    {
        LibraryWindow() = default;
        ~LibraryWindow();

        void InitializeComponent();

        // The status item / hotkey / eikon://lib: shows the window, or hides it when it is open.
        void Toggle();
        // eikon://lib and lib/search: shows it, with the search field set when `search` is given.
        void Open(std::optional<std::wstring> const& search);
        void HideLibrary();
        bool IsOpen() const;

        // XAML handlers.
        void Root_PreviewKeyDown(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void MoreButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Tool_PointerEntered(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void Tool_PointerExited(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void ProgressAction_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SearchBox_TextChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
        void MessageAction_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void GalleryScroll_ViewChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const&);
        void GalleryScroll_SizeChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void GalleryScroll_FocusChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void GalleryScroll_CharacterReceived(winrt::Microsoft::UI::Xaml::UIElement const&, winrt::Microsoft::UI::Xaml::Input::CharacterReceivedRoutedEventArgs const&);
        void GalleryCanvas_PointerPressed(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void SettingsButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void LayoutButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SizeButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        using ItemPtr = std::shared_ptr<::Eikon::LibraryIndex::Item const>;

        struct ItemView
        {
            winrt::Microsoft::UI::Xaml::Controls::Grid root{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border tile{ nullptr };       // basecolor fill + 0.5 pt border
            winrt::Microsoft::UI::Xaml::Controls::Image image{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border selection{ nullptr };  // accent fill + 2 pt ring
            winrt::Microsoft::UI::Xaml::Controls::Border gap{ nullptr };        // lgap, inset 2
            winrt::Microsoft::UI::Xaml::Controls::Image fileIcon{ nullptr };    // list mode
            winrt::Microsoft::UI::Xaml::Controls::TextBlock name{ nullptr };    // list mode
            std::size_t index = SIZE_MAX;
            std::wstring path;
        };

        struct Thumbnail
        {
            winrt::Microsoft::UI::Xaml::Media::ImageSource source{ nullptr };
            int width = 0;
            int height = 0;
        };

        // Window
        void SetUpWindow();
        void PlaceWindow();
        void BuildToolbar();
        void RunTool(std::wstring const& id);
        void ShowTooltip(std::wstring const& id);
        void OnIndexChanged();
        void UpdateState();
        void UpdateStatus();
        void UpdateLayoutButtons();

        // Search
        winrt::fire_and_forget RunSearch(std::wstring query);
        void SetView(std::vector<ItemPtr> items);

        // Gallery
        void Relayout();
        void Realize();
        ItemView& AcquireView();
        void BindView(ItemView& view, std::size_t index);
        void UpdateSelectionVisual(ItemView& view);
        void UpdateAllSelectionVisuals();
        void RequestThumbnail(ItemView& view);
        winrt::fire_and_forget LoadThumbnail(std::wstring path, int width, int height);
        std::optional<std::size_t> HitTest(winrt::Windows::Foundation::Point point) const;
        void Select(std::size_t index, bool extend, bool toggle);
        void SelectOnly(std::size_t index);
        void ScrollIntoView(std::size_t index);
        void SelectionChanged();
        std::vector<std::size_t> SelectedIndices() const;
        std::optional<std::size_t> FirstSelected() const;

        // Actions
        void ShowContextMenu(winrt::Windows::Foundation::Point point);
        void RunAction(std::wstring const& action, std::optional<std::size_t> clicked = std::nullopt);
        bool CheckExists(ItemPtr const& item);
        void ToggleQuickLook();
        void UpdateQuickLook();
        void DeleteSelected();

        // State
        winrt::Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };
        HWND m_hwnd = nullptr;
        bool m_open = false;
        bool m_modal = false;   // a picker or dialog is up: don't hide on deactivate
        std::uint64_t m_indexToken = 0;
        std::uint64_t m_settingsToken = 0;
        std::vector<ItemPtr> m_view;   // what the gallery shows (all items or the search result)
        bool m_searching = false;
        std::wstring m_query;
        std::uint64_t m_searchGeneration = 0;

        ::Eikon::Core::LibraryLayout::Kind m_kind = ::Eikon::Core::LibraryLayout::Kind::Pairs;
        ::Eikon::Core::LibraryLayout::Result m_layout;
        ::Eikon::Core::LibraryLayout::Flow m_flow;
        double m_layoutWidth = 0;
        std::vector<std::unique_ptr<ItemView>> m_views;          // all created views
        std::unordered_map<std::size_t, ItemView*> m_realized;   // index -> view
        std::set<std::size_t> m_selection;
        std::optional<std::size_t> m_anchor;
        std::optional<std::size_t> m_cursor;
        bool m_galleryFocused = false;
        std::chrono::steady_clock::time_point m_lastDown{};
        std::optional<std::size_t> m_lastDownIndex;

        // Thumbnails: an LRU of min(libLimit, 50) entries keyed by path.
        std::list<std::wstring> m_lru;
        std::unordered_map<std::wstring, Thumbnail> m_thumbnails;
        std::set<std::wstring> m_loading;

        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_tooltipTimer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_statusTimer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_searchTimer{ nullptr };
        std::wstring m_hoverTool;

        ::Eikon::QuickLookPanel m_quickLook;
        std::wstring m_quickLookPath;
    };
}

namespace winrt::Eikon::factory_implementation
{
    struct LibraryWindow : LibraryWindowT<LibraryWindow, implementation::LibraryWindow>
    {
    };
}
