#include "pch.h"
#include "Editor/BackdropRender.h"

#include "Core/Settings.h"

#include <winrt/Microsoft.Graphics.Canvas.Effects.h>
#include <winrt/Microsoft.Graphics.Canvas.Geometry.h>
#include <winrt/Microsoft.Graphics.Canvas.Brushes.h>

#include <cmath>

namespace Eikon::Editor::BackdropRender
{
    namespace canvas = winrt::Microsoft::Graphics::Canvas;
    namespace effects = winrt::Microsoft::Graphics::Canvas::Effects;
    namespace bd = Core::Backdrop;
    using winrt::Windows::Foundation::Rect;
    using winrt::Windows::Foundation::Numerics::float2;
    using winrt::Windows::Foundation::Numerics::float3x2;

    namespace
    {
        winrt::Windows::UI::Color ToColor(bd::Rgb c, double alpha = 1.0)
        {
            auto const byte = [](double v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0), 0L, 255L)); };
            return { byte(alpha), byte(c.r), byte(c.g), byte(c.b) };
        }

        Rect ToRect(Core::Rect const& r)
        {
            return { static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.width), static_cast<float>(r.height) };
        }

        canvas::CanvasBitmap MakeBitmap(canvas::ICanvasResourceCreator const& device, Core::Bitmap const& bitmap)
        {
            return canvas::CanvasBitmap::CreateFromBytes(device, winrt::array_view<std::uint8_t const>(bitmap.pixels),
                bitmap.width, bitmap.height, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                96.0f, canvas::CanvasAlphaMode::Premultiplied);
        }

        // Backdrop init seeds the grain and the shadow mask with the default config's random
        // seed, once per launch.
        std::int64_t ProcessSeed()
        {
            static std::int64_t const seed = bd::RandomSeed();
            return seed;
        }

        // Generated bitmaps, per device (the editor's CanvasControl and the shared export device).
        struct DeviceCache
        {
            canvas::CanvasDevice device{ nullptr };
            canvas::CanvasBitmap grain{ nullptr };
            canvas::CanvasBitmap mask{ nullptr };
            std::wstring meshKey;
            canvas::CanvasBitmap mesh{ nullptr };
            std::shared_ptr<Core::Bitmap const> imageSource;
            double imageBlur = -1;
            canvas::CanvasBitmap image{ nullptr };
            Rect imageRect{};
        };

        DeviceCache& CacheFor(canvas::CanvasDevice const& device)
        {
            static std::vector<DeviceCache> caches;
            for (auto& cache : caches)
            {
                if (cache.device == device)
                {
                    return cache;
                }
            }
            if (caches.size() >= 3)
            {
                caches.erase(caches.begin());
            }
            caches.push_back({});
            caches.back().device = device;
            return caches.back();
        }

        canvas::CanvasBitmap const& Grain(DeviceCache& cache)
        {
            if (!cache.grain)
            {
                cache.grain = MakeBitmap(cache.device, bd::Grain(ProcessSeed()));
            }
            return cache.grain;
        }

        canvas::CanvasBitmap const& Mask(DeviceCache& cache)
        {
            if (!cache.mask)
            {
                cache.mask = MakeBitmap(cache.device, bd::ShadowMask(ProcessSeed()));
            }
            return cache.mask;
        }

        canvas::CanvasBitmap const& Mesh(DeviceCache& cache, bd::Config const& config)
        {
            std::wstring const key = config.colors + L"|" + config.stops + L"|" + config.meshType.value_or(L"nil") + L"|" +
                                     std::to_wstring(config.seed);
            if (!cache.mesh || cache.meshKey != key)
            {
                cache.mesh = MakeBitmap(cache.device, bd::Mesh(config));
                cache.meshKey = key;
            }
            return cache.mesh;
        }

        // contentsGravity resizeAspectFill: the centred part of `source` with the canvas's aspect.
        Rect AspectFillSource(Rect source, double w, double h)
        {
            if (w <= 0 || h <= 0 || source.Width <= 0 || source.Height <= 0)
            {
                return source;
            }
            double const target = w / h;
            if (source.Width / source.Height > target)
            {
                float const cw = static_cast<float>(source.Height * target);
                return { source.X + (source.Width - cw) / 2, source.Y, cw, source.Height };
            }
            float const ch = static_cast<float>(source.Width / target);
            return { source.X, source.Y + (source.Height - ch) / 2, source.Width, ch };
        }

        void FillGradient(canvas::CanvasDrawingSession const& session, bd::Gradient const& gradient, double w, double h)
        {
            if (gradient.stops.empty())
            {
                return;
            }
            std::vector<canvas::Brushes::CanvasGradientStop> stops;
            for (auto const& stop : gradient.stops)
            {
                stops.push_back({ static_cast<float>(stop.location), ToColor(stop.color) });
            }
            // Gradient points are in the unit square with y up.
            float2 const start{ static_cast<float>(gradient.start.x * w), static_cast<float>((1 - gradient.start.y) * h) };
            Rect const bounds{ 0, 0, static_cast<float>(w), static_cast<float>(h) };
            if (gradient.radial)
            {
                canvas::Brushes::CanvasRadialGradientBrush brush(session, stops, canvas::CanvasEdgeBehavior::Clamp,
                                                                 canvas::CanvasAlphaMode::Premultiplied);
                brush.Center(start);
                brush.RadiusX(static_cast<float>(std::abs(gradient.end.x - gradient.start.x) * w));
                brush.RadiusY(static_cast<float>(std::abs(gradient.end.y - gradient.start.y) * h));
                session.FillRectangle(bounds, brush);
            }
            else
            {
                canvas::Brushes::CanvasLinearGradientBrush brush(session, stops, canvas::CanvasEdgeBehavior::Clamp,
                                                                 canvas::CanvasAlphaMode::Premultiplied);
                brush.StartPoint(start);
                brush.EndPoint({ static_cast<float>(gradient.end.x * w), static_cast<float>((1 - gradient.end.y) * h) });
                session.FillRectangle(bounds, brush);
            }
        }

        // "At export" (blurMeshLayer): the mesh resized to the canvas's largest
        // side L (halved above 1000, quartered above 2000, an eighth above 3000), blurred by size / 40
        // over itself, then aspect-filled.
        void DrawExportMesh(canvas::CanvasDrawingSession const& session, canvas::CanvasBitmap const& mesh, double w, double h)
        {
            double const largest = std::max(w, h);
            double scale = 1;
            if (largest > 3000)
            {
                scale = 0.125;
            }
            else if (largest > 2000)
            {
                scale = 0.25;
            }
            else if (largest > 1000)
            {
                scale = 0.5;
            }
            float const size = static_cast<float>(std::max(1.0, std::round(largest * scale)));
            canvas::CanvasRenderTarget resized(session, size, size, 96.0f);
            {
                auto const ds = resized.CreateDrawingSession();
                ds.Clear(winrt::Windows::UI::Colors::Transparent());
                ds.DrawImage(mesh, Rect{ 0, 0, size, size }, mesh.Bounds(), 1.0f, canvas::CanvasImageInterpolation::HighQualityCubic);
                ds.Close();
            }
            effects::GaussianBlurEffect blur;
            blur.Source(resized);
            blur.BlurAmount(size / 40.0f);
            blur.BorderMode(effects::EffectBorderMode::Soft);
            blur.Optimization(effects::EffectOptimization::Quality);
            canvas::CanvasCommandList soft(session);
            {
                auto const ds = soft.CreateDrawingSession();
                ds.DrawImage(resized);
                ds.DrawImage(blur);
                ds.Close();
            }
            session.DrawImage(soft, Rect{ 0, 0, static_cast<float>(w), static_cast<float>(h) },
                              AspectFillSource({ 0, 0, size, size }, w, h), 1.0f, canvas::CanvasImageInterpolation::Linear);
        }

        // display: blur 0 shows the picture itself; otherwise a copy resized to
        // 480 px on its longest side, blurred by 2 × blur and cropped by twice that.
        void PrepareImage(DeviceCache& cache, std::shared_ptr<Core::Bitmap const> const& image, double blur)
        {
            if (cache.image && cache.imageSource == image && cache.imageBlur == blur)
            {
                return;
            }
            cache.imageSource = image;
            cache.imageBlur = blur;
            cache.image = nullptr;
            if (!image || image->IsEmpty())
            {
                return;
            }
            canvas::CanvasBitmap const full = MakeBitmap(cache.device, *image);
            if (blur <= 0)
            {
                cache.image = full;
                cache.imageRect = full.Bounds();
                return;
            }
            double const scale = 480.0 / std::max(image->width, image->height);
            float const w = static_cast<float>(std::max(1.0, std::round(image->width * scale)));
            float const h = static_cast<float>(std::max(1.0, std::round(image->height * scale)));
            canvas::CanvasRenderTarget reduced(cache.device, w, h, 96.0f);
            {
                auto const ds = reduced.CreateDrawingSession();
                ds.Clear(winrt::Windows::UI::Colors::Transparent());
                ds.DrawImage(full, Rect{ 0, 0, w, h }, full.Bounds(), 1.0f, canvas::CanvasImageInterpolation::HighQualityCubic);
                ds.Close();
            }
            float const sigma = static_cast<float>(2 * blur);
            effects::GaussianBlurEffect effect;
            effect.Source(reduced);
            effect.BlurAmount(sigma);
            effect.BorderMode(effects::EffectBorderMode::Soft);
            effect.Optimization(effects::EffectOptimization::Quality);
            canvas::CanvasRenderTarget blurred(cache.device, w, h, 96.0f);
            {
                auto const ds = blurred.CreateDrawingSession();
                ds.Clear(winrt::Windows::UI::Colors::Transparent());
                ds.DrawImage(effect);
                ds.Close();
            }
            cache.image = blurred;
            float const inset = std::min({ 2 * sigma, w / 2 - 0.5f, h / 2 - 0.5f });
            cache.imageRect = { inset, inset, w - 2 * inset, h - 2 * inset };
        }

        float3x2 Translation(double x, double y)
        {
            return winrt::Windows::Foundation::Numerics::make_float3x2_translation(static_cast<float>(x), static_cast<float>(y));
        }
    }

    Core::Rect ObjectsBounds(Core::Document const& document)
    {
        Core::Rect bounds = document.Crop().Standardized();
        for (auto const& object : document.Objects())
        {
            if (object)
            {
                bounds = bounds.Union(object->rect.Standardized());
            }
        }
        return bounds;
    }

    Core::Backdrop::Layout DocumentLayout(Core::Document const& document)
    {
        double const dpr = document.ImagePtr() ? document.Image().dpr : 1.0;
        return bd::ComputeLayout(document.backdrop.config, dpr, document.Crop(), ObjectsBounds(document),
                                 Core::Settings::Current().ExpandableCanvas());
    }

    std::optional<Core::Backdrop::Rgb> CanvasColor(Core::Document const& document)
    {
        if (!document.ImagePtr())
        {
            return bd::Rgb{ 1, 1, 1 };
        }
        return bd::BestCanvasColor(document.Image(), document.Crop());
    }

    void DrawBackdrop(canvas::CanvasDrawingSession const& session, Core::Document const& document,
                      Core::Backdrop::Layout const& layout, bool forExport)
    {
        if (!layout.on || !layout.layerVisible || layout.canvasW <= 0 || layout.canvasH <= 0)
        {
            return;
        }
        bd::Config const& config = document.backdrop.config;
        DeviceCache& cache = CacheFor(session.Device());
        double const w = layout.canvasW;
        double const h = layout.canvasH;
        Rect const bounds{ 0, 0, static_cast<float>(w), static_cast<float>(h) };

        // Contents: background colour, then the mode's picture.
        auto const colors = bd::ParseColors(config.colors);
        auto const stops = bd::ParseStops(config.stops);
        canvas::CanvasCommandList base(session);
        {
            auto const ds = base.CreateDrawingSession();
            ds.FillRectangle(bounds, ToColor({ 0.8, 0.8, 0.8 }));
            if (config.mode == L"flat")
            {
                FillGradient(ds, bd::FlatGradient(bd::BaseColor(colors, stops)), w, h);
            }
            else if (config.mode == L"gradient")
            {
                FillGradient(ds, bd::ConfigGradient(config), w, h);
            }
            else if (config.mode == L"image" || config.mode == L"wallpaper")
            {
                PrepareImage(cache, document.backdrop.image, config.blur);
                if (cache.image)
                {
                    ds.DrawImage(cache.image, bounds, AspectFillSource(cache.imageRect, w, h), 1.0f,
                                 canvas::CanvasImageInterpolation::Linear);
                }
            }
            else
            {
                canvas::CanvasBitmap const& mesh = Mesh(cache, config);
                if (forExport)
                {
                    DrawExportMesh(ds, mesh, w, h);
                }
                else
                {
                    ds.DrawImage(mesh, bounds, AspectFillSource(mesh.Bounds(), w, h), 1.0f, canvas::CanvasImageInterpolation::Linear);
                }
            }
            ds.Close();
        }

        // Grain (lnoise): the 256 × 256 tile in overlay blend at opacity `noise`.
        effects::BorderEffect tiledGrain;
        tiledGrain.Source(Grain(cache));
        tiledGrain.ExtendX(canvas::CanvasEdgeBehavior::Wrap);
        tiledGrain.ExtendY(canvas::CanvasEdgeBehavior::Wrap);
        effects::CropEffect grain;
        grain.Source(tiledGrain);
        grain.SourceRectangle(bounds);
        effects::BlendEffect overlay;
        overlay.Mode(effects::BlendEffectMode::Overlay);
        overlay.Background(base);
        overlay.Foreground(grain);
        float const noise = static_cast<float>(std::clamp(config.noise, 0.0, 1.0));
        effects::ArithmeticCompositeEffect grained;
        grained.Source1(overlay);
        grained.Source2(base);
        grained.MultiplyAmount(0);
        grained.Source1Amount(noise);
        grained.Source2Amount(1 - noise);
        grained.Offset(0);

        // Outer corners: masksToBounds with cornerRadius = outerPx.
        canvas::CanvasActiveLayer clip{ nullptr };
        if (layout.outerPx > 0)
        {
            float const r = static_cast<float>(layout.outerPx);
            clip = session.CreateLayer(1.0f, canvas::Geometry::CanvasGeometry::CreateRoundedRectangle(session, bounds, r, r));
        }
        else
        {
            clip = session.CreateLayer(1.0f, bounds);
        }
        session.DrawImage(grained);

        // Shadow group: the stacked shadow layers, black bodies, masked by the noise tile.
        auto const base_ = bd::BaseColor(colors, stops);
        auto const darkest = bd::DarkestColor(colors);
        bool const clear = !CanvasColor(document).has_value();
        auto const layers = bd::ShadowLayers(config, layout.pads, layout.srcW, layout.srcH, layout.innerPx,
                                             bd::ShadowTargetOpacity(bd::Lightness(base_)), clear);
        if (!layers.empty())
        {
            bd::Rgb const shadowColor = bd::ShadowColor(darkest);
            canvas::CanvasCommandList group(session);
            {
                auto const ds = group.CreateDrawingSession();
                for (auto const& layer : layers)
                {
                    Rect const frame = ToRect(layer.frame);
                    float const r = static_cast<float>(layer.cornerRadius);
                    canvas::CanvasCommandList body(session);
                    {
                        auto const bs = body.CreateDrawingSession();
                        bs.FillRoundedRectangle(frame, r, r, winrt::Windows::UI::Colors::Black());
                        bs.Close();
                    }
                    effects::ShadowEffect shadow;
                    shadow.Source(body);
                    // A shadow radius r is roughly a Gaussian with σ = r / 2.
                    shadow.BlurAmount(static_cast<float>(layer.radius / 2));
                    shadow.ShadowColor(ToColor(shadowColor, std::clamp(layer.opacity, 0.0, 1.0)));
                    shadow.Optimization(forExport ? effects::EffectOptimization::Quality : effects::EffectOptimization::Balanced);
                    ds.DrawImage(shadow, 0.0f, static_cast<float>(layer.offsetY));
                    ds.FillRoundedRectangle(frame, r, r, winrt::Windows::UI::Colors::Black());
                }
                ds.Close();
            }
            effects::BorderEffect tiledMask;
            tiledMask.Source(Mask(cache));
            tiledMask.ExtendX(canvas::CanvasEdgeBehavior::Wrap);
            tiledMask.ExtendY(canvas::CanvasEdgeBehavior::Wrap);
            effects::AlphaMaskEffect masked;
            masked.Source(group);
            masked.AlphaMask(tiledMask);
            session.DrawImage(masked);
        }
        clip.Close();
    }

    canvas::Geometry::CanvasGeometry ContentClip(canvas::ICanvasResourceCreator const& device, Core::Backdrop::Layout const& layout)
    {
        if (!layout.on || layout.innerPx <= 0)
        {
            return nullptr;
        }
        float const r = static_cast<float>(layout.innerPx);
        return canvas::Geometry::CanvasGeometry::CreateRoundedRectangle(device, ToRect(layout.content), r, r);
    }

    void DrawPreview(canvas::CanvasDrawingSession const& session, Core::Document const& document, Render::Context const& context,
                     std::function<void()> const& drawContent)
    {
        if (document.backdrop.config.isOn != 1 || !document.ImagePtr())
        {
            drawContent();
            return;
        }
        bd::Layout const layout = DocumentLayout(document);
        Core::Rect const crop = document.Crop().Standardized();
        double const k = context.zoom / context.rasterScale;
        float3x2 const old = session.Transform();
        // Canvas pixel (cx, cy) is image pixel (cx - screenshot.x + crop.x, …).
        float3x2 const toDips = winrt::Windows::Foundation::Numerics::make_float3x2_scale(static_cast<float>(k)) *
                                Translation(context.origin.x + (crop.x - layout.screenshot.x) * k,
                                            context.origin.y + (crop.y - layout.screenshot.y) * k) *
                                old;
        session.Transform(toDips);
        DrawBackdrop(session, document, layout, false);
        canvas::CanvasActiveLayer clip{ nullptr };
        if (auto const geometry = ContentClip(session, layout))
        {
            clip = session.CreateLayer(1.0f, geometry);
        }
        if (auto const color = CanvasColor(document))
        {
            session.FillRectangle(ToRect(layout.content), ToColor(*color));
        }
        session.Transform(old);
        drawContent();
        if (clip)
        {
            clip.Close();
        }
    }
}
