#include "pch.h"
#include "AppController.h"

#include "Core/Capture.h"
#include "Core/FileName.h"
#include "Core/Library.h"
#include "Core/Objects/Factory.h"
#include "Core/Objects/ObjRectangleBasic.h"
#include "Core/Objects/ObjText.h"
#include "Core/OcrText.h"
#include "Editor/BackdropRender.h"
#include "Editor/Compose.h"
#include "EditorWindow.xaml.h"
#include "LibraryWindow.xaml.h"
#include "PinWindow.h"
#include "Platform/Exporter.h"
#include "Platform/FirstCaptureCard.h"
#include "Platform/ImageLoader.h"
#include "Platform/LibraryIndex.h"
#include "Platform/Ocr.h"
#include "Platform/RegionSelector.h"
#include "Platform/Uploader.h"
#include "PreviewWindow.h"
#include "Platform/ScreenCapture.h"
#include "Platform/ScrollCapture.h"
#include "Platform/Secrets.h"
#include "Platform/SystemIntegration.h"
#include "SettingsWindow.xaml.h"
#include "Strings.h"

#include <charconv>
#include <system_error>

namespace winrt
{
    using namespace Microsoft::UI::Dispatching;
    using namespace Microsoft::UI::Xaml;
}

namespace Eikon
{
    using Core::CaptureActions;
    using Core::HotKey;
    using Core::Settings;

    namespace
    {
        constexpr int kHotKeyIdBase = 100;
        constexpr auto kUrlMessageSeconds = 5.0;   // URL and error notifications

        void Log(std::wstring_view message)
        {
            std::wstring line{ L"[Eikon] " };
            line.append(message);
            line.push_back(L'\n');
            ::OutputDebugStringW(line.c_str());
        }

        bool StartsWith(std::wstring_view text, std::wstring_view prefix) noexcept
        {
            return text.substr(0, prefix.size()) == prefix;
        }

        std::wstring ShortcutText(HotKey hotKey)
        {
            return Strings::ShortcutText(Settings::Current().Shortcut(hotKey));
        }

        // Strict integer parse: optional sign and digits only.
        std::optional<std::int64_t> ParseStrictInt(std::wstring_view text)
        {
            std::string narrow;
            for (wchar_t c : text)
            {
                if (static_cast<std::uint32_t>(c) > 0x7F)
                {
                    return std::nullopt;
                }
                narrow.push_back(static_cast<char>(c));
            }
            char const* first = narrow.data();
            char const* last = first + narrow.size();
            if (first != last && *first == '+')
            {
                ++first;
            }
            std::int64_t value = 0;
            auto const [ptr, ec] = std::from_chars(first, last, value);
            if (first == last || ec != std::errc{} || ptr != last)
            {
                return std::nullopt;
            }
            return value;
        }

    }

    AppController& AppController::Current()
    {
        static AppController instance;
        return instance;
    }

    void AppController::Start()
    {
        auto& settings = Settings::Current();
        m_messageWindow = std::make_unique<MessageWindow>();
        m_hotKeys = std::make_unique<HotKeyService>(*m_messageWindow);
        m_messageWindow->AddHandler([this](UINT message, WPARAM, LPARAM) -> std::optional<LRESULT> {
            if (message == WM_ENDSESSION)
            {
                Shutdown();
            }
            return std::nullopt;
        });

        Notifier::Initialize();
        Ocr::WarmUp();

        m_tray = std::make_unique<Eikon::TrayIcon>(*m_messageWindow);
        m_tray->MenuProvider = [this] { return BuildStatusMenu(); };
        m_tray->Invoked.Add([this](POINT anchor) { OnTrayInvoked(anchor); });
        UpdateTrayVisibility();

        settings.Changed.Add([this](std::wstring_view key) { OnSettingChanged(key); });
        ApplyHotKeys();
        RefreshLaunchAtStartup();
        Exporter::CleanDragOutFolder(); // Also at launch
        // ObjImage's G key and "saveGIF" option.
        Core::Environment().saveGif = [this](Core::ProtoObject const& object) {
            if (auto const* overlay = dynamic_cast<Core::ObjImage const*>(&object))
            {
                if (auto const bitmap = overlay->Image())
                {
                    SaveBeforeAfterGif(*bitmap, overlay->rect);
                }
            }
        };

        // The empty editor (the intro) shows at launch unless showIntro != 1 and
        // the hotkeys page was visited.
        if (settings.ShowIntro() == 1 || !settings.EverVisitedHotkeys())
        {
            ShowEditor();
        }
        // Loads the Library's database and starts its 1200 s rescans (idle while libEnabled is off).
        LibraryIndex::Current().Start();
    }

    void AppController::Shutdown()
    {
        if (m_shuttingDown)
        {
            return;
        }
        m_shuttingDown = true;
        for (auto& timer : m_timers)
        {
            timer.Stop();
        }
        m_timers.clear();
        if (m_hotKeys)
        {
            for (std::size_t i = 0; i < Core::AllHotKeys.size(); ++i)
            {
                m_hotKeys->Unregister(kHotKeyIdBase + static_cast<int>(i));
            }
        }
        if (m_tray)
        {
            m_tray->Hide();
        }
        Exporter::CleanDragOutFolder(); // and at quit
        m_pins.clear();
        m_preview.reset();
        Notifier::Shutdown();
        LibraryIndex::Current().Shutdown();
        if (auto library = std::exchange(m_libraryWindow, nullptr))
        {
            library.Close();
        }
        if (m_settingsWindow)
        {
            m_settingsWindow.Close();
            m_settingsWindow = nullptr;
        }
        if (m_editor)
        {
            m_editor.Destroy();
            m_editor = nullptr;
        }
    }

    void AppController::Quit()
    {
        Shutdown();
        winrt::Application::Current().Exit();
    }

    void AppController::After(std::chrono::milliseconds delay, std::function<void()> action)
    {
        auto dispatcher = winrt::DispatcherQueue::GetForCurrentThread();
        if (!dispatcher)
        {
            return;
        }
        auto timer = dispatcher.CreateTimer();
        timer.Interval(delay);
        timer.IsRepeating(false);
        timer.Tick([this, action = std::move(action)](winrt::DispatcherQueueTimer const& sender, auto&&) {
            sender.Stop();
            std::erase_if(m_timers, [&](auto const& t) { return t == sender; });
            try
            {
                action();
            }
            catch (...)
            {
                Log(L"exception in a timer action");
            }
        });
        m_timers.push_back(timer);
        timer.Start();
    }

    // ---- Capture ----

    namespace
    {
        // A popup menu (the tray menu) still on screen, e.g. fading out after the click.
        bool IsMenuShowing()
        {
            HWND menu = nullptr;
            while ((menu = FindWindowExW(nullptr, menu, L"#32768", nullptr)) != nullptr)
            {
                if (IsWindowVisible(menu))
                {
                    return true;
                }
            }
            return false;
        }
    }

    void AppController::GrabScreenshot(std::wstring_view mode, std::wstring_view source, CaptureActions actions)
    {
        // Status menu is gone when its action runs; the Windows tray menu fades out after
        // TrackPopupMenu returns, so wait for it (at most 0.5 s) to keep it out of the capture.
        std::wstring const modeText{ mode };
        std::wstring const sourceText{ source };
        if (WaitForMenu(source, [this, modeText, sourceText, actions] { GrabScreenshot(modeText, sourceText, actions); }))
        {
            return;
        }
        Log(L"grab " + std::wstring{ mode } + L" (" + std::wstring{ source } + L")");
        // No screen-recording permission on Windows. windowShadow -> shadow mode, unknown = 0.
        int const shadow = static_cast<int>(Core::ParseShadowMode(Settings::Current().WindowShadow()).value_or(Core::ShadowMode::Transparent));
        if (mode == L"window")
        {
            GrabActiveWindow(shadow, actions);
        }
        else if (mode == L"fullscreen" || mode == L"delayed")
        {
            GrabFullscreen(std::wstring{ mode }, actions);
        }
        else if (mode == L"interactive")
        {
            GrabArea(actions);   // always the RegionSelector (areaCustomGrabber has no effect)
        }
        else if (mode == L"interactive-repeat")
        {
            GrabRepeatArea(actions);
        }
        else if (mode == L"interactive-window")
        {
            GrabAnyWindow(shadow, actions);
        }
        else
        {
            Log(L"Unrecognized capture mode: " + std::wstring{ mode });
        }
    }

    bool AppController::WaitForMenu(std::wstring_view source, std::function<void()> retry)
    {
        if (source == L"menu" && IsMenuShowing() && m_menuWaits < 30)
        {
            ++m_menuWaits;
            After(std::chrono::milliseconds(16), std::move(retry));
            return true;
        }
        m_menuWaits = 0;
        return false;
    }

    void AppController::GrabFullscreen(std::wstring const& mode, CaptureActions actions)
    {
        // One display: the foreground window's monitor, else the cursor's.
        auto const monitor = ScreenCapture::MainMonitor();
        if (!monitor)
        {
            Log(L"no monitor to capture");
            return;
        }
        // captureCursor: "include" draws the pointer; "auto" and "exclude" leave it out.
        bool const includeCursor = Settings::Current().CaptureCursor() == L"include";
        auto bitmap = ScreenCapture::CaptureMonitor(*monitor, includeCursor);
        if (!bitmap)
        {
            Log(L"fullscreen capture failed");
            return;
        }
        CaptureInfo info;
        if (auto const window = ScreenCapture::AttributeArea(monitor->bounds))
        {
            info.app = window->app;
            info.window = window->title;
        }
        AfterCapture(std::move(*bitmap), mode, actions, std::move(info));
    }

    namespace
    {
        // For a window image: trimmed crops the (absent) shadow; wallpaper and solid fill what is
        // transparent. Windows draws no shadow, so a window image has opaque edges and these modes
        // leave it as it is (the wallpaper and colour fills paint only under transparent pixels).
        Core::Bitmap ApplyShadowMode(Core::Bitmap image, int shadow, bool trimShadow)
        {
            switch (static_cast<Core::ShadowMode>(shadow))
            {
            case Core::ShadowMode::Trimmed:
                return trimShadow ? Core::TrimShadow(std::move(image)) : image;
            case Core::ShadowMode::Wallpaper:
                if (!Core::IsEdgeTransparent(image))
                {
                    return image;   // addWallpaper step 1, without looking up the wallpaper
                }
                return Core::AddWallpaper(std::move(image), ScreenCapture::PrimaryWallpaper());
            case Core::ShadowMode::Solid:
            {
                auto const color = Core::Color::FromHex(Settings::Current().WindowSolidColor()).value_or(Core::Color{ 0x40, 0x44, 0x48, 0xFF });
                return Core::AddColor(image, color);
            }
            default:
                return image;
            }
        }

        RECT ToRect(Core::Rect const& rect)
        {
            return RECT{ static_cast<LONG>(rect.x), static_cast<LONG>(rect.y), static_cast<LONG>(rect.x + rect.width), static_cast<LONG>(rect.y + rect.height) };
        }

        // App and window of the frontmost normal window under the rect's centre.
        void AttributeCentre(RECT const& rect, std::wstring& app, std::wstring& window)
        {
            LONG const x = rect.left + (rect.right - rect.left) / 2;
            LONG const y = rect.top + (rect.bottom - rect.top) / 2;
            if (auto const found = ScreenCapture::AttributeArea(RECT{ x, y, x + 1, y + 1 }))
            {
                app = found->app;
                window = found->title;
            }
        }
    }

    void AppController::GrabActiveWindow(int shadow, CaptureActions actions)
    {
        // The first normal window over 70 × 70 pt, front to back.
        auto const window = ScreenCapture::FrontmostWindow();
        if (!window)
        {
            Notify({ Strings::Get(L"Capture_NoWindowsTitle"), Strings::Get(L"Capture_NoWindowsMessage") });
            return;
        }
        if (!window->opaque)
        {
            GrabFullscreen(L"window", actions);
            return;
        }
        auto image = ScreenCapture::CaptureWindow(*window);
        if (!image)
        {
            Log(L"Window grab failed, frontmost window: " + window->app + L" " + window->title);
            GrabAnyWindow(shadow, actions);
            return;
        }
        if (image->height < 40 * window->scale)
        {
            GrabFullscreen(L"window", actions);
            return;
        }
        CaptureInfo info{ window->app, window->title, false };
        if (shadow >= static_cast<int>(Core::ShadowMode::Wallpaper))
        {
            info.backdropApplied = Core::IsEdgeTransparent(*image);
        }
        AfterCapture(ApplyShadowMode(std::move(*image), shadow, true), L"window", actions, std::move(info));
    }

    void AppController::GrabAnyWindow(int shadow, CaptureActions actions)
    {
        // The RegionSelector's window mode picks the window.
        Platform::RegionOptions options;
        options.mode = Platform::RegionMode::Window;
        Platform::SelectRegion(options, [this, shadow, actions](std::optional<Platform::RegionSelection> selection, auto&&) {
            if (!selection || !selection->window)
            {
                m_rememberedArea.reset();   // a cancelled capture forgets the area (EOF handler)
                return;
            }
            auto const window = ScreenCapture::DescribeWindow(selection->window);
            auto image = window ? ScreenCapture::CaptureWindow(*window) : std::nullopt;
            if (!image)
            {
                Log(L"Any Window capture failed");
                return;
            }
            m_rememberedArea = window->bounds;
            CaptureInfo info{ window->app, window->title, false };
            if (shadow >= static_cast<int>(Core::ShadowMode::Wallpaper))
            {
                info.backdropApplied = Core::IsEdgeTransparent(*image);
            }
            // `-o` already removed the shadow for "trimmed": nothing to trim here.
            AfterCapture(ApplyShadowMode(std::move(*image), shadow, false), L"interactive-window", actions, std::move(info));
        });
    }

    void AppController::GrabArea(CaptureActions actions)
    {
        // Frozen RegionSelector; crops the frozen display to the cutout.
        Platform::RegionOptions options;
        options.mode = Platform::RegionMode::Area;
        Platform::SelectRegion(options, [this, actions](std::optional<Platform::RegionSelection> selection, auto&&) {
            if (!selection || !selection->frozen)
            {
                return;   // Esc or under 5 × 5 pt: nothing is captured
            }
            Core::Rect const r = selection->imageRect;
            Core::Bitmap image = Core::CropBitmap(*selection->frozen, static_cast<int>(r.x), static_cast<int>(r.y),
                                                  static_cast<int>(r.width), static_cast<int>(r.height));
            // The custom grabber always remembers the area; the window shadow modes never apply.
            RECT const area = ToRect(selection->screenRect);
            m_rememberedArea = area;
            CaptureInfo info;
            AttributeCentre(area, info.app, info.window);
            AfterCapture(std::move(image), L"interactive", actions, std::move(info));
        });
    }

    void AppController::GrabRepeatArea(CaptureActions actions)
    {
        // A fresh capture of the remembered rect.
        if (!m_rememberedArea)
        {
            Notify({ Strings::Get(L"Capture_NoAreaTitle"), Strings::Get(L"Capture_NoAreaMessage") });
            return;
        }
        RECT const area = *m_rememberedArea;
        auto image = ScreenCapture::CaptureRect(area);
        if (!image)
        {
            Log(L"Repeat Area capture failed");
            return;
        }
        CaptureInfo info;
        AttributeCentre(area, info.app, info.window);
        AfterCapture(std::move(*image), L"interactive-repeat", actions, std::move(info));
    }

    void AppController::GrabDelayed(double seconds, std::wstring_view source, CaptureActions actions)
    {
        std::wstring const method{ source };
        auto const delay = std::chrono::milliseconds(static_cast<std::int64_t>((std::max)(seconds, 0.0) * 1000));
        After(delay, [this, method, actions] { GrabScreenshot(L"delayed", method, actions); });
    }

    void AppController::GrabScrolling(int direction, std::wstring_view source, CaptureActions actions)
    {
        // Scrolling capture. Windows needs no
        // accessibility permission, so permissionsGranted() always passes.
        Log(L"grab:scrolling " + std::wstring{ source } + L" (direction " + std::to_wstring(direction) + L")");
        auto& settings = Settings::Current();
        if (!settings.EverScrollingCapture())
        {
            // The intro alert, once per install; dismissing it retries after 0.5 s.
            std::wstring const title = Strings::Get(L"Scrolling_IntroTitle");
            std::wstring const text = Strings::Get(L"Scrolling_IntroMessage");
            MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
            settings.EverScrollingCapture(true);
            std::wstring const method{ source };
            After(std::chrono::milliseconds(500), [this, direction, method, actions] { GrabScrolling(direction, method, actions); });
            return;
        }
        ScrollCapture::StartAuto(direction, [this, actions](std::optional<Core::Bitmap> image) {
            // Open as a capture of kind "scrolling".
            if (!image || image->IsEmpty())
            {
                Log(L"Scrolling capture: failed to get the screenshot image");
                return;
            }
            AfterCapture(std::move(*image), L"scrolling", actions);
        });
    }

    void AppController::GrabScrollingManual(std::wstring_view source)
    {
        // Manual scrolling capture: no permission check, no intro.
        Log(L"grab:scrolling manual (" + std::wstring{ source } + L")");
        if (IsEditorVisible())
        {
            HideEditor();
        }
        ScrollCapture::StartManual([this](std::optional<Core::Bitmap> image) {
            if (!image || image->IsEmpty())
            {
                Log(L"Manual scrolling capture: failed to get the screenshot image");
                return;
            }
            AfterCapture(std::move(*image), L"scrolling", {});
        });
    }

    void AppController::AppendCapture()
    {
        auto const document = m_document;
        if (!document)
        {
            Notify({ Strings::Get(L"Capture_NoScreenshotTitle"), Strings::Get(L"Capture_NoScreenshotMessage"), true, 5.0, true });
            return;
        }
        // Hide the editor, then an area capture that remembers the
        // area (windowShadow invalid -> wallpaper, but area selections never get a background).
        // The selector freezes the screen at once, so the editor must be off it first: hide it
        // without the DWM fade and wait for the next composed frame.
        if (HWND const editor = IsEditorVisible() ? EditorHandle() : nullptr)
        {
            BOOL disable = TRUE;
            DwmSetWindowAttribute(editor, DWMWA_TRANSITIONS_FORCEDISABLED, &disable, sizeof(disable));
            HideEditor();
            DwmFlush();
            disable = FALSE;
            DwmSetWindowAttribute(editor, DWMWA_TRANSITIONS_FORCEDISABLED, &disable, sizeof(disable));
        }
        Platform::RegionOptions options;
        options.mode = Platform::RegionMode::Area;
        Platform::SelectRegion(options, [this, document](std::optional<Platform::RegionSelection> selection, auto&&) {
            if (!selection || !selection->frozen)
            {
                return;
            }
            Core::Rect const r = selection->imageRect;
            auto image = std::make_shared<Core::Bitmap const>(Core::CropBitmap(*selection->frozen, static_cast<int>(r.x), static_cast<int>(r.y),
                                                                                 static_cast<int>(r.width), static_cast<int>(r.height)));
            m_rememberedArea = ToRect(selection->screenRect);
            // R is the capture's rect; Eikon measures it from the screenshot's own
            // screen origin so the object lands next to where that area is in the screenshot.
            Core::Rect captureRect = selection->screenRect;
            Core::Rect const& origin = document->Image().screenRect;
            if (!origin.IsEmpty())
            {
                captureRect = captureRect.Offset(-origin.x, -origin.y);
            }
            Core::Size const size{ static_cast<double>(image->width), static_cast<double>(image->height) };
            Core::Point const position = Core::AddCapturePosition(captureRect, document->Crop().Right(), size, document->Image().dpr);
            auto object = Core::CreateImageObject(image, Core::Rect{ position.x, position.y, size.width, size.height }, Settings::Current());
            if (!object)
            {
                Log(L"Add Capture: can't create the image object");
            }
            else
            {
                document->Add(std::move(object));
            }
            if (EnsureEditor())
            {
                winrt::get_self<winrt::Eikon::implementation::EditorWindow>(m_editor)->ShowDocument(document, true);
            }
        });
    }

    void AppController::InstantOcr(std::wstring_view source)
    {
        // Instant OCR with the RegionSelector: the selected pixels go to OCR.
        std::wstring const method{ source };
        if (WaitForMenu(source, [this, method] { InstantOcr(method); }))
        {
            return;
        }
        Platform::RegionOptions options;
        options.mode = Platform::RegionMode::Ocr;
        Platform::SelectRegion(options, [this, method](std::optional<Platform::RegionSelection> selection, auto&&) {
            if (!selection || !selection->frozen)
            {
                Notify({ Strings::Get(L"Capture_OcrNoAreaTitle"), Strings::Get(L"Capture_OcrNoAreaMessage") });
                return;
            }
            Core::Rect const r = selection->imageRect;
            auto image = std::make_shared<Core::Bitmap const>(Core::CropBitmap(*selection->frozen, static_cast<int>(r.x), static_cast<int>(r.y),
                                                                                 static_cast<int>(r.width), static_cast<int>(r.height)));
            RecognizeText(std::move(image), method);
        });
    }

    void AppController::LoadFromClipboard(std::wstring_view source)
    {
        // From the clipboard: no after-capture actions, straight to the editor.
        Log(L"load from clipboard (" + std::wstring{ source } + L")");
        auto image = ImageLoader::ReadClipboard(MessageWindowHandle());
        if (!image)
        {
            Notify({ Strings::Get(L"Capture_ClipboardFailedTitle"), Strings::Get(L"Capture_ClipboardFailedMessage") });
            return;
        }
        auto const monitor = ScreenCapture::MainMonitor();
        image->dpr = monitor ? monitor->scale : 1.0;
        auto document = std::make_shared<Core::Document>(std::move(*image));
        document->metadata.mode = L"clipboard";
        document->metadata.screenshotDate = std::chrono::system_clock::now();
        m_document = document;
        if (EnsureEditor())
        {
            winrt::get_self<winrt::Eikon::implementation::EditorWindow>(m_editor)->ShowDocument(document);
        }
    }

    void AppController::OpenFile(std::wstring_view source)
    {
        Log(L"open file (" + std::wstring{ source } + L")");
        auto const path = ImageLoader::ChooseImageFile(EditorHandle(), Strings::Get(L"Capture_OpenImageTitle"));
        if (!path)
        {
            return;
        }
        auto image = ImageLoader::DecodeFile(*path);
        if (!image)
        {
            Log(L"Failed to open image file");
            return;
        }
        auto const monitor = ScreenCapture::MainMonitor();
        image->dpr = monitor ? monitor->scale : 1.0;
        auto document = std::make_shared<Core::Document>(std::move(*image));
        document->metadata.mode = L"file";
        document->metadata.screenshotDate = std::chrono::system_clock::now();
        m_document = document;
        if (EnsureEditor())
        {
            winrt::get_self<winrt::Eikon::implementation::EditorWindow>(m_editor)->ShowDocument(document);
        }
    }

    void AppController::AfterCapture(Core::Bitmap bitmap, std::wstring_view mode, CaptureActions actions, CaptureInfo info)
    {
        auto& settings = Settings::Current();
        std::wstring const modeText{ mode };
        bool const show = settings.AfterGrabShow();
        std::wstring const areaCaptureMode = settings.AreaCaptureMode();

        // Step 1: the first area capture asks once and remembers the answer, then starts over.
        if (Core::NeedsAreaCaptureQuestion(actions, mode, show, areaCaptureMode))
        {
            auto held = std::make_shared<Core::Bitmap>(std::move(bitmap));
            // The first-capture question as a card (Platform/FirstCaptureCard): "Small preview" -> "preview",
            // "Open the editor" (the default, also Esc) -> "editor".
            After(std::chrono::milliseconds(30), [this, held, modeText, actions, info] {
                FirstCaptureCard::Ask([this, held, modeText, actions, info](std::wstring const& answer) {
                    Settings::Current().AreaCaptureMode(answer);
                    AfterCapture(std::move(*held), modeText, actions, info);
                });
            });
            return;
        }

        // Step 2: the thumbnail replaces the editor for area captures.
        if (mode == L"interactive" && show && areaCaptureMode == L"preview")
        {
            HideEditor();
        }

        // Steps 3–5. The bitmap keeps its native pixels and the capturing monitor's scale.
        auto document = std::make_shared<Core::Document>(std::move(bitmap));
        document->metadata.mode = modeText;
        document->metadata.screenshotDate = std::chrono::system_clock::now();
        document->metadata.app = std::move(info.app);
        document->metadata.window = std::move(info.window);
        document->metadata.backdropApplied = info.backdropApplied;
        m_document = document;

        // Step 7.
        auto const act = Core::ResolveCaptureActions(actions, mode, show, settings.AfterGrabCopy(), settings.AfterGrabSave(), areaCaptureMode);

        // Step 8.
        if (act.copy)
        {
            CopyImage();
        }
        if (act.save)
        {
            After(std::chrono::milliseconds(100), [this] { SaveImage(); });
        }
        if (act.pin)
        {
            PinImage(document->ImagePtr());
        }
        // Step 4: the image is always loaded into the editor, so Reopen shows it after a thumbnail
        // or a capture that shows nothing; the editor itself is shown only for "edit".
        if (EnsureEditor())
        {
            // Loads the image, sizes and centres the window on the capture's monitor.
            winrt::get_self<winrt::Eikon::implementation::EditorWindow>(m_editor)->ShowDocument(document, false, act.edit && !act.thumbnail);
        }
        if (act.thumbnail)
        {
            ShowThumbnail(document);
        }
        else if (!act.edit && m_tray)
        {
            m_tray->ShowOk();   // eikon-22-ok for 1.5 s
        }
    }

    // ---- Image output ----

    namespace
    {
        Exporter::Corners CornersOf(Core::Document const& document)
        {
            if (document.backdrop.config.isOn != 1)
            {
                return {};
            }
            auto const layout = Editor::BackdropRender::DocumentLayout(document);
            return { layout.outerPx, layout.innerPx, layout.pads.bt, layout.pads.bb, layout.pads.bl, layout.pads.br };
        }

        // The metadata's autoName, else a new name from fileNameTemplate.
        std::wstring BaseName(Core::Document const& document)
        {
            if (!document.metadata.autoName.empty())
            {
                return document.metadata.autoName;
            }
            auto date = document.metadata.screenshotDate;
            if (date == std::chrono::system_clock::time_point{})
            {
                date = std::chrono::system_clock::now();
            }
            return Core::FileName::Generate(Settings::Current().FileNameTemplate(), Core::FileName::LocalParts(date), document.metadata.app);
        }

        // The Library fields known when
        // an image is saved (the analysis adds the rest).
        Core::Library::Record SaveTimeFields(Core::Document const& document)
        {
            namespace Lib = Core::Library;
            Lib::Record fields;
            auto const& metadata = document.metadata;
            if (!metadata.app.empty()) fields[std::wstring{ Lib::kApp }] = metadata.app;
            if (!metadata.window.empty()) fields[std::wstring{ Lib::kWindow }] = metadata.window;
            fields[std::wstring{ Lib::kDpr }] = Lib::DprField(document.Image().dpr);
            if (!metadata.mode.empty()) fields[std::wstring{ Lib::kMode }] = metadata.mode;
            std::vector<std::wstring_view> classes;
            std::vector<std::wstring_view> texts;
            for (auto const& object : document.Objects())
            {
                if (!object)
                {
                    continue;
                }
                classes.push_back(object->ClassName());
                if (auto const* text = dynamic_cast<Core::ObjText const*>(object.get()))
                {
                    texts.push_back(text->text);
                }
            }
            fields[std::wstring{ Lib::kLabels }] = Lib::LabelsField(texts);
            fields[std::wstring{ Lib::kObjects }] = Lib::ObjectsField(classes);
            return fields;
        }

        void NotifySaveFailed()
        {
            Notifier::Show({ Strings::Get(L"Output_SaveFailedTitle"), Strings::Get(L"Output_SaveFailedMessage"), true, 5.0, true });
        }

        // "Click to open in File Explorer" with the folder path; the button shows the file.
        void NotifySaved(std::filesystem::path const& file, std::wstring const& button)
        {
            Notifier::Show({ Strings::Get(L"Output_SavedTitle"), file.parent_path().wstring(), false, 3.0, false, button,
                             [file] { Exporter::ShowInExplorer(file); } });
        }

        // `bitmap` resized to width × height (high-quality cubic).
        Core::Bitmap Resized(Core::Bitmap const& bitmap, int width, int height)
        {
            if (bitmap.width == width && bitmap.height == height)
            {
                return bitmap;
            }
            Core::Bitmap out = Core::Bitmap::Create(width, height, bitmap.dpr);
            try
            {
                namespace canvas = winrt::Microsoft::Graphics::Canvas;
                auto const device = canvas::CanvasDevice::GetSharedDevice();
                auto const source = canvas::CanvasBitmap::CreateFromBytes(device, winrt::array_view<std::uint8_t const>(bitmap.pixels),
                    bitmap.width, bitmap.height, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 96.0f,
                    canvas::CanvasAlphaMode::Premultiplied);
                canvas::CanvasRenderTarget target(device, static_cast<float>(width), static_cast<float>(height), 96.0f);
                {
                    auto const session = target.CreateDrawingSession();
                    session.Clear(winrt::Windows::UI::Colors::Transparent());
                    session.DrawImage(source, winrt::Windows::Foundation::Rect{ 0, 0, static_cast<float>(width), static_cast<float>(height) },
                                      source.Bounds(), 1.0f, canvas::CanvasImageInterpolation::HighQualityCubic);
                    session.Close();
                }
                auto const bytes = target.GetPixelBytes();
                std::memcpy(out.pixels.data(), bytes.data(), std::min<std::size_t>(bytes.size(), out.pixels.size()));
            }
            catch (...)
            {
                Log(L"resizing a GIF frame failed");
            }
            return out;
        }

        Core::Bitmap CropOf(Core::Bitmap const& bitmap, int x, int y, int width, int height)
        {
            x = std::clamp(x, 0, bitmap.width);
            y = std::clamp(y, 0, bitmap.height);
            width = std::clamp(width, 0, bitmap.width - x);
            height = std::clamp(height, 0, bitmap.height - y);
            Core::Bitmap out = Core::Bitmap::Create(width, height, bitmap.dpr);
            for (int row = 0; row < height; ++row)
            {
                std::memcpy(out.pixels.data() + out.Stride() * row, bitmap.pixels.data() + bitmap.Stride() * (y + row) + static_cast<std::size_t>(x) * 4,
                            out.Stride());
            }
            return out;
        }
    }

    HWND AppController::EditorHandle() const
    {
        HWND hwnd = nullptr;
        if (m_editor)
        {
            if (auto native = m_editor.try_as<::IWindowNative>())
            {
                native->get_WindowHandle(&hwnd);
            }
        }
        return hwnd;
    }

    bool AppController::CopyImage()
    {
        if (!m_document)
        {
            Log(L"No image to copy");
            return false;
        }
        // Clear the clipboard first so a fast paste doesn't get stale data.
        Exporter::ClearClipboard(MessageWindowHandle());
        auto const image = Editor::Compose(*m_document);
        bool const ok = image && Exporter::CopyToClipboard(*image, MessageWindowHandle());
        if (!ok)
        {
            Log(L"copying the image failed");
        }
        return ok;
    }

    void AppController::SaveImage()
    {
        if (!m_document)
        {
            Log(L"NOTE: No image to save");
            return;
        }
        auto& settings = Settings::Current();
        auto const image = Exporter::DownscaleForSave(Editor::Compose(*m_document), settings.DownscaleOnSave());
        if (!image || image->IsEmpty())
        {
            Log(L"NOTE: No image to save");
            return;
        }
        std::wstring const base = BaseName(*m_document);
        bool const overwrite = settings.AutosaveOverwrite() && !m_document->metadata.autoName.empty();
        std::filesystem::path const folder = Exporter::ScreenshotsFolder();
        std::optional<std::filesystem::path> path;
        for (int attempt = 1; attempt <= 2 && !path; ++attempt)
        {
            path = Exporter::SaveToFolder(*image, CornersOf(*m_document), settings.SaveFormat(), folder, base, overwrite);
            if (!path)
            {
                Log(L"Can't save file, attempt " + std::to_wstring(attempt));
            }
        }
        if (!path)
        {
            NotifySaveFailed();
            return;
        }
        m_document->metadata.autoName = base;
        LibraryIndex::Current().RecordSaved(path->wstring(), SaveTimeFields(*m_document));
        NotifySaved(*path, Strings::Get(L"Output_SavedButton"));
    }

    void AppController::SaveImageAs()
    {
        if (!m_document)
        {
            Log(L"NOTE: No image to save");
            return;
        }
        auto const image = Exporter::DownscaleForSave(Editor::Compose(*m_document), Settings::Current().DownscaleOnSave());
        if (!image || image->IsEmpty())
        {
            return;
        }
        bool failed = false;
        auto const path = Exporter::SaveAs(EditorHandle() ? EditorHandle() : MessageWindowHandle(), *image, BaseName(*m_document), failed);
        if (failed)
        {
            NotifySaveFailed();
        }
        else if (path)
        {
            LibraryIndex::Current().RecordSaved(path->wstring(), SaveTimeFields(*m_document));
        }
    }

    void AppController::PrintImage()
    {
        if (!m_document)
        {
            return;
        }
        auto const image = Editor::Compose(*m_document);
        if (image && !image->IsEmpty())
        {
            Exporter::Print(EditorHandle(), *image, Settings::Current().PrintOrientation());
        }
    }

    std::optional<std::filesystem::path> AppController::WriteDragOutFile()
    {
        if (!m_document)
        {
            return std::nullopt;
        }
        auto& settings = Settings::Current();
        auto const image = Exporter::DownscaleForSave(Editor::Compose(*m_document), settings.DownscaleOnSave());
        if (!image || image->IsEmpty())
        {
            return std::nullopt;
        }
        Exporter::CleanDragOutFolder();
        std::wstring const base = BaseName(*m_document);
        auto const path = Exporter::SaveToFolder(*image, CornersOf(*m_document), settings.SaveFormat(), Exporter::DragOutFolder(), base, false);
        if (!path)
        {
            return std::nullopt;
        }
        m_document->metadata.autoName = base;
        return path;
    }

    void AppController::SaveBeforeAfterGif(Core::Bitmap const& overlay, Core::Rect const& overlayRect)
    {
        if (!m_document || overlay.IsEmpty())
        {
            return;
        }
        // (ObjImage): frame 1 = the overlay inside the screenshot, frame 2 = the
        // screenshot under it.
        Core::Rect const rect = overlayRect.Standardized();
        Core::Rect const inside = rect.Intersection(m_document->Source());
        if (!rect.Intersects(m_document->Source()))
        {
            MessageBeep(MB_ICONWARNING);
            std::wstring const title = Strings::Get(L"Output_GifOverlapTitle");
            std::wstring const text = Strings::Get(L"Output_GifOverlapMessage");
            MessageBoxW(EditorHandle(), text.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
            return;
        }
        if (inside.width < 1 || inside.height < 1)
        {
            Log(L"Zero pixels selected: ");
            return;
        }
        double const sx = overlay.width / rect.width;
        double const sy = overlay.height / rect.height;
        Core::Bitmap first = CropOf(overlay, static_cast<int>(std::lround((inside.x - rect.x) * sx)), static_cast<int>(std::lround((inside.y - rect.y) * sy)),
                                    static_cast<int>(std::lround(inside.width * sx)), static_cast<int>(std::lround(inside.height * sy)));
        Core::Bitmap second = CropOf(m_document->Image(), static_cast<int>(inside.x), static_cast<int>(inside.y), static_cast<int>(inside.width),
                                     static_cast<int>(inside.height));
        // Both frames at the larger of the two sizes.
        bool const firstLarger = static_cast<double>(first.width) * first.height >= static_cast<double>(second.width) * second.height;
        int const w = firstLarger ? first.width : second.width;
        int const h = firstLarger ? first.height : second.height;
        if (w <= 0 || h <= 0)
        {
            return;
        }
        auto const bytes = Exporter::EncodeGif(Resized(first, w, h), Resized(second, w, h), 0.4);
        std::filesystem::path const folder = Exporter::ScreenshotsFolder();
        std::optional<std::filesystem::path> path;
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        for (int attempt = 1; attempt <= 2 && !path && !bytes.empty(); ++attempt)
        {
            auto const candidate = Exporter::FreeFileName(folder, BaseName(*m_document), L".gif");
            if (Exporter::WriteFileAtomic(candidate, bytes))
            {
                path = candidate;
            }
        }
        if (!path)
        {
            Log(L"Can't save GIF file");
            NotifySaveFailed();
            return;
        }
        auto& settings = Settings::Current();
        if (!settings.ReadBool(L"everCreatedGIF", false))
        {
            std::wstring const title = Strings::Get(L"Output_GifHelpTitle");
            std::wstring const text = Strings::Get(L"Output_GifHelpMessage");
            MessageBoxW(EditorHandle(), text.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
        }
        settings.WriteBool(L"everCreatedGIF", true);
        NotifySaved(*path, Strings::Get(L"Output_GifShowButton"));
    }

    // ---- Thumbnail, pins, OCR and upload ----

    namespace
    {
        // Text as CF_UNICODETEXT.
        bool CopyTextToClipboard(std::wstring const& text, HWND owner)
        {
            if (!OpenClipboard(owner))
            {
                return false;
            }
            bool ok = false;
            EmptyClipboard();
            std::size_t const bytes = (text.size() + 1) * sizeof(wchar_t);
            if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes))
            {
                if (void* data = GlobalLock(memory))
                {
                    std::memcpy(data, text.c_str(), bytes);
                    GlobalUnlock(memory);
                    ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
                }
                if (!ok)
                {
                    GlobalFree(memory);
                }
            }
            CloseClipboard();
            return ok;
        }

        POINT RectCenter(RECT const& r) noexcept
        {
            return { r.left + (r.right - r.left) / 2, r.top + (r.bottom - r.top) / 2 };
        }
    }

    void AppController::OpenInEditor(std::shared_ptr<Core::Document> document)
    {
        if (!document || !EnsureEditor())
        {
            return;
        }
        if (m_preview)
        {
            m_preview->Hide();
        }
        m_document = document;
        winrt::get_self<winrt::Eikon::implementation::EditorWindow>(m_editor)->ShowDocument(document);
    }

    void AppController::HideAll()
    {
        if (m_preview)
        {
            m_preview->Hide();
        }
        if (IsEditorVisible())
        {
            if (HWND editor = EditorHandle())
            {
                Notifier::SetLastActiveMonitor(MonitorFromWindow(editor, MONITOR_DEFAULTTONEAREST));
            }
            HideEditor();
        }
    }

    void AppController::ShowThumbnail(std::shared_ptr<Core::Document> document)
    {
        if (!document || document->Image().IsEmpty() || m_shuttingDown)
        {
            return;
        }
        if (!m_preview)
        {
            m_preview = std::make_unique<PreviewWindow>();   // one for the app's lifetime
        }
        // Next to the drawn capture rect; fullscreen and file/clipboard images have none.
        std::optional<Core::Rect> rect;
        Core::Rect const screenRect = document->Image().screenRect;
        if (document->metadata.mode != L"fullscreen" && !screenRect.IsEmpty())
        {
            rect = screenRect;
        }
        auto show = [this, document, rect] {
            if (m_preview && !m_shuttingDown)
            {
                m_preview->Show(document, rect);
            }
        };
        // With the editor or settings open, close settings, hide all, show 0.1 s later.
        if (IsEditorVisible() || m_settingsWindow)
        {
            if (m_settingsWindow)
            {
                m_settingsWindow.Close();
                m_settingsWindow = nullptr;
            }
            HideAll();
            After(std::chrono::milliseconds(100), show);
            return;
        }
        show();
    }

    void AppController::PinImage(std::shared_ptr<Core::Bitmap const> image)
    {
        if (!image || image->IsEmpty() || m_shuttingDown)
        {
            Log(L"Can't precompile image to pin");
            return;
        }
        // Centred on the editor if it is visible, else on the thumbnail, else on the main screen.
        RECT rect{};
        POINT center{};
        HWND const editor = EditorHandle();
        if (IsEditorVisible() && editor && GetWindowRect(editor, &rect))
        {
            center = RectCenter(rect);
        }
        else if (m_preview && m_preview->IsVisible() && GetWindowRect(m_preview->Handle(), &rect))
        {
            center = RectCenter(rect);
        }
        else
        {
            MONITORINFO info{ sizeof(info) };
            if (GetMonitorInfoW(MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &info))
            {
                center = RectCenter(info.rcMonitor);
            }
            else
            {
                center = { 400, 300 };
            }
        }
        auto pin = std::make_unique<PinWindow>(image, center, [this](PinWindow* closed) {
            // The window is gone; drop the controller once the message has unwound.
            After(std::chrono::milliseconds(0), [this, closed] { std::erase_if(m_pins, [&](auto const& p) { return p.get() == closed; }); });
        });
        HWND const handle = pin->Handle();
        if (!handle)
        {
            Log(L"Can't create the pin window");
            return;
        }
        m_pins.push_back(std::move(pin));
        Log(L"pins: " + std::to_wstring(m_pins.size()));
        HideAll();
        SetForegroundWindow(handle);
    }

    void AppController::RecognizeText(std::shared_ptr<Core::Bitmap const> image, std::wstring_view source)
    {
        if (!image || image->IsEmpty())
        {
            return;
        }
        // The editor's size limit.
        if (source == L"editor" && Core::OcrText::IsTooLargeForEditor(image->width, image->height))
        {
            Notify({ Strings::Get(L"Ocr_AreaTooLargeTitle"), Strings::Get(L"Ocr_AreaTooLargeMessage") });
            return;
        }
        // guard.
        if (m_ocrBusy)
        {
            Notify({ Strings::Get(L"Ocr_StillRunningTitle"), Strings::Get(L"Ocr_StillRunningMessage") });
            return;
        }
        if (!Ocr::IsReady())
        {
            Notify({ Strings::Get(L"Ocr_NotReadyTitle"), Strings::Get(L"Ocr_NotReadyMessage") });
            return;
        }
        RunOcr(std::move(image));
    }

    winrt::fire_and_forget AppController::RunOcr(std::shared_ptr<Core::Bitmap const> image)
    {
        // ocrBusy, the animated status icon, the engine off the UI thread, finish on it.
        m_ocrBusy = true;
        BeginBusy();
        winrt::apartment_context ui;
        winrt::hstring text;
        try
        {
            text = co_await Ocr::RecognizeAsync(image);
        }
        catch (...)
        {
            Log(L"Unable to perform the OCR requests");
        }
        co_await ui;
        m_ocrBusy = false;
        if (text.empty())
        {
            EndBusy(false);
            Notification notification{ Strings::Get(L"Ocr_NoTextTitle"), Strings::Get(L"Ocr_NoTextMessage") };
            notification.seconds = 5.0;
            notification.beep = true;
            Notify(std::move(notification));
            co_return;
        }
        EndBusy(true);
        m_lastOcrOriginal = std::wstring{ text };
        CopyOcrText(Settings::Current().OcrRemoveBreaks());
    }

    void AppController::CopyOcrText(bool removeBreaks)
    {
        // The button re-copies lastOCROriginal the other way without touching
        // ocrRemoveBreaks.
        std::wstring const text = removeBreaks ? Core::OcrText::RemoveBreaks(m_lastOcrOriginal) : m_lastOcrOriginal;
        CopyTextToClipboard(text, MessageWindowHandle());
        Notification notification;
        notification.title = Strings::Get(removeBreaks ? L"Ocr_CopiedNoBreaksTitle" : L"Ocr_CopiedTitle");
        notification.message = Core::OcrText::NotificationBody(text);
        notification.seconds = 5.0;
        notification.buttonTitle = Strings::Get(removeBreaks ? L"Ocr_AddBreaks" : L"Ocr_CutBreaks");
        notification.action = [this, removeBreaks] { CopyOcrText(!removeBreaks); };
        Notify(std::move(notification));
    }

    void AppController::UploadImage()
    {
        auto document = m_document;
        if (!document || document->Image().IsEmpty())
        {
            Notification notification{ Strings::Get(L"Upload_NoImageTitle"), Strings::Get(L"Upload_NoImageMessage") };
            notification.seconds = 5.0;
            notification.beep = true;
            Notify(std::move(notification));
            return;
        }
        // Only "s3" uploads; anything else is off.
        if (Settings::Current().UploadMode() != L"s3")
        {
            Notify({ Strings::Get(L"Upload_OffTitle"), Strings::Get(L"Upload_OffMessage") });
            return;
        }
        if (auto const missing = Uploader::CheckSettings())
        {
            Notification notification{ missing->title, missing->message };
            notification.seconds = 5.0;
            notification.beep = true;
            Notify(std::move(notification));
            return;
        }
        BeginBusy();
        Uploader::Upload(Editor::Compose(*document), document->metadata.screenshotDate, [this](Uploader::Result const& result) {
            EndBusy(result.ok);
            if (!result.ok)
            {
                Notification notification{ Strings::Get(L"Upload_FailedTitle"), result.message };
                notification.warning = true;
                notification.seconds = 5.0;
                notification.beep = true;
                Notify(std::move(notification));
                return;
            }
            if (result.url.empty())
            {
                Notify({ Strings::Get(L"Upload_CompleteTitle"), result.message });
                return;
            }
            CopyTextToClipboard(result.url, MessageWindowHandle());
            Notification notification;
            notification.title = Strings::Get(L"Upload_UrlCopiedTitle");
            notification.message = result.url.size() > 100 ? result.url.substr(0, 100) + L"..." : result.url;
            notification.buttonTitle = Strings::Get(L"Upload_Open");
            notification.action = [url = result.url] { ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); };
            Notify(std::move(notification));
        });
    }

    // ---- Windows ----

    void AppController::Reopen(std::wstring_view source)
    {
        Log(L"reopen (" + std::wstring{ source } + L")");
        ShowEditor();
    }

    bool AppController::EnsureEditor()
    {
        if (m_shuttingDown)
        {
            return false;
        }
        if (!m_editor)
        {
            // One editor window for the app's lifetime; closing only hides it.
            m_editor = winrt::make<winrt::Eikon::implementation::EditorWindow>();
        }
        return true;
    }

    void AppController::ShowEditor()
    {
        if (EnsureEditor())
        {
            m_editor.ShowEditor();
        }
    }

    void AppController::HideEditor()
    {
        if (m_editor)
        {
            m_editor.HideEditor();
        }
    }

    bool AppController::IsEditorVisible() const
    {
        return m_editor && m_editor.IsShown();
    }

    void AppController::OpenSettings(std::wstring_view page)
    {
        if (m_shuttingDown)
        {
            return;
        }
        try
        {
            if (!m_settingsWindow)
            {
                m_settingsWindow = winrt::make<winrt::Eikon::implementation::SettingsWindow>();
                m_settingsWindow.Closed([this](auto&&, auto&&) { m_settingsWindow = nullptr; });
            }
            if (!page.empty())
            {
                m_settingsWindow.ShowPage(winrt::hstring{ page });
            }
            m_settingsWindow.Activate();
        }
        catch (winrt::hresult_error const& error)
        {
            Log(L"opening Settings failed: " + std::wstring{ error.message() });
            m_settingsWindow = nullptr;
            return;
        }
        if (auto native = m_settingsWindow.try_as<::IWindowNative>())
        {
            HWND hwnd = nullptr;
            if (SUCCEEDED(native->get_WindowHandle(&hwnd)) && hwnd != nullptr)
            {
                ::ShowWindow(hwnd, ::IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
                ::SetForegroundWindow(hwnd);
            }
        }
    }

    void AppController::ToggleLibrary(std::wstring_view source)
    {
        if (!Settings::Current().LibEnabled())
        {
            // 5 s with a beep.
            Notify({ Strings::Get(L"Library_DisabledTitle"), Strings::Get(L"Library_DisabledMessage"), true, 5.0, true });
            return;
        }
        if (m_shuttingDown)
        {
            return;
        }
        Log(L"lib:show (" + std::wstring{ source } + L")");
        try
        {
            if (!m_libraryWindow)
            {
                CreateLibraryWindow();
            }
            winrt::get_self<winrt::Eikon::implementation::LibraryWindow>(m_libraryWindow)->Toggle();
        }
        catch (winrt::hresult_error const& error)
        {
            Log(L"opening the Library failed: " + std::wstring{ error.message() });
        }
    }

    void AppController::CreateLibraryWindow()
    {
        m_libraryWindow = winrt::make<winrt::Eikon::implementation::LibraryWindow>();
        // Alt+F4 (WM_CLOSE) destroys the popover's window: make a new one next time, as for Settings.
        m_libraryWindow.Closed([this](auto&&, auto&&) { m_libraryWindow = nullptr; });
    }

    void AppController::OpenLibrary(std::optional<std::wstring> search)
    {
        if (!Settings::Current().LibEnabled())
        {
            Notify({ Strings::Get(L"Library_DisabledTitle"), Strings::Get(L"Library_DisabledMessage"), true, 5.0, true });
            return;
        }
        if (m_shuttingDown)
        {
            return;
        }
        try
        {
            if (!m_libraryWindow)
            {
                CreateLibraryWindow();
            }
            winrt::get_self<winrt::Eikon::implementation::LibraryWindow>(m_libraryWindow)->Open(search);
        }
        catch (winrt::hresult_error const& error)
        {
            Log(L"opening the Library failed: " + std::wstring{ error.message() });
        }
    }

    bool AppController::OpenImageFile(std::wstring const& path, double dpr)
    {
        auto bitmap = LibraryFiles::Decode(path, dpr);
        if (!bitmap || !EnsureEditor())
        {
            return false;
        }
        auto document = std::make_shared<Core::Document>(std::move(*bitmap));
        document->metadata.mode = L"file";
        m_document = document;
        winrt::get_self<winrt::Eikon::implementation::EditorWindow>(m_editor)->ShowDocument(document);
        return true;
    }

    void AppController::OpenPastUploads()
    {
        Notify({ Strings::Get(L"Url_UploadsFailedTitle"), Strings::Get(L"Url_UploadsFailedMessage"), true, kUrlMessageSeconds, false });
    }

    // ---- URL scheme ----

    void AppController::UrlWarning(std::wstring const& title, std::wstring const& message)
    {
        Notify({ title, message, true, kUrlMessageSeconds, true });
    }

    void AppController::HandleUrl(std::wstring_view url)
    {
        auto const parsed = Core::ParseUrlCommand(url);
        if (!parsed)
        {
            Log(L"not an Eikon URL: " + std::wstring{ url });
            return;
        }
        auto& settings = Settings::Current();
        std::wstring const& command = parsed->command;
        Log(L"URL command: " + command);

        // Routes that need no permission, in the router's order.
        // Reserved commands: accepted and ignored.
        if (parsed->host == L"flop")
        {
            return;
        }
        if (command == L"settings/license")
        {
            return;
        }
        if (command == L"settings/scrolling/manual/enable")
        {
            settings.ScrollingManualEnabled(true); // the menu is rebuilt each time it opens
            return;
        }
        if (command == L"settings/scrolling/manual/disable")
        {
            settings.ScrollingManualEnabled(false);
            return;
        }
        // The parser drops trailing slashes, so the bare prefix arrives without its "/".
        if (StartsWith(command, L"settings/areacustomgrabber/") || command == L"settings/areacustomgrabber")
        {
            settings.AreaCustomGrabber(command.find(L"/enable") != std::wstring::npos);
            return;
        }
        // Prefix matches.
        if (StartsWith(command, L"settings/capturecursor/auto"))
        {
            settings.CaptureCursor(L"auto");
            return;
        }
        if (StartsWith(command, L"settings/capturecursor/include"))
        {
            settings.CaptureCursor(L"include");
            return;
        }
        if (StartsWith(command, L"settings/capturecursor/exclude"))
        {
            settings.CaptureCursor(L"exclude");
            return;
        }
        if (command.empty() || command == L"show")
        {
            Reopen(L"url");
            return;
        }

        // Everything else needs allowDeeplinks.
        auto& secrets = Secrets::Current();
        if (!secrets.AllowDeeplinks())
        {
            std::wstring const title = Strings::Get(L"Url_DeepLinksTitle");
            std::wstring const text = Strings::Format(L"Url_DeepLinksMessage", parsed->scheme, command);
            int const answer = MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_OKCANCEL | MB_ICONQUESTION | MB_SETFOREGROUND | MB_TOPMOST);
            secrets.AllowDeeplinks(answer == IDOK); // stores the answer either way
            if (answer != IDOK)
            {
                return;
            }
            // The confirm is still fading out on screen when MessageBoxW returns; a capture started at
            // once froze it into the screenshot, so the command waits for it to go.
            After(std::chrono::milliseconds(300), [this, command = *parsed] { RunGatedUrl(command); });
            return;
        }
        RunGatedUrl(*parsed);
    }

    void AppController::RunGatedUrl(Core::UrlCommand const& url)
    {
        auto& settings = Settings::Current();
        std::wstring const& command = url.command;

        // then= is parsed before dispatching any gated command, so the warning can appear
        // for non-capture routes too.
        auto const then = Core::ParseThenParameter(url);
        if (then.unrecognized)
        {
            UrlWarning(Strings::Get(L"Url_UnrecognizedThenTitle"), Strings::Get(L"Url_UnrecognizedThenMessage"));
        }
        CaptureActions const actions = then.actions;

        if (command == L"grab/fullscreen") { GrabScreenshot(L"fullscreen", L"url", actions); return; }
        if (command == L"grab/area") { GrabScreenshot(L"interactive", L"url", actions); return; }
        if (command == L"grab/repeat") { GrabScreenshot(L"interactive-repeat", L"url", actions); return; }
        if (command == L"grab/window") { GrabScreenshot(L"interactive-window", L"url", actions); return; }
        if (command == L"grab/window/active") { GrabScreenshot(L"window", L"url", actions); return; }
        if (command == L"grab/scrolling") { GrabScrolling(-1, L"url", actions); return; }
        if (command == L"grab/scrolling/reverse") { GrabScrolling(1, L"url", actions); return; }
        if (command == L"grab/delayed") { GrabDelayed(Core::DelayedCaptureSeconds(url), L"url", actions); return; }
        if (command == L"grab/append") { AppendCapture(); return; }
        if (command == L"ocr") { InstantOcr(L"url"); return; }
        if (command == L"load/clipboard") { LoadFromClipboard(L"url"); return; }
        if (command == L"load/file")
        {
            Reopen(L"url");
            After(std::chrono::milliseconds(100), [this] { OpenFile(L"url"); });
            return;
        }
        if (command == L"uploads") { OpenPastUploads(); return; }
        if (command == L"settings") { OpenSettings(); return; }
        if (command == L"lib") { OpenLibrary(std::nullopt); return; }
        if (command == L"lib/search")
        {
            auto const it = url.query.find(std::wstring_view{ L"search" });
            OpenLibrary(it != url.query.end() ? std::optional<std::wstring>{ it->second } : url.value);
            return;
        }
        if (command == L"settings/token")
        {
            std::wstring token;
            for (wchar_t c : url.value.value_or(L""))
            {
                if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9'))
                {
                    token.push_back(c);
                }
            }
            if (token.size() >= 11 && token.size() <= 36)
            {
                settings.Token(token);
                Notify({ Strings::Get(L"Url_TokenUpdatedTitle"), Strings::Format(L"Url_TokenUpdatedMessage", token), false, kUrlMessageSeconds, false });
            }
            else if (token == L"none")
            {
                settings.Token(L"");
                Notify({ Strings::Get(L"Url_TokenRemovedTitle"), Strings::Get(L"Url_TokenRemovedMessage"), false, kUrlMessageSeconds, false });
            }
            else
            {
                UrlWarning(Strings::Format(L"Url_CommandFailed", command));
            }
            return;
        }
        if (command == L"settings/library/enable") { settings.LibEnabled(true); return; }
        if (command == L"settings/library/disable") { settings.LibEnabled(false); return; }
        if (command == L"settings/library/limit")
        {
            std::int64_t const limit = ParseStrictInt(url.value.value_or(L"0")).value_or(0);
            if (limit >= 1)
            {
                settings.LibLimit(limit);
            }
            return;
        }
        UrlWarning(Strings::Format(L"Url_UnknownCommand", command));
    }

    // ---- Status icon ----

    void AppController::BeginBusy()
    {
        if (m_tray)
        {
            m_tray->BeginBusy();
        }
    }

    void AppController::EndBusy(bool success)
    {
        if (m_tray)
        {
            m_tray->EndBusy(success);
        }
    }

    void AppController::Notify(Notification notification)
    {
        Notifier::Show(std::move(notification));
    }

    void AppController::UpdateTrayVisibility()
    {
        if (!m_tray)
        {
            return;
        }
        // Hidden only when showMenubarIcon == 0.
        if (Settings::Current().ShowMenubarIcon() == 0)
        {
            m_tray->Hide();
        }
        else
        {
            m_tray->Show();
        }
    }

    void AppController::OnTrayInvoked(POINT anchor)
    {
        // With the Library enabled (libEnabled), a click opens the
        // Library. The menu stays on right click (Windows mapping).
        if (Settings::Current().LibEnabled())
        {
            ToggleLibrary(L"menu");
            return;
        }
        m_tray->ShowMenu(anchor);
    }

    std::vector<Eikon::TrayIcon::MenuItem> AppController::BuildStatusMenu()
    {
        using Item = Eikon::TrayIcon::MenuItem;
        auto& settings = Settings::Current();
        std::vector<Item> items;

        items.push_back({ Strings::Get(L"Tray_Reopen"), ShortcutText(HotKey::Reopen), L"16-menu-eikon", true, [this] { Reopen(L"menu"); } });
        items.push_back(Item::Separator());
        items.push_back({ Strings::Get(L"Tray_CaptureScreen"), ShortcutText(HotKey::Fullscreen), L"16-menu-screen", true,
                          [this] { GrabScreenshot(L"fullscreen", L"menu"); } });
        items.push_back({ Strings::Get(L"Tray_CaptureArea"), ShortcutText(HotKey::Area), L"16-menu-area", true,
                          [this] { GrabScreenshot(L"interactive", L"menu"); } });
        items.push_back({ Strings::Get(L"Tray_ScrollingCapture"), ShortcutText(HotKey::Scrolling), L"16-menu-scrolling", true,
                          [this] { GrabScrolling(-1, L"menu"); } });
        if (settings.ScrollingManualEnabled())
        {
            items.push_back({ Strings::Get(L"Tray_ManualScrollingCapture"), {}, L"16-menu-scrolling", true,
                              [this] { GrabScrollingManual(L"menu"); } });
        }
        items.push_back({ Strings::Get(L"Tray_RecognizeText"), ShortcutText(HotKey::Ocr), L"16-menu-ocr", true, [this] { InstantOcr(L"menu"); } });

        Item more{ Strings::Get(L"Tray_More"), {}, L"16-menu-more", true, nullptr };
        more.submenu = {
            { Strings::Get(L"Tray_RepeatArea"), ShortcutText(HotKey::RepeatArea), L"16-menu-area", true,
              [this] { GrabScreenshot(L"interactive-repeat", L"menu"); } },
            { Strings::Get(L"Tray_ActiveWindow"), ShortcutText(HotKey::Window), L"16-menu-window", true,
              [this] { GrabScreenshot(L"window", L"menu"); } },
            { Strings::Get(L"Tray_AnyWindow"), ShortcutText(HotKey::AnyWindow), L"16-menu-window", true,
              [this] { GrabScreenshot(L"interactive-window", L"menu"); } },
            { Strings::Get(L"Tray_Delayed"), {}, L"16-menu-timer", true, [this] { GrabDelayed(3.0, L"menu"); } },
            { Strings::Get(L"Tray_ScrollingUp"), {}, L"16-menu-scrolling-up", true, [this] { GrabScrolling(1, L"menu"); } },
            Item::Separator(),
            { Strings::Get(L"Tray_OpenFile"), {}, L"16-menu-file", true, [this] { OpenFile(L"menu"); } },
            { Strings::Get(L"Tray_LoadClipboard"), ShortcutText(HotKey::LoadClipboard), L"16-menu-clipboard", true,
              [this] { LoadFromClipboard(L"menu"); } },
        };
        items.push_back(std::move(more));

        items.push_back(Item::Separator());
        Item launch{ Strings::Get(L"Tray_LaunchAtStartup"), {}, {}, true, [this] { ToggleLaunchAtStartup(); } };
        launch.checked = m_launchAtStartup;
        items.push_back(std::move(launch));
        items.push_back({ Strings::Get(L"Tray_Settings"), Strings::ShortcutText(KeyShortcut{ Modifiers::Ctrl, VK_OEM_COMMA }), {}, true,
                          [this] { OpenSettings(); } });
        items.push_back({ Strings::Get(L"Tray_Quit"), {}, {}, true, [this] { Quit(); } });
        return items;
    }

    // ---- Hotkeys ----

    void AppController::RunHotKey(HotKey hotKey)
    {
        switch (hotKey)
        {
        case HotKey::Fullscreen: GrabScreenshot(L"fullscreen", L"hotkey"); break;
        case HotKey::Area: GrabScreenshot(L"interactive", L"hotkey"); break;
        case HotKey::Ocr: InstantOcr(L"hotkey"); break;
        case HotKey::Window: GrabScreenshot(L"window", L"hotkey"); break;
        case HotKey::AnyWindow: GrabScreenshot(L"interactive-window", L"hotkey"); break;
        case HotKey::RepeatArea: GrabScreenshot(L"interactive-repeat", L"hotkey"); break;
        case HotKey::Scrolling: GrabScrolling(-1, L"hotkey"); break;
        case HotKey::Reopen: Reopen(L"hotkey"); break;
        case HotKey::LoadClipboard: LoadFromClipboard(L"hotkey"); break;
        case HotKey::Library: ToggleLibrary(L"hotkey"); break;
        }
    }

    bool AppController::ApplyHotKeys()
    {
        if (!m_hotKeys)
        {
            return false;
        }
        auto& settings = Settings::Current();
        bool ok = true;
        for (std::size_t i = 0; i < Core::AllHotKeys.size(); ++i)
        {
            HotKey const hotKey = Core::AllHotKeys[i];
            int const id = kHotKeyIdBase + static_cast<int>(i);
            KeyShortcut const shortcut = settings.Shortcut(hotKey);
            if (m_hotKeysSuspended || shortcut.IsEmpty())
            {
                m_hotKeys->Unregister(id);
                continue;
            }
            // WM_HOTKEY fires on key down.
            if (!m_hotKeys->Register(id, shortcut, [this, hotKey] { RunHotKey(hotKey); }))
            {
                ok = false;
                Log(L"could not register the " + std::wstring{ Core::HotKeyName(hotKey) } + L" hotkey " + shortcut.ToString());
                HotKeyRegistrationFailed(hotKey);
            }
        }
        return ok;
    }

    void AppController::SuspendHotKeys(bool suspend)
    {
        m_hotKeysSuspended = suspend;
        ApplyHotKeys();
    }

    void AppController::OnSettingChanged(std::wstring_view key)
    {
        if (StartsWith(key, L"KeyboardShortcuts_"))
        {
            ApplyHotKeys();
        }
        else if (key == Settings::kShowMenubarIcon)
        {
            UpdateTrayVisibility();
        }
        else if (key == Settings::kPrimaryOCRLang)
        {
            Ocr::WarmUp();
        }
    }

    // ---- Launch at startup ----

    winrt::fire_and_forget AppController::RefreshLaunchAtStartup()
    {
        bool const enabled = co_await SystemIntegration::IsLaunchAtLoginEnabled();
        m_launchAtStartup = enabled;
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AppController::TestS3Connection()
    {
        // Uploader::TestConnection puts and deletes a test object; "" means good to go.
        struct Outcome
        {
            wil::unique_event done{ wil::EventOptions::ManualReset };
            bool ok = false;
            std::wstring message;
        };
        auto outcome = std::make_shared<Outcome>();
        winrt::apartment_context ui;
        Uploader::TestConnection([outcome](bool ok, std::wstring const& message) {
            outcome->ok = ok;
            outcome->message = message;
            outcome->done.SetEvent();
        });
        co_await winrt::resume_on_signal(outcome->done.get());
        co_await ui;
        co_return winrt::hstring{ outcome->ok ? std::wstring{} : (outcome->message.empty() ? Strings::Get(L"Prefs_S3Failed") : outcome->message) };
    }

    void AppController::ToggleLaunchAtStartup()
    {
        [](AppController* self, bool enable) -> winrt::fire_and_forget {
            bool const enabled = co_await SystemIntegration::SetLaunchAtLoginEnabled(enable);
            self->m_launchAtStartup = enabled;
        }(this, !m_launchAtStartup);
    }
}
