#pragma once

// Scrolling capture engines: auto-scroll, where Eikon scrolls the
// view under the selected region with wheel events, and manual mode, where the user
// scrolls and Eikon samples the region every 0.3 s. Both select the region with the
// RegionSelector in Scrolling mode and keep its overlay up (click-through) until they finish.
// The maths lives in Core/Stitch. UI thread only.

#include "Core/Bitmap.h"

#include <functional>
#include <optional>

namespace Eikon::ScrollCapture
{
    // Called on the UI thread with the stitched image (nullopt: nothing to open).
    using Done = std::function<void(std::optional<Core::Bitmap> image)>;

    // Auto-scrolling capture with the custom grabber. direction -1 = down,
    // +1 = up. Ignored while an auto-scroll capture is running (its own key press stops it).
    void StartAuto(int direction, Done done);

    // Starts manual scrolling capture. A running manual session is stopped without
    // calling its callback.
    void StartManual(Done done);

    bool IsRunning() noexcept;
}
