#include "pch.h"
#include "Platform/ImageLoader.h"

#include "Platform/ImageCodec.h"
#include "Strings.h"

#include <wil/com.h>
#include <shobjidl_core.h>

#include <cstring>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace Eikon::ImageLoader
{
    namespace
    {
        void Log(std::wstring_view message)
        {
            std::wstring line{ L"[Eikon] " };
            line.append(message);
            line.push_back(L'\n');
            ::OutputDebugStringW(line.c_str());
        }

        wil::com_ptr<IWICImagingFactory> Factory()
        {
            wil::com_ptr<IWICImagingFactory> factory;
            if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()))))
            {
                return nullptr;
            }
            return factory;
        }

        std::optional<Core::Bitmap> FromSource(IWICImagingFactory* factory, IWICBitmapSource* source)
        {
            wil::com_ptr<IWICFormatConverter> converter;
            if (FAILED(factory->CreateFormatConverter(converter.put())) ||
                FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut)))
            {
                return std::nullopt;
            }
            UINT width = 0;
            UINT height = 0;
            if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0 || width > 32768 || height > 32768)
            {
                return std::nullopt;
            }
            Core::Bitmap bitmap;
            bitmap.width = static_cast<std::int32_t>(width);
            bitmap.height = static_cast<std::int32_t>(height);
            bitmap.pixels.resize(bitmap.Stride() * height);
            if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(bitmap.Stride()), static_cast<UINT>(bitmap.pixels.size()), bitmap.pixels.data())))
            {
                return std::nullopt;
            }
            return bitmap;
        }

        std::optional<Core::Bitmap> FromDecoder(IWICImagingFactory* factory, IWICBitmapDecoder* decoder)
        {
            wil::com_ptr<IWICBitmapFrameDecode> frame;
            if (FAILED(decoder->GetFrame(0, frame.put())))
            {
                return std::nullopt;
            }
            return FromSource(factory, frame.get());
        }

        std::vector<std::uint8_t> GlobalBytes(HANDLE handle)
        {
            std::vector<std::uint8_t> bytes;
            if (!handle)
            {
                return bytes;
            }
            SIZE_T const size = ::GlobalSize(handle);
            if (void const* data = ::GlobalLock(handle))
            {
                auto const* begin = static_cast<std::uint8_t const*>(data);
                bytes.assign(begin, begin + size);
                ::GlobalUnlock(handle);
            }
            return bytes;
        }

        // "*.png;*.bmp;..." for every installed WIC decoder.
        std::wstring DecoderPatterns(IWICImagingFactory* factory)
        {
            std::wstring patterns;
            wil::com_ptr<IEnumUnknown> components;
            if (FAILED(factory->CreateComponentEnumerator(WICDecoder, WICComponentEnumerateDefault, components.put())))
            {
                return L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff";
            }
            wil::com_ptr<IUnknown> unknown;
            ULONG fetched = 0;
            while (components->Next(1, unknown.put(), &fetched) == S_OK && fetched == 1)
            {
                auto info = unknown.try_query<IWICBitmapDecoderInfo>();
                if (!info)
                {
                    continue;
                }
                UINT length = 0;
                if (FAILED(info->GetFileExtensions(0, nullptr, &length)) || length == 0)
                {
                    continue;
                }
                std::wstring extensions(length, L'\0');
                if (FAILED(info->GetFileExtensions(length, extensions.data(), &length)))
                {
                    continue;
                }
                extensions.resize(wcsnlen(extensions.c_str(), extensions.size()));
                std::size_t start = 0;
                while (start < extensions.size())
                {
                    std::size_t end = extensions.find(L',', start);
                    if (end == std::wstring::npos)
                    {
                        end = extensions.size();
                    }
                    std::wstring extension = extensions.substr(start, end - start);
                    if (!extension.empty())
                    {
                        std::wstring const pattern = L"*" + extension;
                        if (patterns.find(pattern + L";") == std::wstring::npos)
                        {
                            patterns += pattern + L";";
                        }
                    }
                    start = end + 1;
                }
            }
            if (!patterns.empty())
            {
                patterns.pop_back();
            }
            return patterns;
        }
    }

    std::optional<Core::Bitmap> DecodeBytes(std::span<std::uint8_t const> bytes)
    {
        auto factory = Factory();
        if (!factory || bytes.empty())
        {
            return std::nullopt;
        }
        wil::com_ptr<IWICStream> stream;
        if (FAILED(factory->CreateStream(stream.put())) ||
            FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))))
        {
            return std::nullopt;
        }
        wil::com_ptr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put())))
        {
            return std::nullopt;
        }
        return FromDecoder(factory.get(), decoder.get());
    }

    std::optional<Core::Bitmap> DecodeFile(std::wstring const& path)
    {
        auto factory = Factory();
        if (!factory)
        {
            return std::nullopt;
        }
        wil::com_ptr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put())))
        {
            return std::nullopt;
        }
        return FromDecoder(factory.get(), decoder.get());
    }

    Core::Bitmap Scale(Core::Bitmap const& image, int width, int height)
    {
        if (image.width == width && image.height == height)
        {
            return image;
        }
        auto factory = Factory();
        if (!factory || image.IsEmpty() || width <= 0 || height <= 0)
        {
            return {};
        }
        wil::com_ptr<IWICBitmap> source;
        wil::com_ptr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapFromMemory(static_cast<UINT>(image.width), static_cast<UINT>(image.height), GUID_WICPixelFormat32bppPBGRA,
                                                   static_cast<UINT>(image.Stride()), static_cast<UINT>(image.pixels.size()),
                                                   const_cast<BYTE*>(image.pixels.data()), source.put())) ||
            FAILED(factory->CreateBitmapScaler(scaler.put())) ||
            FAILED(scaler->Initialize(source.get(), static_cast<UINT>(width), static_cast<UINT>(height), WICBitmapInterpolationModeFant)))
        {
            return {};
        }
        Core::Bitmap out;
        out.width = width;
        out.height = height;
        out.dpr = image.dpr;
        out.pixels.resize(out.Stride() * static_cast<std::size_t>(height));
        if (FAILED(scaler->CopyPixels(nullptr, static_cast<UINT>(out.Stride()), static_cast<UINT>(out.pixels.size()), out.pixels.data())))
        {
            return {};
        }
        return out;
    }

    std::optional<Core::Bitmap> ReadClipboard(HWND owner)
    {
        // Another process may hold the clipboard for a moment.
        bool opened = false;
        for (int attempt = 0; attempt < 10 && !opened; ++attempt)
        {
            opened = ::OpenClipboard(owner) != FALSE;
            if (!opened)
            {
                ::Sleep(10);
            }
        }
        if (!opened)
        {
            Log(L"can't open the clipboard");
            return std::nullopt;
        }
        auto close = wil::scope_exit([] { ::CloseClipboard(); });

        static UINT const png = ::RegisterClipboardFormatW(L"PNG");
        if (png && ::IsClipboardFormatAvailable(png))
        {
            auto const bytes = GlobalBytes(::GetClipboardData(png));
            if (auto bitmap = DecodeBytes(bytes))
            {
                return bitmap;
            }
        }
        for (UINT format : { static_cast<UINT>(CF_DIBV5), static_cast<UINT>(CF_DIB) })
        {
            if (!::IsClipboardFormatAvailable(format))
            {
                continue;
            }
            auto const dib = GlobalBytes(::GetClipboardData(format));
            auto const encoded = ImageCodec::DibToPng(dib);
            if (auto bitmap = DecodeBytes(encoded))
            {
                return bitmap;
            }
        }
        if (::IsClipboardFormatAvailable(CF_HDROP))
        {
            if (auto drop = static_cast<HDROP>(::GetClipboardData(CF_HDROP)))
            {
                if (::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0) == 1)
                {
                    UINT const length = ::DragQueryFileW(drop, 0, nullptr, 0);
                    std::wstring path(length + 1, L'\0');
                    ::DragQueryFileW(drop, 0, path.data(), length + 1);
                    path.resize(length);
                    if (auto bitmap = DecodeFile(path))
                    {
                        return bitmap;
                    }
                }
            }
        }
        return std::nullopt;
    }

    std::optional<std::wstring> ChooseImageFile(HWND owner, std::wstring const& title)
    {
        wil::com_ptr<IFileOpenDialog> dialog;
        if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()))))
        {
            return std::nullopt;
        }
        dialog->SetTitle(title.c_str());
        FILEOPENDIALOGOPTIONS options = 0;
        dialog->GetOptions(&options);
        // Single file, no folders, hidden files not shown (the default).
        options |= FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM;
        options &= ~(FOS_ALLOWMULTISELECT | FOS_PICKFOLDERS | FOS_FORCESHOWHIDDEN);
        dialog->SetOptions(options);
        std::wstring patterns = L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff";
        if (auto factory = Factory())
        {
            patterns = DecoderPatterns(factory.get());
        }
        std::wstring const name = Strings::Get(L"Capture_OpenImageFilter");
        COMDLG_FILTERSPEC const filter{ name.c_str(), patterns.c_str() };
        dialog->SetFileTypes(1, &filter);
        if (FAILED(dialog->Show(owner)))
        {
            return std::nullopt;   // cancelled
        }
        wil::com_ptr<IShellItem> item;
        if (FAILED(dialog->GetResult(item.put())))
        {
            return std::nullopt;
        }
        wil::unique_cotaskmem_string path;
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path)
        {
            return std::nullopt;
        }
        return std::wstring{ path.get() };
    }
}
