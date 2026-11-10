#include "pch.h"
#include "Platform/ConfettiWindow.h"
#include "Platform/Candy.h"

#include "Core/Confetti.h"

#include <ShellScalingApi.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <atomic>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shcore.lib")

namespace Eikon::ConfettiWindow
{
    namespace
    {
        namespace Confetti = Core::Confetti;
        constexpr UINT_PTR kFrameTimer = 1;
        constexpr wchar_t kClassName[] = L"EikonConfetti";

        struct State
        {
            HWND window = nullptr;
            std::vector<Confetti::Particle> particles;
            std::chrono::steady_clock::time_point start;
            POINT origin{};          // window position (physical px)
            SIZE size{};
            double scale = 1.0;      // physical px per pt
            double halfWidth = 0;    // the icon's half size in pt
            double halfHeight = 0;
            std::uint8_t level = 0;  // 0 black, 255 white
            std::atomic<bool> stop{ false };
        };

        // The puff now playing (a new one stops it).
        std::shared_ptr<State>& Playing()
        {
            static std::shared_ptr<State> playing;
            return playing;
        }

        void Draw(State& state, double t)
        {
            int const w = state.size.cx;
            int const h = state.size.cy;
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = w;
            info.bmiHeader.biHeight = -h;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HDC const screen = GetDC(nullptr);
            HBITMAP const dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            HDC const memory = CreateCompatibleDC(screen);
            if (!dib || !bits || !memory)
            {
                if (dib) DeleteObject(dib);
                if (memory) DeleteDC(memory);
                ReleaseDC(nullptr, screen);
                return;
            }
            auto* pixels = static_cast<std::uint8_t*>(bits);
            std::memset(pixels, 0, static_cast<std::size_t>(w) * h * 4);

            // Each dot: an antialiased disc, coverage from 4 × 4 samples per pixel, premultiplied
            // source-over. The dots are 0–1.4 pt across, so most are a faint pixel or two.
            double const cx = w / 2.0;
            double const cy = h / 2.0;
            // Candy: the dots take turns in peach #FF8A6B, lilac #B49BFF, mint #7FD9B5 and yellow #FFD166;
            // high contrast keeps the glyph colour.
            struct Rgb { double r, g, b; };
            static constexpr Rgb kCandy[] = { { 0xFF, 0x8A, 0x6B }, { 0xB4, 0x9B, 0xFF }, { 0x7F, 0xD9, 0xB5 }, { 0xFF, 0xD1, 0x66 } };
            bool const candy = !Candy::HighContrast();
            std::size_t index = 0;
            for (auto const& dot : Confetti::Frame(state.particles, t, state.halfWidth, state.halfHeight))
            {
                Rgb const tint = candy ? kCandy[index++ % 4] : Rgb{ static_cast<double>(state.level), static_cast<double>(state.level),
                                                                    static_cast<double>(state.level) };
                double const x = cx + dot.x * state.scale;
                double const y = cy + dot.y * state.scale;
                double const r = dot.diameter * state.scale / 2;
                int const x0 = (std::max)(0, static_cast<int>(std::floor(x - r)));
                int const x1 = (std::min)(w - 1, static_cast<int>(std::floor(x + r)));
                int const y0 = (std::max)(0, static_cast<int>(std::floor(y - r)));
                int const y1 = (std::min)(h - 1, static_cast<int>(std::floor(y + r)));
                for (int py = y0; py <= y1; ++py)
                {
                    for (int px = x0; px <= x1; ++px)
                    {
                        int inside = 0;
                        for (int sy = 0; sy < 4; ++sy)
                        {
                            for (int sx = 0; sx < 4; ++sx)
                            {
                                double const dx = px + (sx + 0.5) / 4 - x;
                                double const dy = py + (sy + 0.5) / 4 - y;
                                inside += dx * dx + dy * dy <= r * r ? 1 : 0;
                            }
                        }
                        if (inside == 0)
                        {
                            continue;
                        }
                        double const a = dot.alpha * inside / 16.0;
                        std::uint8_t* p = pixels + (static_cast<std::size_t>(py) * w + px) * 4;
                        double const keep = 1.0 - a;
                        p[0] = static_cast<std::uint8_t>(std::lround(tint.b * a + p[0] * keep));
                        p[1] = static_cast<std::uint8_t>(std::lround(tint.g * a + p[1] * keep));
                        p[2] = static_cast<std::uint8_t>(std::lround(tint.r * a + p[2] * keep));
                        p[3] = static_cast<std::uint8_t>(std::lround(255 * a + p[3] * keep));
                    }
                }
            }

            HGDIOBJ const previous = SelectObject(memory, dib);
            POINT source{ 0, 0 };
            SIZE size = state.size;
            BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            UpdateLayeredWindow(state.window, screen, &state.origin, &size, memory, &source, 0, &blend, ULW_ALPHA);
            SelectObject(memory, previous);
            DeleteObject(dib);
            DeleteDC(memory);
            ReleaseDC(nullptr, screen);
        }

        LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == WM_NCHITTEST)
            {
                return HTTRANSPARENT;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        // The emitter must not depend on the UI thread, so a busy app doesn't eat the
        // puff: each puff has its own thread with its own window and frame timer: the success
        // notification that is built on the UI thread right after it can't stall the half second
        // the dots live.
        void Run(std::shared_ptr<State> state)
        {
            state->window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                kClassName, L"", WS_POPUP, state->origin.x, state->origin.y, state->size.cx, state->size.cy, nullptr, nullptr,
                GetModuleHandleW(nullptr), nullptr);
            if (!state->window)
            {
                return;
            }
            state->start = std::chrono::steady_clock::now();
            Draw(*state, 0.0);
            ShowWindow(state->window, SW_SHOWNOACTIVATE);
            SetTimer(state->window, kFrameTimer, 16, nullptr);
            MSG msg{};
            while (GetMessageW(&msg, nullptr, 0, 0) > 0)
            {
                if (msg.message == WM_TIMER && msg.hwnd == state->window && msg.wParam == kFrameTimer)
                {
                    double const t = std::chrono::duration<double>(std::chrono::steady_clock::now() - state->start).count();
                    if (t >= Confetti::kShowSeconds || state->stop)
                    {
                        break;
                    }
                    Draw(*state, t);
                    continue;
                }
                DispatchMessageW(&msg);
            }
            KillTimer(state->window, kFrameTimer);
            DestroyWindow(state->window);
            state->window = nullptr;
        }

        bool RegisterOnce()
        {
            static bool const registered = [] {
                WNDCLASSEXW wc{};
                wc.cbSize = sizeof(wc);
                wc.lpfnWndProc = WindowProc;
                wc.hInstance = GetModuleHandleW(nullptr);
                wc.lpszClassName = kClassName;
                return RegisterClassExW(&wc) != 0;
            }();
            return registered;
        }
    }

    void Play(RECT const& icon, bool darkTaskbar)
    {
        if (!RegisterOnce() || icon.right <= icon.left || icon.bottom <= icon.top)
        {
            return;
        }
        auto state = std::make_shared<State>();
        UINT dpiX = 96;
        UINT dpiY = 96;
        POINT const centre{ (icon.left + icon.right) / 2, (icon.top + icon.bottom) / 2 };
        if (FAILED(GetDpiForMonitor(MonitorFromPoint(centre, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) || dpiX == 0)
        {
            dpiX = 96;
        }
        state->scale = dpiX / 96.0;
        state->halfWidth = (icon.right - icon.left) / 2.0 / state->scale;
        state->halfHeight = (icon.bottom - icon.top) / 2.0 / state->scale;
        // The mask: the icon's box grown by 20 pt on every side.
        int const margin = static_cast<int>(std::ceil(Confetti::kMaskMargin * state->scale));
        state->origin = { icon.left - margin, icon.top - margin };
        state->size = { icon.right - icon.left + 2 * margin, icon.bottom - icon.top + 2 * margin };
        state->level = darkTaskbar ? 255 : 0;
        std::mt19937_64 engine{ std::random_device{}() };
        std::uniform_real_distribution<double> uniform{ 0.0, 1.0 };
        state->particles = Confetti::Emit([&] { return uniform(engine); });
        if (auto& playing = Playing())
        {
            playing->stop = true;   // its thread ends at its next frame
        }
        Playing() = state;
        std::thread{ Run, state }.detach();
    }
}
