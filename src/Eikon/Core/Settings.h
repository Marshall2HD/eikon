#pragma once

// Every setting key, with its type and default, plus
// the global hotkey bindings. Stored in ApplicationData.LocalSettings (Core/AppData.cpp).
//
// Typed getters write the default back when the key is missing, so here too a getter
// stores the default on first read and returns it. A stored value that cannot be parsed reads as the
// default without being overwritten. Setters persist immediately and raise Changed(key) when the
// value changed. UI thread only.

#include "Core/Event.h"
#include "Core/KeyShortcut.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Eikon::Core
{
    // Persistence backend. Values are strings; Settings handles (de)serialization. The app uses the
    // LocalSettings backend; unit tests install an in-memory one with Settings::UseStore.
    class SettingsStore
    {
    public:
        virtual ~SettingsStore() = default;
        virtual std::optional<std::wstring> Read(std::wstring_view key) const = 0;
        virtual void Write(std::wstring_view key, std::wstring_view value) = 0;
        virtual void Remove(std::wstring_view key) = 0;
    };

    // ApplicationData::Current().LocalSettings() (in-memory when there is no package identity).
    // Defined in Core/AppData.cpp, the only Core file that uses WinRT.
    std::unique_ptr<SettingsStore> CreateDefaultSettingsStore();
    // The package's LocalState folder (ApplicationData LocalFolder), %LOCALAPPDATA%\Eikon without
    // package identity. Also in Core/AppData.cpp.
    std::filesystem::path DataFolder();

    // The ten KeyboardShortcuts names, in menu order.
    enum class HotKey : std::int32_t
    {
        Fullscreen,
        Area,
        Ocr,
        Window,
        AnyWindow,
        RepeatArea,
        Scrolling,
        Reopen,
        LoadClipboard,
        Library,
    };
    inline constexpr std::array<HotKey, 10> AllHotKeys = {
        HotKey::Fullscreen, HotKey::Area, HotKey::Ocr, HotKey::Window, HotKey::AnyWindow,
        HotKey::RepeatArea, HotKey::Scrolling, HotKey::Reopen, HotKey::LoadClipboard, HotKey::Library,
    };
    // "fullscreen", "area", "ocr", "window", "anyWindow", "repeatArea", "scrolling", "reopen",
    // "loadClipboard", "library".
    std::wstring_view HotKeyName(HotKey hotKey) noexcept;

// X-macros: (Property, "key", default). One typed getter/setter pair per key.
#define EIKON_SETTINGS_BOOL(X)                                       \
    X(AfterGrabCopy, L"afterGrabCopy", false)                        \
    X(AfterGrabSave, L"afterGrabSave", false)                        \
    X(AfterGrabShow, L"afterGrabShow", true)                         \
    X(AllowTelemetry, L"allowTelemetry", true)                       \
    X(AltZoomDirection, L"altZoomDirection", false)                  \
    X(AlwaysOnTop, L"alwaysOnTop", false)                            \
    X(AreaCustomGrabber, L"areaCustomGrabber", false)                \
    X(AutosaveOverwrite, L"autosaveOverwrite", false)                \
    X(CopyOnEsc, L"copyOnEsc", true)                                 \
    X(DownscaleOnSave, L"downscaleOnSave", false)                    \
    X(EnableMagicMouseZoom, L"enableMagicMouseZoom", false)          \
    X(EverHidMainWindow, L"everHidMainWindow", false)                \
    X(EverScrollingCapture, L"everScrollingCapture", false)          \
    X(EverVisitedHotkeys, L"everVisitedHotkeys", false)              \
    X(ExpandableCanvas, L"expandableCanvas", true)                   \
    X(GrowingToolbar, L"growingToolbar", false)                      \
    X(HeadlessTextRendering, L"headlessTextRendering", true)         \
    X(LibAllowOCR, L"libAllowOCR", true)                             \
    X(LibEnabled, L"libEnabled", false)                              \
    X(LibIndexNonshots, L"libIndexNonshots", false)                  \
    X(LibQLCurtain, L"libQLCurtain", false)                          \
    X(OcrRemoveBreaks, L"ocrRemoveBreaks", false)                    \
    X(PreferLargeWindow, L"preferLargeWindow", true)                 \
    X(RealPixels, L"realPixels", false)                              \
    X(SaveOnEsc, L"saveOnEsc", false)                                \
    X(ScrollingCustomGrabber, L"scrollingCustomGrabber", false)      \
    X(ScrollingManualEnabled, L"scrollingManualEnabled", false)      \
    X(ScrollingReverseAutoscroll, L"scrollingReverseAutoscroll", false) \
    X(ShowExportCopy, L"showExportCopy", true)                       \
    X(ShowExportPin, L"showExportPin", true)                         \
    X(ShowExportSave, L"showExportSave", true)                       \
    X(ShowExportUpload, L"showExportUpload", true)

#define EIKON_SETTINGS_INT(X)                                        \
    X(ActiveAppVersion, L"activeAppVersion", 0)                      \
    X(LibLimit, L"libLimit", 10000)                                  \
    X(LocalEventCounter, L"localEventCounter", 0)                    \
    X(ScrollingMax, L"scrollingMax", 20000)                          \
    X(ScrollingSpeed, L"scrollingSpeed", 2)                          \
    X(ShowDockIcon, L"showDockIcon", 1)                              \
    X(ShowIntro, L"showIntro", 1)                                    \
    X(ShowMenubarIcon, L"showMenubarIcon", 1)                        \
    X(SnappingMode, L"snappingMode", 2)                              \
    X(UploadS3PresignExpiration, L"uploadS3PresignExpiration", 604800)

#define EIKON_SETTINGS_STRING(X)                                                     \
    X(AreaCaptureMode, L"areaCaptureMode", L"none")                                  \
    X(BdrConfig, L"bdrConfig", L"")                                                  \
    X(CaptureCursor, L"captureCursor", L"auto")                                      \
    X(CmdQAction, L"cmdQAction", L"?")                                               \
    X(ColorFormat, L"colorFormat", L"HEX")                                           \
    X(ContrastType, L"contrastType", L"wcag2")                                       \
    X(CustomBackdropColor, L"customBackdropColor", L"#6080A0")                       \
    X(CustomDrawingColor, L"customDrawingColor", L"#F6F4E7")                         \
    X(CustomGradFrom, L"customGradFrom", L"#221448")                                 \
    X(CustomGradTo, L"customGradTo", L"#919BD2")                                     \
    X(CustomHighlighterColor, L"customHighlighterColor", L"#FFDAEF")                 \
    X(DefaultColor, L"defaultColor", L"#FF0C01")                                     \
    X(DefaultFolder, L"defaultFolder", L"")                                          \
    X(DefaultTool, L"defaultTool", L"")                                              \
    X(FileNameTemplate, L"fileNameTemplate", L"SCR-%Y%m%d-%t")                       \
    X(LibAction, L"libAction", L"eikon")                                             \
    X(LibLayout, L"libLayout", L"medium")                                            \
    X(LibPopoveerSize, L"libPopoveerSize", L"small")                                 \
    X(NotificationType, L"notificationType", L"custom")                              \
    X(PrimaryOCRLang, L"primaryOCRLang", L"en-US")                                   \
    X(PrintOrientation, L"printOrientation", L"auto")                                \
    X(SaveFormat, L"saveFormat", L"Auto")                                            \
    X(ThumbnailClosing, L"thumbnailClosing", L"manual")                              \
    X(ToolbarOrder, L"toolbarOrder", L"")                                            \
    X(Token, L"token", L"")                                                          \
    X(UploadMode, L"uploadMode", L"none")                                            \
    X(UploadS3Bucket, L"uploadS3Bucket", L"")                                        \
    X(UploadS3Endpoint, L"uploadS3Endpoint", L"")                                    \
    X(UploadS3Prefix, L"uploadS3Prefix", L"")                                        \
    X(UploadS3PublicUrl, L"uploadS3PublicUrl", L"")                                  \
    X(UploadS3Region, L"uploadS3Region", L"")                                        \
    X(UploadS3SharingMode, L"uploadS3SharingMode", L"none")                          \
    X(WindowShadow, L"windowShadow", L"transparent")                                 \
    X(WindowSolidColor, L"windowSolidColor", L"#404448")

    class Settings
    {
    public:
        static Settings& Current();
        // Replaces the persistence backend (tests only) and clears the cache.
        static void UseStore(std::unique_ptr<SettingsStore> store);

        // Raised after a setter changed a value (not for the first-read write-back).
        Event<std::wstring_view> Changed;

#define EIKON_DECLARE_BOOL(Name, KeyText, Default) \
        static constexpr std::wstring_view k##Name = KeyText; \
        bool Name();                                \
        void Name(bool value);
#define EIKON_DECLARE_INT(Name, KeyText, Default)  \
        static constexpr std::wstring_view k##Name = KeyText; \
        std::int64_t Name();                        \
        void Name(std::int64_t value);
#define EIKON_DECLARE_STRING(Name, KeyText, Default) \
        static constexpr std::wstring_view k##Name = KeyText; \
        std::wstring Name();                          \
        void Name(std::wstring const& value);

        EIKON_SETTINGS_BOOL(EIKON_DECLARE_BOOL)
        EIKON_SETTINGS_INT(EIKON_DECLARE_INT)
        EIKON_SETTINGS_STRING(EIKON_DECLARE_STRING)

#undef EIKON_DECLARE_BOOL
#undef EIKON_DECLARE_INT
#undef EIKON_DECLARE_STRING

        // `defaultFolderBookmark` (D, no default): unused; the folder is stored as a path. Never
        // written on read.
        static constexpr std::wstring_view kDefaultFolderBookmark = L"defaultFolderBookmark";
        std::optional<std::wstring> DefaultFolderBookmark();
        void DefaultFolderBookmark(std::optional<std::wstring> const& value);

        // `uid` (S, generated): anonymous install id. Generated (random UUID) and stored on first read.
        static constexpr std::wstring_view kUid = L"uid";
        std::wstring Uid();
        void Uid(std::wstring const& value);

        // ---- Global hotkeys (`KeyboardShortcuts_<name>`) ----
        // Stored as KeyShortcut::ToString ("Ctrl+Shift+1"); "" = unbound. A missing key means the
        // default (fullscreen Ctrl+Shift+1, area Ctrl+Shift+2, ocr Ctrl+Alt+Shift+O, the
        // rest unbound); it is not written back.
        static std::wstring ShortcutKey(HotKey hotKey);       // "KeyboardShortcuts_fullscreen"
        static KeyShortcut DefaultShortcut(HotKey hotKey) noexcept;
        KeyShortcut Shortcut(HotKey hotKey);
        void Shortcut(HotKey hotKey, KeyShortcut const& value);

        // ---- Keys that are not typed getters ----
        // Per-object preferences ("<Type>: size") and other dynamic keys use the same
        // write-default-on-first-read rule.
        bool ReadBool(std::wstring_view key, bool fallback);
        void WriteBool(std::wstring_view key, bool value);
        std::int64_t ReadInt(std::wstring_view key, std::int64_t fallback);
        void WriteInt(std::wstring_view key, std::int64_t value);
        double ReadDouble(std::wstring_view key, double fallback);
        void WriteDouble(std::wstring_view key, double value);
        std::wstring ReadString(std::wstring_view key, std::wstring_view fallback);
        void WriteString(std::wstring_view key, std::wstring_view value);
        bool Contains(std::wstring_view key);
        void Remove(std::wstring_view key);

    private:
        Settings() = default;

        SettingsStore& Store();
        std::optional<std::wstring> ReadRaw(std::wstring_view key);
        // Stores without raising Changed (first-read write-back).
        void StoreRaw(std::wstring_view key, std::wstring const& value);
        // Stores and raises Changed(key) when the raw value differs.
        void SetRaw(std::wstring_view key, std::wstring const& value);

        std::unique_ptr<SettingsStore> m_store;
        // Raw values read so far; nullopt caches "not stored".
        std::vector<std::pair<std::wstring, std::optional<std::wstring>>> m_cache;
    };
}
