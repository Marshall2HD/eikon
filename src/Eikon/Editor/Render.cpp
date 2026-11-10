#include "pch.h"
#include <winrt/Microsoft.Graphics.Canvas.Brushes.h>
#include <winrt/Microsoft.Graphics.Canvas.Geometry.h>
#include <winrt/Microsoft.Graphics.Canvas.Text.h>
#include "Editor/Render.h"

namespace Eikon::Editor::Render
{
    namespace canvas = winrt::Microsoft::Graphics::Canvas;
    using winrt::Windows::Foundation::Point;
    using winrt::Windows::Foundation::Rect;
    using winrt::Windows::UI::Color;
    using canvas::CanvasImageInterpolation;

    namespace
    {
        Color Rgba(double r, double g, double b, double a)
        {
            auto c = [](double v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0, 1.0) * 255 + 0.5); };
            return { c(a), c(r), c(g), c(b) };
        }

        canvas::Text::CanvasTextFormat LabelFormat()
        {
            canvas::Text::CanvasTextFormat format;
            format.FontFamily(L"Segoe UI Variable Text");
            format.FontSize(11);
            format.FontWeight(winrt::Windows::UI::Text::FontWeight{ 500 });
            format.WordWrapping(canvas::Text::CanvasWordWrapping::NoWrap);
            return format;
        }

        // A label crop layers draw it: text 0.95 white on rgba(0.04, 0.07, 0.08, 0.5),
        // corner radius 3, background = text frame expanded by (-5, -3, +10, +6). `centerX`, `top` in DIPs.
        Rect DrawLabel(canvas::CanvasDrawingSession const& session, std::wstring const& text, float centerX, float top, double backgroundAlpha)
        {
            if (text.empty())
            {
                return {};
            }
            canvas::Text::CanvasTextLayout layout{ session, text, LabelFormat(), 4096.0f, 4096.0f };
            auto const bounds = layout.LayoutBounds();
            float const textWidth = bounds.Width + 4;   // text frame: width + 4, starting 2 left
            float const textX = std::round(centerX - bounds.Width / 2) - 2;
            Rect const background{ textX - 3, top - 3, textWidth + 6, bounds.Height + 6 };
            session.FillRoundedRectangle(background, 3, 3, Rgba(0.04, 0.07, 0.08, backgroundAlpha));
            session.DrawTextLayout(layout, textX + 2, top, Rgba(0.95, 0.95, 0.95, 1));
            return background;
        }
    }

    void DrawImage(canvas::CanvasDrawingSession const& session, canvas::CanvasBitmap const& bitmap, Core::Rect const& source,
                   Rect const& dest, double zoom)
    {
        if (!bitmap)
        {
            return;
        }
        Rect const sourceRect{ static_cast<float>(source.x), static_cast<float>(source.y), static_cast<float>(source.width),
                               static_cast<float>(source.height) };
        auto const interpolation = zoom >= 1.0 ? CanvasImageInterpolation::NearestNeighbor : CanvasImageInterpolation::HighQualityCubic;
        session.DrawImage(bitmap, dest, sourceRect, 1.0f, interpolation);
    }

    void DrawCheckerboard(canvas::CanvasDrawingSession const& session, Rect const& rect, bool dark)
    {
        if (rect.Width <= 0 || rect.Height <= 0)
        {
            return;
        }
        Color const base = dark ? Rgba(0.165, 0.165, 0.165, 1) : Rgba(1, 1, 1, 1);
        Color const square = dark ? Rgba(0.2, 0.2, 0.2, 1) : Rgba(0.95, 0.95, 0.95, 1);
        session.FillRectangle(rect, base);
        // Squares on a 16-DIP grid anchored at the rect's origin: (0, 0) and (8, 8) of each tile.
        float const right = rect.X + rect.Width;
        float const bottom = rect.Y + rect.Height;
        auto const clip = session.CreateLayer(1.0f, rect);
        for (float y = rect.Y; y < bottom; y += 16)
        {
            for (float x = rect.X; x < right; x += 16)
            {
                session.FillRectangle(Rect{ x, y, 8, 8 }, square);
                session.FillRectangle(Rect{ x + 8, y + 8, 8, 8 }, square);
            }
        }
        clip.Close();
    }

    Rect DrawMarquee(canvas::CanvasDrawingSession const& session, Rect const& area, Rect const& selection, MarqueeLabels const& labels,
                     double rasterScale)
    {
        float const px = static_cast<float>(1.0 / rasterScale);
        // Shade outside the selection, even-odd: rgba(0, 0.01, 0.01, 0.25).
        auto const outer = canvas::Geometry::CanvasGeometry::CreateRectangle(session, area);
        auto const inner = canvas::Geometry::CanvasGeometry::CreateRectangle(session, selection);
        session.FillGeometry(outer.CombineWith(inner, winrt::Windows::Foundation::Numerics::float3x2::identity(),
                                               canvas::Geometry::CanvasGeometryCombine::Exclude),
                             Rgba(0, 0.01, 0.01, 0.25));
        // Border: black 1 px α 0.6 at rect ± 1 px, white 1 px α 0.4 on the rect.
        Rect const outerBorder{ selection.X - px * 0.5f, selection.Y - px * 0.5f, selection.Width + px, selection.Height + px };
        Rect const innerBorder{ selection.X + px * 0.5f, selection.Y + px * 0.5f, selection.Width - px, selection.Height - px };
        session.DrawRectangle(outerBorder, Rgba(0, 0, 0, 0.6), px);
        if (innerBorder.Width > 0 && innerBorder.Height > 0)
        {
            session.DrawRectangle(innerBorder, Rgba(1, 1, 1, 0.4), px);
        }
        float const centerX = selection.X + selection.Width / 2;
        Rect const sizeFrame = DrawLabel(session, labels.size, centerX, selection.Y + selection.Height + 8, 0.5);
        if (!labels.hint.empty() && sizeFrame.Height > 0)
        {
            DrawLabel(session, labels.hint, centerX, sizeFrame.Y + sizeFrame.Height + 6, 0.5);
        }
        return sizeFrame;
    }

    void DrawKnob(canvas::CanvasDrawingSession const& session, Point center, bool alternate)
    {
        float const size = alternate ? 10.0f : 9.0f;
        Rect const outer{ center.X - size / 2, center.Y - size / 2, size, size };
        // Shadow: opacity 0.15, offset 4 pt down, radius 5 (approximated with soft rings).
        for (int i = 3; i >= 1; --i)
        {
            float const grow = static_cast<float>(i) * 1.6f;
            Rect const shadow{ outer.X - grow, outer.Y + 4 - grow, outer.Width + grow * 2, outer.Height + grow * 2 };
            session.FillRoundedRectangle(shadow, 3 + grow, 3 + grow, Rgba(0, 0, 0, 0.05));
        }
        session.FillRoundedRectangle(outer, 3, 3, Rgba(0, 0, 0, alternate ? 0.5 : 0.7));
        Rect const inner{ center.X - 4, center.Y - 4, 8, 8 };
        canvas::Brushes::CanvasLinearGradientBrush gradient{ session, Rgba(0.95, 0.95, 0.95, 1), alternate ? Rgba(0.88, 0.88, 0.88, 1) : Rgba(1, 1, 1, 1) };
        gradient.StartPoint({ inner.X, inner.Y });
        gradient.EndPoint({ inner.X, inner.Y + inner.Height });
        session.FillRoundedRectangle(inner, 2.5f, 2.5f, gradient);
    }

    void DrawGuide(canvas::CanvasDrawingSession const& session, Point from, Point to, double rasterScale)
    {
        canvas::Geometry::CanvasStrokeStyle style;
        float const px = static_cast<float>(1.0 / rasterScale);
        style.CustomDashStyle({ 3.0f, 2.0f });
        session.DrawLine(from, to, Color{ 255, 0xFF, 0x00, 0x8C }, px, style);
    }
}
