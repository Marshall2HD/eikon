#include "pch.h"
#include "Platform/ImageCodec.h"

#include <bit>
#include <cstring>
#include <limits>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace Eikon::ImageCodec
{
    namespace
    {
        using Bytes = std::vector<std::uint8_t>;

        winrt::com_ptr<IWICImagingFactory> CreateFactory() noexcept
        {
            winrt::com_ptr<IWICImagingFactory> factory;
            HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
            if (hr == CO_E_NOTINITIALIZED)
            {
                // Worker thread without COM: join the MTA (intentionally never uninitialized).
                (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                factory = nullptr;
                hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
            }
            if (FAILED(hr))
            {
                OutputDebugStringW(L"Eikon: creating the WIC imaging factory failed\n");
                return nullptr;
            }
            return factory;
        }

        Bytes ReadStream(IStream* stream) noexcept
        {
            ULARGE_INTEGER size{};
            if (FAILED(IStream_Size(stream, &size)) || size.QuadPart == 0 || size.QuadPart > (std::numeric_limits<ULONG>::max)())
            {
                return {};
            }
            if (FAILED(IStream_Reset(stream)))
            {
                return {};
            }
            Bytes bytes;
            try
            {
                bytes.resize(static_cast<std::size_t>(size.QuadPart));
            }
            catch (...)
            {
                return {};
            }
            if (FAILED(IStream_Read(stream, bytes.data(), static_cast<ULONG>(bytes.size()))))
            {
                return {};
            }
            return bytes;
        }

        Bytes EncodePng(IWICImagingFactory* factory, IWICBitmapSource* source, double dpi = 0) noexcept
        {
            if (!factory || !source)
            {
                return {};
            }

            winrt::com_ptr<IWICBitmapSource> converted;
            if (FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, source, converted.put())))
            {
                return {};
            }

            winrt::com_ptr<IStream> stream;
            stream.attach(SHCreateMemStream(nullptr, 0));
            if (!stream)
            {
                return {};
            }

            winrt::com_ptr<IWICBitmapEncoder> encoder;
            if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())) ||
                FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)))
            {
                return {};
            }

            winrt::com_ptr<IWICBitmapFrameEncode> frame;
            winrt::com_ptr<IPropertyBag2> properties;
            if (FAILED(encoder->CreateNewFrame(frame.put(), properties.put())) ||
                FAILED(frame->Initialize(properties.get())))
            {
                return {};
            }

            UINT width = 0;
            UINT height = 0;
            if (FAILED(converted->GetSize(&width, &height)) || width == 0 || height == 0)
            {
                return {};
            }

            WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
            if (dpi > 0 && FAILED(frame->SetResolution(dpi, dpi)))
            {
                return {};
            }
            if (FAILED(frame->SetSize(width, height)) ||
                FAILED(frame->SetPixelFormat(&format)) ||
                FAILED(frame->WriteSource(converted.get(), nullptr)) ||
                FAILED(frame->Commit()) ||
                FAILED(encoder->Commit()))
            {
                return {};
            }

            return ReadStream(stream.get());
        }

        Bytes EncodeBgra(IWICImagingFactory* factory, UINT width, UINT height, Bytes& pixels) noexcept
        {
            winrt::com_ptr<IWICBitmap> bitmap;
            if (FAILED(factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppBGRA, width * 4,
                    static_cast<UINT>(pixels.size()), pixels.data(), bitmap.put())))
            {
                return {};
            }
            return EncodePng(factory, bitmap.get());
        }

        std::uint32_t ReadU32(std::uint8_t const* p) noexcept
        {
            std::uint32_t v;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }

        // Extracts the channel selected by `mask` from `pixel`, scaled to 8 bits.
        std::uint8_t Channel(std::uint32_t pixel, std::uint32_t mask) noexcept
        {
            if (mask == 0)
            {
                return 0;
            }
            const int shift = std::countr_zero(mask);
            const int bits = std::popcount(mask);
            std::uint32_t value = (pixel & mask) >> shift;
            if (bits > 8)
            {
                value >>= (bits - 8);
            }
            else if (bits < 8)
            {
                const std::uint32_t max = (1u << bits) - 1u;
                value = max ? (value * 255u) / max : 0u;
            }
            return static_cast<std::uint8_t>(value & 0xFF);
        }

        // WIC fallback: prepend a BITMAPFILEHEADER and let the BMP decoder handle it.
        Bytes DecodeWithWic(IWICImagingFactory* factory, std::span<std::uint8_t const> dib, std::size_t pixelOffset) noexcept
        {
            BITMAPFILEHEADER fileHeader{};
            fileHeader.bfType = 0x4D42; // "BM"
            fileHeader.bfSize = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + dib.size());
            fileHeader.bfOffBits = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + pixelOffset);

            Bytes file;
            try
            {
                file.resize(sizeof(BITMAPFILEHEADER) + dib.size());
            }
            catch (...)
            {
                return {};
            }
            std::memcpy(file.data(), &fileHeader, sizeof(fileHeader));
            std::memcpy(file.data() + sizeof(fileHeader), dib.data(), dib.size());

            winrt::com_ptr<IStream> stream;
            stream.attach(SHCreateMemStream(file.data(), static_cast<UINT>(file.size())));
            if (!stream)
            {
                return {};
            }

            winrt::com_ptr<IWICBitmapDecoder> decoder;
            if (FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put())))
            {
                return {};
            }
            winrt::com_ptr<IWICBitmapFrameDecode> frame;
            if (FAILED(decoder->GetFrame(0, frame.put())))
            {
                return {};
            }
            return EncodePng(factory, frame.get());
        }
    }

    std::vector<std::uint8_t> DibToPng(std::span<std::uint8_t const> dib) noexcept
    {
        try
        {
            if (dib.size() < sizeof(BITMAPINFOHEADER))
            {
                return {};
            }

            BITMAPINFOHEADER header{};
            std::memcpy(&header, dib.data(), sizeof(header));
            if (header.biSize < sizeof(BITMAPINFOHEADER) || header.biSize > dib.size() ||
                header.biWidth <= 0 || header.biHeight == 0 || header.biHeight == (std::numeric_limits<LONG>::min)())
            {
                return {};
            }

            const std::size_t headerSize = header.biSize;

            // Compressed payloads: PNG data can be returned as is.
            if (header.biCompression == BI_PNG)
            {
                std::size_t size = dib.size() - headerSize;
                if (header.biSizeImage != 0 && header.biSizeImage <= size)
                {
                    size = header.biSizeImage;
                }
                return Bytes(dib.begin() + headerSize, dib.begin() + headerSize + size);
            }

            auto factory = CreateFactory();
            if (!factory)
            {
                return {};
            }

            const std::uint32_t width = static_cast<std::uint32_t>(header.biWidth);
            const bool topDown = header.biHeight < 0;
            const std::uint32_t height = static_cast<std::uint32_t>(topDown ? -header.biHeight : header.biHeight);
            const std::uint32_t bpp = header.biBitCount;

            constexpr DWORD BiAlphaBitfields = 6;
            std::size_t maskBytes = 0;
            if (headerSize == sizeof(BITMAPINFOHEADER))
            {
                if (header.biCompression == BI_BITFIELDS)
                {
                    maskBytes = 12;
                }
                else if (header.biCompression == BiAlphaBitfields)
                {
                    maskBytes = 16;
                }
            }
            std::size_t colorEntries = header.biClrUsed;
            if (colorEntries == 0 && bpp <= 8)
            {
                colorEntries = std::size_t{ 1 } << bpp;
            }
            if (colorEntries > 256 && bpp <= 8)
            {
                colorEntries = 256;
            }
            std::size_t pixelOffset = headerSize + maskBytes + colorEntries * sizeof(RGBQUAD);

            const bool direct = (bpp == 24 && header.biCompression == BI_RGB) ||
                (bpp == 32 && (header.biCompression == BI_RGB || header.biCompression == BI_BITFIELDS || header.biCompression == BiAlphaBitfields));
            if (!direct)
            {
                return DecodeWithWic(factory.get(), dib, pixelOffset);
            }

            const std::uint64_t stride = ((static_cast<std::uint64_t>(width) * bpp + 31) / 32) * 4;
            const std::uint64_t pixelBytes = stride * height;
            if (pixelBytes > 0x7FFFFFFF || static_cast<std::uint64_t>(width) * height * 4 > 0x7FFFFFFF)
            {
                return {};
            }
            if (pixelOffset + pixelBytes > dib.size())
            {
                return {};
            }

            // Some producers put the three colour masks after a V4/V5 header as well; detect that from
            // the payload size (GlobalSize adds at most a few bytes of padding).
            if (headerSize > sizeof(BITMAPINFOHEADER) && header.biCompression == BI_BITFIELDS &&
                dib.size() - (pixelOffset + pixelBytes) >= 12)
            {
                pixelOffset += 12;
            }

            std::uint32_t redMask = 0x00FF0000;
            std::uint32_t greenMask = 0x0000FF00;
            std::uint32_t blueMask = 0x000000FF;
            std::uint32_t alphaMask = 0;
            const bool bitfields = header.biCompression != BI_RGB;
            if (bitfields)
            {
                std::uint8_t const* masks = dib.data() + sizeof(BITMAPINFOHEADER); // same offset for V4/V5 headers
                redMask = ReadU32(masks);
                greenMask = ReadU32(masks + 4);
                blueMask = ReadU32(masks + 8);
                if (headerSize >= sizeof(BITMAPINFOHEADER) + 16 || header.biCompression == BiAlphaBitfields)
                {
                    alphaMask = ReadU32(masks + 12);
                }
            }
            else if (bpp == 32)
            {
                alphaMask = 0xFF000000; // BI_RGB: the high byte is "reserved"; treated as alpha if used
            }

            Bytes pixels(static_cast<std::size_t>(width) * height * 4);
            bool anyAlpha = false;
            std::uint8_t const* source = dib.data() + pixelOffset;
            for (std::uint32_t y = 0; y < height; ++y)
            {
                const std::uint32_t sourceRow = topDown ? y : (height - 1 - y);
                std::uint8_t const* src = source + static_cast<std::size_t>(sourceRow * stride);
                std::uint8_t* dst = pixels.data() + static_cast<std::size_t>(y) * width * 4;
                for (std::uint32_t x = 0; x < width; ++x, dst += 4)
                {
                    if (bpp == 24)
                    {
                        dst[0] = src[x * 3 + 0];
                        dst[1] = src[x * 3 + 1];
                        dst[2] = src[x * 3 + 2];
                        dst[3] = 0xFF;
                    }
                    else
                    {
                        const std::uint32_t pixel = ReadU32(src + x * 4);
                        dst[0] = Channel(pixel, blueMask);
                        dst[1] = Channel(pixel, greenMask);
                        dst[2] = Channel(pixel, redMask);
                        dst[3] = Channel(pixel, alphaMask);
                        anyAlpha = anyAlpha || dst[3] != 0;
                    }
                }
            }

            // No alpha information (all zero): the image is opaque.
            if (bpp == 32 && !anyAlpha)
            {
                for (std::size_t i = 3; i < pixels.size(); i += 4)
                {
                    pixels[i] = 0xFF;
                }
            }

            return EncodeBgra(factory.get(), width, height, pixels);
        }
        catch (...)
        {
            return {};
        }
    }

    std::vector<std::uint8_t> PngToDibV5(std::span<std::uint8_t const> png) noexcept
    {
        try
        {
            if (png.empty())
            {
                return {};
            }
            auto factory = CreateFactory();
            if (!factory)
            {
                return {};
            }

            winrt::com_ptr<IStream> stream;
            stream.attach(SHCreateMemStream(png.data(), static_cast<UINT>(png.size())));
            if (!stream)
            {
                return {};
            }
            winrt::com_ptr<IWICBitmapDecoder> decoder;
            if (FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put())))
            {
                return {};
            }
            winrt::com_ptr<IWICBitmapFrameDecode> frame;
            if (FAILED(decoder->GetFrame(0, frame.put())))
            {
                return {};
            }
            winrt::com_ptr<IWICBitmapSource> converted;
            if (FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, frame.get(), converted.put())))
            {
                return {};
            }
            UINT width = 0;
            UINT height = 0;
            if (FAILED(converted->GetSize(&width, &height)) || width == 0 || height == 0)
            {
                return {};
            }
            const std::uint64_t stride = static_cast<std::uint64_t>(width) * 4;
            const std::uint64_t imageSize = stride * height;
            if (imageSize > 0x7FFFFFFF)
            {
                return {};
            }

            Bytes topDown(static_cast<std::size_t>(imageSize));
            if (FAILED(converted->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(imageSize), topDown.data())))
            {
                return {};
            }

            BITMAPV5HEADER header{};
            header.bV5Size = sizeof(BITMAPV5HEADER);
            header.bV5Width = static_cast<LONG>(width);
            header.bV5Height = static_cast<LONG>(height); // positive: bottom-up
            header.bV5Planes = 1;
            header.bV5BitCount = 32;
            header.bV5Compression = BI_BITFIELDS;
            header.bV5SizeImage = static_cast<DWORD>(imageSize);
            header.bV5RedMask = 0x00FF0000;
            header.bV5GreenMask = 0x0000FF00;
            header.bV5BlueMask = 0x000000FF;
            header.bV5AlphaMask = 0xFF000000;
            header.bV5CSType = LCS_sRGB;
            header.bV5Intent = LCS_GM_IMAGES;

            Bytes dib(sizeof(header) + static_cast<std::size_t>(imageSize));
            std::memcpy(dib.data(), &header, sizeof(header));
            std::uint8_t* pixels = dib.data() + sizeof(header);
            for (UINT y = 0; y < height; ++y)
            {
                std::memcpy(pixels + static_cast<std::size_t>(height - 1 - y) * stride,
                    topDown.data() + static_cast<std::size_t>(y) * stride, static_cast<std::size_t>(stride));
            }
            return dib;
        }
        catch (...)
        {
            return {};
        }
    }

    std::vector<std::uint8_t> IconToPng(HICON icon) noexcept
    {
        try
        {
            if (!icon)
            {
                return {};
            }
            auto factory = CreateFactory();
            if (!factory)
            {
                return {};
            }
            winrt::com_ptr<IWICBitmap> bitmap;
            if (FAILED(factory->CreateBitmapFromHICON(icon, bitmap.put())))
            {
                return {};
            }
            return EncodePng(factory.get(), bitmap.get());
        }
        catch (...)
        {
            return {};
        }
    }

    std::vector<std::uint8_t> BitmapToPng(HBITMAP hbitmap) noexcept
    {
        try
        {
            if (!hbitmap)
            {
                return {};
            }
            auto factory = CreateFactory();
            if (!factory)
            {
                return {};
            }
            winrt::com_ptr<IWICBitmap> bitmap;
            if (FAILED(factory->CreateBitmapFromHBITMAP(hbitmap, nullptr, WICBitmapUsePremultipliedAlpha, bitmap.put())))
            {
                return {};
            }
            return EncodePng(factory.get(), bitmap.get());
        }
        catch (...)
        {
            return {};
        }
    }

    std::vector<std::uint8_t> BgraToPng(std::uint32_t width, std::uint32_t height, std::span<std::uint8_t const> pixels,
                                        bool premultiplied, double dpi) noexcept
    {
        try
        {
            if (width == 0 || height == 0 || pixels.size() < static_cast<std::size_t>(width) * height * 4)
            {
                return {};
            }
            auto factory = CreateFactory();
            if (!factory)
            {
                return {};
            }
            winrt::com_ptr<IWICBitmap> bitmap;
            if (FAILED(factory->CreateBitmapFromMemory(width, height,
                    premultiplied ? GUID_WICPixelFormat32bppPBGRA : GUID_WICPixelFormat32bppBGRA, width * 4,
                    static_cast<UINT>(static_cast<std::size_t>(width) * height * 4), const_cast<BYTE*>(pixels.data()), bitmap.put())))
            {
                return {};
            }
            return EncodePng(factory.get(), bitmap.get(), dpi);
        }
        catch (...)
        {
            return {};
        }
    }

    std::vector<std::uint8_t> DecodeFileScaled(wchar_t const* path, std::uint32_t width, std::uint32_t height, bool premultiplied) noexcept
    {
        try
        {
            auto factory = CreateFactory();
            if (!factory || width == 0 || height == 0)
            {
                return {};
            }
            winrt::com_ptr<IWICBitmapDecoder> decoder;
            winrt::com_ptr<IWICBitmapFrameDecode> frame;
            if (FAILED(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put())) ||
                FAILED(decoder->GetFrame(0, frame.put())))
            {
                return {};
            }
            // Scale in premultiplied space so transparent edges don't bleed colour.
            winrt::com_ptr<IWICBitmapSource> premultipliedSource;
            if (FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppPBGRA, frame.get(), premultipliedSource.put())))
            {
                return {};
            }
            winrt::com_ptr<IWICBitmapScaler> scaler;
            if (FAILED(factory->CreateBitmapScaler(scaler.put())) ||
                FAILED(scaler->Initialize(premultipliedSource.get(), width, height, WICBitmapInterpolationModeHighQualityCubic)))
            {
                return {};
            }
            winrt::com_ptr<IWICBitmapSource> output;
            if (FAILED(WICConvertBitmapSource(premultiplied ? GUID_WICPixelFormat32bppPBGRA : GUID_WICPixelFormat32bppBGRA,
                    scaler.get(), output.put())))
            {
                return {};
            }
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
            if (FAILED(output->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())))
            {
                return {};
            }
            return pixels;
        }
        catch (...)
        {
            return {};
        }
    }
}
