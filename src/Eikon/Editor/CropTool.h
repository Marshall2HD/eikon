#pragma once

// The crop selection (CropTool singleton,; mouse /
// Keys). It holds the selection rect in image pixels,
// drags it (create, move, resize by zone), animates programmatic changes over 0.15 s and draws the
// marquee. ShotView owns one and routes input to it.

#include "Core/CropGeometry.h"
#include "Core/Framer.h"
#include "Editor/Render.h"

#include <chrono>
#include <optional>
#include <string>

namespace Eikon::Editor
{
    class CropTool
    {
    public:
        // What a mouse-up asked for.
        enum class UpResult
        {
            None,          // nothing to do (no drag in progress)
            Changed,       // the selection was created, moved or resized
            Cleared,       // a quick tiny click removed it
            ToggleUnits,   // a click on the size label: toggle realPixels ( "units")
            AdjustEdge,    // a quick click on an edge: smart-adjust that edge
        };

        bool HasSelection() const noexcept { return m_selection.has_value(); }
        std::optional<Core::Rect> Selection() const noexcept { return m_selection; }
        // Sets the selection (image pixels); `animate` tweens the marquee for 0.15 s.
        void SetSelection(Core::Rect rect, bool animate);
        // resetCutter.
        void Reset() noexcept;

        // The zone under `view` (view pixels): "units" over the size label, a CropZone otherwise.
        std::wstring_view HitTest(Core::Framer const& framer, Core::Point view) const;

        // Starts a drag. `zone` from HitTest; "default" starts a new selection ("select").
        void MouseDown(std::wstring_view zone, Core::Point image, double timeSeconds);
        // `bounds`: the image (selection stays inside it).
        void MouseDragged(Core::Point image, Core::Rect bounds);
        UpResult MouseUp(Core::Point image, double timeSeconds, double dpr, bool realPixels);
        bool IsDragging() const noexcept { return m_state != L"default"; }
        // The edge that AdjustEdge refers to: "left", "right", "top" or "bottom".
        std::wstring const& AdjustEdgeName() const noexcept { return m_adjustEdge; }

        // Draws the marquee over `area` (the canvas in DIPs). `badgeHint`, when not empty, replaces the
        // hint line (the pointer is over the contrast badge). Returns true while animating.
        bool Draw(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session, Render::Context const& context,
                  winrt::Windows::Foundation::Rect const& area, double imageDpr, bool realPixels, bool hoverUnits,
                  std::wstring_view badgeHint = {});

    private:
        std::optional<Core::Rect> m_selection;
        std::wstring m_state{ L"default" };   // "default", "select", "units" or a zone
        Core::Point m_downImage;
        Core::Rect m_downRect;
        double m_downTime = 0;
        std::wstring m_adjustEdge;
        // Animation of programmatic changes.
        Core::Rect m_animFrom;
        std::chrono::steady_clock::time_point m_animStart{};
        bool m_animating = false;
        // The size label's frame in DIPs from the last draw.
        winrt::Windows::Foundation::Rect m_sizeLabel{};
    };
}
