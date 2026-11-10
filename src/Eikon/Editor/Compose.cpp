#include "pch.h"
#include "Editor/Compose.h"

#include "Editor/BackdropRender.h"
#include "Editor/Render.h"
#include "Editor/RenderObjects.h"

#include <algorithm>
#include <cstring>

// The final image, drawn with Win2D on the shared device.
namespace Eikon::Editor
{
    namespace canvas = winrt::Microsoft::Graphics::Canvas;
    namespace bd = Core::Backdrop;
    using winrt::Windows::Foundation::Rect;

    namespace
    {
        std::shared_ptr<Core::Bitmap const> CropOnly(std::shared_ptr<Core::Bitmap const> const& image, Core::Rect const& crop)
        {
            int const x = std::clamp(static_cast<int>(crop.Left()), 0, image->width);
            int const y = std::clamp(static_cast<int>(crop.Top()), 0, image->height);
            int const w = std::clamp(static_cast<int>(std::abs(crop.width)), 0, image->width - x);
            int const h = std::clamp(static_cast<int>(std::abs(crop.height)), 0, image->height - y);
            if (x == 0 && y == 0 && w == image->width && h == image->height)
            {
                return image;
            }
            auto out = std::make_shared<Core::Bitmap>(Core::Bitmap::Create(w, h, image->dpr));
            out->screenRect = image->screenRect;
            for (int row = 0; row < h; ++row)
            {
                std::memcpy(out->pixels.data() + out->Stride() * row,
                            image->pixels.data() + image->Stride() * (y + row) + static_cast<std::size_t>(x) * 4, out->Stride());
            }
            return out;
        }

        // A CanvasBitmap of image rows [top, top + count) (full width).
        canvas::CanvasBitmap RowsBitmap(canvas::ICanvasResourceCreator const& device, Core::Bitmap const& image, int top, int count)
        {
            auto const* begin = image.pixels.data() + image.Stride() * static_cast<std::size_t>(top);
            return canvas::CanvasBitmap::CreateFromBytes(device,
                winrt::array_view<std::uint8_t const>(begin, static_cast<std::uint32_t>(image.Stride() * static_cast<std::size_t>(count))),
                image.width, count, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 96.0f,
                canvas::CanvasAlphaMode::Premultiplied);
        }
    }

    std::shared_ptr<Core::Bitmap const> Compose(Core::Document const& document, ComposeOptions const& options)
    {
        auto const& image = document.ImagePtr();
        if (!image || image->IsEmpty())
        {
            return image;
        }
        bool const withBackdrop = document.backdrop.config.isOn == 1 && !options.ignoreBackdrop;
        // Step 1: nothing to draw over the screenshot.
        if (document.Objects().empty() && !withBackdrop)
        {
            return CropOnly(image, document.Crop());
        }
        try
        {
            Core::Rect const crop = document.Crop().Standardized();
            bd::Layout layout;
            if (withBackdrop)
            {
                layout = BackdropRender::DocumentLayout(document);
            }
            else
            {
                // No backdrop: the editor's canvas (crop plus its growth around the objects),
                // transparent outside the screenshot.
                bd::Config off = document.backdrop.config;
                off.isOn = 0;
                layout = bd::ComputeLayout(off, image->dpr, crop, crop, false);
                Core::Rect const canvasRect = document.Canvas().Standardized().Union(crop);
                layout.canvasW = canvasRect.width;
                layout.canvasH = canvasRect.height;
                layout.screenshot = { crop.x - canvasRect.x, crop.y - canvasRect.y, crop.width, crop.height };
                layout.content = { 0, 0, canvasRect.width, canvasRect.height };
            }
            int const width = static_cast<int>(std::lround(layout.canvasW));
            int const height = static_cast<int>(std::lround(layout.canvasH));
            if (width <= 0 || height <= 0)
            {
                return CropOnly(image, crop);
            }
            canvas::CanvasDevice const device = canvas::CanvasDevice::GetSharedDevice();
            int const maxSize = device.MaximumBitmapSizeInPixels();
            if (width > maxSize || image->width > maxSize)
            {
                OutputDebugStringW(L"Eikon: the image is too wide to compose\n");
                return CropOnly(image, crop);
            }
            auto const canvasColor = withBackdrop ? BackdropRender::CanvasColor(document) : std::nullopt;

            // Tall images (scrolling captures) are drawn in strips that fit a render target.
            int const stripHeight = std::clamp(16 * 1024 * 1024 / width, 256, maxSize);
            canvas::CanvasBitmap whole{ nullptr };
            if (image->height <= maxSize)
            {
                whole = RowsBitmap(device, *image, 0, image->height);
            }

            // The objects read the image and its scale from Environment(), which follows the editor's
            // document; compose may run for another one (a thumbnail's), so point it here meanwhile.
            struct EnvironmentScope
            {
                Core::ObjectEnvironment& env = Core::Environment();
                Core::ObjectEnvironment saved{ env.dpr, env.zoom, env.image, env.bitmap };
                ~EnvironmentScope()
                {
                    env.dpr = saved.dpr;
                    env.image = saved.image;
                    env.bitmap = saved.bitmap;
                }
            } const environment;
            environment.env.dpr = image->dpr;
            environment.env.image = document.Source();
            environment.env.bitmap = image.get();

            auto out = std::make_shared<Core::Bitmap>(Core::Bitmap::Create(width, height, image->dpr));
            out->screenRect = image->screenRect;
            Render::Context context;
            context.zoom = 1.0;
            context.dpr = image->dpr;
            context.rasterScale = 1.0;
            context.origin = { layout.screenshot.x - crop.x, layout.screenshot.y - crop.y };
            // As ShotView: the magnifier lens and the highlighter's blend modes read the screenshot.
            // A scrolling capture taller than a bitmap can be has none (they then draw without it).
            context.image = whole;

            for (int top = 0; top < height; top += stripHeight)
            {
                int const rows = std::min(stripHeight, height - top);
                canvas::CanvasRenderTarget target(device, static_cast<float>(width), static_cast<float>(rows), 96.0f);
                {
                    auto const session = target.CreateDrawingSession();
                    session.Clear(winrt::Windows::UI::Colors::Transparent());
                    session.Transform(winrt::Windows::Foundation::Numerics::make_float3x2_translation(0.0f, static_cast<float>(-top)));
                    if (withBackdrop)
                    {
                        BackdropRender::DrawBackdrop(session, document, layout, true);
                    }
                    // The painter layers (canvas colour, screenshot, objects), clipped to the inner radius.
                    canvas::CanvasActiveLayer clip{ nullptr };
                    if (auto const geometry = BackdropRender::ContentClip(session, layout))
                    {
                        clip = session.CreateLayer(1.0f, geometry);
                    }
                    else
                    {
                        clip = session.CreateLayer(1.0f, Rect{ static_cast<float>(layout.content.x), static_cast<float>(layout.content.y),
                                                               static_cast<float>(layout.content.width), static_cast<float>(layout.content.height) });
                    }
                    if (canvasColor)
                    {
                        auto const byte = [](double v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0), 0L, 255L)); };
                        session.FillRectangle(Rect{ static_cast<float>(layout.content.x), static_cast<float>(layout.content.y),
                                                    static_cast<float>(layout.content.width), static_cast<float>(layout.content.height) },
                                              winrt::Windows::UI::Color{ 255, byte(canvasColor->r), byte(canvasColor->g), byte(canvasColor->b) });
                    }

                    // Screenshot rows visible in this strip.
                    double const shotTop = layout.screenshot.y;
                    double const visibleTop = std::max<double>(top, shotTop);
                    double const visibleBottom = std::min<double>(top + rows, shotTop + crop.height);
                    if (visibleBottom > visibleTop)
                    {
                        double const sourceTop = crop.y + (visibleTop - shotTop);
                        double const sourceRows = visibleBottom - visibleTop;
                        Rect const dest{ static_cast<float>(layout.screenshot.x), static_cast<float>(visibleTop),
                                         static_cast<float>(crop.width), static_cast<float>(sourceRows) };
                        if (whole)
                        {
                            session.DrawImage(whole, dest,
                                              Rect{ static_cast<float>(crop.x), static_cast<float>(sourceTop), static_cast<float>(crop.width),
                                                    static_cast<float>(sourceRows) },
                                              1.0f, canvas::CanvasImageInterpolation::NearestNeighbor);
                        }
                        else
                        {
                            int const firstRow = std::clamp(static_cast<int>(std::floor(sourceTop)), 0, image->height - 1);
                            int const lastRow = std::clamp(static_cast<int>(std::ceil(sourceTop + sourceRows)), firstRow + 1, image->height);
                            auto const part = RowsBitmap(device, *image, firstRow, lastRow - firstRow);
                            session.DrawImage(part, dest,
                                              Rect{ static_cast<float>(crop.x), static_cast<float>(sourceTop - firstRow),
                                                    static_cast<float>(crop.width), static_cast<float>(sourceRows) },
                                              1.0f, canvas::CanvasImageInterpolation::NearestNeighbor);
                        }
                    }
                    // The spotlight curtain under the objects, as on the canvas (ShotView::OnDraw).
                    Render::DrawSpotlights(session, document.Objects(), context, document.Source());
                    for (auto const& object : document.Objects())
                    {
                        if (object)
                        {
                            Render::Draw(session, *object, context);
                        }
                    }
                    clip.Close();
                    session.Close();
                }
                auto const bytes = target.GetPixelBytes();
                std::size_t const stripBytes = out->Stride() * static_cast<std::size_t>(rows);
                if (bytes.size() >= stripBytes)
                {
                    std::memcpy(out->pixels.data() + out->Stride() * static_cast<std::size_t>(top), bytes.data(), stripBytes);
                }
            }
            return out;
        }
        catch (winrt::hresult_error const& e)
        {
            OutputDebugStringW((L"Eikon: composing the image failed: " + std::wstring{ e.message() } + L"\n").c_str());
        }
        catch (...)
        {
            OutputDebugStringW(L"Eikon: composing the image failed\n");
        }
        return CropOnly(image, document.Crop());
    }
}
