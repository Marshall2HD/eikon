#include "pch.h"
#include "Platform/FileDrag.h"

#include "Platform/Exporter.h"
#include "Platform/Graphics.h"

#include <ctime>
#include <fstream>

namespace Eikon::FileDrag
{
    namespace
    {
        HBITMAP DragImageBitmap()
        {
            auto asset = Gfx::Asset(L"44-ico-draggable-file");
            UINT width = 0;
            UINT height = 0;
            if (!asset || FAILED(asset->GetSize(&width, &height)) || width == 0 || height == 0)
            {
                return nullptr;
            }
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = static_cast<LONG>(width);
            info.bmiHeader.biHeight = -static_cast<LONG>(height);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!bitmap || FAILED(asset->CopyPixels(nullptr, width * 4, width * height * 4, static_cast<BYTE*>(bits))))
            {
                if (bitmap)
                {
                    DeleteObject(bitmap);
                }
                return nullptr;
            }
            return bitmap;
        }
    }

    std::wstring DefaultFileName(std::chrono::system_clock::time_point time)
    {
        std::time_t const t = std::chrono::system_clock::to_time_t(time);
        std::tm local{};
        localtime_s(&local, &t);
        auto const sinceSecond = time - std::chrono::system_clock::from_time_t(t);
        auto const quarter = std::chrono::duration_cast<std::chrono::milliseconds>(sinceSecond).count() / 250;
        std::int64_t n = (local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec) * 4 + (std::max<std::int64_t>)(0, quarter) + 17576;
        std::wstring letters;
        do
        {
            letters.insert(letters.begin(), static_cast<wchar_t>(L'a' + n % 26));
            n /= 26;
        } while (n > 0);
        wchar_t date[16];
        swprintf_s(date, L"%04d%02d%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
        return L"SCR-" + std::wstring{ date } + L"-" + letters;
    }

    bool DragImage(Core::Bitmap const& image, std::wstring const& fileName, HWND source)
    {
        std::vector<std::uint8_t> const png = Exporter::EncodePng(image);
        if (png.empty())
        {
            OutputDebugStringW(L"[Eikon] No image to save\n");
            return false;
        }
        wchar_t temp[MAX_PATH + 1]{};
        GetTempPathW(MAX_PATH, temp);
        std::filesystem::path const folder = std::filesystem::path{ temp } / L"Eikon";
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        std::filesystem::path const path = folder / (fileName + L".png");
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<char const*>(png.data()), static_cast<std::streamsize>(png.size()));
            if (!file)
            {
                return false;
            }
        }

        winrt::com_ptr<IShellItem> item;
        winrt::com_ptr<IDataObject> data;
        if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(item.put()))) ||
            FAILED(item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(data.put()))))
        {
            return false;
        }
        if (HBITMAP bitmap = DragImageBitmap())
        {
            winrt::com_ptr<IDragSourceHelper> helper;
            if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(helper.put()))))
            {
                BITMAP info{};
                GetObjectW(bitmap, sizeof(info), &info);
                SHDRAGIMAGE drag{};
                drag.sizeDragImage = { info.bmWidth, info.bmHeight };
                drag.ptOffset = { info.bmWidth / 2, info.bmHeight / 2 }; // under the cursor
                drag.hbmpDragImage = bitmap;                              // owned by the helper on success
                drag.crColorKey = CLR_NONE;
                if (FAILED(helper->InitializeFromBitmap(&drag, data.get())))
                {
                    DeleteObject(bitmap);
                }
            }
            else
            {
                DeleteObject(bitmap);
            }
        }
        DWORD effect = DROPEFFECT_NONE;
        SHDoDragDrop(source, data.get(), nullptr, DROPEFFECT_COPY, &effect);
        return true;
    }
}
