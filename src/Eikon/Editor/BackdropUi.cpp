#include "pch.h"
#include "Editor/BackdropUi.h"

#include "AppController.h"
#include "BackdropPanel.xaml.h"
#include "Core/Backdrop.h"
#include "Core/Settings.h"
#include "Editor/BackdropRender.h"
#include "Platform/Exporter.h"
#include "Platform/Notifier.h"
#include "Strings.h"

namespace Eikon::Editor
{
    namespace bd = Core::Backdrop;

    namespace
    {
        std::filesystem::path WallpaperPath()
        {
            wchar_t path[MAX_PATH + 1]{};
            if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0) && path[0])
            {
                return path;
            }
            return {};
        }
    }

    void RenderBackdrop(Core::Document& document)
    {
        document.backdrop.config.isOn = 1;
        Core::Settings::Current().BdrConfig(bd::Encode(document.backdrop.config));
        document.backdrop.Changed();
    }

    void TurnOffBackdrop(Core::Document& document)
    {
        auto& config = document.backdrop.config;
        config.isOn = 0;
        config.innerRadius = 0;
        config.outerRadius = 0;
        config.inset = -1;
        document.backdrop.Changed();
    }

    void LoadBackdropImage(std::shared_ptr<Core::Document> document)
    {
        if (!document)
        {
            return;
        }
        std::wstring const path = document->backdrop.config.filepath;
        auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        [](std::shared_ptr<Core::Document> document, std::wstring path,
           winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher) -> winrt::fire_and_forget {
            co_await winrt::resume_background();
            Core::Bitmap image;
            if (!path.empty() && path != L"wallpaper")
            {
                image = Exporter::LoadImageFile(path);
            }
            if (image.IsEmpty())
            {
                if (path != L"wallpaper")
                {
                    OutputDebugStringW(L"Eikon: backdrop: failed to open the image file\n");
                }
                if (auto const wallpaper = WallpaperPath(); !wallpaper.empty())
                {
                    image = Exporter::LoadImageFile(wallpaper);
                }
            }
            co_await wil::resume_foreground(dispatcher);
            auto& state = document->backdrop;
            if (state.config.filepath != path)
            {
                co_return; // another file was picked meanwhile
            }
            state.image = image.IsEmpty() ? nullptr : std::make_shared<Core::Bitmap const>(std::move(image));
            state.imagePath = path;
            // Image mode, the average colour as palette, noise 0.03.
            state.config.mode = L"image";
            if (state.image)
            {
                std::wstring hex = bd::ToHex(bd::AverageColor(*state.image)).substr(1);
                state.config.colors = hex + L"," + hex;
            }
            state.config.stops = L"";
            state.config.noise = 0.03;
            if (state.config.isOn == 1)
            {
                RenderBackdrop(*document);
            }
            else
            {
                state.Changed();
            }
        }(std::move(document), path, dispatcher);
    }

    void ShowBackdropPanel(winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor, std::shared_ptr<Core::Document> document)
    {
        if (!document || !anchor)
        {
            return;
        }
        auto& state = document->backdrop;
        // The size gate, with the canvas as it is now.
        auto const layout = BackdropRender::DocumentLayout(*document);
        if (bd::Megapixels(bd::CanvasPixelsForLimits(layout.pads, layout.srcW, layout.srcH)) > 15)
        {
            Notifier::Show({ Strings::Get(L"Output_TooLargeTitle"), Strings::Get(L"Output_TooLargeMessage"), true, 5.0, true });
            return;
        }
        if (document->metadata.backdropApplied)
        {
            // A window captured with Windows' own frame around it.
            Notifier::Show({ Strings::Get(L"Output_HasBackdropTitle"), Strings::Get(L"Output_HasBackdropMessage"), true, 5.0, false,
                             Strings::Get(L"Output_HasBackdropButton"), [] { AppController::Current().OpenSettings(); } });
        }
        // Step 3: the last-used look from bdrConfig, then render.
        std::wstring const saved = Core::Settings::Current().BdrConfig();
        if (!saved.empty())
        {
            if (auto const dict = Core::JsonValue::Parse(saved); dict && dict->IsObject())
            {
                bd::Apply(state.config, *dict);
            }
        }
        RenderBackdrop(*document);
        if ((state.config.mode == L"image" || state.config.mode == L"wallpaper") && (!state.image || state.imagePath != state.config.filepath))
        {
            LoadBackdropImage(document);
        }

        using namespace winrt::Microsoft::UI::Xaml;
        Controls::Flyout flyout;
        Style presenter{ winrt::xaml_typename<Controls::FlyoutPresenter>() };
        presenter.Setters().Append(Setter(Controls::Control::PaddingProperty(), winrt::box_value(Thickness{ 0, 0, 0, 0 })));
        presenter.Setters().Append(Setter(FrameworkElement::MaxWidthProperty(), winrt::box_value(400.0)));
        presenter.Setters().Append(Setter(FrameworkElement::MaxHeightProperty(), winrt::box_value(900.0)));
        // Candy: a floating 16-px card on the surface colour (the flyout keeps the system shadow).
        presenter.Setters().Append(Setter(Controls::Control::CornerRadiusProperty(), winrt::box_value(CornerRadius{ 16, 16, 16, 16 })));
        if (auto surface = Application::Current().Resources().TryLookup(winrt::box_value(L"CandySurfaceBrush")))
        {
            presenter.Setters().Append(Setter(Controls::Control::BackgroundProperty(), surface));
        }
        flyout.FlyoutPresenterStyle(presenter);
        flyout.Placement(Controls::Primitives::FlyoutPlacementMode::Bottom);
        winrt::Eikon::BackdropPanel panel;
        auto* self = winrt::get_self<winrt::Eikon::implementation::BackdropPanel>(panel);
        winrt::weak_ref<Controls::Flyout> weakFlyout = flyout;
        self->Open(document, [weakFlyout] {
            if (auto f = weakFlyout.get())
            {
                f.Hide();
            }
        });
        flyout.Content(panel);
        flyout.ShowAt(anchor);
    }
}
