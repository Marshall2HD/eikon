#include "pch.h"
#include "Platform/RegionSelector.h"

#include "Core/Capture.h"
#include "Core/Settings.h"
#include "Platform/ScreenCapture.h"

#include <wil/com.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>

#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxgi.lib")

// RegionSelector + CrosshairView as one Win32 popup that follows the mouse
// from monitor to monitor: WS_EX_TOPMOST (level 1000), WS_EX_NOREDIRECTIONBITMAP with a
// DirectComposition swap chain drawn by Direct2D in physical pixels, WDA_EXCLUDEFROMCAPTURE so it
// never shows up in a capture. One "pt" is the monitor scale in pixels; 1 pt lines are
// round(scale) pixels so they stay crisp at 125 % and 150 %.
namespace Eikon::Platform
{
    namespace
    {
        constexpr wchar_t kClassName[] = L"EikonRegionSelector";
        constexpr UINT WM_SELECTOR_ESCAPE = WM_APP + 0x51;
        constexpr UINT_PTR kPollTimer = 1;
        constexpr UINT_PTR kActivateTimer = 2;
        constexpr UINT kPollMs = 16;              // global mouse-moved monitor
        constexpr UINT kActivateRetryMs = 50;     // 0.05 s, twice

        enum class State
        {
            SelectingScreen = 0,
            Dragging = 1,
            Scrolling = 2,
        };

        void Log(std::wstring_view message)
        {
            std::wstring line{ L"[Eikon] " };
            line.append(message);
            line.push_back(L'\n');
            ::OutputDebugStringW(line.c_str());
        }

        // Test hook for the desktop checks: with this file in the data folder the overlay is not
        // excluded from captures, so a screenshot can show what the user sees.
        bool OverlayVisibleInCaptures()
        {
            std::error_code error;
            return std::filesystem::exists(Core::DataFolder() / L"capture-overlay-visible", error);
        }

        D2D1_COLOR_F Rgba(float r, float g, float b, float a)
        {
            return D2D1_COLOR_F{ r, g, b, a };
        }

        D2D1_RECT_F ToD2D(Core::Rect const& rect)
        {
            Core::Rect const r = rect.Standardized();
            return D2D1::RectF(static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.x + r.width), static_cast<float>(r.y + r.height));
        }

        class Selector;
        std::shared_ptr<Selector> g_active;
        HHOOK g_keyboardHook = nullptr;

        LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam);
        void RemoveKeyboardHookIfIdle();

        class Selector final : public RegionOverlay, public std::enable_shared_from_this<Selector>
        {
        public:
            Selector(RegionOptions const& options, RegionDone done)
                : m_options(options), m_done(std::move(done))
            {
                // Frozen for area and OCR; live for scrolling. Window mode is live.
                m_frozen = options.mode == RegionMode::Area || options.mode == RegionMode::Ocr;
            }

            ~Selector() override { Destroy(); }

            bool Open()
            {
                RegisterClass();
                m_window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, kClassName, L"Eikon", WS_POPUP,
                                           0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), this);
                if (!m_window)
                {
                    Log(L"RegionSelector: CreateWindowEx failed");
                    return false;
                }
                ScreenCapture::RegisterOverlay(m_window);
                if (!OverlayVisibleInCaptures())
                {
                    SetWindowDisplayAffinity(m_window, WDA_EXCLUDEFROMCAPTURE);
                }
                if (!CreateDevice())
                {
                    Log(L"RegionSelector: Direct2D/DirectComposition setup failed");
                    return false;
                }
                GetCursorPos(&m_mouse);
                FollowMouse();
                SetTimer(m_window, kPollTimer, kPollMs, nullptr);
                if (!g_keyboardHook)
                {
                    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, &KeyboardProc, GetModuleHandleW(nullptr), 0);
                }
                return true;
            }

            void SetStatus(std::wstring const& text) override
            {
                m_status = text;
                Invalidate();
            }

            void Close() override
            {
                auto const self = shared_from_this();
                Destroy();
                if (g_active == self)
                {
                    g_active.reset();
                }
            }

            State CurrentState() const noexcept { return m_state; }
            bool Completed() const noexcept { return m_completed; }

            // Esc (keyCode 53) in any state except scrolling: onSelection(0, 0, 0, 0).
            void Escape()
            {
                if (m_state != State::Scrolling && !m_completed)
                {
                    Finish(std::nullopt);
                }
            }

            // Closed by a newer selector before it finished: the caller still hears about it.
            void Abandon()
            {
                bool const pending = !m_completed;
                m_completed = true;
                Destroy();
                RemoveKeyboardHookIfIdle();
                if (pending && m_done)
                {
                    auto done = std::move(m_done);
                    done(std::nullopt, nullptr);
                }
            }

        private:
            static void RegisterClass()
            {
                static bool registered = false;
                if (registered)
                {
                    return;
                }
                WNDCLASSEXW wc{};
                wc.cbSize = sizeof(wc);
                wc.lpfnWndProc = &Selector::WndProc;
                wc.hInstance = GetModuleHandleW(nullptr);
                wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
                wc.lpszClassName = kClassName;
                registered = RegisterClassExW(&wc) != 0;
            }

            static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
            {
                if (message == WM_NCCREATE)
                {
                    auto const* create = reinterpret_cast<CREATESTRUCTW const*>(lParam);
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
                }
                auto* self = reinterpret_cast<Selector*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                if (self && self->m_window == hwnd)
                {
                    try
                    {
                        return self->Handle(message, wParam, lParam);
                    }
                    catch (...)
                    {
                        Log(L"RegionSelector: exception in the window procedure");
                    }
                }
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }

            LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam)
            {
                switch (message)
                {
                case WM_SETCURSOR:
                    if (LOWORD(lParam) == HTCLIENT)
                    {
                        SetCursor(LoadCursorW(nullptr, m_options.mode == RegionMode::Window ? IDC_ARROW : IDC_CROSS));
                        return TRUE;
                    }
                    break;
                case WM_MOUSEACTIVATE:
                    return MA_ACTIVATE;
                case WM_PAINT:
                    ValidateRect(m_window, nullptr);
                    Render();
                    return 0;
                case WM_ERASEBKGND:
                    return 1;
                case WM_DPICHANGED:
                    // The overlay always covers its monitor exactly; ignore the suggested rect.
                    return 0;
                case WM_TIMER:
                    if (wParam == kPollTimer)
                    {
                        PollMouse();
                    }
                    else if (wParam == kActivateTimer)
                    {
                        KillTimer(m_window, kActivateTimer);
                        ContinueActivation();
                    }
                    return 0;
                case WM_MOUSEMOVE:
                    PollMouse();
                    return 0;
                case WM_LBUTTONDOWN:
                    MouseDown();
                    return 0;
                case WM_LBUTTONUP:
                    MouseUp();
                    return 0;
                case WM_KEYDOWN:
                    if (wParam == VK_ESCAPE)
                    {
                        Escape();
                    }
                    return 0;
                case WM_SELECTOR_ESCAPE:
                    Escape();
                    return 0;
                case WM_CANCELMODE:
                case WM_CAPTURECHANGED:
                    break;
                case WM_CLOSE:
                    return 0;   // only Eikon closes it
                default:
                    break;
                }
                return DefWindowProcW(m_window, message, wParam, lParam);
            }

            // ---- Mouse ----

            POINT Local(POINT global) const
            {
                return { global.x - m_monitor.bounds.left, global.y - m_monitor.bounds.top };
            }

            Core::Size MonitorSize() const
            {
                return { static_cast<double>(m_monitor.bounds.right - m_monitor.bounds.left),
                         static_cast<double>(m_monitor.bounds.bottom - m_monitor.bounds.top) };
            }

            void PollMouse()
            {
                if (!m_window || m_completed)
                {
                    return;
                }
                POINT position{};
                if (!GetCursorPos(&position) || (position.x == m_mouse.x && position.y == m_mouse.y))
                {
                    return;
                }
                m_mouse = position;
                if (m_state == State::Dragging)
                {
                    POINT const local = Local(position);
                    Core::Point const current{ static_cast<double>(local.x), static_cast<double>(local.y) };
                    m_cutout = Core::SelectorCutout(m_dragStart, current, MonitorSize());
                    if (!m_cutout.IsEmpty())
                    {
                        m_borderRect = m_cutout;
                    }
                    Invalidate();
                }
                else if (m_state == State::SelectingScreen)
                {
                    FollowMouse();
                }
            }

            // followMouse: moves to the monitor under the mouse, freezing it on entry.
            void FollowMouse()
            {
                auto const monitor = ScreenCapture::MonitorFromPoint(m_mouse);
                if (monitor)
                {
                    bool const changed = monitor->handle != m_monitor.handle || !EqualRect(&monitor->bounds, &m_monitor.bounds) || monitor->scale != m_monitor.scale;
                    if (changed)
                    {
                        MoveTo(*monitor);
                    }
                }
                if (m_options.mode == RegionMode::Window)
                {
                    UpdateHighlight();
                }
                Invalidate();
            }

            void MoveTo(ScreenCapture::Monitor const& monitor)
            {
                m_monitor = monitor;
                if (m_frozen)
                {
                    // Same API and cursor rule as fullscreen: "include" draws the pointer.
                    bool const includeCursor = Core::Settings::Current().CaptureCursor() == L"include";
                    auto bitmap = ScreenCapture::CaptureMonitor(monitor, includeCursor);
                    m_frozenImage = bitmap ? std::make_shared<Core::Bitmap const>(std::move(*bitmap)) : nullptr;
                    m_frozenBitmap = nullptr;
                }
                int const width = monitor.bounds.right - monitor.bounds.left;
                int const height = monitor.bounds.bottom - monitor.bounds.top;
                SetWindowPos(m_window, HWND_TOPMOST, monitor.bounds.left, monitor.bounds.top, width, height, SWP_NOACTIVATE);
                ResizeSwapChain(width, height);
                if (m_frozen && GetForegroundWindow() != m_window)
                {
                    ShowWindow(m_window, SW_SHOW);
                    BeginActivation();
                }
                else
                {
                    ShowWindow(m_window, IsWindowVisible(m_window) ? SW_SHOWNA : SW_SHOW);
                }
                Render();
            }

            // activateIgnoringOtherApps + makeKey, rechecked after 0.05 s twice.
            void BeginActivation()
            {
                SetForegroundWindow(m_window);
                SetFocus(m_window);
                m_activationAttempt = 1;
                SetTimer(m_window, kActivateTimer, kActivateRetryMs, nullptr);
            }

            void ContinueActivation()
            {
                if (GetForegroundWindow() == m_window)
                {
                    return;
                }
                if (m_activationAttempt == 1)
                {
                    // The usual workaround for the foreground lock: an Alt tap, then attach to the
                    // foreground thread's input for the call.
                    INPUT inputs[2]{};
                    inputs[0].type = INPUT_KEYBOARD;
                    inputs[0].ki.wVk = VK_MENU;
                    inputs[1].type = INPUT_KEYBOARD;
                    inputs[1].ki.wVk = VK_MENU;
                    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, inputs, sizeof(INPUT));
                    HWND const foreground = GetForegroundWindow();
                    DWORD const foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
                    DWORD const ownThread = GetCurrentThreadId();
                    bool const attached = foregroundThread && foregroundThread != ownThread && AttachThreadInput(ownThread, foregroundThread, TRUE);
                    SetForegroundWindow(m_window);
                    SetFocus(m_window);
                    if (attached)
                    {
                        AttachThreadInput(ownThread, foregroundThread, FALSE);
                    }
                    m_activationAttempt = 2;
                    SetTimer(m_window, kActivateTimer, kActivateRetryMs, nullptr);
                    return;
                }
                // Esc still works through the keyboard hook.
                Log(L"Failed to activate app for RegionSelector, no matter what");
            }

            void UpdateHighlight()
            {
                m_highlight = ScreenCapture::WindowAtPoint(m_mouse, false);
            }

            void MouseDown()
            {
                if (m_completed || m_state != State::SelectingScreen)
                {
                    return;
                }
                GetCursorPos(&m_mouse);
                if (m_options.mode == RegionMode::Window)
                {
                    m_highlight = ScreenCapture::WindowAtPoint(m_mouse, true);
                    if (!m_highlight)
                    {
                        return;   // nothing to capture under the cursor
                    }
                    RegionSelection selection;
                    RECT const& b = m_highlight->bounds;
                    selection.screenRect = Core::Rect{ static_cast<double>(b.left), static_cast<double>(b.top),
                                                       static_cast<double>(b.right - b.left), static_cast<double>(b.bottom - b.top) };
                    selection.monitor = m_monitor.handle;
                    selection.dpr = m_highlight->scale;
                    selection.window = m_highlight->handle;
                    m_state = State::Scrolling;
                    Finish(selection);
                    return;
                }
                // mouseDown: dragging, cutout cleared, the curtain turns black α 0.4.
                m_state = State::Dragging;
                POINT const local = Local(m_mouse);
                m_dragStart = Core::Point{ static_cast<double>(local.x), static_cast<double>(local.y) };
                m_cutout = Core::Rect{};
                m_dimmed = true;
                SetCapture(m_window);
                Invalidate();
            }

            void MouseUp()
            {
                if (m_completed || m_state != State::Dragging)
                {
                    return;
                }
                ReleaseCapture();
                // mouseUp: state scrolling, ignores the mouse, reports the global rect.
                m_state = State::Scrolling;
                Core::Rect const cutout = m_cutout.Standardized();
                if (Core::IsSelectionTooSmall(cutout, m_monitor.scale))
                {
                    Finish(std::nullopt);   // Under 5 × 5 pt
                    return;
                }
                RegionSelection selection;
                selection.screenRect = cutout.Offset(m_monitor.bounds.left, m_monitor.bounds.top);
                selection.monitor = m_monitor.handle;
                selection.dpr = m_monitor.scale;
                selection.frozen = m_frozenImage;
                selection.imageRect = cutout;
                Finish(selection);
            }

            void Finish(std::optional<RegionSelection> selection)
            {
                if (m_completed)
                {
                    return;
                }
                auto const self = shared_from_this();   // Close() may drop the last reference
                m_completed = true;
                RemoveKeyboardHookIfIdle();
                if (GetCapture() == m_window)
                {
                    ReleaseCapture();
                }
                bool const keep = selection.has_value() && m_options.keepOverlay;
                std::shared_ptr<RegionOverlay> overlay;
                if (keep)
                {
                    // Scrolling: the overlay stays, dimmed and click-through, with its status label.
                    KillTimer(m_window, kPollTimer);
                    MakeClickThrough();
                    overlay = shared_from_this();
                    Invalidate();
                }
                else
                {
                    Close();
                }
                auto done = std::move(m_done);
                if (!done)
                {
                    return;
                }
                // Out of the window procedure, on the UI thread.
                auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
                auto run = [done = std::move(done), selection = std::move(selection), overlay]() mutable {
                    try
                    {
                        done(std::move(selection), std::move(overlay));
                    }
                    catch (...)
                    {
                        Log(L"RegionSelector: exception in the selection callback");
                    }
                };
                if (!dispatcher || !dispatcher.TryEnqueue(std::move(run)))
                {
                    Log(L"RegionSelector: no dispatcher for the callback");
                }
            }

            void MakeClickThrough()
            {
                // ignoresMouseEvents = true.
                auto const exStyle = GetWindowLongPtrW(m_window, GWL_EXSTYLE);
                SetWindowLongPtrW(m_window, GWL_EXSTYLE, exStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT);
                SetLayeredWindowAttributes(m_window, 0, 255, LWA_ALPHA);
            }

            void Invalidate()
            {
                if (m_window)
                {
                    InvalidateRect(m_window, nullptr, FALSE);
                }
            }

            // ---- Direct2D / DirectComposition ----

            bool CreateDevice()
            {
                UINT const flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
                HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, m_d3d.put(), nullptr, nullptr);
                if (FAILED(hr))
                {
                    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, m_d3d.put(), nullptr, nullptr);
                }
                if (FAILED(hr))
                {
                    return false;
                }
                auto dxgiDevice = m_d3d.try_query<IDXGIDevice>();
                wil::com_ptr<IDXGIAdapter> adapter;
                wil::com_ptr<IDXGIFactory2> factory;
                if (!dxgiDevice || FAILED(dxgiDevice->GetAdapter(adapter.put())) || FAILED(adapter->GetParent(IID_PPV_ARGS(factory.put()))))
                {
                    return false;
                }
                D2D1_FACTORY_OPTIONS options{};
                if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, m_d2dFactory.put_void())))
                {
                    return false;
                }
                wil::com_ptr<ID2D1Device> d2dDevice;
                if (FAILED(m_d2dFactory->CreateDevice(dxgiDevice.get(), d2dDevice.put())) ||
                    FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, m_context.put())))
                {
                    return false;
                }
                if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(m_dwrite.put()))))
                {
                    return false;
                }
                DXGI_SWAP_CHAIN_DESC1 desc{};
                desc.Width = 8;
                desc.Height = 8;
                desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                desc.BufferCount = 2;
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
                if (FAILED(factory->CreateSwapChainForComposition(m_d3d.get(), &desc, nullptr, m_swapChain.put())))
                {
                    return false;
                }
                if (FAILED(DCompositionCreateDevice(dxgiDevice.get(), IID_PPV_ARGS(m_dcomp.put()))) ||
                    FAILED(m_dcomp->CreateTargetForHwnd(m_window, TRUE, m_target.put())) ||
                    FAILED(m_dcomp->CreateVisual(m_visual.put())) ||
                    FAILED(m_visual->SetContent(m_swapChain.get())) ||
                    FAILED(m_target->SetRoot(m_visual.get())) ||
                    FAILED(m_dcomp->Commit()))
                {
                    return false;
                }
                return true;
            }

            void ResizeSwapChain(int width, int height)
            {
                if (!m_swapChain || width <= 0 || height <= 0 || (width == m_width && height == m_height))
                {
                    return;
                }
                m_context->SetTarget(nullptr);
                if (SUCCEEDED(m_swapChain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0)))
                {
                    m_width = width;
                    m_height = height;
                }
            }

            ID2D1SolidColorBrush* Brush(D2D1_COLOR_F const& color)
            {
                if (!m_brush)
                {
                    m_context->CreateSolidColorBrush(color, m_brush.put());
                }
                else
                {
                    m_brush->SetColor(color);
                }
                return m_brush.get();
            }

            void Fill(Core::Rect const& rect, D2D1_COLOR_F const& color)
            {
                if (rect.width > 0 && rect.height > 0)
                {
                    m_context->FillRectangle(ToD2D(rect), Brush(color));
                }
            }

            // A ring of `thickness` px just inside `rect` (a border drawn inside its frame).
            void Ring(Core::Rect const& rect, double thickness, D2D1_COLOR_F const& color)
            {
                Core::Rect const r = rect.Standardized();
                double const t = (std::min)({ thickness, r.width / 2, r.height / 2 });
                if (t <= 0)
                {
                    return;
                }
                Fill({ r.x, r.y, r.width, t }, color);
                Fill({ r.x, r.y + r.height - t, r.width, t }, color);
                Fill({ r.x, r.y + t, t, r.height - 2 * t }, color);
                Fill({ r.x + r.width - t, r.y + t, t, r.height - 2 * t }, color);
            }

            // One dashed crosshair line ([2, 4] in pt, white at phase 3 over black at phase 0).
            // `along` is the line length; the dash pattern starts at the origin of path:
            // the left edge for the horizontal line and the bottom edge for the vertical.
            void Crosshair(bool horizontal, double position, double along, double unit)
            {
                double const period = 6 * unit;
                double const dash = 2 * unit;
                for (int pass = 0; pass < 2; ++pass)
                {
                    D2D1_COLOR_F const color = pass == 0 ? Rgba(0, 0, 0, 1) : Rgba(1, 1, 1, 1);
                    double const offset = pass == 0 ? 0 : 3 * unit;
                    for (double s = offset; s < along; s += period)
                    {
                        double const length = (std::min)(dash, along - s);
                        if (horizontal)
                        {
                            Fill({ s, position, length, unit }, color);
                        }
                        else
                        {
                            Fill({ position, along - s - length, unit, length }, color);
                        }
                    }
                }
            }

            void Render()
            {
                if (!m_context || !m_swapChain || m_width <= 0)
                {
                    return;
                }
                wil::com_ptr<IDXGISurface> surface;
                if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(surface.put()))))
                {
                    return;
                }
                D2D1_BITMAP_PROPERTIES1 const properties = D2D1::BitmapProperties1(
                    D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
                wil::com_ptr<ID2D1Bitmap1> target;
                if (FAILED(m_context->CreateBitmapFromDxgiSurface(surface.get(), &properties, target.put())))
                {
                    return;
                }
                m_context->SetTarget(target.get());
                m_context->SetDpi(96.0f, 96.0f);
                m_context->BeginDraw();
                m_context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
                m_context->Clear(Rgba(0, 0, 0, 0));
                Draw();
                HRESULT const hr = m_context->EndDraw();
                m_context->SetTarget(nullptr);
                if (FAILED(hr))
                {
                    Log(L"RegionSelector: EndDraw failed");
                    return;
                }
                m_swapChain->Present(1, 0);
            }

            void Draw()
            {
                double const scale = m_monitor.scale > 0 ? m_monitor.scale : 1.0;
                double const unit = (std::max)(1.0, std::round(scale));   // 1 pt line
                Core::Size const size = MonitorSize();
                Core::Rect const bounds{ 0, 0, size.width, size.height };

                // Content layer: the frozen display (native pixels).
                if (m_frozen && m_frozenImage && !m_frozenImage->IsEmpty())
                {
                    if (!m_frozenBitmap)
                    {
                        D2D1_BITMAP_PROPERTIES1 const props = D2D1::BitmapProperties1(
                            D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
                        m_context->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(m_frozenImage->width), static_cast<UINT32>(m_frozenImage->height)),
                                                m_frozenImage->pixels.data(), static_cast<UINT32>(m_frozenImage->Stride()), &props, m_frozenBitmap.put());
                    }
                    if (m_frozenBitmap)
                    {
                        m_context->DrawBitmap(m_frozenBitmap.get(), ToD2D(bounds), 1.0f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                    }
                }

                // lshape: bounds minus (cutout inset by -1 pt), even-odd. Grey α 0.001 until the
                // first mouseDown (invisible), black α 0.4 from then on.
                D2D1_COLOR_F const curtain = m_dimmed ? Rgba(0, 0, 0, 0.4f) : Rgba(0.5f, 0.5f, 0.5f, 0.001f);
                Core::Rect const cutout = m_cutout.Standardized();
                Core::Rect const hole = cutout.IsEmpty() ? Core::Rect{} : cutout.Inset(-unit, -unit).Intersection(bounds);
                if (hole.IsEmpty())
                {
                    Fill(bounds, curtain);
                }
                else
                {
                    Fill({ 0, 0, size.width, hole.y }, curtain);
                    Fill({ 0, hole.y + hole.height, size.width, size.height - hole.y - hole.height }, curtain);
                    Fill({ 0, hole.y, hole.x, hole.height }, curtain);
                    Fill({ hole.x + hole.width, hole.y, size.width - hole.x - hole.width, hole.height }, curtain);
                }

                // Borders: white α 0.4 inside the cutout, black α 0.6 one pt outside. They keep their
                // last frame while the cutout is empty.
                if (!m_borderRect.IsEmpty())
                {
                    Ring(m_borderRect, unit, Rgba(1, 1, 1, 0.4f));
                    Ring(m_borderRect.Inset(-unit, -unit), unit, Rgba(0, 0, 0, 0.6f));
                }

                // Crosshair through the mouse, only while choosing where to start.
                if (m_state == State::SelectingScreen && m_options.mode != RegionMode::Window)
                {
                    POINT const local = Local(m_mouse);
                    Crosshair(true, static_cast<double>(local.y), size.width, unit);
                    Crosshair(false, static_cast<double>(local.x), size.height, unit);
                }

                // Window mode: the window under the cursor, tinted.
                if (m_options.mode == RegionMode::Window && m_highlight && m_state == State::SelectingScreen)
                {
                    RECT const& b = m_highlight->bounds;
                    Core::Rect const window{ static_cast<double>(b.left - m_monitor.bounds.left), static_cast<double>(b.top - m_monitor.bounds.top),
                                             static_cast<double>(b.right - b.left), static_cast<double>(b.bottom - b.top) };
                    Fill(window.Intersection(bounds), Rgba(0.29f, 0.55f, 0.96f, 0.3f));
                }

                // Status label, only in the scrolling state.
                if (m_state == State::Scrolling && !m_status.empty() && m_dwrite)
                {
                    DrawStatus(cutout, size, scale);
                }
            }

            void DrawStatus(Core::Rect const& cutout, Core::Size const& size, double scale)
            {
                if (!m_textFormat || m_textScale != scale)
                {
                    m_textFormat = nullptr;
                    m_textScale = scale;
                    m_dwrite->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                               DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(12.0 * scale), L"en-us", m_textFormat.put());
                    if (!m_textFormat)
                    {
                        return;
                    }
                    m_textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                }
                wil::com_ptr<IDWriteTextLayout> layout;
                if (FAILED(m_dwrite->CreateTextLayout(m_status.c_str(), static_cast<UINT32>(m_status.size()), m_textFormat.get(),
                                                      static_cast<float>(size.width), static_cast<float>(size.height), layout.put())))
                {
                    return;
                }
                DWRITE_TEXT_METRICS metrics{};
                layout->GetMetrics(&metrics);
                Core::Size const text{ std::ceil(metrics.widthIncludingTrailingWhitespace), std::ceil(metrics.height) };
                auto const frame = Core::LayoutStatusLabel(cutout, text, size, scale);
                m_context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                float const radius = static_cast<float>(8 * scale);
                // Candy: an ink label (#1A1530 α 0.92, radius 8) with base-coloured text; is black α 0.5.
                m_context->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(frame.box), radius, radius), Brush(Rgba(0x1A / 255.0f, 0x15 / 255.0f, 0x30 / 255.0f, 0.92f)));
                m_context->DrawTextLayout(D2D1::Point2F(static_cast<float>(frame.text.x), static_cast<float>(frame.text.y)), layout.get(),
                                          Brush(Rgba(0xFB / 255.0f, 0xF7 / 255.0f, 1.0f, 1.0f)));
                m_context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
            }

            void Destroy()
            {
                if (!m_window)
                {
                    return;
                }
                HWND const window = m_window;
                KillTimer(window, kPollTimer);
                KillTimer(window, kActivateTimer);
                if (GetCapture() == window)
                {
                    ReleaseCapture();
                }
                ScreenCapture::UnregisterOverlay(window);
                m_window = nullptr;
                m_frozenBitmap = nullptr;
                m_brush = nullptr;
                m_textFormat = nullptr;
                m_visual = nullptr;
                m_target = nullptr;
                m_dcomp = nullptr;
                m_swapChain = nullptr;
                m_context = nullptr;
                m_d2dFactory = nullptr;
                m_dwrite = nullptr;
                m_d3d = nullptr;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                DestroyWindow(window);
            }

            RegionOptions m_options;
            RegionDone m_done;
            HWND m_window = nullptr;
            bool m_frozen = false;
            bool m_completed = false;
            bool m_dimmed = false;
            State m_state = State::SelectingScreen;
            ScreenCapture::Monitor m_monitor;
            std::shared_ptr<Core::Bitmap const> m_frozenImage;
            POINT m_mouse{ INT_MIN, INT_MIN };
            Core::Point m_dragStart;
            Core::Rect m_cutout;
            Core::Rect m_borderRect;
            std::optional<ScreenCapture::WindowInfo> m_highlight;
            std::wstring m_status;
            int m_activationAttempt = 0;
            int m_width = 0;
            int m_height = 0;
            double m_textScale = 0;

            wil::com_ptr<ID3D11Device> m_d3d;
            wil::com_ptr<ID2D1Factory1> m_d2dFactory;
            wil::com_ptr<ID2D1DeviceContext> m_context;
            wil::com_ptr<IDWriteFactory> m_dwrite;
            wil::com_ptr<IDXGISwapChain1> m_swapChain;
            wil::com_ptr<IDCompositionDevice> m_dcomp;
            wil::com_ptr<IDCompositionTarget> m_target;
            wil::com_ptr<IDCompositionVisual> m_visual;
            wil::com_ptr<ID2D1Bitmap1> m_frozenBitmap;
            wil::com_ptr<ID2D1SolidColorBrush> m_brush;
            wil::com_ptr<IDWriteTextFormat> m_textFormat;
        };

        void RemoveKeyboardHookIfIdle()
        {
            // Called from Finish (g_active is the finishing selector) and after an abandoned open.
            if (g_keyboardHook && (!g_active || g_active->Completed()))
            {
                UnhookWindowsHookEx(g_keyboardHook);
                g_keyboardHook = nullptr;
            }
        }

        // The global key monitor: Esc works even when the overlay could not be activated.
        LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam)
        {
            if (code == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN))
            {
                auto const* key = reinterpret_cast<KBDLLHOOKSTRUCT const*>(lParam);
                if (key->vkCode == VK_ESCAPE && g_active && !g_active->Completed() && g_active->CurrentState() != State::Scrolling)
                {
                    // Handled out of the hook, which must return quickly.
                    auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
                    if (dispatcher)
                    {
                        dispatcher.TryEnqueue([] {
                            if (auto active = g_active)
                            {
                                active->Escape();
                            }
                        });
                    }
                    return 1;
                }
            }
            return CallNextHookEx(nullptr, code, wParam, lParam);
        }
    }

    void SelectRegion(RegionOptions const& options, RegionDone done)
    {
        // Closes any previous instance first.
        if (auto previous = std::exchange(g_active, nullptr))
        {
            previous->Abandon();
        }
        auto selector = std::make_shared<Selector>(options, std::move(done));
        g_active = selector;
        if (!selector->Open())
        {
            g_active.reset();
            selector->Abandon();
            RemoveKeyboardHookIfIdle();
        }
    }

    bool IsSelectingRegion() noexcept
    {
        return g_active && !g_active->Completed();
    }
}
