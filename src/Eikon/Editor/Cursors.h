#pragma once

// The editor's cursors, by name. Windows' own cursors wherever it has the shape, so they follow the user's pointer
// scheme, size and colour: "move" and "closed-hand" SizeAll, "l-resize"/"r-resize" SizeWestEast,
// "t-resize"/"b-resize" SizeNorthSouth, "tl-resize"/"br-resize" SizeNorthwestSoutheast,
// "bl-resize"/"tr-resize" SizeNortheastSouthwest, "point" and "default" Arrow, "open-hand" Hand,
// "crosshair" Cross, "ibeam" IBeam. "zoom" and "pen" have no system shape: they are drawn
// (Assets/Cursors, tools/generate_icons.py) at the standard cursor size for the display's scale;
// Windows enlarges them with the Mouse pointer size setting as it does its own.

#include <string_view>

namespace Eikon::Editor
{
    // Sets the cursor shown over `element` (UIElement.ProtectedCursor). Repeated calls with the same
    // name are cheap.
    void SetCursor(winrt::Microsoft::UI::Xaml::UIElement const& element, std::wstring_view name);
}
