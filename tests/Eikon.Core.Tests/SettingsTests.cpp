#include "pch.h"
#include "TestFramework.h"
#include "TestSupport.h"

#include "Core/Settings.h"

using namespace Eikon;
using namespace Eikon::Core;
using EikonTests::ResetSettings;

namespace
{
    std::wstring Raw(bool value) { return value ? L"1" : L"0"; }
    std::wstring Raw(std::int64_t value) { return std::to_wstring(value); }
    std::wstring Raw(std::wstring value) { return value; }

    struct Expected
    {
        wchar_t const* key;
        wchar_t const* raw;                              // what the first read stores
        std::function<std::wstring(Settings&)> read;     // the typed getter, as its raw form
    };

    // Typed by hand (not from Settings.h), so a wrong key name,
    // type or default in the X-macro tables fails here.
    std::vector<Expected> ExpectedTable()
    {
        return {
            { L"activeAppVersion", L"0", [](Settings& s) { return Raw(s.ActiveAppVersion()); } },
            { L"afterGrabCopy", L"0", [](Settings& s) { return Raw(s.AfterGrabCopy()); } },
            { L"afterGrabSave", L"0", [](Settings& s) { return Raw(s.AfterGrabSave()); } },
            { L"afterGrabShow", L"1", [](Settings& s) { return Raw(s.AfterGrabShow()); } },
            { L"allowTelemetry", L"1", [](Settings& s) { return Raw(s.AllowTelemetry()); } },
            { L"altZoomDirection", L"0", [](Settings& s) { return Raw(s.AltZoomDirection()); } },
            { L"alwaysOnTop", L"0", [](Settings& s) { return Raw(s.AlwaysOnTop()); } },
            { L"areaCaptureMode", L"none", [](Settings& s) { return Raw(s.AreaCaptureMode()); } },
            { L"areaCustomGrabber", L"0", [](Settings& s) { return Raw(s.AreaCustomGrabber()); } },
            { L"autosaveOverwrite", L"0", [](Settings& s) { return Raw(s.AutosaveOverwrite()); } },
            { L"bdrConfig", L"", [](Settings& s) { return Raw(s.BdrConfig()); } },
            { L"captureCursor", L"auto", [](Settings& s) { return Raw(s.CaptureCursor()); } },
            { L"cmdQAction", L"?", [](Settings& s) { return Raw(s.CmdQAction()); } },
            { L"colorFormat", L"HEX", [](Settings& s) { return Raw(s.ColorFormat()); } },
            { L"contrastType", L"wcag2", [](Settings& s) { return Raw(s.ContrastType()); } },
            { L"copyOnEsc", L"1", [](Settings& s) { return Raw(s.CopyOnEsc()); } },
            { L"customBackdropColor", L"#6080A0", [](Settings& s) { return Raw(s.CustomBackdropColor()); } },
            { L"customDrawingColor", L"#F6F4E7", [](Settings& s) { return Raw(s.CustomDrawingColor()); } },
            { L"customGradFrom", L"#221448", [](Settings& s) { return Raw(s.CustomGradFrom()); } },
            { L"customGradTo", L"#919BD2", [](Settings& s) { return Raw(s.CustomGradTo()); } },
            { L"customHighlighterColor", L"#FFDAEF", [](Settings& s) { return Raw(s.CustomHighlighterColor()); } },
            { L"defaultColor", L"#FF0C01", [](Settings& s) { return Raw(s.DefaultColor()); } },
            { L"defaultFolder", L"", [](Settings& s) { return Raw(s.DefaultFolder()); } },
            { L"defaultTool", L"", [](Settings& s) { return Raw(s.DefaultTool()); } },
            { L"downscaleOnSave", L"0", [](Settings& s) { return Raw(s.DownscaleOnSave()); } },
            { L"enableMagicMouseZoom", L"0", [](Settings& s) { return Raw(s.EnableMagicMouseZoom()); } },
            { L"everHidMainWindow", L"0", [](Settings& s) { return Raw(s.EverHidMainWindow()); } },
            { L"everScrollingCapture", L"0", [](Settings& s) { return Raw(s.EverScrollingCapture()); } },
            { L"everVisitedHotkeys", L"0", [](Settings& s) { return Raw(s.EverVisitedHotkeys()); } },
            { L"expandableCanvas", L"1", [](Settings& s) { return Raw(s.ExpandableCanvas()); } },
            { L"fileNameTemplate", L"SCR-%Y%m%d-%t", [](Settings& s) { return Raw(s.FileNameTemplate()); } },
            { L"growingToolbar", L"0", [](Settings& s) { return Raw(s.GrowingToolbar()); } },
            { L"headlessTextRendering", L"1", [](Settings& s) { return Raw(s.HeadlessTextRendering()); } },
            { L"libAction", L"eikon", [](Settings& s) { return Raw(s.LibAction()); } },
            { L"libAllowOCR", L"1", [](Settings& s) { return Raw(s.LibAllowOCR()); } },
            { L"libEnabled", L"0", [](Settings& s) { return Raw(s.LibEnabled()); } },
            { L"libIndexNonshots", L"0", [](Settings& s) { return Raw(s.LibIndexNonshots()); } },
            { L"libLayout", L"medium", [](Settings& s) { return Raw(s.LibLayout()); } },
            { L"libLimit", L"10000", [](Settings& s) { return Raw(s.LibLimit()); } },
            { L"libPopoveerSize", L"small", [](Settings& s) { return Raw(s.LibPopoveerSize()); } },
            { L"libQLCurtain", L"0", [](Settings& s) { return Raw(s.LibQLCurtain()); } },
            { L"localEventCounter", L"0", [](Settings& s) { return Raw(s.LocalEventCounter()); } },
            { L"notificationType", L"custom", [](Settings& s) { return Raw(s.NotificationType()); } },
            { L"ocrRemoveBreaks", L"0", [](Settings& s) { return Raw(s.OcrRemoveBreaks()); } },
            { L"preferLargeWindow", L"1", [](Settings& s) { return Raw(s.PreferLargeWindow()); } },
            { L"primaryOCRLang", L"en-US", [](Settings& s) { return Raw(s.PrimaryOCRLang()); } },
            { L"printOrientation", L"auto", [](Settings& s) { return Raw(s.PrintOrientation()); } },
            { L"realPixels", L"0", [](Settings& s) { return Raw(s.RealPixels()); } },
            { L"saveFormat", L"Auto", [](Settings& s) { return Raw(s.SaveFormat()); } },
            { L"saveOnEsc", L"0", [](Settings& s) { return Raw(s.SaveOnEsc()); } },
            { L"scrollingCustomGrabber", L"0", [](Settings& s) { return Raw(s.ScrollingCustomGrabber()); } },
            { L"scrollingManualEnabled", L"0", [](Settings& s) { return Raw(s.ScrollingManualEnabled()); } },
            { L"scrollingMax", L"20000", [](Settings& s) { return Raw(s.ScrollingMax()); } },
            { L"scrollingReverseAutoscroll", L"0", [](Settings& s) { return Raw(s.ScrollingReverseAutoscroll()); } },
            { L"scrollingSpeed", L"2", [](Settings& s) { return Raw(s.ScrollingSpeed()); } },
            { L"showDockIcon", L"1", [](Settings& s) { return Raw(s.ShowDockIcon()); } },
            { L"showExportCopy", L"1", [](Settings& s) { return Raw(s.ShowExportCopy()); } },
            { L"showExportPin", L"1", [](Settings& s) { return Raw(s.ShowExportPin()); } },
            { L"showExportSave", L"1", [](Settings& s) { return Raw(s.ShowExportSave()); } },
            { L"showExportUpload", L"1", [](Settings& s) { return Raw(s.ShowExportUpload()); } },
            { L"showIntro", L"1", [](Settings& s) { return Raw(s.ShowIntro()); } },
            { L"showMenubarIcon", L"1", [](Settings& s) { return Raw(s.ShowMenubarIcon()); } },
            { L"snappingMode", L"2", [](Settings& s) { return Raw(s.SnappingMode()); } },
            { L"thumbnailClosing", L"manual", [](Settings& s) { return Raw(s.ThumbnailClosing()); } },
            { L"toolbarOrder", L"", [](Settings& s) { return Raw(s.ToolbarOrder()); } },
            { L"token", L"", [](Settings& s) { return Raw(s.Token()); } },
            { L"uploadMode", L"none", [](Settings& s) { return Raw(s.UploadMode()); } },
            { L"uploadS3Bucket", L"", [](Settings& s) { return Raw(s.UploadS3Bucket()); } },
            { L"uploadS3Endpoint", L"", [](Settings& s) { return Raw(s.UploadS3Endpoint()); } },
            { L"uploadS3Prefix", L"", [](Settings& s) { return Raw(s.UploadS3Prefix()); } },
            { L"uploadS3PresignExpiration", L"604800", [](Settings& s) { return Raw(s.UploadS3PresignExpiration()); } },
            { L"uploadS3PublicUrl", L"", [](Settings& s) { return Raw(s.UploadS3PublicUrl()); } },
            { L"uploadS3Region", L"", [](Settings& s) { return Raw(s.UploadS3Region()); } },
            { L"uploadS3SharingMode", L"none", [](Settings& s) { return Raw(s.UploadS3SharingMode()); } },
            { L"windowShadow", L"transparent", [](Settings& s) { return Raw(s.WindowShadow()); } },
            { L"windowSolidColor", L"#404448", [](Settings& s) { return Raw(s.WindowSolidColor()); } },
        };
    }
}

TEST(Settings_EveryKeyReadsTheSpecDefaultAndWritesItBack)
{
    auto const table = ExpectedTable();
    // These 76 plus defaultFolderBookmark (no default) and uid (generated).
    CHECK_EQ(table.size(), std::size_t{ 76 });
    for (auto const& entry : table)
    {
        auto& store = ResetSettings();
        auto& settings = Settings::Current();
        std::wstring const value = entry.read(settings);
        if (value != entry.raw)
        {
            EikonTests::ReportFailure(__FILE__, __LINE__, "default of " + EikonTests::Narrow(entry.key) + ": " +
                EikonTests::Narrow(value) + " != " + EikonTests::Narrow(entry.raw));
        }
        // The getter wrote exactly this key, with the default.
        CHECK_EQ(store.values.size(), std::size_t{ 1 });
        auto const it = store.values.find(std::wstring_view{ entry.key });
        if (it == store.values.end())
        {
            EikonTests::ReportFailure(__FILE__, __LINE__, "not written back under its key: " + EikonTests::Narrow(entry.key));
        }
        else
        {
            CHECK_EQ(it->second, std::wstring{ entry.raw });
        }
    }
}

TEST(Settings_WriteBackHappensOnceAndDoesNotRaiseChanged)
{
    auto& store = ResetSettings();
    auto& s = Settings::Current();
    int changes = 0;
    auto token = s.Changed.Add([&](std::wstring_view) { ++changes; });
    CHECK_EQ(s.LibLimit(), std::int64_t{ 10000 });
    CHECK_EQ(s.LibLimit(), std::int64_t{ 10000 });
    CHECK_EQ(store.writes, 1);
    CHECK_EQ(changes, 0);
    s.Changed.Remove(token);
}

TEST(Settings_StoredValuesWinOverDefaults)
{
    auto& store = ResetSettings({ { L"afterGrabShow", L"0" }, { L"captureCursor", L"include" }, { L"libLimit", L"25" } });
    auto& s = Settings::Current();
    CHECK_EQ(s.AfterGrabShow(), false);
    CHECK_EQ(s.CaptureCursor(), std::wstring{ L"include" });
    CHECK_EQ(s.LibLimit(), std::int64_t{ 25 });
    CHECK_EQ(store.writes, 0);
}

TEST(Settings_StoredEmptyStringIsNotReplacedByTheDefault)
{
    // A key that exists with "" is a value, not a missing key.
    ResetSettings({ { L"notificationType", L"" } });
    CHECK_EQ(Settings::Current().NotificationType(), std::wstring{});
}

TEST(Settings_UnparseableValueReadsDefaultAndIsKept)
{
    auto& store = ResetSettings({ { L"libLimit", L"lots" }, { L"copyOnEsc", L"maybe" } });
    auto& s = Settings::Current();
    CHECK_EQ(s.LibLimit(), std::int64_t{ 10000 });
    CHECK_EQ(s.CopyOnEsc(), true);
    CHECK_EQ(store.values[L"libLimit"], std::wstring{ L"lots" });
    CHECK_EQ(store.writes, 0);
}

TEST(Settings_SetterPersistsAndRaisesChangedOnlyWhenChanged)
{
    auto& store = ResetSettings();
    auto& s = Settings::Current();
    std::vector<std::wstring> changed;
    auto token = s.Changed.Add([&](std::wstring_view key) { changed.emplace_back(key); });
    s.ScrollingManualEnabled(true);
    s.ScrollingManualEnabled(true);
    s.CaptureCursor(L"exclude");
    CHECK_EQ(changed, (std::vector<std::wstring>{ L"scrollingManualEnabled", L"captureCursor" }));
    CHECK_EQ(store.values[L"scrollingManualEnabled"], std::wstring{ L"1" });
    CHECK_EQ(store.values[L"captureCursor"], std::wstring{ L"exclude" });
    CHECK_EQ(s.ScrollingManualEnabled(), true);
    s.Changed.Remove(token);
}

TEST(Settings_LargeIntegersSurvive)
{
    // showDockIcon's magic value and 64-bit integers.
    ResetSettings();
    auto& s = Settings::Current();
    s.ShowDockIcon(0x77C5FB89);
    CHECK_EQ(s.ShowDockIcon(), std::int64_t{ 0x77C5FB89 });
    s.LocalEventCounter(std::int64_t{ 1 } << 40);
    CHECK_EQ(s.LocalEventCounter(), std::int64_t{ 1 } << 40);
}

TEST(Settings_BookmarkHasNoDefaultAndUidIsGeneratedOnce)
{
    auto& store = ResetSettings();
    auto& s = Settings::Current();
    CHECK(!s.DefaultFolderBookmark().has_value());
    CHECK(store.values.find(std::wstring_view{ L"defaultFolderBookmark" }) == store.values.end());

    std::wstring const uid = s.Uid();
    CHECK_EQ(uid.size(), std::size_t{ 36 });
    CHECK_EQ(uid[8], L'-');
    CHECK_EQ(uid[14], L'4');
    CHECK_EQ(s.Uid(), uid);
    CHECK_EQ(store.values[L"uid"], uid);
}

TEST(Settings_HotkeyDefaultsAreCtrlShiftDigits)
{
    auto& store = ResetSettings();
    auto& s = Settings::Current();
    CHECK_EQ(s.Shortcut(HotKey::Fullscreen).ToString(), std::wstring{ L"Ctrl+Shift+1" });
    CHECK_EQ(s.Shortcut(HotKey::Area).ToString(), std::wstring{ L"Ctrl+Shift+2" });
    CHECK_EQ(s.Shortcut(HotKey::Ocr).ToString(), std::wstring{ L"Ctrl+Alt+Shift+O" });
    for (auto hotKey : { HotKey::Window, HotKey::AnyWindow, HotKey::RepeatArea, HotKey::Scrolling, HotKey::Reopen,
                         HotKey::LoadClipboard, HotKey::Library })
    {
        CHECK(s.Shortcut(hotKey).IsEmpty());
    }
    // KeyboardShortcuts doesn't store defaults.
    CHECK_EQ(store.writes, 0);
}

TEST(Settings_HotkeyNamesAndUnbinding)
{
    std::vector<std::wstring> keys;
    for (auto hotKey : AllHotKeys)
    {
        keys.push_back(Settings::ShortcutKey(hotKey));
    }
    CHECK_EQ(keys, (std::vector<std::wstring>{
        L"KeyboardShortcuts_fullscreen", L"KeyboardShortcuts_area", L"KeyboardShortcuts_ocr", L"KeyboardShortcuts_window",
        L"KeyboardShortcuts_anyWindow", L"KeyboardShortcuts_repeatArea", L"KeyboardShortcuts_scrolling",
        L"KeyboardShortcuts_reopen", L"KeyboardShortcuts_loadClipboard", L"KeyboardShortcuts_library" }));

    auto& store = ResetSettings();
    auto& s = Settings::Current();
    s.Shortcut(HotKey::Fullscreen, KeyShortcut{});
    CHECK_EQ(store.values[L"KeyboardShortcuts_fullscreen"], std::wstring{});
    CHECK(s.Shortcut(HotKey::Fullscreen).IsEmpty()); // cleared stays cleared, not the default
    s.Shortcut(HotKey::Reopen, KeyShortcut{ Modifiers::Ctrl | Modifiers::Alt, 'R' });
    CHECK_EQ(store.values[L"KeyboardShortcuts_reopen"], std::wstring{ L"Ctrl+Alt+R" });
    CHECK_EQ(s.Shortcut(HotKey::Reopen), (KeyShortcut{ Modifiers::Ctrl | Modifiers::Alt, 'R' }));
}

TEST(Settings_DynamicKeysUseTheSameWriteBack)
{
    auto& store = ResetSettings();
    auto& s = Settings::Current();
    CHECK_EQ(s.ReadInt(L"ObjArrow: size", 2), std::int64_t{ 2 });
    CHECK_EQ(store.values[L"ObjArrow: size"], std::wstring{ L"2" });
    CHECK_EQ(s.ReadDouble(L"ObjCounter: counterAngle", -2.356), -2.356);
    CHECK_EQ(s.ReadDouble(L"ObjCounter: counterAngle", 0.0), -2.356);
}
