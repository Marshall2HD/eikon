#include "pch.h"
#include "Editor/DragOut.h"

#include "AppController.h"
#include "Platform/Exporter.h"

#include <winrt/Windows.ApplicationModel.DataTransfer.h>

#include <cmath>
#include <cstring>
#include <memory>

namespace Eikon::Editor
{
    namespace
    {
        using winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation;
        using winrt::Windows::Graphics::Imaging::BitmapAlphaMode;
        using winrt::Windows::Graphics::Imaging::BitmapPixelFormat;
        using winrt::Windows::Graphics::Imaging::SoftwareBitmap;

        // 44-ico-draggable-file at its 44 × 44 pixels (premultiplied BGRA), or null.
        SoftwareBitmap DragImage()
        {
            static SoftwareBitmap const image = []() -> SoftwareBitmap {
                try
                {
                    auto const path = std::filesystem::path{ std::wstring{ winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path() } } /
                                      L"Assets" / L"44-ico-draggable-file.png";
                    Core::Bitmap const icon = Exporter::LoadImageFile(path);
                    if (icon.IsEmpty())
                    {
                        return nullptr;
                    }
                    winrt::Windows::Storage::Streams::Buffer buffer{ static_cast<std::uint32_t>(icon.pixels.size()) };
                    std::memcpy(buffer.data(), icon.pixels.data(), icon.pixels.size());
                    buffer.Length(static_cast<std::uint32_t>(icon.pixels.size()));
                    return SoftwareBitmap::CreateCopyFromBuffer(buffer, BitmapPixelFormat::Bgra8, icon.width, icon.height, BitmapAlphaMode::Premultiplied);
                }
                catch (...)
                {
                    return nullptr;
                }
            }();
            return image;
        }

        winrt::fire_and_forget FillDrag(winrt::Microsoft::UI::Xaml::DragStartingEventArgs args)
        {
            auto deferral = args.GetDeferral();
            winrt::apartment_context ui;
            std::optional<std::filesystem::path> file;
            try
            {
                file = AppController::Current().WriteDragOutFile();
            }
            catch (...)
            {
            }
            if (!file)
            {
                OutputDebugStringW(L"[Eikon] drag-out: no file to drag\n");
                args.Cancel(true);
                deferral.Complete();
                co_return;
            }
            try
            {
                auto const storage = co_await winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(file->wstring());
                co_await ui;
                // Copy.
                args.AllowedOperations(DataPackageOperation::Copy);
                auto const data = args.Data();
                data.RequestedOperation(DataPackageOperation::Copy);
                data.SetStorageItems(winrt::single_threaded_vector<winrt::Windows::Storage::IStorageItem>({ storage }));
                // The drag image centred on the pointer.
                if (auto const image = DragImage())
                {
                    args.DragUI().SetContentFromSoftwareBitmap(image, { image.PixelWidth() / 2.0f, image.PixelHeight() / 2.0f });
                }
            }
            catch (...)
            {
                OutputDebugStringW(L"[Eikon] drag-out failed\n");
                args.Cancel(true);
            }
            deferral.Complete();
        }
    }

    void AttachDragOutSource(winrt::Microsoft::UI::Xaml::UIElement const& element)
    {
        element.DragStarting([](auto&&, winrt::Microsoft::UI::Xaml::DragStartingEventArgs const& args) { FillDrag(args); });
    }

    void StartDragOut(winrt::Microsoft::UI::Xaml::UIElement const& element, winrt::Microsoft::UI::Input::PointerPoint const& point)
    {
        try
        {
            element.ReleasePointerCaptures();
            element.StartDragAsync(point);
        }
        catch (...)
        {
            OutputDebugStringW(L"[Eikon] drag-out could not start\n");
        }
    }

    void AttachDragOut(winrt::Microsoft::UI::Xaml::UIElement const& control)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs;
        struct State
        {
            bool down = false;
            winrt::Windows::Foundation::Point start{};
        };
        AttachDragOutSource(control);
        auto state = std::make_shared<State>();
        // Buttons mark pointer presses handled, so listen to handled events too.
        control.AddHandler(UIElement::PointerPressedEvent(),
            winrt::box_value(Input::PointerEventHandler([state](winrt::Windows::Foundation::IInspectable const& sender, PointerRoutedEventArgs const& args) {
                auto const element = sender.as<UIElement>();
                auto const point = args.GetCurrentPoint(element);
                state->down = point.Properties().IsLeftButtonPressed();
                state->start = point.Position();
            })),
            true);
        auto const release = [state](winrt::Windows::Foundation::IInspectable const&, PointerRoutedEventArgs const&) { state->down = false; };
        control.AddHandler(UIElement::PointerReleasedEvent(), winrt::box_value(Input::PointerEventHandler(release)), true);
        control.AddHandler(UIElement::PointerCaptureLostEvent(), winrt::box_value(Input::PointerEventHandler(release)), true);
        control.AddHandler(UIElement::PointerMovedEvent(),
            winrt::box_value(Input::PointerEventHandler([state](winrt::Windows::Foundation::IInspectable const& sender, PointerRoutedEventArgs const& args) {
                if (!state->down)
                {
                    return;
                }
                auto const element = sender.as<UIElement>();
                auto const point = args.GetCurrentPoint(element);
                if (!point.Properties().IsLeftButtonPressed())
                {
                    state->down = false;
                    return;
                }
                double const scale = element.XamlRoot() ? element.XamlRoot().RasterizationScale() : 1.0;
                double const dx = std::abs(point.Position().X - state->start.X) * scale;
                double const dy = std::abs(point.Position().Y - state->start.Y) * scale;
                if (dx < GetSystemMetrics(SM_CXDRAG) && dy < GetSystemMetrics(SM_CYDRAG))
                {
                    return;
                }
                state->down = false;
                StartDragOut(element, point);
            })),
            true);
    }
}
