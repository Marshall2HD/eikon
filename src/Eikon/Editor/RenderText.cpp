#include "pch.h"
#include "Editor/RenderObjects.h"

#include "Core/Objects/ObjCounter.h"
#include "Core/Objects/ObjText.h"

#include <winrt/Microsoft.Graphics.Canvas.Effects.h>
#include <winrt/Microsoft.Graphics.Canvas.Geometry.h>
#include <winrt/Microsoft.Graphics.Canvas.Text.h>

#include <cmath>

// Text and counter rendering with DirectWrite through Win2D.
namespace Eikon::Editor::Render
{
    namespace canvas = winrt::Microsoft::Graphics::Canvas;
    namespace text = winrt::Microsoft::Graphics::Canvas::Text;
    namespace effects = winrt::Microsoft::Graphics::Canvas::Effects;

    namespace
    {
        float F(double v) { return static_cast<float>(v); }

        winrt::Windows::UI::Text::FontWeight Weight(bool medium)
        {
            return { static_cast<std::uint16_t>(medium ? 500 : 400) };
        }

        text::CanvasTextFormat TextFormat(Core::ObjText const& object, double dpr)
        {
            text::CanvasTextFormat format;
            format.FontFamily(winrt::hstring{ object.FontFamily() });
            format.FontSize(F(object.FontSize(dpr)));
            format.FontWeight(Weight(object.style == Core::AnnotationStyle::Pointy && object.UsesMediumWeight()));
            format.WordWrapping(text::CanvasWordWrapping::Wrap);
            double const lineHeight = object.LineHeight(dpr);
            format.LineSpacingMode(text::CanvasLineSpacingMode::Uniform);
            format.LineSpacing(F(lineHeight));
            format.LineSpacingBaseline(F(lineHeight * 0.8));
            return format;
        }

        text::CanvasTextLayout Layout(canvas::ICanvasResourceCreator const& device, Core::ObjText const& object, double dpr, double wrap)
        {
            std::wstring const& s = object.text;
            // A trailing newline still adds a line.
            std::wstring const shown = s.empty() ? std::wstring{ L" " } : (s.back() == L'\n' ? s + L" " : s);
            return text::CanvasTextLayout{ device, winrt::hstring{ shown }, TextFormat(object, dpr), F((std::max)(wrap, 1.0)), 100000.0f };
        }
    }

    Core::Size MeasureText(Core::ProtoObject const& object, double maxWidth)
    {
        auto const* t = dynamic_cast<Core::ObjText const*>(&object);
        if (!t)
        {
            return {};
        }
        try
        {
            auto const layout = Layout(canvas::CanvasDevice::GetSharedDevice(), *t, Core::Environment().dpr, maxWidth);
            auto const bounds = layout.LayoutBounds();
            double width = bounds.Width;
            if (t->text.empty())
            {
                width = 0;
            }
            return { std::ceil(width), std::ceil(bounds.Height) };
        }
        catch (...)
        {
            return {};
        }
    }

    void DrawText(canvas::CanvasDrawingSession const& session, Core::ProtoObject const& object, Context const& context)
    {
        auto const& t = static_cast<Core::ObjText const&>(object);
        double const dpr = context.dpr;
        auto const shapes = t.Shapes(dpr, context.zoom);
        if (!t.nobg && !shapes.empty())
        {
            // Layer shadow: black, offset (0, 10 dpr) on screen, radius 10 dpr, opacity 0.08.
            canvas::CanvasCommandList silhouette{ session };
            {
                auto ds = silhouette.CreateDrawingSession();
                ds.FillGeometry(ToGeometry(session, shapes.front().path), winrt::Windows::UI::Colors::Black());
            }
            effects::ShadowEffect shadow;
            shadow.Source(silhouette);
            shadow.BlurAmount(F(10.0 * dpr / 2.0));
            shadow.ShadowColor({ static_cast<std::uint8_t>(std::lround(0.08 * 255)), 0, 0, 0 });
            session.DrawImage(shadow, { 0.0f, F(10.0 * dpr) });
        }
        DrawShapes(session, shapes);
        if (t.editing)
        {
            // The overlay TextBox shows the glyphs while editing.
            return;
        }
        Core::Rect const area = t.TextRect(dpr);
        auto const layout = Layout(session, t, dpr, area.width + 1.0);
        winrt::Windows::Foundation::Numerics::float2 const at{ F(area.x), F(area.y) };
        Core::Color const ink = t.TextColor();
        if (t.nobg && Core::Environment().bitmap)
        {
            // Halo: the glyphs stroked in the background colour at 0.8 when the contrast is above 1.3.
            if (auto const bg = Core::DetectBackground(*Core::Environment().bitmap, t.rect, 400); bg && Core::ContrastRatio(ink, *bg) > 1.3)
            {
                auto const outline = canvas::Geometry::CanvasGeometry::CreateText(layout);
                canvas::Geometry::CanvasStrokeStyle round;
                round.LineJoin(canvas::Geometry::CanvasLineJoin::Round);
                round.StartCap(canvas::Geometry::CanvasCapStyle::Round);
                round.EndCap(canvas::Geometry::CanvasCapStyle::Round);
                session.DrawGeometry(outline, at, ToColor(*bg, 0.8), F(t.FontSize(dpr) * 0.1), round);
            }
        }
        session.DrawTextLayout(layout, at, ToColor(ink));
    }

    void DrawCounter(canvas::CanvasDrawingSession const& session, Core::ProtoObject const& object, Context const& context)
    {
        auto const& c = static_cast<Core::ObjCounter const&>(object);
        double const dpr = context.dpr;
        DrawShapes(session, c.Shapes(dpr, context.zoom));
        text::CanvasTextFormat format;
        format.FontFamily(L"Segoe UI Variable Display");
        format.FontSize(F(c.FontSize(dpr)));
        format.FontWeight(Weight(c.UsesMediumWeight()));
        format.HorizontalAlignment(text::CanvasHorizontalAlignment::Center);
        format.VerticalAlignment(text::CanvasVerticalAlignment::Center);
        format.WordWrapping(text::CanvasWordWrapping::NoWrap);
        double const box = c.Radius(dpr) * 4.0;
        Core::Point const centre = c.TextCentre(dpr);
        session.DrawText(winrt::hstring{ std::to_wstring(c.count) },
                         winrt::Windows::Foundation::Rect{ F(centre.x - box / 2), F(centre.y - box / 2), F(box), F(box) }, ToColor(c.TextColor()), format);
    }
}
