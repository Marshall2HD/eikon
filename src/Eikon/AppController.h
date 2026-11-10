#pragma once

// The composition root that owns every service and window and
// implements every user action. Menus, the tray, hotkeys, URLs and the
// toolbar all call these methods with mode strings ("fullscreen", "interactive",
// "window", "interactive-window", "interactive-repeat", "delayed") and a source ("menu",
// "hotkey", "url"). UI thread only.

#include "Core/Document.h"
#include "Core/Event.h"
#include "Core/Settings.h"
#include "Core/UrlCommand.h"
#include "Platform/HotKeyService.h"
#include "Platform/MessageWindow.h"
#include "Platform/Notifier.h"
#include "Platform/TrayIcon.h"

#include <winrt/Eikon.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Eikon
{
    class PreviewWindow;
    class PinWindow;

    class AppController
    {
    public:
        static AppController& Current();

        // Called once from App::OnLaunched.
        void Start();
        // Tears down hotkeys, the tray icon and the windows (Quit, WM_ENDSESSION).
        void Shutdown();
        void Quit();

        // ---- Capture ----
        // A capture by mode: "fullscreen", "delayed", "window",
        // "interactive", "interactive-window", "interactive-repeat".
        void GrabScreenshot(std::wstring_view mode, std::wstring_view source, Core::CaptureActions actions = {});
        // grabDelayed: GrabScreenshot("delayed", ...) after `seconds` (3 from the menu).
        void GrabDelayed(double seconds, std::wstring_view source, Core::CaptureActions actions = {});
        // Scrolling capture: direction -1 = down, 1 = up.
        void GrabScrolling(int direction, std::wstring_view source, Core::CaptureActions actions = {});
        void GrabScrollingManual(std::wstring_view source);
        void AppendCapture();                                 // grab/append
        void InstantOcr(std::wstring_view source);            // select area -> RecognizeText
        void LoadFromClipboard(std::wstring_view source);
        void OpenFile(std::wstring_view source);              // The "Open Image" dialog

        // ---- Windows ----
        // "Reopen Eikon", the reopen hotkey and eikon://show: shows the editor with the current image.
        void Reopen(std::wstring_view source);
        void ShowEditor();
        void HideEditor();
        bool IsEditorVisible() const;
        // `page` is a Settings pane tag: "general", "hotkeys", "uploading", "advanced" ("" = General).
        void OpenSettings(std::wstring_view page = {});
        // Library: the tray click, the library hotkey and eikon://lib toggle or
        // open it; it is off until eikon://settings/library/enable.
        void ToggleLibrary(std::wstring_view source);
        void OpenLibrary(std::optional<std::wstring> search);
        // For the Library's "Edit in Eikon": loads an image file into the editor.
        // Returns false when it cannot be decoded.
        bool OpenImageFile(std::wstring const& path, double dpr = 1.0);
        // eikon://uploads: Eikon has no upload history, so it reports the failure.
        void OpenPastUploads();

        // ---- URL scheme ----
        void HandleUrl(std::wstring_view url);

        // ---- Image ----
        std::shared_ptr<Core::Document> const& CurrentDocument() const noexcept { return m_document; }
        // Resize Image loads its result as the current image.
        void SetCurrentDocument(std::shared_ptr<Core::Document> document) { m_document = std::move(document); }
        // Copies the current image (PNG + CF_DIBV5). Returns false when there is none or it failed.
        bool CopyImage();
        // Saves to the screenshots folder and notifies.
        void SaveImage();
        void SaveImageAs();
        void PrintImage();
        // Drag-out: saves the composed image to the temporary folder for the editor's file
        // drag (Editor/DragOut) and returns its path.
        std::optional<std::filesystem::path> WriteDragOutFile();
        // Before/after GIF from an overlay image placed at `overlayRect` (image pixels).
        void SaveBeforeAfterGif(Core::Bitmap const& overlay, Core::Rect const& overlayRect);
        void UploadImage();
        void PinImage(std::shared_ptr<Core::Bitmap const> image);
        void ShowThumbnail(std::shared_ptr<Core::Document> document);
        // OCR of an image (editor, thumbnail) or of a selected region (instant OCR).
        void RecognizeText(std::shared_ptr<Core::Bitmap const> image, std::wstring_view source);
        // Loads a document into the editor and shows it (thumbnail Edit, pin Edit).
        void OpenInEditor(std::shared_ptr<Core::Document> document);
        // Closes the thumbnail and hides the editor, remembering the
        // editor's monitor for the next notification.
        void HideAll();

        // ---- Status icon ----
        Eikon::TrayIcon& TrayIcon() noexcept { return *m_tray; }
        void BeginBusy();
        void EndBusy(bool success);
        void Notify(Notification notification);

        // ---- Hotkeys ----
        // Registers every hotkey from Settings (re-run after changes). Returns false if one could not
        // be registered (HotKeyRegistrationFailed names it).
        bool ApplyHotKeys();
        // Releases the hotkeys while the shortcut recorder listens; resuming re-applies them.
        void SuspendHotKeys(bool suspend);
        Event<Core::HotKey> HotKeyRegistrationFailed;

        // ---- Launch at startup (MSIX StartupTask) ----
        bool IsLaunchAtStartupEnabled() const noexcept { return m_launchAtStartup; }
        void ToggleLaunchAtStartup();
        // Re-reads the StartupTask state (the General pane's Autostart switch changed it).
        winrt::fire_and_forget RefreshLaunchAtStartup();

        // ---- Uploading pane ----
        // "Test Connection": "" when the S3 settings work, else the error text
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> TestS3Connection();

        HWND MessageWindowHandle() const noexcept { return m_messageWindow ? m_messageWindow->Handle() : nullptr; }
        // The editor window's handle (nullptr before it exists).
        HWND EditorHandle() const;

    private:
        void CreateLibraryWindow();
        AppController() = default;

        // Window attribution and backdrop flag of a capture.
        struct CaptureInfo
        {
            std::wstring app;
            std::wstring window;
            bool backdropApplied = false;
        };
        // Loads the capture and runs the after-capture actions.
        void AfterCapture(Core::Bitmap bitmap, std::wstring_view mode, Core::CaptureActions actions, CaptureInfo info = {});
        // Fullscreen of the main monitor; active window; Any Window; Repeat Area;
        // area through the RegionSelector.
        void GrabFullscreen(std::wstring const& mode, Core::CaptureActions actions);
        void GrabActiveWindow(int shadowMode, Core::CaptureActions actions);
        void GrabAnyWindow(int shadowMode, Core::CaptureActions actions);
        void GrabRepeatArea(Core::CaptureActions actions);
        void GrabArea(Core::CaptureActions actions);
        // Tray menu actions run while the menu fades out: retries `retry` until it is gone (0.5 s max).
        bool WaitForMenu(std::wstring_view source, std::function<void()> retry);
        // Runs the routes that need permission, after the deep-link permission check.
        void RunGatedUrl(Core::UrlCommand const& command);
        void UrlWarning(std::wstring const& title, std::wstring const& message = {});
        void RunHotKey(Core::HotKey hotKey);
        bool EnsureEditor();
        std::vector<Eikon::TrayIcon::MenuItem> BuildStatusMenu();
        void OnTrayInvoked(POINT anchor);
        void UpdateTrayVisibility();
        void OnSettingChanged(std::wstring_view key);
        // Runs `action` once after `delay` on the UI thread.
        void After(std::chrono::milliseconds delay, std::function<void()> action);

        std::unique_ptr<MessageWindow> m_messageWindow;
        std::unique_ptr<HotKeyService> m_hotKeys;
        std::unique_ptr<Eikon::TrayIcon> m_tray;
        bool m_hotKeysSuspended = false;
        bool m_launchAtStartup = false;
        bool m_shuttingDown = false;
        std::shared_ptr<Core::Document> m_document;
        // The remembered area of Repeat Area: global physical pixels, memory only.
        std::optional<RECT> m_rememberedArea;
        int m_menuWaits = 0;
        std::vector<winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer> m_timers;

        winrt::Eikon::EditorWindow m_editor{ nullptr };
        winrt::Eikon::SettingsWindow m_settingsWindow{ nullptr };

        // The one thumbnail, the pins, OCR state.
        winrt::fire_and_forget RunOcr(std::shared_ptr<Core::Bitmap const> image);
        void CopyOcrText(bool removeBreaks);
        std::unique_ptr<PreviewWindow> m_preview;
        std::vector<std::unique_ptr<PinWindow>> m_pins;
        bool m_ocrBusy = false;
        std::wstring m_lastOcrOriginal;
        winrt::Eikon::LibraryWindow m_libraryWindow{ nullptr };
    };
}
