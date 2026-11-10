#pragma once

// Drawing for the editor canvas (ARCHITECTURE.md "Objects": rendering lives here so that Core stays
// free of Win2D): the image, the transparent-area checkerboard, the crop marquee, knobs and the snap
// guides. Annotation objects draw
// through Render::Draw (RenderObjects.cpp, owned by the annotation work).

#include "Core/Geometry.h"
#include "Core/Objects/ProtoObject.h"

#include <string_view>

namespace Eikon::Editor::Render
{
    // What a renderer needs to map image pixels to the canvas.
    struct Context
    {
        double zoom = 1.0;          // device pixels per image pixel (Framer zoom)
        double dpr = 1.0;           // the image's backing scale
        double rasterScale = 1.0;   // canvas device pixels per DIP
        Core::Point origin;         // canvas position (DIPs) of image pixel (0, 0)
        // The screenshot as drawn (96 DPI, one DIP per pixel): the magnifier lens and the highlighter's
        // blend modes read it.
        winrt::Microsoft::Graphics::Canvas::CanvasBitmap image{ nullptr };

        // Image pixels -> canvas DIPs.
        winrt::Windows::Foundation::Point ToCanvas(Core::Point p) const noexcept
        {
            double const k = zoom / rasterScale;
            return { static_cast<float>(origin.x + p.x * k), static_cast<float>(origin.y + p.y * k) };
        }
        winrt::Windows::Foundation::Rect ToCanvasRect(Core::Rect r) const noexcept
        {
            Core::Rect const s = r.Standardized();
            auto const a = ToCanvas(s.Origin());
            double const k = zoom / rasterScale;
            return { a.X, a.Y, static_cast<float>(s.width * k), static_cast<float>(s.height * k) };
        }
    };

    // Draws `source` (image pixels) of `bitmap` (created at 96 DPI, one DIP per pixel) into `dest`
    // (DIPs): nearest neighbour at zoom >= 1 so pixels stay sharp, high-quality cubic below
    void DrawImage(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session,
                   winrt::Microsoft::Graphics::Canvas::CanvasBitmap const& bitmap,
                   Core::Rect const& source, winrt::Windows::Foundation::Rect const& dest, double zoom);

    // Editor::Render::Draw(session, object, context): one renderer per Obj* class, chosen by
    // ClassName() (RenderObjects.cpp).
    void Draw(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session,
              Core::ProtoObject const& object, Context const& context);

    // The transparency checkerboard: a 16 × 16 pt pattern with two 8 × 8 squares; light: white with 0.95 grey,
    // dark: 0.165 grey with 0.2 grey. `rect` in DIPs.
    void DrawCheckerboard(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session,
                          winrt::Windows::Foundation::Rect const& rect, bool dark);

    // The shade outside `selection` (over `area`), the black/white border and the labels.
    struct MarqueeLabels
    {
        std::wstring size;        // "W×H px", 8 pt below the bottom edge
        std::wstring hint;        // "Hit Enter to crop" / "Click to switch PX/PT", under the size label
    };
    // Returns the size label's frame (DIPs) so clicks on it can be recognised.
    winrt::Windows::Foundation::Rect DrawMarquee(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session,
                                                 winrt::Windows::Foundation::Rect const& area,
                                                 winrt::Windows::Foundation::Rect const& selection,
                                                 MarqueeLabels const& labels, double rasterScale);

    // Knob: 9 × 9 pt (10 × 10 when `alternate`), centred on `center` (DIPs).
    void DrawKnob(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session,
                  winrt::Windows::Foundation::Point center, bool alternate);

    // Snap guide: Display-P3 (1, 0, 0.55) ≈ sRGB #FF008C, 1 px, dash [3, 2].
    void DrawGuide(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session,
                   winrt::Windows::Foundation::Point from, winrt::Windows::Foundation::Point to, double rasterScale);
}
