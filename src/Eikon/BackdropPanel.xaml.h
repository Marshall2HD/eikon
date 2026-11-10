#pragma once

#include "BackdropPanel.g.h"

#include "Core/Document.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace winrt::Eikon::implementation
{
    struct BackdropPanel : BackdropPanelT<BackdropPanel>
    {
        BackdropPanel() = default;

        void InitializeComponent();

        // Binds the panel to `document` and sets every control from its config. `close` hides
        // the flyout.
        void Open(std::shared_ptr<::Eikon::Core::Document> document, std::function<void()> close);

        // XAML event handlers.
        void PaddingSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void ShadowSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void InsetSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void OuterRadiusSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void InnerRadiusSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void BlurSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void Recenter_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ModeTab_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void CustomGradOk_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void GradientType_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void Reverse_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Randomize_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Remove_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Ok_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        enum class Group { Solid, Colormix, Image };

        // A CtrlRadioImage swatch.
        struct Swatch
        {
            winrt::Microsoft::UI::Xaml::Controls::Grid root{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border fill{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border ring{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border inner{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border inner2{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border pressed{ nullptr };
            int tag = 0;
        };

        // One entry of the image library (scanCustomImages): the desktop wallpaper first, then up to six
        // backdrop-th-*/backdrop-fs-* pairs, newest first.
        struct LibraryImage
        {
            std::wstring path;          // full-size file, or "wallpaper"
            std::filesystem::path thumb;
        };

        Swatch MakeSwatch(double width, double height, winrt::Microsoft::UI::Xaml::Media::Brush const& brush, int tag,
                          std::function<void()> onClick);
        static void SetSelected(Swatch& swatch, bool selected);
        static void Wrap(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& host, std::vector<Swatch> const& items, double itemWidth, double gap);
        void Select(std::vector<Swatch>& group, int tag);

        void BuildAspectMenu();
        void BuildSolids();
        void BuildGradients();
        void BuildImages();
        void ScanLibrary();
        void RefreshCustomGradBox();

        void SyncControls();
        void ShowGroup(Group group);
        void UpdateInnerEnabled();
        void UpdateRandomizeEnabled();

        // isOn = 1, save bdrConfig, repaint.
        void Render();
        // A new seed (only while on), then render.
        void Reseed();
        void ApplyTemplate(::Eikon::Core::JsonValue const& dict);
        void PickGradient(int tag);
        void PickSolid(int tag);
        void PickImage(int tag);
        void PickColor(winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor, std::wstring const& current,
                       std::function<void(std::wstring const&)> changed);
        winrt::fire_and_forget AddImage();
        void ForgetImage(int index);
        void LoadImage(std::wstring const& path);

        std::shared_ptr<::Eikon::Core::Document> m_document;
        std::function<void()> m_close;
        bool m_syncing = false;
        std::vector<Swatch> m_solids;
        std::vector<Swatch> m_gradients;
        std::vector<Swatch> m_images;
        std::vector<LibraryImage> m_library;
        Swatch m_gradFrom;
        Swatch m_gradTo;
        std::uint64_t m_loadGeneration = 0;
    };
}

namespace winrt::Eikon::factory_implementation
{
    struct BackdropPanel : BackdropPanelT<BackdropPanel, implementation::BackdropPanel>
    {
    };
}
