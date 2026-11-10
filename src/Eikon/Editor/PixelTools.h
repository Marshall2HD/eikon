#pragma once

// Pixel tools inside the editor: the ruler (keys 1/2/arrows, wheel
// sensitivity, click to imprint), the colour under the cursor and its copy keys, and the contrast
// badge above the crop selection. ShotView owns one instance and forwards input to it before its own
// handling; a `true` return means the event was consumed. Drawing happens after the objects.
//
// Points passed in are image pixels of the document's source image (y down, like the objects),
// computed from the pointer (: device x − 1, then the Framer's
// view→image mapping). PixelTools converts them once into crop-relative y-up space, so the
// single-pixel picker's `height − y` row and the ruler's `height − 1 − y` row
// stay.

#include "Core/Color.h"
#include "Core/Contrast.h"
#include "Core/Document.h"
#include "Core/KeyShortcut.h"
#include "Editor/Render.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace Eikon::Editor
{
    class PixelTools
    {
    public:
        struct Host
        {
            std::function<void()> invalidate;                 // repaint the canvas
            std::function<void()> hideEditor;                 // colour copy hides the editor
            std::function<std::optional<Core::Rect>()> selection;  // current crop selection, image px
            std::function<void(Core::ObjectPtr)> addObject;   // imprint an ObjRuler/ObjLine
            // Canvas size in DIPs: the ruler label and badge positions use it.
            std::function<Core::Size()> viewSize;
            // ObjectHandlers.active != nil: an annotation is selected (no ruler, no Tab copy).
            std::function<bool()> objectActive;
            // A helper panel or the backdrop panel is open: Tab is swallowed.
            std::function<bool()> helperOpen;
            // The colour readout changed (info bar label and swatch).
            std::function<void()> readoutChanged;
            // Inline-help popover: title and message.
            std::function<void(std::wstring const&, std::wstring const&)> showInlineHelp;
        };

        explicit PixelTools(Host host);
        ~PixelTools();

        void SetDocument(std::shared_ptr<Core::Document> document);

        // `key` is a virtual-key code; Modifiers::Alt is Option (guide), Shift its Shift.
        // Ruler keys are never consumed.
        bool KeyDown(std::uint32_t key, Modifiers modifiers);
        bool KeyUp(std::uint32_t key, Modifiers modifiers);
        void PointerMoved(Core::Point imagePoint);
        // Primary button. Imprints the live ruler when it is on.
        bool PointerPressed(Core::Point imagePoint, Modifiers modifiers);
        // `deltaY` in wheel notches (WM_MOUSEWHEEL delta / 120, up = positive). Consumed while the
        // ruler is on: it changes the sensitivity instead of zooming.
        bool Wheel(double deltaY);
        void ZoomChanged();   // the ruler hides when the zoom changes

        // The crop selection: a mouse-down on it hides the badge (contrast = nil) and a change that is
        // done (mouse-up, keyboard adjust, undo) computes the contrast in the background.
        void SelectionPressed();
        void SelectionChanged();

        // The live colour readout for the info bar, already formatted per colorFormat; the
        // label width is Core::ColorFormats()[Core::ColorFormatIndex(colorFormat)].labelWidth.
        std::wstring ColorReadout() const;
        Core::Color ReadoutColor() const;

        // Ruler, guides and the contrast badge (drawn above `selection` when there is one).
        void Draw(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session, Render::Context const& context);
        // Click on the contrast badge opens its help; Alt+click toggles contrastType (:
        // the Option bit selects the toggle). `canvasPoint` in DIPs.
        bool BadgePressed(Core::Point canvasPoint, Modifiers modifiers);
        // The crop hint line's text while the pointer is over the badge ("WCAG 2.0 Contrast Ratio" /
        // "APCA Contrast Ratio"), else empty.
        std::wstring BadgeHint() const;

    private:
        // Measure at the stored cursor, turn on, restart the 0.8 s hide timer.
        void Measure(bool vertical, std::int64_t persistence, bool guide);
        // The live ruler becomes an ObjRuler (or an ObjLine for a guide).
        void Imprint();
        // Clipboard, hide the editor, "Copied to the clipboard" with `color` as the swatch.
        void CopyColor(std::wstring const& text, std::wstring const& title, Core::Color color);

        struct State;
        Host m_host;
        std::unique_ptr<State> m_state;
        std::shared_ptr<Core::Document> m_document;
    };
}
