#pragma once

// Dragging a screenshot out of the thumbnail or a pin as a file: the image is written to %TEMP%\Eikon\<name>.png and dragged with CF_HDROP and
// the 44-ico-draggable-file drag image. Modal, UI thread.

#include "Core/Bitmap.h"

#include <chrono>
#include <string>

namespace Eikon::FileDrag
{
    // Default save name "SCR-%Y%m%d-%t" for `time`; %t is four
    // letters for the time of day at 1/4 s resolution.
    std::wstring DefaultFileName(std::chrono::system_clock::time_point time);

    // Runs the drag. Returns false when the image could not be written (nothing was dragged).
    bool DragImage(Core::Bitmap const& image, std::wstring const& fileName, HWND source);
}
