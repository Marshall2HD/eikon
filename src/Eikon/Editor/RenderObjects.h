#pragma once

// Object rendering services beyond Render::Draw: the shared spotlight
// curtain, the ObjectEnvironment the objects read while they are edited, and the DirectWrite text
// measurer ObjText::Layout uses. UI thread only.

#include "Core/Document.h"
#include "Editor/Render.h"

#include <winrt/Microsoft.Graphics.Canvas.Geometry.h>

#include <functional>

namespace Eikon::Editor::Render
{
    // Keeps Core::Environment() in step with the canvas (Framer dpr and zoom, image bounds and pixels,
    // the counter) and installs the text measurer. `redraw` is called when background work (OCR for
    // the "-text" conceal modes) has a result to show.
    void SyncEnvironment(std::shared_ptr<Core::Document> const& document, Context const& context, std::function<void()> redraw);

    // The curtain of every ObjSpotlight (ObjectHandlers.lcurtain): the image minus each spotlight's
    // shape, filled black at the `transparency` preference. Nothing when there is no spotlight.
    void DrawSpotlights(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session,
                        std::vector<Core::ObjectPtr> const& objects, Context const& context, Core::Rect const& image);

    // ---- Shared with RenderText.cpp ----
    winrt::Microsoft::Graphics::Canvas::Geometry::CanvasGeometry ToGeometry(
        winrt::Microsoft::Graphics::Canvas::ICanvasResourceCreator const& device, Core::Path const& path, bool evenOdd = false);
    winrt::Windows::UI::Color ToColor(Core::Color color, double alpha = 1.0);
    void DrawShapes(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session, std::vector<Core::Shape> const& shapes);
    void DrawText(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session, Core::ProtoObject const& object, Context const& context);
    void DrawCounter(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session, Core::ProtoObject const& object, Context const& context);
    // DirectWrite measurement of an ObjText (installed as Environment().measureText).
    Core::Size MeasureText(Core::ProtoObject const& text, double maxWidth);
}
