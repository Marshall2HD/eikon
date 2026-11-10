#include "pch.h"
#include "Platform/Exporter.h"

#include "Core/FileName.h"
#include "Core/Settings.h"
#include "Platform/ImageCodec.h"
#include "Strings.h"

#include <commdlg.h>
#include <shobjidl.h>

#include <cstring>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace Eikon::Exporter
{
    namespace
    {
        using Bytes = std::vector<std::uint8_t>;

        void Log(std::wstring const& message)
        {
            OutputDebugStringW((L"Eikon: " + message + L"\n").c_str());
        }

        bool SetData(UINT format, Bytes const& bytes)
        {
            if (bytes.empty())
            {
                return false;
            }
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
            if (!memory)
            {
                return false;
            }
            void* target = GlobalLock(memory);
            if (!target)
            {
                GlobalFree(memory);
                return false;
            }
            std::memcpy(target, bytes.data(), bytes.size());
            GlobalUnlock(memory);
            if (!SetClipboardData(format, memory))
            {
                GlobalFree(memory); // still ours when SetClipboardData fails
                return false;
            }
            return true;
        }

        bool OpenClipboardWithRetry(HWND owner)
        {
            // Another process may hold the clipboard for a moment (clipboard managers).
            for (int attempt = 0; attempt < 10; ++attempt)
            {
                if (OpenClipboard(owner))
                {
                    return true;
                }
                Sleep(20);
            }
            return false;
        }

        winrt::com_ptr<IWICImagingFactory> Factory()
        {
            winrt::com_ptr<IWICImagingFactory> factory;
            HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
            if (hr == CO_E_NOTINITIALIZED)
            {
                (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                factory = nullptr;
                hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
            }
            return SUCCEEDED(hr) ? factory : nullptr;
        }

        winrt::com_ptr<IWICBitmap> WicBitmap(IWICImagingFactory* factory, Core::Bitmap const& bitmap)
        {
            winrt::com_ptr<IWICBitmap> out;
            if (FAILED(factory->CreateBitmapFromMemory(static_cast<UINT>(bitmap.width), static_cast<UINT>(bitmap.height),
                    GUID_WICPixelFormat32bppPBGRA, static_cast<UINT>(bitmap.Stride()), static_cast<UINT>(bitmap.pixels.size()),
                    const_cast<BYTE*>(bitmap.pixels.data()), out.put())))
            {
                return nullptr;
            }
            return out;
        }

        Bytes ReadAll(IStream* stream)
        {
            STATSTG stat{};
            if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)))
            {
                return {};
            }
            Bytes bytes(static_cast<std::size_t>(stat.cbSize.QuadPart));
            LARGE_INTEGER zero{};
            if (FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr)))
            {
                return {};
            }
            ULONG read = 0;
            if (!bytes.empty() && (FAILED(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read)) || read != bytes.size()))
            {
                return {};
            }
            return bytes;
        }

        void SetBytesProperty(IWICMetadataQueryWriter* writer, wchar_t const* name, std::span<std::uint8_t const> bytes)
        {
            PROPVARIANT value;
            PropVariantInit(&value);
            value.vt = VT_UI1 | VT_VECTOR;
            value.caub.cElems = static_cast<ULONG>(bytes.size());
            value.caub.pElems = const_cast<UCHAR*>(bytes.data());
            (void)writer->SetMetadataByName(name, &value);
        }

        std::filesystem::path KnownFolder(KNOWNFOLDERID const& id)
        {
            wil::unique_cotaskmem_string path;
            if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, path.put())))
            {
                return path.get();
            }
            return {};
        }
    }

    std::vector<std::uint8_t> EncodePng(Core::Bitmap const& bitmap)
    {
        if (bitmap.IsEmpty())
        {
            return {};
        }
        return ImageCodec::BgraToPng(static_cast<std::uint32_t>(bitmap.width), static_cast<std::uint32_t>(bitmap.height),
                                     bitmap.pixels, true, 96.0 * bitmap.dpr);
    }

    std::vector<std::uint8_t> EncodeJpeg(Core::Bitmap const& bitmap, double quality, bool screenshotComment)
    {
        try
        {
            if (bitmap.IsEmpty() || bitmap.height >= 65535 || bitmap.width >= 65535)
            {
                return {};
            }
            auto const factory = Factory();
            if (!factory)
            {
                return {};
            }
            auto const source = WicBitmap(factory.get(), bitmap);
            winrt::com_ptr<IStream> stream;
            stream.attach(SHCreateMemStream(nullptr, 0));
            winrt::com_ptr<IWICBitmapEncoder> encoder;
            winrt::com_ptr<IWICBitmapFrameEncode> frame;
            winrt::com_ptr<IPropertyBag2> properties;
            if (!source || !stream || FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, encoder.put())) ||
                FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)) ||
                FAILED(encoder->CreateNewFrame(frame.put(), properties.put())))
            {
                return {};
            }
            PROPBAG2 option{};
            option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
            VARIANT value;
            VariantInit(&value);
            value.vt = VT_R4;
            value.fltVal = static_cast<float>(std::clamp(quality, 0.0, 1.0));
            (void)properties->Write(1, &option, &value);
            if (FAILED(frame->Initialize(properties.get())))
            {
                return {};
            }
            double const dpi = 96.0 * bitmap.dpr;
            WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
            if (FAILED(frame->SetSize(static_cast<UINT>(bitmap.width), static_cast<UINT>(bitmap.height))) ||
                FAILED(frame->SetResolution(dpi, dpi)) || FAILED(frame->SetPixelFormat(&format)))
            {
                return {};
            }
            if (screenshotComment)
            {
                winrt::com_ptr<IWICMetadataQueryWriter> writer;
                if (SUCCEEDED(frame->GetMetadataQueryWriter(writer.put())))
                {
                    PROPVARIANT comment;
                    PropVariantInit(&comment);
                    comment.vt = VT_LPWSTR;
                    comment.pwszVal = const_cast<LPWSTR>(L"Screenshot");
                    if (FAILED(writer->SetMetadataByName(L"/app1/ifd/exif/{ushort=37510}", &comment)))
                    {
                        Log(L"could not write the EXIF user comment");
                    }
                }
            }
            winrt::com_ptr<IWICFormatConverter> converter;
            if (FAILED(factory->CreateFormatConverter(converter.put())) ||
                FAILED(converter->Initialize(source.get(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0,
                                             WICBitmapPaletteTypeCustom)) ||
                FAILED(frame->WriteSource(converter.get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit()))
            {
                return {};
            }
            return ReadAll(stream.get());
        }
        catch (...)
        {
            return {};
        }
    }

    std::vector<std::uint8_t> EncodeGif(Core::Bitmap const& first, Core::Bitmap const& second, double delaySeconds)
    {
        try
        {
            auto const factory = Factory();
            winrt::com_ptr<IStream> stream;
            stream.attach(SHCreateMemStream(nullptr, 0));
            winrt::com_ptr<IWICBitmapEncoder> encoder;
            if (!factory || !stream || first.IsEmpty() || second.IsEmpty() ||
                FAILED(factory->CreateEncoder(GUID_ContainerFormatGif, nullptr, encoder.put())) ||
                FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)))
            {
                return {};
            }
            // kCGImagePropertyGIFLoopCount = 0: the NETSCAPE2.0 extension with loop count 0.
            winrt::com_ptr<IWICMetadataQueryWriter> global;
            if (SUCCEEDED(encoder->GetMetadataQueryWriter(global.put())))
            {
                static std::uint8_t const application[] = { 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0' };
                static std::uint8_t const loop[] = { 3, 1, 0, 0, 0 };
                SetBytesProperty(global.get(), L"/appext/application", application);
                SetBytesProperty(global.get(), L"/appext/data", loop);
            }
            for (Core::Bitmap const* image : { &first, &second })
            {
                auto const source = WicBitmap(factory.get(), *image);
                winrt::com_ptr<IWICBitmapFrameEncode> frame;
                winrt::com_ptr<IPropertyBag2> properties;
                winrt::com_ptr<IWICPalette> palette;
                winrt::com_ptr<IWICFormatConverter> converter;
                if (!source || FAILED(encoder->CreateNewFrame(frame.put(), properties.put())) || FAILED(frame->Initialize(properties.get())) ||
                    FAILED(frame->SetSize(static_cast<UINT>(image->width), static_cast<UINT>(image->height))) ||
                    FAILED(factory->CreatePalette(palette.put())) || FAILED(palette->InitializeFromBitmap(source.get(), 256, FALSE)) ||
                    FAILED(factory->CreateFormatConverter(converter.put())) ||
                    FAILED(converter->Initialize(source.get(), GUID_WICPixelFormat8bppIndexed, WICBitmapDitherTypeErrorDiffusion,
                                                 palette.get(), 0, WICBitmapPaletteTypeCustom)))
                {
                    return {};
                }
                WICPixelFormatGUID format = GUID_WICPixelFormat8bppIndexed;
                (void)frame->SetPixelFormat(&format);
                (void)frame->SetPalette(palette.get());
                winrt::com_ptr<IWICMetadataQueryWriter> writer;
                if (SUCCEEDED(frame->GetMetadataQueryWriter(writer.put())))
                {
                    PROPVARIANT delay;
                    PropVariantInit(&delay);
                    delay.vt = VT_UI2;
                    delay.uiVal = static_cast<USHORT>(std::lround(delaySeconds * 100)); // centiseconds
                    (void)writer->SetMetadataByName(L"/grctlext/Delay", &delay);
                }
                if (FAILED(frame->WriteSource(converter.get(), nullptr)) || FAILED(frame->Commit()))
                {
                    return {};
                }
            }
            if (FAILED(encoder->Commit()))
            {
                return {};
            }
            return ReadAll(stream.get());
        }
        catch (...)
        {
            return {};
        }
    }

    Core::Bitmap LoadImageFile(std::filesystem::path const& path)
    {
        try
        {
            auto const factory = Factory();
            winrt::com_ptr<IWICBitmapDecoder> decoder;
            winrt::com_ptr<IWICBitmapFrameDecode> frame;
            winrt::com_ptr<IWICBitmapSource> converted;
            if (!factory ||
                FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put())) ||
                FAILED(decoder->GetFrame(0, frame.put())) || FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppPBGRA, frame.get(), converted.put())))
            {
                return {};
            }
            UINT width = 0;
            UINT height = 0;
            if (FAILED(converted->GetSize(&width, &height)) || width == 0 || height == 0)
            {
                return {};
            }
            Core::Bitmap bitmap;
            bitmap.width = static_cast<std::int32_t>(width);
            bitmap.height = static_cast<std::int32_t>(height);
            bitmap.pixels.resize(bitmap.Stride() * height);
            if (FAILED(converted->CopyPixels(nullptr, static_cast<UINT>(bitmap.Stride()), static_cast<UINT>(bitmap.pixels.size()), bitmap.pixels.data())))
            {
                return {};
            }
            return bitmap;
        }
        catch (...)
        {
            return {};
        }
    }

    bool CopyToClipboard(Core::Bitmap const& bitmap, HWND owner)
    {
        std::vector<std::uint8_t> const png = EncodePng(bitmap);
        if (png.empty())
        {
            Log(L"PNG encoding failed");
            return false;
        }
        std::vector<std::uint8_t> const dib = ImageCodec::PngToDibV5(png);
        static UINT const pngFormat = RegisterClipboardFormatW(L"PNG");

        if (!OpenClipboardWithRetry(owner))
        {
            Log(L"could not open the clipboard");
            return false;
        }
        bool ok = EmptyClipboard() != FALSE;
        ok = ok && SetData(pngFormat, png);
        if (!dib.empty())
        {
            ok = SetData(CF_DIBV5, dib) && ok;
        }
        CloseClipboard();
        return ok;
    }

    void ClearClipboard(HWND owner)
    {
        if (OpenClipboardWithRetry(owner))
        {
            EmptyClipboard();
            CloseClipboard();
        }
    }

    std::shared_ptr<Core::Bitmap const> DownscaleForSave(std::shared_ptr<Core::Bitmap const> image, bool downscaleOnSave)
    {
        if (!downscaleOnSave || !image || image->IsEmpty() || image->dpr < 2)
        {
            return image;
        }
        int const w = std::max(1, image->width / 2);
        int const h = std::max(1, image->height / 2);
        auto out = std::make_shared<Core::Bitmap>(Core::Bitmap::Create(w, h, image->dpr / 2));
        out->screenRect = image->screenRect;
        for (int y = 0; y < h; ++y)
        {
            int const y0 = std::min(2 * y, image->height - 1);
            int const y1 = std::min(2 * y + 1, image->height - 1);
            for (int x = 0; x < w; ++x)
            {
                int const x0 = std::min(2 * x, image->width - 1);
                int const x1 = std::min(2 * x + 1, image->width - 1);
                std::uint8_t const* p[4] = {
                    image->pixels.data() + image->Stride() * y0 + static_cast<std::size_t>(x0) * 4,
                    image->pixels.data() + image->Stride() * y0 + static_cast<std::size_t>(x1) * 4,
                    image->pixels.data() + image->Stride() * y1 + static_cast<std::size_t>(x0) * 4,
                    image->pixels.data() + image->Stride() * y1 + static_cast<std::size_t>(x1) * 4,
                };
                std::uint8_t* q = out->pixels.data() + out->Stride() * y + static_cast<std::size_t>(x) * 4;
                for (int c = 0; c < 4; ++c)
                {
                    q[c] = static_cast<std::uint8_t>((p[0][c] + p[1][c] + p[2][c] + p[3][c] + 2) / 4);
                }
            }
        }
        return out;
    }

    bool WriteFileAtomic(std::filesystem::path const& path, std::vector<std::uint8_t> const& bytes)
    {
        std::filesystem::path temp = path;
        temp += L".eikon-tmp";
        wil::unique_hfile file{ CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
        if (!file)
        {
            return false;
        }
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            DWORD const chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1u << 30));
            DWORD written = 0;
            if (!WriteFile(file.get(), bytes.data() + offset, chunk, &written, nullptr) || written != chunk)
            {
                file.reset();
                DeleteFileW(temp.c_str());
                return false;
            }
            offset += written;
        }
        FlushFileBuffers(file.get());
        file.reset();
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(temp.c_str());
            return false;
        }
        return true;
    }

    std::filesystem::path FreeFileName(std::filesystem::path const& folder, std::wstring const& baseName, std::wstring_view extension)
    {
        for (int attempt = 1;; ++attempt)
        {
            std::filesystem::path candidate = folder / (Core::FileName::WithCollisionSuffix(baseName, attempt) + std::wstring{ extension });
            std::error_code error;
            if (!std::filesystem::exists(candidate, error) || attempt > 100000)
            {
                return candidate;
            }
        }
    }

    std::filesystem::path ScreenshotsFolder()
    {
        std::wstring const folder = Core::Settings::Current().DefaultFolder();
        if (!folder.empty())
        {
            return std::filesystem::path{ folder };
        }
        return KnownFolder(FOLDERID_Desktop);
    }

    std::optional<std::filesystem::path> SaveToFolder(Core::Bitmap const& image, Corners const& corners, std::wstring const& saveFormat,
                                                      std::filesystem::path const& folder, std::wstring const& baseName, bool overwrite)
    {
        if (image.IsEmpty() || folder.empty())
        {
            return std::nullopt;
        }
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        if (!std::filesystem::is_directory(folder, error))
        {
            Log(L"Can't create the default directory");
            return std::nullopt;
        }
        Bytes bytes;
        std::wstring_view extension = Core::FileName::kPngExtension;
        if (saveFormat == L"JPEG")
        {
            bytes = EncodeJpeg(image, 0.92, true);
            extension = Core::FileName::kJpegExtension;
        }
        else
        {
            bytes = EncodePng(image);
            if (saveFormat != L"PNG" &&
                Core::FileName::JpegAllowed(image.height, corners.outerPx, corners.innerPx, corners.padTop, corners.padBottom,
                                            corners.padLeft, corners.padRight))
            {
                Bytes jpeg = EncodeJpeg(image, 0.92, true);
                if (!jpeg.empty() && Core::FileName::PreferJpeg(bytes.size(), jpeg.size()))
                {
                    bytes = std::move(jpeg);
                    extension = Core::FileName::kJpegExtension;
                }
            }
        }
        if (bytes.empty())
        {
            Log(L"encoding the image failed");
            return std::nullopt;
        }
        std::filesystem::path const path = overwrite ? folder / (baseName + std::wstring{ extension }) : FreeFileName(folder, baseName, extension);
        if (!WriteFileAtomic(path, bytes))
        {
            Log(L"writing " + path.wstring() + L" failed");
            return std::nullopt;
        }
        if (!std::filesystem::exists(path, error))
        {
            Log(L"Image was saved, no exception fired, yet the file doesn't exist");
            return std::nullopt;
        }
        return path;
    }

    std::optional<std::filesystem::path> SaveAs(HWND owner, Core::Bitmap const& image, std::wstring const& baseName, bool& failed)
    {
        failed = false;
        try
        {
            auto dialog = winrt::create_instance<IFileSaveDialog>(CLSID_FileSaveDialog);
            std::wstring const title = Strings::Get(L"Output_SaveAsTitle");
            std::wstring const prompt = Strings::Get(L"Output_SaveAsPrompt");
            winrt::check_hresult(dialog->SetTitle(title.c_str()));
            winrt::check_hresult(dialog->SetOkButtonLabel(prompt.c_str()));
            COMDLG_FILTERSPEC const types[] = { { L"png", L"*.png" }, { L"jpeg", L"*.jpeg;*.jpg" } };
            winrt::check_hresult(dialog->SetFileTypes(2, types));
            std::wstring lower = baseName;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
            bool const jpeg = lower.ends_with(L"jpeg") || lower.ends_with(L"jpg");
            winrt::check_hresult(dialog->SetFileTypeIndex(jpeg ? 2 : 1));
            winrt::check_hresult(dialog->SetDefaultExtension(jpeg ? L"jpeg" : L"png"));
            winrt::check_hresult(dialog->SetFileName(baseName.c_str()));
            DWORD options = 0;
            if (SUCCEEDED(dialog->GetOptions(&options)))
            {
                dialog->SetOptions(options | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_STRICTFILETYPES);
            }
            std::filesystem::path const folder = ScreenshotsFolder();
            winrt::com_ptr<IShellItem> folderItem;
            if (!folder.empty() && SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(folderItem.put()))))
            {
                dialog->SetFolder(folderItem.get());
            }
            HRESULT const shown = dialog->Show(owner);
            if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED))
            {
                return std::nullopt;
            }
            winrt::check_hresult(shown);
            UINT typeIndex = 1;
            dialog->GetFileTypeIndex(&typeIndex);
            winrt::com_ptr<IShellItem> result;
            winrt::check_hresult(dialog->GetResult(result.put()));
            wil::unique_cotaskmem_string path;
            winrt::check_hresult(result->GetDisplayName(SIGDN_FILESYSPATH, path.put()));
            Bytes const bytes = typeIndex == 1 ? EncodePng(image) : EncodeJpeg(image, 0.85, false);
            std::filesystem::path const target{ path.get() };
            if (bytes.empty() || !WriteFileAtomic(target, bytes))
            {
                Log(L"Failed to save file");
                failed = true;
                return std::nullopt;
            }
            return target;
        }
        catch (...)
        {
            Log(L"Save As failed");
            failed = true;
            return std::nullopt;
        }
    }

    std::filesystem::path DragOutFolder()
    {
        wchar_t temp[MAX_PATH + 1]{};
        DWORD const length = GetTempPathW(MAX_PATH + 1, temp);
        return std::filesystem::path{ std::wstring{ temp, length } } / L"Eikon";
    }

    void CleanDragOutFolder()
    {
        std::error_code error;
        auto const folder = DragOutFolder();
        if (!std::filesystem::is_directory(folder, error))
        {
            return;
        }
        auto const limit = std::filesystem::file_time_type::clock::now() - std::chrono::seconds(86400);
        for (auto const& entry : std::filesystem::directory_iterator(folder, error))
        {
            std::error_code timeError;
            auto const written = entry.last_write_time(timeError);
            if (!timeError && written < limit)
            {
                std::filesystem::remove_all(entry.path(), timeError);
            }
        }
    }

    bool Print(HWND owner, Core::Bitmap const& image, std::wstring const& orientation)
    {
        if (image.IsEmpty())
        {
            return false;
        }
        // Landscape when w >= h, unless printOrientation names one.
        bool landscape = image.width >= image.height;
        std::wstring lower = orientation;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        if (lower.find(L'p') != std::wstring::npos)
        {
            landscape = false;
        }
        else if (lower.find(L'l') != std::wstring::npos)
        {
            landscape = true;
        }

        HGLOBAL devMode = GlobalAlloc(GHND, sizeof(DEVMODEW));
        if (auto* mode = static_cast<DEVMODEW*>(GlobalLock(devMode)))
        {
            mode->dmSize = sizeof(DEVMODEW);
            mode->dmFields = DM_ORIENTATION;
            mode->dmOrientation = static_cast<short>(landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT);
            GlobalUnlock(devMode);
        }
        PRINTPAGERANGE range{ 1, 1 };
        PRINTDLGEXW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner ? owner : GetDesktopWindow();
        dialog.hDevMode = devMode;
        dialog.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_NOCURRENTPAGE | PD_USEDEVMODECOPIESANDCOLLATE;
        dialog.nPageRanges = 0;
        dialog.nMaxPageRanges = 1;
        dialog.lpPageRanges = &range;
        dialog.nMinPage = 1;
        dialog.nMaxPage = 1;
        dialog.nCopies = 1;
        dialog.nStartPage = START_PAGE_GENERAL;
        HRESULT const hr = PrintDlgExW(&dialog);
        auto const cleanup = wil::scope_exit([&] {
            if (dialog.hDC)
            {
                DeleteDC(dialog.hDC);
            }
            if (dialog.hDevMode)
            {
                GlobalFree(dialog.hDevMode);
            }
            if (dialog.hDevNames)
            {
                GlobalFree(dialog.hDevNames);
            }
        });
        if (FAILED(hr) || dialog.dwResultAction != PD_RESULT_PRINT || !dialog.hDC)
        {
            return false;
        }
        HDC const dc = dialog.hDC;
        int const pageW = GetDeviceCaps(dc, HORZRES);
        int const pageH = GetDeviceCaps(dc, VERTRES);
        double const scale = std::min(static_cast<double>(pageW) / image.width, static_cast<double>(pageH) / image.height);
        int const destW = static_cast<int>(std::lround(image.width * scale));
        int const destH = static_cast<int>(std::lround(image.height * scale));
        int const destX = (pageW - destW) / 2;
        int const destY = (pageH - destH) / 2;

        // Printers ignore alpha: flatten onto white paper.
        std::vector<std::uint8_t> pixels = image.pixels;
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
        {
            std::uint8_t const inverse = static_cast<std::uint8_t>(255 - pixels[i + 3]);
            pixels[i] = static_cast<std::uint8_t>(std::min(255, pixels[i] + inverse));
            pixels[i + 1] = static_cast<std::uint8_t>(std::min(255, pixels[i + 1] + inverse));
            pixels[i + 2] = static_cast<std::uint8_t>(std::min(255, pixels[i + 2] + inverse));
            pixels[i + 3] = 255;
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = image.width;
        info.bmiHeader.biHeight = -image.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        std::wstring const name = L"Eikon";
        DOCINFOW doc{};
        doc.cbSize = sizeof(doc);
        doc.lpszDocName = name.c_str();
        if (StartDocW(dc, &doc) <= 0)
        {
            return false;
        }
        bool ok = StartPage(dc) > 0;
        if (ok)
        {
            SetStretchBltMode(dc, HALFTONE);
            SetBrushOrgEx(dc, 0, 0, nullptr);
            ok = StretchDIBits(dc, destX, destY, destW, destH, 0, 0, image.width, image.height, pixels.data(), &info, DIB_RGB_COLORS,
                               SRCCOPY) != GDI_ERROR;
            ok = EndPage(dc) > 0 && ok;
        }
        if (ok)
        {
            EndDoc(dc);
        }
        else
        {
            AbortDoc(dc);
        }
        return ok;
    }

    void ShowInExplorer(std::filesystem::path const& file)
    {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(file.c_str(), nullptr, &pidl, 0, nullptr)) && pidl)
        {
            SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
            CoTaskMemFree(pidl);
        }
    }
}
