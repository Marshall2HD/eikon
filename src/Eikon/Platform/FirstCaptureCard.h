#pragma once

// The first-area-capture question as Eikon's Candy card: "Where should captures
// go?" with two picture choices, "Small preview" and "Open the editor" (selected by default), and a
// Continue button. A small layered window drawn with Direct2D on the colour fields, centred on the
// monitor under the pointer. Keyboard: ←/→ (and Tab) move the choice, Enter or Space continues, Esc
// takes the default ("editor"). The answer is the areaCaptureMode value: "preview" or "editor".

#include <functional>
#include <string>

namespace Eikon::FirstCaptureCard
{
    // Shows the card (one at a time) and calls `done` with the answer once the user continues.
    void Ask(std::function<void(std::wstring const& mode)> done);
    bool IsOpen() noexcept;
}
