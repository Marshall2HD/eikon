#pragma once

// RegionSelector: a frozen, per-monitor topmost overlay where the user drags
// a rectangle. Lucida uses it for every interactive capture: area,
// window pick (highlights the window under the cursor), OCR and scrolling capture. Win32 layered
// windows drawn with Direct2D, on the UI thread, excluded from capture (WDA_EXCLUDEFROMCAPTURE).

#include "Core/Bitmap.h"
#include "Core/Geometry.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Eikon::Platform
{
    enum class RegionMode
    {
        Area,       // "interactive" and Add Capture
        Window,     // "interactive-window": click a window
        Ocr,        // instant OCR
        Scrolling,  // scrolling capture: the overlay can stay up, click-through, after selection
    };

    struct RegionSelection
    {
        Core::Rect screenRect;                       // physical pixels, virtual-screen coordinates
        HMONITOR monitor = nullptr;
        double dpr = 1.0;                            // the monitor's scale
        std::shared_ptr<Core::Bitmap const> frozen;  // the frozen monitor image the user selected on
        Core::Rect imageRect;                        // screenRect inside `frozen`, in its pixels
        HWND window = nullptr;                       // Window mode: the picked top-level window
    };

    // The overlay left on screen after a Scrolling selection.
    class RegionOverlay
    {
    public:
        virtual ~RegionOverlay() = default;
        virtual void SetStatus(std::wstring const& text) = 0;   // status label
        virtual void Close() = 0;
    };

    struct RegionOptions
    {
        RegionMode mode = RegionMode::Area;
        bool keepOverlay = false;   // Scrolling: keep the dimmed overlay, click-through, until closed
    };

    // Shows the selector. `done` runs on the UI thread with the selection, or nullopt when cancelled
    // (Esc, or a selection under 5×5 pt). With keepOverlay, `overlay` is the overlay still on screen.
    // Area and Ocr freeze each monitor when the mouse enters it; Window and Scrolling are live. A new
    // request closes the selector still open, whose `done` then gets nullopt.
    using RegionDone = std::function<void(std::optional<RegionSelection> selection, std::shared_ptr<RegionOverlay> overlay)>;
    void SelectRegion(RegionOptions const& options, RegionDone done);

    // True while a selector is waiting for a selection (not counting a kept Scrolling overlay).
    bool IsSelectingRegion() noexcept;
}
