#include "pch.h"
#include "Editor/Cursors.h"

#include "Platform/ImageCodec.h"

#include <winrt/Microsoft.UI.Input.InputCursor.Interop.h>

#include <cmath>
#include <cstring>
#include <map>
#include <string>

namespace Eikon::Editor
{
    namespace
    {
        using winrt::Microsoft::UI::Input::InputCursor;
        using winrt::Microsoft::UI::Input::InputSystemCursor;
        using winrt::Microsoft::UI::Input::InputSystemCursorShape;

        // The two cursors Windows has no shape for: Assets/Cursors/<name>.png, 256 px masters drawn by
        // tools/generate_icons.py for a 32-unit cursor box, with their hotspots in those units.
        struct Drawn
        {
            wchar_t const* asset;
            double hotspotX;
            double hotspotY;
        };
        constexpr Drawn kZoom{ L"cursor-zoom", 16.0, 16.0 };     // the lens's centre
        constexpr Drawn kPen{ L"cursor-pen", 8.03, 23.97 };      // the pencil's tip

        std::wstring ExeDirectory()
        {
            wchar_t path[MAX_PATH]{};
            ::GetModuleFileNameW(nullptr, path, MAX_PATH);
            std::wstring folder{ path };
            return folder.substr(0, folder.find_last_of(L'\\'));
        }

        // The standard cursor size in device pixels for this element's display (32 at 100 %). Not the
        // Mouse pointer size setting (CursorBaseSize): Windows already enlarges every app cursor by
        // CursorBaseSize / 32 when it shows it, as it does the system cursors, so drawing at that size too
        // made zoom and pen twice the size of the system cursors at a larger pointer size.
        int CursorSize(winrt::Microsoft::UI::Xaml::UIElement const& element)
        {
            double scale = 1.0;
            try
            {
                if (auto root = element.XamlRoot())
                {
                    scale = root.RasterizationScale();
                }
            }
            catch (...)
            {
            }
            return std::clamp(static_cast<int>(std::lround(32.0 * scale)), 16, 256);
        }

        // An HCURSOR of `size` × `size` from a drawn master (high-quality cubic scaling).
        HCURSOR MakeDrawnCursor(Drawn const& drawn, int size)
        {
            std::wstring const path = ExeDirectory() + L"\\Assets\\Cursors\\" + drawn.asset + L".png";
            auto const pixels = ImageCodec::DecodeFileScaled(path.c_str(), static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size), false);
            if (pixels.size() < static_cast<std::size_t>(size) * size * 4)
            {
                return nullptr;
            }
            BITMAPV5HEADER header{};
            header.bV5Size = sizeof(header);
            header.bV5Width = size;
            header.bV5Height = -size;
            header.bV5Planes = 1;
            header.bV5BitCount = 32;
            header.bV5Compression = BI_BITFIELDS;
            header.bV5RedMask = 0x00FF0000;
            header.bV5GreenMask = 0x0000FF00;
            header.bV5BlueMask = 0x000000FF;
            header.bV5AlphaMask = 0xFF000000;
            void* bits = nullptr;
            HDC const screen = ::GetDC(nullptr);
            wil::unique_hbitmap color{ ::CreateDIBSection(screen, reinterpret_cast<BITMAPINFO const*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0) };
            ::ReleaseDC(nullptr, screen);
            if (!color || !bits)
            {
                return nullptr;
            }
            std::memcpy(bits, pixels.data(), static_cast<std::size_t>(size) * size * 4);
            ::GdiFlush();
            std::vector<std::uint8_t> maskBits(static_cast<std::size_t>((size + 15) / 16) * 2 * size, 0);
            wil::unique_hbitmap mask{ ::CreateBitmap(size, size, 1, 1, maskBits.data()) };
            if (!mask)
            {
                return nullptr;
            }
            ICONINFO info{};
            info.fIcon = FALSE;
            info.xHotspot = static_cast<DWORD>(std::lround(drawn.hotspotX * size / 32.0));
            info.yHotspot = static_cast<DWORD>(std::lround(drawn.hotspotY * size / 32.0));
            info.hbmMask = mask.get();
            info.hbmColor = color.get();
            return ::CreateIconIndirect(&info);
        }

        InputCursor FromHCursor(HCURSOR cursor)
        {
            auto const interop = winrt::get_activation_factory<InputCursor, winrt::Microsoft::UI::Input::IInputCursorStaticsInterop>();
            InputCursor result{ nullptr };
            winrt::check_hresult(interop->CreateFromHCursor(cursor, reinterpret_cast<winrt::Microsoft::UI::Input::IInputCursor**>(winrt::put_abi(result))));
            return result;
        }

        InputCursor Make(std::wstring_view name, int size)
        {
            // Windows' own cursors wherever it has the shape, so they follow the user's pointer scheme,
            // size and colour.
            if (name == L"move" || name == L"closed-hand") return InputSystemCursor::Create(InputSystemCursorShape::SizeAll);
            if (name == L"l-resize" || name == L"r-resize") return InputSystemCursor::Create(InputSystemCursorShape::SizeWestEast);
            if (name == L"t-resize" || name == L"b-resize") return InputSystemCursor::Create(InputSystemCursorShape::SizeNorthSouth);
            if (name == L"tl-resize" || name == L"br-resize") return InputSystemCursor::Create(InputSystemCursorShape::SizeNorthwestSoutheast);
            if (name == L"bl-resize" || name == L"tr-resize") return InputSystemCursor::Create(InputSystemCursorShape::SizeNortheastSouthwest);
            if (name == L"open-hand") return InputSystemCursor::Create(InputSystemCursorShape::Hand);
            if (name == L"crosshair") return InputSystemCursor::Create(InputSystemCursorShape::Cross);
            if (name == L"ibeam") return InputSystemCursor::Create(InputSystemCursorShape::IBeam);
            if (name == L"zoom" || name == L"pen")
            {
                // Drawn at the system cursor's current size, so they match the others.
                if (HCURSOR const cursor = MakeDrawnCursor(name == L"zoom" ? kZoom : kPen, size))
                {
                    return FromHCursor(cursor);   // kept for the process's lifetime (the cache below)
                }
            }
            return InputSystemCursor::Create(InputSystemCursorShape::Arrow);   // "point", "default"
        }

        // The cursor-* names objects use for their handles ("cursor-move", ...).
        std::wstring_view Plain(std::wstring_view name)
        {
            constexpr std::wstring_view prefix = L"cursor-";
            return name.substr(0, prefix.size()) == prefix ? name.substr(prefix.size()) : name;
        }
    }

    void SetCursor(winrt::Microsoft::UI::Xaml::UIElement const& element, std::wstring_view name)
    {
        static std::map<std::wstring, InputCursor, std::less<>> cache;
        static std::wstring current;
        name = Plain(name);
        if (name.empty())
        {
            name = L"default";
        }
        if (current == name)
        {
            return;
        }
        try
        {
            // The drawn cursors are cached per size: the display's scale can change.
            bool const drawn = name == L"zoom" || name == L"pen";
            int const size = drawn ? CursorSize(element) : 0;
            std::wstring key{ name };
            if (drawn)
            {
                key += L"@" + std::to_wstring(size);
            }
            auto it = cache.find(key);
            if (it == cache.end())
            {
                it = cache.emplace(key, Make(name, size)).first;
            }
            if (auto target = element.try_as<winrt::Microsoft::UI::Xaml::IUIElementProtected>())
            {
                target.ProtectedCursor(it->second);
                current = std::wstring{ name };
            }
        }
        catch (...)
        {
            OutputDebugStringW(L"Eikon: setting the cursor failed\n");
        }
    }
}
