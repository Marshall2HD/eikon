#pragma once

#include "EditorWindow.g.h"

#include "Core/Document.h"
#include "Core/Snake.h"
#include "Editor/CtrlHelper.h"
#include "Editor/ShotView.h"
#include "Editor/TextEditOverlay.h"

#include <memory>
#include <string>
#include <vector>

namespace winrt::Eikon::implementation
{
    // The editor window: the unified toolbar in a custom
    // title bar, the canvas (Editor::ShotView), the helper popovers, the options bar, the intro panel
    // and the editor's keyboard map.
    struct EditorWindow : EditorWindowT<EditorWindow>
    {
        EditorWindow() = default;

        void InitializeComponent();

        bool IsShown() const;
        void ShowEditor();
        void HideEditor();
        void Destroy();

        // A new capture: loads `document`, sizes the window (unless
        // `keepSize`), centres it on the captured screen and shows it unless
        // `present` is false (a capture that goes to the thumbnail or nowhere still loads).
        void ShowDocument(std::shared_ptr<::Eikon::Core::Document> document, bool keepSize = false, bool present = true);

        ::Eikon::Editor::ShotView* View() noexcept { return m_view.get(); }

    private:
        using Helper = ::Eikon::Editor::CtrlHelper;
        using Modifiers = ::Eikon::Modifiers;

        // A segmented control: buttons with value codes.
        struct Segments
        {
            std::wstring property;                    // options-bar property ("lineType", ...)
            std::vector<std::wstring> codes;          // value per segment
            std::vector<winrt::Microsoft::UI::Xaml::Controls::Button> buttons;
            winrt::Microsoft::UI::Xaml::Controls::Border root{ nullptr };
        };

        // ---- Setup ----
        void SetUpWindow();
        void SetUpStrings();
        void SetUpToolbar();
        void SetUpInfo();
        void SetUpHelpers();
        void SetUpOptionsBar();
        void SetUpResize();
        void SetUpMenu();
        void SetUpIntro();
        void ApplyThemeImages();
        void ApplyAlwaysOnTop();
        void SizeAndPlaceFor(::Eikon::Core::Bitmap const& image);
        void Present();

        // ---- Toolbar ----
        winrt::Microsoft::UI::Xaml::Controls::Button IconButton(std::wstring_view asset, double width, double height,
                                                                std::wstring const& tooltip);
        void AttachTooltip(winrt::Microsoft::UI::Xaml::FrameworkElement const& element, std::wstring const& tooltip);
        void BuildExportButtons();
        void LayoutTools();
        void UpdateInfoFit();
        void UpdateToolTint();
        void UpdateToolbarMode();
        void UpdateDragRegions();
        void ShowMoreMenu();

        // ---- Info bar and state ----
        void OnViewStateChanged();
        void UpdateInfo();
        void UpdateColorInfo();
        void UpdateIntro();
        void ApplyIntroTheme();
        void PlayIntroRise();

        // ---- Options bar ----
        Segments& AddSegments(std::wstring const& property, std::vector<std::wstring> codes, std::vector<std::wstring> images,
                              std::vector<std::wstring> labels, double width);
        std::wstring OptionsClass() const;
        std::wstring OptionsProperty(std::wstring_view key) const;
        void SetOptionsProperty(std::wstring_view key, std::wstring_view value);
        void UpdateOptionsBar();

        // ---- Helpers ----
        bool AnyHelperOpen() const;
        bool CloseHelpers();
        void ToggleHelper(Helper& helper);
        void ShowTooltip(winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor, std::wstring const& text);
        void HideTooltip();
        void FillResizeFields();
        void UpdateResizePresets();
        void ResizeImage();

        // ---- Actions ----
        void SelectTool(std::wstring_view id);
        void CopyImage();
        void SaveImage();
        void SaveImageAs();
        void UploadImage();
        void PinImage();
        void PrintImage();
        void RecognizeText();
        void ImprintSelectionSize();
        void PasteImage();
        void Escape();
        winrt::fire_and_forget QuitAction();
        winrt::fire_and_forget Alert(std::wstring title, std::wstring message);
        void ToggleFullScreen();
        void SetSnappingMode(std::int64_t mode);
        bool HandleKeyDown(std::uint32_t key, Modifiers modifiers, bool repeat);
        bool GlobalKey(std::uint32_t key, Modifiers modifiers);
        void Notify(std::wstring const& title, std::wstring const& message);

        std::unique_ptr<::Eikon::Editor::ShotView> m_view;
        std::unique_ptr<::Eikon::Editor::TextEditOverlay> m_textOverlay;
        bool m_allowClose = false;
        bool m_hasCapture = false;
        std::uint64_t m_settingsToken = 0;

        // Toolbar.
        std::vector<winrt::Microsoft::UI::Xaml::Controls::Button> m_toolButtons;   // one per kEditorTools entry
        winrt::Microsoft::UI::Xaml::Controls::Button m_moreButton{ nullptr };
        std::size_t m_visibleTools = 0;
        std::wstring m_selectedTool;   // highlighted toolbar item ("" = none)
        std::wstring m_mode{ L"default" };

        // Helpers.
        std::unique_ptr<Helper> m_tooltip;
        std::unique_ptr<Helper> m_optionsBar;
        std::unique_ptr<Helper> m_zoomHelper;
        std::unique_ptr<Helper> m_colorHelper;
        std::unique_ptr<Helper> m_pixelsHelper;
        std::unique_ptr<Helper> m_helpHelper;
        winrt::Microsoft::UI::Xaml::FrameworkElement m_tooltipAnchor{ nullptr };

        // Options bar.
        winrt::Microsoft::UI::Xaml::Controls::Button m_backButton{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button m_colorButton{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Border m_colorSwatch{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ColorPicker m_colorPicker{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Slider m_sizeSlider{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Grid m_sizeBox{ nullptr };          // the slider over its wedge
        winrt::Microsoft::UI::Xaml::Shapes::Polygon m_sizeWedge{ nullptr };       // cellSliderSize track
        winrt::Microsoft::UI::Xaml::Controls::Button m_arrowStyleButton{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton m_transparencyButton{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton m_gifButton{ nullptr };
        std::vector<std::unique_ptr<Segments>> m_segments;
        ::Eikon::Core::ObjectPtr m_template;   // the selected tool's object while none is active
        bool m_updatingOptions = false;

        // Resize Image.
        std::vector<winrt::Microsoft::UI::Xaml::Controls::Button> m_presetButtons;
        bool m_updatingResize = false;

        // Main menu ("⋯").
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyout m_menu{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem m_menuResetCrop{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem m_menuOpenClipboard{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ToggleMenuFlyoutItem m_menuSnap{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ToggleMenuFlyoutItem m_menuSnapSimilar{ nullptr };
        std::vector<winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItemBase> m_documentItems;

        // Snake: the game drawn over the toolbar logo.
        void BuildSnakeField();
        void StartSnake();
        void SnakeStep();
        void PaintSnake();
        std::unique_ptr<::Eikon::Core::SnakeGame> m_snake;
        std::vector<winrt::Microsoft::UI::Xaml::Shapes::Rectangle> m_snakeCells;   // column-major
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_snakeTimer{ nullptr };

        // The welcome panel: high contrast drops its colours for the system ones, so it
        // follows the system colours (UISettings works in a desktop app; AccessibilitySettings' event does not).
        winrt::Windows::UI::ViewManagement::UISettings m_uiSettings;
        winrt::event_token m_colorValuesToken{};

        bool m_dialogOpen = false;
        std::vector<winrt::Windows::Graphics::RectInt32> m_passthrough;   // title-bar controls (client px)
    };
}

namespace winrt::Eikon::factory_implementation
{
    struct EditorWindow : EditorWindowT<EditorWindow, implementation::EditorWindow>
    {
    };
}
