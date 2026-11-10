#include "pch.h"
#include "Core/QrCode.h"

#include "Core/S3.h"

#include <ReadBarcode.h>

#include <vector>

namespace Eikon::Core::QrCode
{
    namespace
    {
        // CharacterSet.whitespacesAndNewlines (Unicode White_Space).
        bool IsWhitespaceOrNewline(wchar_t c) noexcept
        {
            return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
                   c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
        }
    }

    std::wstring Read(Bitmap const& image)
    {
        if (image.IsEmpty())
        {
            return {};
        }
        try
        {
            ZXing::ImageView const view{ image.pixels.data(), image.width, image.height, ZXing::ImageFormat::BGRA, static_cast<int>(image.Stride()) };
            // High accuracy: tryHarder + tryRotate.
            ZXing::ReaderOptions options;
            options.formats(ZXing::BarcodeFormats(std::vector<ZXing::BarcodeFormat>{ ZXing::BarcodeFormat::QRCodeModel1, ZXing::BarcodeFormat::QRCodeModel2 }));
            options.tryHarder(true);
            options.tryRotate(true);
            std::wstring joined;
            for (auto const& barcode : ZXing::ReadBarcodes(view, options))
            {
                // ZXing reports a regular QR code as the QRCode symbology (Model 1/2 only when it
                // tells them apart); Micro QR and rMQR are other symbols, not read here.
                auto const format = barcode.format();
                bool const qr = format == ZXing::BarcodeFormat::QRCode || format == ZXing::BarcodeFormat::QRCodeModel1 ||
                                format == ZXing::BarcodeFormat::QRCodeModel2;
                if (!barcode.isValid() || !qr)
                {
                    continue;
                }
                if (!joined.empty())
                {
                    joined += L'\n';
                }
                joined += S3::FromUtf8(barcode.text());
            }
            // trimmingCharacters(in: .whitespacesAndNewlines)
            std::size_t first = 0;
            std::size_t last = joined.size();
            while (first < last && IsWhitespaceOrNewline(joined[first]))
            {
                ++first;
            }
            while (last > first && IsWhitespaceOrNewline(joined[last - 1]))
            {
                --last;
            }
            return joined.substr(first, last - first);
        }
        catch (...)
        {
            return {};
        }
    }
}
