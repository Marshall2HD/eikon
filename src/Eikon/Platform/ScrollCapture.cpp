#include "pch.h"
#include "Platform/ScrollCapture.h"

#include "Core/Settings.h"
#include "Core/Stitch.h"
#include "Platform/Notifier.h"
#include "Platform/RegionSelector.h"
#include "Platform/ScrollDriver.h"
#include "Strings.h"

#include <timeapi.h>

#include <cmath>
#include <cstring>
#include <numeric>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")

namespace Eikon::ScrollCapture
{
    namespace
    {
        using Core::Bitmap;
        using Core::Settings;
        namespace Stitch = Core::Stitch;
        using winrt::Microsoft::UI::Dispatching::DispatcherQueue;
        using winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer;

        void Log(std::wstring_view function, std::wstring_view message)
        {
            std::wstring line{ L"[Eikon] " };
            line.append(function).append(L": ").append(message).push_back(L'\n');
            OutputDebugStringW(line.c_str());
        }

        double Now()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        winrt::Windows::Foundation::TimeSpan Seconds(double seconds)
        {
            return std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::duration<double>((std::max)(seconds, 0.0)));
        }

        RECT ToRect(Core::Rect const& rect)
        {
            return { static_cast<LONG>(std::lround(rect.x)), static_cast<LONG>(std::lround(rect.y)),
                     static_cast<LONG>(std::lround(rect.x + rect.width)), static_cast<LONG>(std::lround(rect.y + rect.height)) };
        }

        // The composed desktop under `rect` in physical
        // pixels, without the cursor. The selector overlay and its label are excluded from capture
        // (WDA_EXCLUDEFROMCAPTURE), so they never appear in a frame.
        std::optional<Bitmap> CaptureRect(RECT const& rect, double dpr)
        {
            int const width = rect.right - rect.left;
            int const height = rect.bottom - rect.top;
            if (width <= 0 || height <= 0)
            {
                return std::nullopt;
            }
            HDC const screen = GetDC(nullptr);
            if (!screen)
            {
                return std::nullopt;
            }
            auto releaseScreen = wil::scope_exit([&] { ReleaseDC(nullptr, screen); });
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            wil::unique_hbitmap dib{ CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0) };
            wil::unique_hdc memory{ CreateCompatibleDC(screen) };
            if (!dib || !bits || !memory)
            {
                return std::nullopt;
            }
            HGDIOBJ const previous = SelectObject(memory.get(), dib.get());
            BOOL const copied = BitBlt(memory.get(), 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT);
            GdiFlush();
            SelectObject(memory.get(), previous);
            if (!copied)
            {
                return std::nullopt;
            }
            Bitmap bitmap;
            bitmap.width = width;
            bitmap.height = height;
            bitmap.dpr = dpr;
            bitmap.screenRect = { static_cast<double>(rect.left), static_cast<double>(rect.top), static_cast<double>(width), static_cast<double>(height) };
            bitmap.pixels.resize(bitmap.Stride() * static_cast<std::size_t>(height));
            std::memcpy(bitmap.pixels.data(), bits, bitmap.pixels.size());
            for (std::size_t i = 3; i < bitmap.pixels.size(); i += 4)
            {
                bitmap.pixels[i] = 0xFF; // GDI leaves alpha undefined; the screen is opaque
            }
            return bitmap;
        }

        bool TooSmall(Platform::RegionSelection const& selection)
        {
            double const dpr = selection.dpr > 0 ? selection.dpr : 1.0;
            return selection.screenRect.width / dpr < 5 || selection.screenRect.height / dpr < 5;
        }

        // One-shot timers on the UI thread, cancelled together when the capture ends.
        class Timers
        {
        public:
            void After(double seconds, std::function<void()> action)
            {
                auto dispatcher = DispatcherQueue::GetForCurrentThread();
                if (!dispatcher)
                {
                    return;
                }
                DispatcherQueueTimer timer = dispatcher.CreateTimer();
                timer.Interval(Seconds(seconds));
                timer.IsRepeating(false);
                timer.Tick([this, action = std::move(action)](DispatcherQueueTimer const& sender, auto&&) {
                    sender.Stop();
                    std::erase_if(m_timers, [&](auto const& t) { return t == sender; });
                    try
                    {
                        action();
                    }
                    catch (...)
                    {
                        Log(L"scrolling capture", L"exception in a timer action");
                    }
                });
                m_timers.push_back(timer);
                timer.Start();
            }

            void Cancel()
            {
                for (auto& timer : m_timers)
                {
                    timer.Stop();
                }
                m_timers.clear();
            }

        private:
            std::vector<DispatcherQueueTimer> m_timers;
        };

        // ---- Auto-scroll ----

        struct AutoCapture
        {
            int direction = -1;
            Done done;
            std::shared_ptr<Platform::RegionOverlay> overlay;
            RECT rect{};
            POINT centre{};
            double dpr = 1.0;
            double delay = 0.12;
            bool reverse = false;
            std::optional<POINT> savedCursor;
            std::unique_ptr<Stitch::AutoScrollSession> session;
            std::unique_ptr<ScrollDriver::InputMonitor> monitor;   // the shared `stop` flag
            Timers timers;
        };

        std::unique_ptr<AutoCapture>& Auto()
        {
            static std::unique_ptr<AutoCapture> state;
            return state;
        }

        void AutoStep(std::uint32_t settleHash = 0, int polls = 0);

        // Centre the cursor, post after 0.15d,
        // park after another 0.15d.
        void AutoScroll()
        {
            auto& state = Auto();
            if (!state)
            {
                return;
            }
            ScrollDriver::MoveCursor(state->centre);
            Stitch::ScrollRequest const request = state->session->NextScroll();
            double const d = state->delay;
            state->timers.After(0.15 * d, [request] {
                auto& state = Auto();
                if (!state)
                {
                    return;
                }
                if (request.method == Stitch::ScrollMethod::Wheel)
                {
                    ScrollDriver::PostWheel(state->direction, request.count, state->reverse);
                }
                else
                {
                    ScrollDriver::PostAutomationScroll(state->centre, state->direction, request.count);
                }
                // The UI Automation scroll is a cross-process COM call that pumps messages: a step
                // timer can finish the capture meanwhile and release the state.
                if (!state)
                {
                    return;
                }
                state->timers.After(0.15 * state->delay, [] {
                    if (Auto())
                    {
                        ScrollDriver::MoveCursor(ScrollDriver::ParkingPoint());
                    }
                });
            });
        }

        std::wstring_view StopName(Stitch::AutoStop stop)
        {
            switch (stop)
            {
            case Stitch::AutoStop::EndReached: return L"endReached";
            case Stitch::AutoStop::WithoutStarting: return L"withoutStarting";
            case Stitch::AutoStop::MaxHeight: return L"maxHeight";
            case Stitch::AutoStop::Interrupted: return L"interrupted";
            default: return L"";
            }
        }

        // The end of an automatic scroll.
        void FinishAuto(Stitch::AutoStepResult result)
        {
            std::unique_ptr<AutoCapture> state = std::move(Auto());
            if (!state)
            {
                return;
            }
            state->timers.Cancel();
            state->monitor.reset();
            timeEndPeriod(1);
            Log(L"grab:scrolling", L"stopped " + std::wstring{ StopName(result.stopped) } + L", segments " + std::to_wstring(state->session->Segments()));

            if (result.maxHeightNotice)
            {
                Notifier::Show({ Strings::Get(L"Scrolling_MaxHeightTitle"), Strings::Get(L"Scrolling_MaxHeightMessage"), true });
            }
            if (result.cantScrollAlert)
            {
                std::wstring const title = Strings::Get(L"Scrolling_CantScrollTitle");
                std::wstring const text = Strings::Get(L"Scrolling_CantScrollMessage");
                MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
            }
            if (state->savedCursor)
            {
                ScrollDriver::MoveCursor(*state->savedCursor);
            }
            if (result.image)
            {
                Log(L"finish", L"Final image size: " + std::to_wstring(result.image->height) + L"px");
            }
            if (state->overlay)
            {
                state->overlay->Close();
            }
            if (state->done)
            {
                state->done(std::move(result.image));
            }
        }

        // step(...).
        void AutoStep(std::uint32_t settleHash, int polls)
        {
            auto& state = Auto();
            if (!state)
            {
                return;
            }
            auto frame = CaptureRect(state->rect, state->dpr);
            // A calibrating step measures the travel once the view has stopped moving.
            // Smooth-scrolling views (Notepad animates one notch over ~0.3 s) are still mid-scroll at
            // the step time; a partial travel makes every later step overshoot the matchers.
            constexpr double kSettleInterval = 0.03;
            constexpr int kSettlePolls = 50;
            if (state->session->NotchTravel() == 0 && frame && polls < kSettlePolls)
            {
                std::uint32_t const hash = Stitch::CentreHash(*frame);
                if (polls == 0 || hash != settleHash)
                {
                    state->timers.After(kSettleInterval, [hash, polls] { AutoStep(hash, polls + 1); });
                    return;
                }
            }
            Stitch::AutoStepResult result = state->session->Step(std::move(frame), state->monitor->Triggered());
            if (!result.finished)
            {
                double const d = state->delay;
                state->timers.After(0.25 * d, [] { AutoScroll(); });
                state->timers.After(d, [] { AutoStep(); });
                return;
            }
            FinishAuto(std::move(result));
        }

        // Starts auto-scrolling capture of the selected region.
        void BeginAuto(int direction, Done done, Platform::RegionSelection const& selection, std::shared_ptr<Platform::RegionOverlay> overlay)
        {
            auto state = std::make_unique<AutoCapture>();
            state->direction = direction;
            state->done = std::move(done);
            state->overlay = std::move(overlay);
            state->rect = ToRect(selection.screenRect);
            state->dpr = selection.dpr > 0 ? selection.dpr : 1.0;

            state->savedCursor = ScrollDriver::CursorPosition();
            ScrollDriver::MoveCursor(ScrollDriver::ParkingPoint());

            auto& settings = Settings::Current();
            Stitch::AutoScrollParams params;
            params.direction = direction;
            params.dpr = state->dpr;
            params.speed = settings.ScrollingSpeed();
            params.scrollingMax = settings.ScrollingMax();
            state->reverse = settings.ScrollingReverseAutoscroll();

            RECT const& r = state->rect;
            state->centre = { r.left + (r.right - r.left) / 2, r.top + (r.bottom - r.top) / 2 };

            auto first = CaptureRect(r, state->dpr);
            if (!first)
            {
                // Fails without calling `then`, and takes the overlay down.
                Log(L"capture", L"Failed to capture the region");
                if (state->savedCursor)
                {
                    ScrollDriver::MoveCursor(*state->savedCursor);
                }
                if (state->overlay)
                {
                    state->overlay->Close();
                }
                return;
            }
            state->monitor = std::make_unique<ScrollDriver::InputMonitor>(ScrollDriver::InputMonitor::Trigger::AnyKeyOrClick);
            auto const sample = [&] {
                auto frame = CaptureRect(r, state->dpr);
                return frame ? Stitch::CentreHash(*frame) : 0u;
            };
            std::uint32_t const prevHash = sample();
            std::uint32_t const lastHash = sample();
            state->session = std::make_unique<Stitch::AutoScrollSession>(params, std::move(*first), prevHash, lastHash);
            state->delay = state->session->Geometry().delay;

            timeBeginPeriod(1); // the sub-timings go down to 0.15d = 15 ms
            double const d = state->delay;
            Auto() = std::move(state);
            Auto()->timers.After(2 * d * 0.25, [] { AutoScroll(); });
            Auto()->timers.After(2 * d, [] { AutoStep(); });
        }

        // ---- Manual ----

        struct ManualCapture
        {
            Done done;
            std::shared_ptr<Platform::RegionOverlay> overlay;
            RECT rect{};
            double dpr = 1.0;
            std::unique_ptr<ScrollDriver::InputMonitor> monitor;
            DispatcherQueueTimer timer{ nullptr };
        };

        // The manual-mode session.
        Stitch::ManualSession& Session()
        {
            static Stitch::ManualSession session;
            return session;
        }

        std::unique_ptr<ManualCapture>& Manual()
        {
            static std::unique_ptr<ManualCapture> state;
            return state;
        }

        std::wstring LabelText(Stitch::ManualLabel label)
        {
            switch (label)
            {
            case Stitch::ManualLabel::Start: return Strings::Get(L"Scrolling_ManualStart");
            case Stitch::ManualLabel::KeepDown: return Strings::Get(L"Scrolling_ManualKeepDown");
            case Stitch::ManualLabel::KeepUp: return Strings::Get(L"Scrolling_ManualKeepUp");
            case Stitch::ManualLabel::TooFast: return Strings::Get(L"Scrolling_ManualTooFast");
            default: return {};
            }
        }

        // stop: no callback.
        std::unique_ptr<ManualCapture> StopManual()
        {
            std::unique_ptr<ManualCapture> state = std::move(Manual());
            Session().Stop();
            if (state)
            {
                if (state->timer)
                {
                    state->timer.Stop();
                }
                state->monitor.reset();
            }
            return state;
        }

        // finish.
        void FinishManual(Stitch::ManualTickResult result)
        {
            std::unique_ptr<ManualCapture> state = StopManual();
            if (!state)
            {
                return;
            }
            if (state->overlay)
            {
                state->overlay->Close();
            }
            if (state->done)
            {
                state->done(std::move(result.image));
            }
            auto const& shifts = Session().Shifts();
            double const average = shifts.empty() ? 0.0 : static_cast<double>(std::accumulate(shifts.begin(), shifts.end(), std::int64_t{ 0 })) / shifts.size();
            Log(L"ManualStitch", L"Avg shift: " + std::to_wstring(average));
            Log(L"ManualStitch", L"nilShifts: " + std::to_wstring(Session().NilShifts()) + L" / " + std::to_wstring(shifts.size()));
            if (result.error)
            {
                Log(L"ManualStitch", L"Error computing image alignment: the frame size changed");
                Notifier::Show({ Strings::Get(L"Scrolling_ErrorTitle"), Strings::Get(L"Scrolling_ErrorMessage"), true, 5.0, true });
            }
        }

        // captureAndProcess.
        void ManualTick()
        {
            auto& state = Manual();
            if (!state)
            {
                return;
            }
            if (state->monitor && state->monitor->Triggered())
            {
                Session().Cancel(); // Esc (cancelPlease)
            }
            auto frame = CaptureRect(state->rect, state->dpr);
            Stitch::ManualTickResult result = Session().Tick(std::move(frame), Now());
            if (result.label != Stitch::ManualLabel::None && state->overlay)
            {
                state->overlay->SetStatus(LabelText(result.label));
            }
            if (result.finished)
            {
                FinishManual(std::move(result));
            }
        }

        // begin.
        void BeginManual(Done done, Platform::RegionSelection const& selection, std::shared_ptr<Platform::RegionOverlay> overlay)
        {
            auto dispatcher = DispatcherQueue::GetForCurrentThread();
            if (Session().Capturing() || !dispatcher)
            {
                return;
            }
            auto state = std::make_unique<ManualCapture>();
            state->done = std::move(done);
            state->overlay = std::move(overlay);
            state->rect = ToRect(selection.screenRect);
            state->dpr = selection.dpr > 0 ? selection.dpr : 1.0;
            Session().Begin(Now(), selection.screenRect.height / state->dpr);
            state->monitor = std::make_unique<ScrollDriver::InputMonitor>(ScrollDriver::InputMonitor::Trigger::Escape);
            if (state->overlay)
            {
                state->overlay->SetStatus(LabelText(Stitch::ManualLabel::Start));
            }
            state->timer = dispatcher.CreateTimer();
            state->timer.Interval(Seconds(Stitch::ManualSession::kScreenshotInterval));
            state->timer.IsRepeating(true);
            state->timer.Tick([](auto&&, auto&&) {
                try
                {
                    ManualTick();
                }
                catch (...)
                {
                    Log(L"ManualStitch", L"exception in captureAndProcess");
                }
            });
            Manual() = std::move(state);
            Manual()->timer.Start();
        }
    }

    void StartAuto(int direction, Done done)
    {
        if (Auto())
        {
            Log(L"grabScrolling", L"a scrolling capture is already running");
            return;
        }
        Platform::RegionOptions options;
        options.mode = Platform::RegionMode::Scrolling;
        options.keepOverlay = true;
        Platform::SelectRegion(options, [direction, done = std::move(done)](std::optional<Platform::RegionSelection> selection,
                                                                           std::shared_ptr<Platform::RegionOverlay> overlay) mutable {
            if (!selection || TooSmall(*selection) || Auto())
            {
                // Under 5 x 5 pt closes the selector and stops silently.
                if (overlay)
                {
                    overlay->Close();
                }
                return;
            }
            BeginAuto(direction, std::move(done), *selection, std::move(overlay));
        });
    }

    void StartManual(Done done)
    {
        if (auto previous = StopManual(); previous && previous->overlay)
        {
            previous->overlay->Close();
        }
        Platform::RegionOptions options;
        options.mode = Platform::RegionMode::Scrolling;
        options.keepOverlay = true;
        Platform::SelectRegion(options, [done = std::move(done)](std::optional<Platform::RegionSelection> selection,
                                                                std::shared_ptr<Platform::RegionOverlay> overlay) mutable {
            if (!selection || TooSmall(*selection))
            {
                if (overlay)
                {
                    overlay->Close();
                }
                return;
            }
            BeginManual(std::move(done), *selection, std::move(overlay));
        });
    }

    bool IsRunning() noexcept
    {
        return Auto() != nullptr || Manual() != nullptr;
    }
}
