#pragma once

// Draws the backdrop with Win2D: lbackdrop
// (background colour, mesh / gradient / image contents, grain) and lshadowGroup (the stacked shadows,
// masked by a noise tile), clipped to the outer radius. Everything is drawn in output-canvas pixels
// (Core::Backdrop::Layout); callers set the session transform. UI thread or any thread that owns the
// session; the generated bitmaps are cached per device.

#include "Core/Backdrop.h"
#include "Core/Document.h"
#include "Editor/Render.h"

#include <functional>

namespace Eikon::Editor::BackdropRender
{
    // The document's output layout: crop, canvas expansion around the objects (`expandableCanvas`),
    // padding, ratio and radii.
    Core::Backdrop::Layout DocumentLayout(Core::Document const& document);

    // Union of the crop and every object's rect (the objects' bounding box).
    Core::Rect ObjectsBounds(Core::Document const& document);

    // The colour behind the screenshot (nullopt = clear, which also turns the shadow off).
    std::optional<Core::Backdrop::Rgb> CanvasColor(Core::Document const& document);

    // The backdrop layer in canvas pixels: nothing when it is hidden. `forExport` re-renders the
    // mesh at the canvas size; on screen the 68 × 68 mesh is stretched.
    void DrawBackdrop(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session,
                      Core::Document const& document, Core::Backdrop::Layout const& layout, bool forExport);

    // Rounded clip of the content (screenshot + canvas expansion) with the inner radius; null when
    // there is no radius.
    winrt::Microsoft::Graphics::Canvas::Geometry::CanvasGeometry ContentClip(
        winrt::Microsoft::Graphics::Canvas::ICanvasResourceCreator const& device, Core::Backdrop::Layout const& layout);

    // For the editor canvas: the backdrop behind the image, then the canvas colour and `drawContent`
    // (the editor's screenshot and objects) inside the inner-radius clip. `context` maps image pixels
    // to DIPs as for Render::Draw. With the backdrop off it only calls drawContent.
    void DrawPreview(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session,
                     Core::Document const& document, Render::Context const& context,
                     std::function<void()> const& drawContent);
}
