#include "pch.h"
#include "Editor/Render.h"
#include "Editor/RenderObjects.h"

#include "Core/Objects/ObjConceal.h"
#include "Core/Objects/ObjCounter.h"
#include "Core/Objects/ObjFreehand.h"
#include "Core/Objects/ObjOval.h"
#include "Core/Objects/ObjRectangleBasic.h"
#include "Core/Objects/ObjText.h"
#include "Core/Settings.h"
#include "Platform/Ocr.h"

#include <winrt/Microsoft.Graphics.Canvas.Effects.h>
#include <winrt/Microsoft.Graphics.Canvas.Geometry.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include <cmath>
#include <map>
#include <unordered_map>

// Editor::Render::Draw for annotation objects: one renderer per Obj* class.
// Objects draw in image pixels: the session transform maps them onto the canvas, so stroke widths,
// radii and fonts keep pixel values.
namespace Eikon::Editor::Render
{
    namespace canvas = winrt::Microsoft::Graphics::Canvas;
    namespace geometry = winrt::Microsoft::Graphics::Canvas::Geometry;
    namespace effects = winrt::Microsoft::Graphics::Canvas::Effects;
    using winrt::Windows::Foundation::Numerics::float2;
    using winrt::Windows::Foundation::Numerics::float3x2;

    namespace
    {
        constexpr double kPi = 3.141592653589793;

        float F(double v) { return static_cast<float>(v); }
        float2 V(Core::Point p) { return { F(p.x), F(p.y) }; }
        winrt::Windows::Foundation::Rect R(Core::Rect const& r)
        {
            Core::Rect const s = r.Standardized();
            return { F(s.x), F(s.y), F(s.width), F(s.height) };
        }

        // Image pixels → canvas DIPs for the duration of one object.
        class PixelSpace
        {
        public:
            PixelSpace(canvas::CanvasDrawingSession const& session, Context const& context) : m_session(session), m_saved(session.Transform())
            {
                float const k = F(context.zoom / context.rasterScale);
                session.Transform(winrt::Windows::Foundation::Numerics::make_float3x2_scale(k) *
                                  winrt::Windows::Foundation::Numerics::make_float3x2_translation(F(context.origin.x), F(context.origin.y)) * m_saved);
            }
            ~PixelSpace() { m_session.Transform(m_saved); }

        private:
            canvas::CanvasDrawingSession const& m_session;
            float3x2 m_saved;
        };

        canvas::Geometry::CanvasStrokeStyle StrokeStyle(Core::Shape const& shape)
        {
            canvas::Geometry::CanvasStrokeStyle style;
            auto const cap = shape.cap == Core::Shape::Cap::Round    ? canvas::Geometry::CanvasCapStyle::Round
                             : shape.cap == Core::Shape::Cap::Square ? canvas::Geometry::CanvasCapStyle::Square
                                                                     : canvas::Geometry::CanvasCapStyle::Flat;
            style.StartCap(cap);
            style.EndCap(cap);
            style.DashCap(cap);
            style.LineJoin(shape.join == Core::Shape::Join::Round ? canvas::Geometry::CanvasLineJoin::Round : canvas::Geometry::CanvasLineJoin::Miter);
            if (!shape.dashes.empty() && shape.strokeWidth > 0)
            {
                // Win2D dashes are in stroke widths; lineDashPattern is in pixels.
                std::vector<float> dashes;
                for (double d : shape.dashes)
                {
                    dashes.push_back(F(d / shape.strokeWidth));
                }
                style.CustomDashStyle(dashes);
            }
            return style;
        }

        canvas::CanvasDevice SharedDevice()
        {
            return canvas::CanvasDevice::GetSharedDevice();
        }

        // CanvasBitmaps made from Core bitmaps (conceal results, pasted images), per device.
        canvas::CanvasBitmap BitmapFor(canvas::ICanvasResourceCreator const& device, std::shared_ptr<Core::Bitmap const> const& bitmap)
        {
            struct Entry
            {
                std::weak_ptr<Core::Bitmap const> source;
                canvas::CanvasBitmap bitmap{ nullptr };
            };
            static std::map<Core::Bitmap const*, Entry> cache;
            if (!bitmap || bitmap->IsEmpty())
            {
                return nullptr;
            }
            for (auto it = cache.begin(); it != cache.end();)
            {
                it = it->second.source.expired() ? cache.erase(it) : std::next(it);
            }
            auto& entry = cache[bitmap.get()];
            if (entry.bitmap && entry.source.lock() == bitmap && entry.bitmap.Device() == device.Device())
            {
                return entry.bitmap;
            }
            entry.source = bitmap;
            entry.bitmap = canvas::CanvasBitmap::CreateFromBytes(device, winrt::array_view<std::uint8_t const>(bitmap->pixels), bitmap->width,
                                                                 bitmap->height, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                                                 96.0f, canvas::CanvasAlphaMode::Premultiplied);
            return entry.bitmap;
        }

        // OCR word boxes per (image, area): the conceal text modes look their area up here; a miss starts
        // recognition and redraws when it lands.
        struct OcrKey
        {
            Core::Bitmap const* image;
            double x, y, w, h;
            bool operator<(OcrKey const& o) const
            {
                return std::tie(image, x, y, w, h) < std::tie(o.image, o.x, o.y, o.w, o.h);
            }
        };
        std::map<OcrKey, std::optional<std::vector<Core::Rect>>>& OcrCache()
        {
            static std::map<OcrKey, std::optional<std::vector<Core::Rect>>> cache;
            return cache;
        }
        std::function<void()>& Redraw()
        {
            static std::function<void()> redraw;
            return redraw;
        }

        void DrawConceal(canvas::CanvasDrawingSession const& session, Core::ObjConceal const& conceal, Context const& context)
        {
            auto const& env = Core::Environment();
            Core::Bitmap const* image = env.bitmap;
            if (image && !image->IsEmpty() && !conceal.IsCreating())
            {
                if (auto const area = conceal.PendingOcr())
                {
                    OcrKey const key{ image, area->x, area->y, area->width, area->height };
                    auto& cache = OcrCache();
                    auto it = cache.find(key);
                    if (it == cache.end())
                    {
                        cache[key] = std::nullopt;
                        // Crop the area and recognise it off the UI thread.
                        auto const w = static_cast<std::int32_t>(area->width);
                        auto const h = static_cast<std::int32_t>(area->height);
                        std::vector<std::uint8_t> crop(static_cast<std::size_t>(w) * h * 4);
                        for (std::int32_t y = 0; y < h; ++y)
                        {
                            std::memcpy(crop.data() + static_cast<std::size_t>(y) * w * 4,
                                        image->pixels.data() + (static_cast<std::size_t>(area->y) + y) * image->Stride() + static_cast<std::size_t>(area->x) * 4,
                                        static_cast<std::size_t>(w) * 4);
                        }
                        Eikon::RecognizeWordBoxes(std::move(crop), w, h, [key](std::vector<Core::Rect> boxes) {
                            OcrCache()[key] = std::move(boxes);
                            if (Redraw()) Redraw()();
                        });
                    }
                    else if (it->second)
                    {
                        const_cast<Core::ObjConceal&>(conceal).SetTextBoxes(*area, *it->second);
                    }
                }
                if (auto const bitmap = conceal.Concealed(*image, context.dpr))
                {
                    if (auto const cb = BitmapFor(session, bitmap))
                    {
                        session.DrawImage(cb, R(conceal.Area()), { 0, 0, F(bitmap->width), F(bitmap->height) }, 1.0f,
                                          canvas::CanvasImageInterpolation::NearestNeighbor);
                    }
                }
            }
            DrawShapes(session, conceal.Shapes(context.dpr, context.zoom));
        }

        void DrawMagnifier(canvas::CanvasDrawingSession const& session, Core::ObjMagnifier const& magnifier, Context const& context)
        {
            Core::Rect const source = magnifier.rect.Standardized();
            Core::Rect const lens = magnifier.LensRect();
            if (context.image && source.width > 0 && source.height > 0)
            {
                // Nearest-neighbour pixels of the source, clipped to the lens shape.
                auto const clip = ToGeometry(session, magnifier.OutlinePath(lens, context.dpr, context.zoom));
                auto layer = session.CreateLayer(1.0f, clip);
                session.DrawImage(context.image, R(lens), R(source), 1.0f, canvas::CanvasImageInterpolation::NearestNeighbor);
                layer.Close();
            }
            DrawShapes(session, magnifier.Shapes(context.dpr, context.zoom));
        }

        void DrawImageObject(canvas::CanvasDrawingSession const& session, Core::ObjImage const& image, Context const& context)
        {
            if (auto const cb = BitmapFor(session, image.Image()))
            {
                auto const size = cb.SizeInPixels();
                session.DrawImage(cb, R(image.rect), { 0, 0, F(size.Width), F(size.Height) }, F(image.Opacity()),
                                  context.zoom >= 1.0 ? canvas::CanvasImageInterpolation::Linear : canvas::CanvasImageInterpolation::HighQualityCubic);
            }
            DrawShapes(session, image.ObjRectangleBasic::Shapes(context.dpr, context.zoom));
            DrawShapes(session, image.Shapes(context.dpr, context.zoom));
        }

        void DrawHighlighter(canvas::CanvasDrawingSession const& session, Core::ObjHighlighter const& marker, Context const& context)
        {
            Core::Path const& path = marker.Outline(context.dpr);
            if (path.Empty())
            {
                return;
            }
            auto const look = marker.Look(Core::Environment().bitmap, context.dpr);
            auto const shape = ToGeometry(session, path);
            if (look.blend == Core::HighlighterLook::Blend::Normal || !context.image)
            {
                session.FillGeometry(shape, ToColor(look.color, look.opacity));
                return;
            }
            // Multiply / Screen with the screenshot inside the stroke.
            canvas::CanvasCommandList ink{ session };
            {
                auto ds = ink.CreateDrawingSession();
                ds.FillGeometry(shape, ToColor(look.color));
            }
            effects::BlendEffect blend;
            blend.Mode(look.blend == Core::HighlighterLook::Blend::Multiply ? effects::BlendEffectMode::Multiply : effects::BlendEffectMode::Screen);
            blend.Background(context.image);
            blend.Foreground(ink);
            auto layer = session.CreateLayer(F(look.opacity), shape);
            session.DrawImage(blend);
            layer.Close();
        }
    }

    winrt::Windows::UI::Color ToColor(Core::Color color, double alpha)
    {
        double const a = std::clamp(alpha * color.a / 255.0, 0.0, 1.0);
        return { static_cast<std::uint8_t>(std::lround(a * 255)), color.r, color.g, color.b };
    }

    geometry::CanvasGeometry ToGeometry(canvas::ICanvasResourceCreator const& device, Core::Path const& path, bool evenOdd)
    {
        geometry::CanvasPathBuilder builder{ device };
        builder.SetFilledRegionDetermination(evenOdd ? geometry::CanvasFilledRegionDetermination::Alternate
                                                     : geometry::CanvasFilledRegionDetermination::Winding);
        bool open = false;
        Core::Point current{};
        auto begin = [&](Core::Point p) {
            if (open)
            {
                builder.EndFigure(geometry::CanvasFigureLoop::Open);
            }
            builder.BeginFigure(V(p));
            open = true;
            current = p;
        };
        for (auto const& c : path.commands)
        {
            switch (c.kind)
            {
            case Core::PathCommand::Kind::Move:
                begin(c.p);
                break;
            case Core::PathCommand::Kind::Line:
                if (!open) begin(current);
                builder.AddLine(V(c.p));
                current = c.p;
                break;
            case Core::PathCommand::Kind::Quad:
                if (!open) begin(current);
                builder.AddQuadraticBezier(V(c.c1), V(c.p));
                current = c.p;
                break;
            case Core::PathCommand::Kind::Cubic:
                if (!open) begin(current);
                builder.AddCubicBezier(V(c.c1), V(c.c2), V(c.p));
                current = c.p;
                break;
            case Core::PathCommand::Kind::Arc:
            {
                Core::Point const first{ c.p.x + c.radius * std::cos(c.start), c.p.y + c.radius * std::sin(c.start) };
                if (!open)
                {
                    begin(first);
                }
                else
                {
                    builder.AddLine(V(first));
                }
                // Direct2D cannot draw a full turn as one arc: split it in halves.
                int const parts = std::fabs(c.sweep) >= 2 * kPi - 1e-6 ? 2 : 1;
                for (int i = 0; i < parts; ++i)
                {
                    double const s = c.start + c.sweep * i / parts;
                    builder.AddArc(V(c.p), F(c.radius), F(c.radius), F(s), F(c.sweep / parts));
                }
                double const end = c.start + c.sweep;
                current = { c.p.x + c.radius * std::cos(end), c.p.y + c.radius * std::sin(end) };
                break;
            }
            case Core::PathCommand::Kind::Close:
                if (open)
                {
                    builder.EndFigure(geometry::CanvasFigureLoop::Closed);
                    open = false;
                }
                break;
            }
        }
        if (open)
        {
            builder.EndFigure(geometry::CanvasFigureLoop::Open);
        }
        return geometry::CanvasGeometry::CreatePath(builder);
    }

    void DrawShapes(canvas::CanvasDrawingSession const& session, std::vector<Core::Shape> const& shapes)
    {
        for (auto const& shape : shapes)
        {
            if (shape.path.Empty())
            {
                continue;
            }
            auto const g = ToGeometry(session, shape.path, shape.evenOdd);
            if (shape.fill)
            {
                session.FillGeometry(g, ToColor(shape.fillPaint.color, shape.fillPaint.alpha));
            }
            if (shape.stroke && shape.strokeWidth > 0)
            {
                session.DrawGeometry(g, ToColor(shape.strokePaint.color, shape.strokePaint.alpha), F(shape.strokeWidth), StrokeStyle(shape));
            }
        }
    }

    void SyncEnvironment(std::shared_ptr<Core::Document> const& document, Context const& context, std::function<void()> redraw)
    {
        auto& env = Core::Environment();
        if (!document)
        {
            return;
        }
        env.dpr = context.dpr;
        env.zoom = context.zoom;
        env.image = document->Source();
        // Environment holds a raw pointer; keep the pixels alive until another document replaces them.
        static std::shared_ptr<Core::Bitmap const> keepAlive;
        keepAlive = document->ImagePtr();
        env.bitmap = keepAlive.get();
        env.nextCounter = [weak = std::weak_ptr<Core::Document>(document)] {
            auto const d = weak.lock();
            return d ? d->NextCounter() : 1;
        };
        if (!env.measureText)
        {
            env.measureText = MeasureText;
        }
        Redraw() = std::move(redraw);
    }

    void DrawSpotlights(canvas::CanvasDrawingSession const& session, std::vector<Core::ObjectPtr> const& objects, Context const& context,
                        Core::Rect const& image)
    {
        geometry::CanvasGeometry curtain{ nullptr };
        for (auto const& object : objects)
        {
            if (!object || !object->wantsSpotlight)
            {
                continue;
            }
            auto const* spot = dynamic_cast<Core::ObjSpotlight const*>(object.get());
            Core::Rect const b = object->rect.Standardized();
            if (!spot || b.width <= 0 || b.height <= 0)
            {
                continue;
            }
            if (!curtain)
            {
                curtain = geometry::CanvasGeometry::CreateRectangle(session, R(image));
            }
            curtain = curtain.CombineWith(ToGeometry(session, spot->OutlinePath(b, context.dpr, context.zoom)),
                                          winrt::Windows::Foundation::Numerics::float3x2::identity(), geometry::CanvasGeometryCombine::Exclude);
        }
        if (!curtain)
        {
            return;
        }
        PixelSpace space{ session, context };
        double const alpha = Core::ObjSpotlight::Transparency(Core::Settings::Current());
        session.FillGeometry(curtain, ToColor(Core::Color{ 0, 0, 0, 255 }, alpha));
    }

    void Draw(canvas::CanvasDrawingSession const& session, Core::ProtoObject const& object, Context const& context)
    {
        PixelSpace space{ session, context };
        std::wstring_view const name = object.ClassName();
        if (name == L"ObjConceal")
        {
            DrawConceal(session, static_cast<Core::ObjConceal const&>(object), context);
        }
        else if (name == L"ObjMagnifier")
        {
            DrawMagnifier(session, static_cast<Core::ObjMagnifier const&>(object), context);
        }
        else if (name == L"ObjImage")
        {
            DrawImageObject(session, static_cast<Core::ObjImage const&>(object), context);
        }
        else if (name == L"ObjHighlighter")
        {
            DrawHighlighter(session, static_cast<Core::ObjHighlighter const&>(object), context);
        }
        else if (name == L"ObjText")
        {
            DrawText(session, object, context);
        }
        else if (name == L"ObjCounter")
        {
            DrawCounter(session, object, context);
        }
        else
        {
            DrawShapes(session, object.Shapes(context.dpr, context.zoom));
        }
    }
}
