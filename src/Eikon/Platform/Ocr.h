#pragma once

// Text and QR recognition: ZXing-C++ reads QR codes first (only QR);
// when there is none, Windows.Media.Ocr reads the text with the engines
// Core::OcrText::EngineLanguages picks for primaryOCRLang. The result is text assembly
// (lines joined with "\n", full-width punctuation cleaned up); QR text is returned as decoded.

#include "Core/Bitmap.h"

#include <winrt/Windows.Foundation.h>

#include "Core/Geometry.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Eikon
{
    // Windows.Media.Ocr text recognition for the Library index. Runs off the UI thread; returns "" when OCR is unavailable or nothing
    // was recognised. Lines are joined with '\n'.
    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RecognizeTextAsync(std::vector<std::uint8_t> png);

    // The word rectangles (in the image's pixels) of a BGRA8 premultiplied image, for the "-text"
    // conceal modes. Recognises off the UI thread and calls `done` back on the
    // calling thread; an empty list when OCR is unavailable or finds nothing.
    winrt::fire_and_forget RecognizeWordBoxes(std::vector<std::uint8_t> bgra, std::int32_t width, std::int32_t height,
                                              std::function<void(std::vector<Core::Rect>)> done);
    namespace Ocr
    {
        // Creates the engines for primaryOCRLang and runs each once, off the UI thread.
        // Call at launch and whenever primaryOCRLang changes.
        void WarmUp();
        // guard: the warm-up has finished.
        bool IsReady() noexcept;

        // Runs the engine on a background thread: "" for images under 4 px, the QR payloads (joined with
        // "\n", trimmed) when there are any, else the recognised text after clean-up.
        // `scale` is the capture's display scale (text shorter than 6 pt is ignored).
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RecognizeAsync(std::shared_ptr<Core::Bitmap const> image);
    }
}
