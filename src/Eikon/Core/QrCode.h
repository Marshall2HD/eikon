#pragma once

// QR detection for OCR, done with the vendored ZXing-C++ (third_party/zxing-cpp, built with
// the QR reader only). Platform-neutral so the core tests run it on real images.

#include "Core/Bitmap.h"

#include <string>

namespace Eikon::Core::QrCode
{
    // The payload of every QR code (Model 1 or 2; no Micro QR, rMQR or other symbologies) in
    // `image`, joined with "\n" and trimmed of whitespace and newlines. "" when there is none.
    std::wstring Read(Bitmap const& image);
}
