#include "pch.h"
#include "EditorWindow.xaml.h"
#if __has_include("EditorWindow.g.cpp")
#include "EditorWindow.g.cpp"
#endif

#include "AppController.h"
#include "Core/EditorTools.h"
#include "Core/ImageResize.h"
#include "Core/Objects/Factory.h"
#include "Core/Objects/ObjText.h"
#include "Core/Settings.h"
#include "Editor/BackdropUi.h"
#include "Editor/Compose.h"
#include "Editor/DragOut.h"
#include "Platform/Candy.h"
#include "Strings.h"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>

#include <cmath>
#include <random>

namespace winrt
{
    using namespace Microsoft::UI::Windowing;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Controls::Primitives;
    using namespace Microsoft::UI::Xaml::Media;
    using namespace Microsoft::UI::Xaml::Media::Imaging;
    using namespace Windows::Graphics;
}

namespace
{
    using ::Eikon::Modifiers;
    using ::Eikon::Core::Settings;
    namespace Strings = ::Eikon::Strings;
    namespace Core = ::Eikon::Core;

    constexpr double kTitleBarHeight = 48;   // DIPs, the Tall caption height of the custom title bar
    constexpr double kOverflowSafety = 4;    // keeps the toolbar from touching the menu button

    std::wstring IconPath()
    {
        wchar_t buffer[MAX_PATH]{};
        DWORD const length = GetModuleFileNameW(nullptr, buffer, ARRAYSIZE(buffer));
        std::wstring path{ buffer, length };
        auto const slash = path.find_last_of(L'\\');
        path = slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash + 1);
        return path + L"Assets\\Eikon.ico";
    }

    double ScaleOfDisplay(winrt::DisplayArea const& display)
    {
        auto const bounds = display.OuterBounds();
        POINT const center{ bounds.X + bounds.Width / 2, bounds.Y + bounds.Height / 2 };
        UINT dpiX = USER_DEFAULT_SCREEN_DPI;
        UINT dpiY = USER_DEFAULT_SCREEN_DPI;
        if (SUCCEEDED(GetDpiForMonitor(MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX > 0)
        {
            return static_cast<double>(dpiX) / USER_DEFAULT_SCREEN_DPI;
        }
        return 1.0;
    }

    winrt::Windows::Foundation::Uri AssetUri(std::wstring_view name)
    {
        return winrt::Windows::Foundation::Uri{ winrt::hstring{ L"ms-appx:///Assets/Editor/" + std::wstring{ name } + L".png" } };
    }

    winrt::BitmapIcon TemplateIcon(std::wstring_view asset, double size)
    {
        winrt::BitmapIcon icon;
        icon.UriSource(AssetUri(asset));
        icon.ShowAsMonochrome(true);
        icon.Width(size);
        icon.Height(size);
        return icon;
    }

    // The editor's named colours for code-built controls (the XAML uses the same values as ThemeResources).
    winrt::SolidColorBrush ThemeBrush(std::wstring_view key, bool dark)
    {
        using C = winrt::Windows::UI::Color;
        C color{ 0, 0, 0, 0 };
        auto const& candy = ::Eikon::Candy::For(dark);
        auto const from = [](::Eikon::Candy::Argb c) { return C{ c.a, c.r, c.g, c.b }; };
        if (::Eikon::Candy::HighContrast() && key != L"clrMain" && key != L"clrToolbarLabel")
        {
            // High contrast: system colours; the selected tool/segment is Highlight with HighlightText.
            auto const system = [](int index) { COLORREF const c = GetSysColor(index); return C{ 0xFF, GetRValue(c), GetGValue(c), GetBValue(c) }; };
            if (key == L"CandyAccent") color = system(COLOR_HIGHLIGHTTEXT);
            else if (key == L"CandyAccentSoft" || key == L"CtrlButtonSelected") color = system(COLOR_HIGHLIGHT);
            else if (key == L"CandyInk2") color = system(COLOR_WINDOWTEXT);
            else if (key == L"SegmentsBackground") color = system(COLOR_WINDOW);
            else if (key == L"InfoSeparator") color = system(COLOR_GRAYTEXT);
            return winrt::SolidColorBrush{ color };
        }
        if (key == L"clrMain") color = C{ 0xFF, 0xFF, 0x0C, 0x01 };
        else if (key == L"CandyAccent") color = from(candy.accent);
        else if (key == L"CandyAccentSoft") color = from(candy.accentSoft);
        else if (key == L"CandyInk2") color = from(candy.ink2);
        else if (key == L"clrToolbarLabel") color = dark ? C{ 0xFF, 0xD0, 0xD0, 0xD0 } : C{ 0xFF, 0x28, 0x28, 0x28 };
        else if (key == L"CtrlButtonSelected") color = from(candy.accentSoft);
        else if (key == L"SegmentsBackground") color = from(candy.surface2);
        else if (key == L"InfoSeparator") color = from(candy.line);
        return winrt::SolidColorBrush{ color };
    }

    winrt::Windows::UI::Color ColorFromHex(std::wstring_view hex)
    {
        if (auto const color = Core::Color::FromHex(hex))
        {
            return { 0xFF, color->r, color->g, color->b };
        }
        return { 0xFF, 0xFF, 0x0C, 0x01 };
    }

    std::wstring HexOf(winrt::Windows::UI::Color color)
    {
        wchar_t buffer[8]{};
        swprintf_s(buffer, L"#%02X%02X%02X", color.R, color.G, color.B);
        return buffer;
    }

    std::wstring Shortcut(std::uint32_t key, Modifiers modifiers = Modifiers::None)
    {
        return Strings::ShortcutText(::Eikon::KeyShortcut{ modifiers, key });
    }

    // Drawing tool id -> the Obj* class it creates.
    std::wstring_view ClassForTool(std::wstring_view tool)
    {
        static constexpr std::pair<std::wstring_view, std::wstring_view> map[] = {
            { L"arrow", L"ObjArrow" }, { L"text", L"ObjText" }, { L"rectangle", L"ObjRectangle" },
            { L"freehand", L"ObjFreehand" }, { L"magnifier", L"ObjMagnifier" }, { L"blur", L"ObjConceal" },
            { L"highlighter", L"ObjHighlighter" }, { L"spotlight", L"ObjSpotlight" }, { L"counter", L"ObjCounter" },
            { L"oval", L"ObjOval" }, { L"line", L"ObjLine" },
        };
        for (auto const& [id, name] : map)
        {
            if (id == tool)
            {
                return name;
            }
        }
        return {};
    }

    // The colour format popup (key, menu title).
    constexpr std::pair<std::wstring_view, std::wstring_view> kColorFormats[] = {
        { L"HEX", L"HEX #FFC200" },
        { L"HEXno", L"HEX FFC200" },
        { L"RGBA", L"CSS rgba(255, 194, 0, 1)" },
        { L"RGB1", L"RGB (1.00, 0.76, 0.00)" },
        { L"Android", L"Android 0xFFFFC200" },
        { L"UIColor", L"UIColor (red: 1.00, gre..." },
        { L"CSharp", L"C# Color.fromArgb()" },
        { L"OKLCH", L"OKLCH" },
    };
    // The colour label widths of the same table (constraintColorWidth).
    constexpr double kColorLabelWidths[] = { 102, 102, 112, 120, 112, 120, 112, 112 };

    std::size_t ColorFormatIndex()
    {
        auto const format = Settings::Current().ColorFormat();
        for (std::size_t i = 0; i < std::size(kColorFormats); ++i)
        {
            if (kColorFormats[i].first == format)
            {
                return i;
            }
        }
        return 0;
    }

    bool ClipboardHasImage()
    {
        return IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_DIBV5) || IsClipboardFormatAvailable(CF_BITMAP) ||
               IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG"));
    }
}

namespace winrt::Eikon::implementation
{
    void EditorWindow::InitializeComponent()
    {
        EditorWindowT<EditorWindow>::InitializeComponent();
        // Window is not a DependencyObject, so x:Uid can't set its title.
        Title(winrt::hstring{ Strings::Get(L"Editor_WindowTitle") });

        ::Eikon::Editor::ShotView::Host host;
        host.stateChanged = [this] {
            OnViewStateChanged();
            if (m_textOverlay && m_view) m_textOverlay->Reposition(m_view->Context());
        };
        host.editText = [this](::Eikon::Core::ProtoObject& object) {
            auto* text = dynamic_cast<::Eikon::Core::ObjText*>(&object);
            if (!text || !m_textOverlay)
            {
                return;
            }
            std::int64_t const id = object.id;
            m_textOverlay->Begin(*text, m_view->Context(), [this] { m_view->Invalidate(); }, [this, id] {
                // An empty text object goes away when its editor closes; otherwise the edit is an undo step.
                auto* active = m_view->Objects().Active();
                auto* edited = active && active->id == id ? dynamic_cast<::Eikon::Core::ObjText*>(active) : nullptr;
                if (edited && edited->text.empty())
                {
                    m_view->Objects().DeleteActive();
                }
                else
                {
                    m_view->RecordUndo();
                }
                m_view->Invalidate();
            });
        };
        host.hoverChanged = [this] { UpdateColorInfo(); };
        host.anyHelperOpen = [this] { return AnyHelperOpen(); };
        host.hideEditor = [this] { HideEditor(); };
        host.alert = [this](std::wstring const& title, std::wstring const& message) { Alert(title, message); };
        host.dragOut = [this](winrt::Microsoft::UI::Input::PointerPoint const& point) { ::Eikon::Editor::StartDragOut(Canvas(), point); };
        host.canvasPressed = [this] {
            // Transient helpers close on a click outside them.
            CloseHelpers();
        };
        m_view = std::make_unique<::Eikon::Editor::ShotView>(Canvas(), std::move(host));
        m_textOverlay = std::make_unique<::Eikon::Editor::TextEditOverlay>(TextOverlay());

        SetUpWindow();
        SetUpStrings();
        SetUpHelpers();
        SetUpToolbar();
        SetUpInfo();
        SetUpOptionsBar();
        SetUpResize();
        SetUpMenu();
        SetUpIntro();
        ApplyThemeImages();
        ::Eikon::Editor::AttachDragOut(DragOutButton());
        ::Eikon::Editor::AttachDragOutSource(Canvas());  // Ctrl+Alt + drag

        RootGrid().ActualThemeChanged([this](auto&&, auto&&) {
            ApplyThemeImages();
            m_view->Invalidate();
        });
        RootGrid().PreviewKeyDown([this](auto&&, winrt::Input::KeyRoutedEventArgs const& args) {
            auto const key = static_cast<std::uint32_t>(args.Key());
            if (HandleKeyDown(key, ::Eikon::CurrentModifiers(), args.KeyStatus().WasKeyDown))
            {
                args.Handled(true);
            }
        });
        RootGrid().PreviewKeyUp([this](auto&&, winrt::Input::KeyRoutedEventArgs const& args) {
            if (m_view && m_view->KeyUp(static_cast<std::uint32_t>(args.Key()), ::Eikon::CurrentModifiers()))
            {
                args.Handled(true);
            }
        });
        TitleBarArea().SizeChanged([this](auto&&, auto&&) {
            UpdateInfoFit();
            LayoutTools();
            UpdateDragRegions();
        });
        ButtonsGroup().SizeChanged([this](auto&&, auto&&) {
            LayoutTools();
            UpdateDragRegions();
        });
        InfoGroup().SizeChanged([this](auto&&, auto&&) { UpdateDragRegions(); });
        // Moves without a size change (the window resized for a capture) also move the controls.
        TitleBarArea().LayoutUpdated([this](auto&&, auto&&) { UpdateDragRegions(); });

        m_settingsToken = Settings::Current().Changed.Add([weak = get_weak()](std::wstring_view key) {
            auto self = weak.get();
            if (!self)
            {
                return;
            }
            if (key == Settings::kAlwaysOnTop)
            {
                self->ApplyAlwaysOnTop();
            }
            else if (key == Settings::kShowExportCopy || key == Settings::kShowExportSave || key == Settings::kShowExportPin ||
                     key == Settings::kShowExportUpload || key == Settings::kUploadMode)
            {
                self->BuildExportButtons();
            }
            else if (key == Settings::kRealPixels)
            {
                self->PixelsPointsSwitch().IsOn(!Settings::Current().RealPixels());
                self->UpdateInfo();
                self->m_view->Invalidate();
            }
            else if (key == Settings::kColorFormat)
            {
                self->UpdateColorInfo();
            }
            else if (key == Settings::kSnappingMode)
            {
                auto const mode = Settings::Current().SnappingMode();
                self->m_menuSnap.IsChecked(mode == 1);
                self->m_menuSnapSimilar.IsChecked(mode == 2);
            }
            else if (key == Settings::kEverVisitedHotkeys)
            {
                self->UpdateIntro();
            }
        });
        OnViewStateChanged();
        UpdateIntro();
    }

    // ---- Window ----

    void EditorWindow::SetUpWindow()
    {
        try
        {
            ExtendsContentIntoTitleBar(true);
            SetTitleBar(TitleBarArea());
        }
        catch (...)
        {
            OutputDebugStringW(L"Eikon: custom title bar not available\n");
        }
        auto appWindow = AppWindow();
        if (!appWindow)
        {
            return;
        }
        try
        {
            appWindow.TitleBar().PreferredHeightOption(winrt::TitleBarHeightOption::Tall);
        }
        catch (...)
        {
        }
        try
        {
            appWindow.SetIcon(IconPath());
        }
        catch (...)
        {
        }
        // Style mask titled + closable; resizable is added with the first capture.
        if (auto presenter = appWindow.Presenter().try_as<winrt::OverlappedPresenter>())
        {
            presenter.IsMinimizable(false);
            presenter.IsMaximizable(false);
            presenter.IsResizable(false);
        }
        // No open/close animation;: closing hides the window for the next capture.
        appWindow.Closing([weak = get_weak()](winrt::AppWindow const&, winrt::AppWindowClosingEventArgs const& args) {
            if (auto self = weak.get())
            {
                if (!self->m_allowClose)
                {
                    args.Cancel(true);
                    self->HideEditor();
                }
            }
        });
        appWindow.Changed([weak = get_weak()](winrt::AppWindow const&, winrt::AppWindowChangedEventArgs const& args) {
            if (auto self = weak.get(); self && (args.DidSizeChange() || args.DidPresenterChange()))
            {
                self->UpdateDragRegions();
            }
        });
    }

    bool EditorWindow::IsShown() const
    {
        try
        {
            auto appWindow = AppWindow();
            return appWindow && appWindow.IsVisible();
        }
        catch (...)
        {
            return false;
        }
    }

    void EditorWindow::ApplyAlwaysOnTop()
    {
        // alwaysOnTop -> window level 3 (floating).
        try
        {
            if (auto presenter = AppWindow().Presenter().try_as<winrt::OverlappedPresenter>())
            {
                presenter.IsAlwaysOnTop(Settings::Current().AlwaysOnTop());
            }
        }
        catch (...)
        {
        }
    }

    void EditorWindow::SizeAndPlaceFor(Core::Bitmap const& image)
    {
        auto appWindow = AppWindow();
        if (!appWindow)
        {
            return;
        }
        try
        {
            // The screen the capture came from (else the primary one).
            winrt::DisplayArea display{ nullptr };
            if (!image.screenRect.IsEmpty())
            {
                auto const center = image.screenRect.Center();
                display = winrt::DisplayArea::GetFromPoint(
                    winrt::PointInt32{ static_cast<int32_t>(center.x), static_cast<int32_t>(center.y) }, winrt::DisplayAreaFallback::Primary);
            }
            else
            {
                display = winrt::DisplayArea::Primary();
            }
            double const scale = ScaleOfDisplay(display);
            auto const bounds = display.OuterBounds();
            auto const work = display.WorkArea();

            // Move onto that display first so the resize below uses its DPI.
            appWindow.Move(winrt::PointInt32{ work.X, work.Y });

            Core::Size const imagePixels{ static_cast<double>(image.width), static_cast<double>(image.height) };
            Core::Size const screenPoints{ bounds.Width / scale, bounds.Height / scale };
            auto const content = Core::InitialEditorContentSize(imagePixels, image.width > 0 ? image.dpr : 1.0, screenPoints,
                                                                Settings::Current().PreferLargeWindow());
            winrt::SizeInt32 const client{ static_cast<int32_t>(Core::frinta(content.width * scale)),
                                           static_cast<int32_t>(Core::frinta((content.height + kTitleBarHeight) * scale)) };
            appWindow.ResizeClient(client);
            // ResizeClient assumes the standard caption, but with the content extended into the title bar the
            // client area is the whole window: correct by what the client really got.
            HWND hwnd = nullptr;
            if (auto native = this->try_as<::IWindowNative>())
            {
                native->get_WindowHandle(&hwnd);
            }
            RECT actual{};
            if (hwnd && ::GetClientRect(hwnd, &actual) && (actual.right != client.Width || actual.bottom != client.Height))
            {
                auto const outerSize = appWindow.Size();
                appWindow.Resize(winrt::SizeInt32{ outerSize.Width + client.Width - actual.right, outerSize.Height + client.Height - actual.bottom });
            }

            // Centred on the visible frame, raised by 5 % of the spare height.
            auto const outer = appWindow.Size();
            double const spareHeight = static_cast<double>(work.Height - outer.Height);
            int32_t const x = work.X + (work.Width - outer.Width) / 2;
            int32_t y = work.Y + static_cast<int32_t>(Core::frinta(spareHeight / 2 - 0.05 * spareHeight));
            y = (std::max)(y, work.Y);
            appWindow.Move(winrt::PointInt32{ x, y });
        }
        catch (...)
        {
            OutputDebugStringW(L"Eikon: failed to size the editor window\n");
        }
    }

    void EditorWindow::Present()
    {
        bool const opening = !IsShown();
        ApplyAlwaysOnTop();
        auto appWindow = AppWindow();
        if (appWindow)
        {
            appWindow.Show();
        }
        Activate();
        HWND hwnd = nullptr;
        if (auto native = this->try_as<::IWindowNative>())
        {
            native->get_WindowHandle(&hwnd);
        }
        if (hwnd)
        {
            if (::IsIconic(hwnd))
            {
                ::ShowWindow(hwnd, SW_RESTORE);
            }
            ::SetForegroundWindow(hwnd);
        }
        try
        {
            Canvas().Focus(winrt::FocusState::Programmatic);
        }
        catch (...)
        {
        }
        UpdateIntro();
        if (opening && !(m_view && m_view->Document()))
        {
            PlayIntroRise();   // the welcome panel rises each time the empty window opens
        }
        UpdateDragRegions();
    }

    void EditorWindow::ShowEditor()
    {
        if (!IsShown() && !(m_view && m_view->Document()))
        {
            // First show without an image: the empty-image size (836 × 418) on the primary screen.
            SizeAndPlaceFor(Core::Bitmap{});
        }
        Present();
    }

    void EditorWindow::ShowDocument(std::shared_ptr<Core::Document> document, bool keepSize, bool present)
    {
        if (!document)
        {
            return;
        }
        // Resizable from now on.
        if (!m_hasCapture)
        {
            m_hasCapture = true;
            if (auto presenter = AppWindow().Presenter().try_as<winrt::OverlappedPresenter>())
            {
                presenter.IsResizable(true);
                presenter.IsMaximizable(true);
            }
        }
        if (!keepSize)
        {
            SizeAndPlaceFor(document->Image());
        }
        CloseHelpers();
        m_template.reset();
        m_selectedTool.clear();
        if (m_view)
        {
            m_view->SetDocument(std::move(document), keepSize);
        }
        UpdateIntro();
        if (present)
        {
            Present();
        }
    }

    void EditorWindow::HideEditor()
    {
        try
        {
            HideTooltip();
            if (auto appWindow = AppWindow())
            {
                appWindow.Hide();
            }
            Settings::Current().EverHidMainWindow(true);
        }
        catch (...)
        {
        }
    }

    void EditorWindow::Destroy()
    {
        m_allowClose = true;
        Settings::Current().Changed.Remove(m_settingsToken);
        if (m_colorValuesToken)
        {
            m_uiSettings.ColorValuesChanged(m_colorValuesToken);
        }
        m_view.reset();
        Close();
    }

    void EditorWindow::ToggleFullScreen()
    {
        auto appWindow = AppWindow();
        if (!appWindow)
        {
            return;
        }
        if (appWindow.Presenter().Kind() == winrt::AppWindowPresenterKind::FullScreen)
        {
            appWindow.SetPresenter(winrt::AppWindowPresenterKind::Overlapped);
            if (auto presenter = appWindow.Presenter().try_as<winrt::OverlappedPresenter>())
            {
                presenter.IsMinimizable(false);
                presenter.IsMaximizable(m_hasCapture);
                presenter.IsResizable(m_hasCapture);
            }
            ApplyAlwaysOnTop();
        }
        else
        {
            appWindow.SetPresenter(winrt::AppWindowPresenterKind::FullScreen);
        }
    }

    void EditorWindow::UpdateDragRegions()
    {
        // The title bar is a drag region; its controls must stay clickable.
        try
        {
            auto appWindow = AppWindow();
            auto root = RootGrid().XamlRoot();
            if (!appWindow || !root)
            {
                return;
            }
            double const scale = root.RasterizationScale();
            double const inset = appWindow.TitleBar().RightInset() / scale;
            if (std::abs(CaptionColumn().Width().Value - inset) > 0.5)
            {
                CaptionColumn().Width(winrt::GridLengthHelper::FromPixels(inset));
            }
            std::vector<winrt::RectInt32> rects;
            for (winrt::FrameworkElement const element : { ButtonsGroup().as<winrt::FrameworkElement>(), MenuButton().as<winrt::FrameworkElement>(),
                                                           InfoGroup().as<winrt::FrameworkElement>() })
            {
                if (element.Visibility() != winrt::Visibility::Visible || element.ActualWidth() <= 0)
                {
                    continue;
                }
                auto const origin = element.TransformToVisual(nullptr).TransformPoint({ 0, 0 });
                rects.push_back({ static_cast<int32_t>(origin.X * scale), static_cast<int32_t>(origin.Y * scale),
                                  static_cast<int32_t>(element.ActualWidth() * scale), static_cast<int32_t>(element.ActualHeight() * scale) });
            }
            auto const same = [](std::vector<winrt::RectInt32> const& a, std::vector<winrt::RectInt32> const& b) {
                return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](auto const& x, auto const& y) {
                           return x.X == y.X && x.Y == y.Y && x.Width == y.Width && x.Height == y.Height;
                       });
            };
            if (same(rects, m_passthrough))
            {
                return;
            }
            m_passthrough = rects;
            auto source = winrt::Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(appWindow.Id());
            source.SetRegionRects(winrt::Microsoft::UI::Input::NonClientRegionKind::Passthrough, rects);
        }
        catch (...)
        {
        }
    }

    // ---- Strings and theme ----

    void EditorWindow::SetUpStrings()
    {
        CropButton().Content(winrt::box_value(winrt::hstring{ Strings::Get(L"Editor_Crop") }));
        ColorHint().Text(Strings::Get(L"Info_TabToCopy"));
        ZoomDescription().Text(Strings::Get(L"Info_Zoom"));
        PixelsPointsLabel().Text(Strings::Get(L"Pixels_ShowPoints"));
        PixelsExplanation().Text(Strings::Get(L"Pixels_Explanation"));
        ResizeTitle().Text(Strings::Get(L"Resize_Title"));
        ResizeWarning().Text(Strings::Get(L"Resize_Warning"));
        ResizePx().Text(L"px");
        ResizeButton().Content(winrt::box_value(winrt::hstring{ Strings::Get(L"Resize_Button") }));
        ColorFormatLabel().Text(Strings::Get(L"ColorHelper_Format"));
        ColorPixelTitle().Text(Strings::Get(L"ColorHelper_PixelTitle"));
        ColorPixelText().Text(Strings::Get(L"ColorHelper_PixelText"));
        ColorTextTitle().Text(Strings::Get(L"ColorHelper_TextTitle"));
        ColorTextText().Text(Strings::Get(L"ColorHelper_TextText"));
        ColorAverageTitle().Text(Strings::Get(L"ColorHelper_AverageTitle"));
        ColorAverageText().Text(Strings::Get(L"ColorHelper_AverageText"));
        ZoomSheetIn().Text(Strings::Get(L"ZoomSheet_In"));
        ZoomSheetOut().Text(Strings::Get(L"ZoomSheet_Out"));
        ZoomSheetSelection().Text(Strings::Get(L"ZoomSheet_Selection"));
        for (auto const& key : { ZoomSheetInCtrl(), ZoomSheetOutCtrl(), ZoomSheetSelectionCtrl() })
        {
            key.Text(Strings::Get(L"Key_Ctrl"));
        }
        ZoomSheetWheel().Text(Strings::Get(L"ZoomSheet_Wheel"));
        ZoomSheetRightDrag().Text(Strings::Get(L"ZoomSheet_RightDrag"));
        HelpTitle().Text(Strings::Get(L"Help_Title"));
        HelpText().Text(Strings::Get(L"Help_Text"));
        HelpGotIt().Content(winrt::box_value(winrt::hstring{ Strings::Get(L"Help_GotIt") }));
        {
            // "Welcome to " and the name are two TextBlocks (the name has its own gradient); the panel spaces them.
            std::wstring welcome = Strings::Get(L"Intro_Welcome");
            while (!welcome.empty() && welcome.back() == L' ')
            {
                welcome.pop_back();
            }
            IntroWelcomeText().Text(welcome);
        }
        IntroTagline().Text(Strings::Get(L"Intro_Tagline"));
        IntroRunAtStartup().Content(winrt::box_value(winrt::hstring{ Strings::Get(L"Intro_RunAtStartup") }));
        IntroSetupHotkeys().Content(winrt::box_value(winrt::hstring{ Strings::Get(L"Intro_SetupHotkeys") }));
        winrt::ToolTipService::SetToolTip(MenuButton(), winrt::box_value(winrt::hstring{ Strings::Get(L"Menu_More") }));
    }

    void EditorWindow::ApplyThemeImages()
    {
        bool const dark = RootGrid().ActualTheme() == winrt::ElementTheme::Dark;
        auto image = [dark](std::wstring_view name) {
            return winrt::BitmapImage{ AssetUri(dark ? std::wstring{ name } + L"-dark" : std::wstring{ name }) };
        };
        ColorPixelImage().Source(image(L"helper-color-pixel"));
        ColorTextImage().Source(image(L"helper-color-text"));
        ColorAverageImage().Source(image(L"helper-color-average"));
        HelpImage().Source(image(L"helper-ruler"));
        // The wordmark in the Candy gradient (#FF5A36 → #B14BFF); the window text colour in high contrast.
        if (::Eikon::Candy::HighContrast())
        {
            COLORREF const text = ::GetSysColor(COLOR_WINDOWTEXT);
            LogoText().Foreground(winrt::SolidColorBrush{ winrt::Windows::UI::Color{ 0xFF, GetRValue(text), GetGValue(text), GetBValue(text) } });
        }
        else
        {
            winrt::LinearGradientBrush wordmark;
            wordmark.StartPoint({ 0, 0.5f });
            wordmark.EndPoint({ 1, 0.5f });
            for (auto const& [offset, c] : { std::pair{ 0.0, ::Eikon::Candy::kGradientFrom }, std::pair{ 1.0, ::Eikon::Candy::kGradientTo } })
            {
                winrt::GradientStop stop;
                stop.Offset(offset);
                stop.Color({ c.a, c.r, c.g, c.b });
                wordmark.GradientStops().Append(stop);
            }
            LogoText().Foreground(wordmark);
        }
        ApplyIntroTheme();
        CropButton().BorderBrush(ThemeBrush(L"InfoSeparator", dark));
        for (auto const& segments : m_segments)
        {
            segments->root.Background(ThemeBrush(L"SegmentsBackground", dark));
        }
        UpdateToolTint();
        if (m_sizeSlider)
        {
            // cellSliderSize: black α 0.04 fill, α 0.25 stroke (white in dark).
            auto wedge = m_sizeWedge;
            std::uint8_t const level = dark ? 0xFF : 0x00;
            wedge.Fill(winrt::SolidColorBrush{ winrt::Windows::UI::Color{ 0x0A, level, level, level } });
            wedge.Stroke(winrt::SolidColorBrush{ winrt::Windows::UI::Color{ 0x40, level, level, level } });
            UpdateOptionsBar();
        }
        UpdateResizePresets();
    }

    // ---- Toolbar ----

    winrt::Button EditorWindow::IconButton(std::wstring_view asset, double width, double height, std::wstring const& tooltip)
    {
        winrt::Button button;
        button.Style(RootGrid().Resources().Lookup(winrt::box_value(L"IconButtonStyle")).as<winrt::Style>());
        button.Width(width);
        button.Height(height);
        button.Content(TemplateIcon(asset, 20));
        if (!tooltip.empty())
        {
            AttachTooltip(button, tooltip);
        }
        return button;
    }

    void EditorWindow::AttachTooltip(winrt::FrameworkElement const& element, std::wstring const& tooltip)
    {
        // CtrlTooltippedSegments / CtrlToolbar: the red CtrlHelper tooltip instead of the system one.
        // The icon-only buttons take the tooltip as their accessible name (as the Library's do).
        winrt::Automation::AutomationProperties::SetName(element, winrt::hstring{ tooltip });
        element.PointerEntered([this, weak = winrt::make_weak(element), tooltip](auto&&, auto&&) {
            if (auto anchor = weak.get())
            {
                ShowTooltip(anchor, tooltip);
            }
        });
        element.PointerExited([this](auto&&, auto&&) { HideTooltip(); });
        element.PointerCanceled([this](auto&&, auto&&) { HideTooltip(); });
        element.PointerPressed([this](auto&&, auto&&) { HideTooltip(); });
    }

    void EditorWindow::SetUpToolbar()
    {
        BuildExportButtons();
        for (winrt::UIElement const element : { ToolsPanel().as<winrt::UIElement>(), DragOutButton().as<winrt::UIElement>(),
                                                CropButton().as<winrt::UIElement>(), SelectionSegments().as<winrt::UIElement>() })
        {
            winrt::ScalarTransition fade;
            fade.Duration(std::chrono::milliseconds(100));
            element.OpacityTransition(fade);
        }
        CropButton().Opacity(0);
        SelectionSegments().Opacity(0);

        // CtrlToolbar items, in kEditorTools order.
        for (auto const& tool : Core::kEditorTools)
        {
            auto button = IconButton(tool.asset, Core::ToolbarMetrics::itemWidth, Core::ToolbarMetrics::itemHeight,
                                     Strings::Get(tool.tooltipKey));
            std::wstring const id{ tool.id };
            button.Click([this, id](auto&&, auto&&) { SelectTool(id); });
            m_toolButtons.push_back(button);
        }
        m_moreButton = IconButton(L"20-more", Core::ToolbarMetrics::itemWidth, Core::ToolbarMetrics::itemHeight, {});
        m_moreButton.Click([this](auto&&, auto&&) { ShowMoreMenu(); });

        CropButton().Click([this](auto&&, auto&&) { m_view->CropToSelection(); });

        // The selection buttons.
        struct SelectionItem
        {
            std::wstring_view asset;
            std::wstring_view tooltipKey;
            std::function<void()> action;
        };
        std::vector<SelectionItem> const items{
            { L"20-adjust-lite", L"Selection_Adjust", [this] { m_view->SmartAdjust(); } },
            { L"20-resize", L"Selection_Resize", [this] { ToggleHelper(*m_pixelsHelper); } },
            { L"20-ocr-sf", L"Selection_Ocr", [this] { RecognizeText(); } },
            { L"20-selectionsize-lite-sf", L"Selection_Size", [this] { ImprintSelectionSize(); } },
            { L"20-erase-lite-sf", L"Selection_Erase", [this] { m_view->KeyDown(VK_DELETE, Modifiers::None); } },
            { L"20-scissors", L"Selection_CutOut", [this] { m_view->CutOut(); } },
            { L"20-reset-lite", L"Selection_Reset", [this] { m_view->ResetSelection(); m_view->RecordUndo(); } },
        };
        for (auto const& item : items)
        {
            auto button = IconButton(item.asset, 32, 32, Strings::Get(item.tooltipKey));
            button.Click([action = item.action](auto&&, auto&&) { action(); });
            SelectionButtons().Children().Append(button);
        }
        // Snake: Ctrl+click between x 24 and 34 of the 75-pt box, i.e. 15.5…25.5 pt into the centred
        // 58-pt logo art, starts the game and moves the pointer 20 pt down, off the board.
        LogoBox().PointerPressed([this](auto&&, winrt::Input::PointerRoutedEventArgs const& args) {
            double const x = args.GetCurrentPoint(LogoBox()).Position().X + (75.0 - 58.0) / 2;
            bool const ctrl = (args.KeyModifiers() & winrt::Windows::System::VirtualKeyModifiers::Control) != winrt::Windows::System::VirtualKeyModifiers::None;
            if (!ctrl || x < 24.0 || x > 34.0)
            {
                return;
            }
            POINT cursor{};
            if (GetCursorPos(&cursor))
            {
                double const scale = RootGrid().XamlRoot() ? RootGrid().XamlRoot().RasterizationScale() : 1.0;
                SetCursorPos(cursor.x, cursor.y + static_cast<int>(std::lround(20 * scale)));
            }
            StartSnake();
            args.Handled(true);
        });
    }

    void EditorWindow::BuildExportButtons()
    {
        // Which export segments exist.
        auto& settings = Settings::Current();
        auto panel = ExportButtons();
        panel.Children().Clear();
        auto add = [&](std::wstring_view asset, std::wstring_view tooltipKey, std::function<void()> action) {
            auto button = IconButton(asset, 32, 32, Strings::Get(tooltipKey));
            button.Click([action = std::move(action)](auto&&, auto&&) { action(); });
            panel.Children().Append(button);
        };
        if (settings.ShowExportCopy())
        {
            add(L"20-copy-lite", L"Export_Copy", [this] { CopyImage(); });
        }
        if (settings.ShowExportSave())
        {
            add(L"20-save-lite", L"Export_Save", [this] { SaveImage(); });
        }
        if (settings.ShowExportPin())
        {
            add(L"20-pin-lite-sf", L"Export_Pin", [this] { PinImage(); });
        }
        if (settings.ShowExportUpload() && settings.UploadMode() != L"none")
        {
            add(L"20-upload-lite", L"Export_Upload", [this] { UploadImage(); });
        }
        ExportSegments().Visibility(panel.Children().Size() > 0 ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);
        LayoutTools();
    }

    void EditorWindow::UpdateInfoFit()
    {
        // Items that don't fit move into the overflow, the trailing ones first. The
        // Buttons item (630 pt minimum) with the eight selection segments needs more than the 836-pt
        // window leaves beside the Info item, so the Info item gives way while the selection shows.
        bool const empty = !m_view || !m_view->Document();
        bool fits = true;
        double const total = TitleBarArea().ActualWidth();
        if (!empty && total > 0 && SelectionSegments().Visibility() == winrt::Visibility::Visible)
        {
            // The selection row from the controls' fixed widths (measuring a group that has just been
            // made visible returns its old, collapsed size): logo, export segments, Crop, the 30-wide
            // selection segments, 7 apart; then the menu button, the 278-wide Info item and the
            // caption buttons.
            double const crop = (std::max)(CropButton().MinWidth(), CropButton().ActualWidth());
            double needed = ButtonsGroup().Margin().Left + LogoBox().Width() + 32.0 * ExportButtons().Children().Size() + 4 + crop +
                32.0 * SelectionButtons().Children().Size() + 4 + 3 * ButtonsGroup().Spacing();
            needed += MenuButton().Width() + MenuButton().Margin().Right;
            needed += 278 + InfoGroup().Margin().Right;
            needed += CaptionColumn().Width().Value;
            fits = needed <= total;
        }
        InfoGroup().Visibility(!empty && fits ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);
    }

    void EditorWindow::LayoutTools()
    {
        // The toolbar is the first thing to shrink; it gets what the rest of the title bar leaves.
        if (ToolsPanel().Visibility() != winrt::Visibility::Visible || m_toolButtons.empty())
        {
            return;
        }
        double const total = TitleBarArea().ActualWidth();
        if (total <= 0)
        {
            return;
        }
        double used = ButtonsGroup().Margin().Left;
        int visibleChildren = 0;
        for (auto const& child : ButtonsGroup().Children())
        {
            auto element = child.as<winrt::FrameworkElement>();
            if (element.Visibility() != winrt::Visibility::Visible)
            {
                continue;
            }
            ++visibleChildren;
            if (element == ToolsPanel())
            {
                continue;
            }
            used += element.ActualWidth() > 0 ? element.ActualWidth() : element.Width();
        }
        used += ButtonsGroup().Spacing() * (std::max)(0, visibleChildren - 1);
        used += MenuButton().ActualWidth() + MenuButton().Margin().Right;
        used += InfoGroup().ActualWidth() + InfoGroup().Margin().Right;
        used += CaptionColumn().ActualWidth();
        double const available = (std::max)(Core::ToolbarMetrics::itemWidth, total - used - kOverflowSafety);
        std::size_t const capacity = Core::ToolbarCapacity(available);
        std::size_t const count = Core::ToolbarVisibleCount(m_toolButtons.size(), capacity);
        if (count == m_visibleTools && ToolsPanel().Children().Size() > 0)
        {
            return;
        }
        m_visibleTools = count;
        auto children = ToolsPanel().Children();
        children.Clear();
        for (std::size_t i = 0; i < count; ++i)
        {
            children.Append(m_toolButtons[i]);
        }
        if (count < m_toolButtons.size())
        {
            children.Append(m_moreButton);
        }
        UpdateToolTint();
    }

    void EditorWindow::UpdateToolTint()
    {
        // Candy: the active tool is a lilac fill with an accent-tinted glyph.
        bool const dark = RootGrid().ActualTheme() == winrt::ElementTheme::Dark;
        auto const normal = ThemeBrush(L"CandyInk2", dark);
        auto const accent = ThemeBrush(L"CandyAccent", dark);
        auto const soft = ThemeBrush(L"CandyAccentSoft", dark);
        winrt::SolidColorBrush const clear{ winrt::Windows::UI::Color{ 0, 0, 0, 0 } };
        for (std::size_t i = 0; i < m_toolButtons.size(); ++i)
        {
            bool const selected = !m_selectedTool.empty() && Core::kEditorTools[i].id == m_selectedTool;
            m_toolButtons[i].Foreground(selected ? accent : normal);
            m_toolButtons[i].Background(selected ? soft : clear);
        }
        if (!m_moreButton)
        {
            return;
        }
        // The more button shows a hidden selected tool's icon with the 20-more-overlay (24 × 20 composite).
        std::size_t const index = Core::EditorToolIndex(m_selectedTool);
        bool const hiddenSelected = !m_selectedTool.empty() && index < Core::kEditorTools.size() && index >= m_visibleTools;
        if (hiddenSelected)
        {
            winrt::Grid composite;
            composite.Width(24);
            composite.Height(20);
            auto toolIcon = TemplateIcon(Core::kEditorTools[index].asset, 20);
            toolIcon.HorizontalAlignment(winrt::HorizontalAlignment::Left);
            toolIcon.Margin({ -2, 0, 0, 0 });
            auto overlay = TemplateIcon(L"20-more-overlay", 20);
            overlay.HorizontalAlignment(winrt::HorizontalAlignment::Left);
            overlay.Margin({ 4, 0, 0, 0 });
            composite.Children().Append(toolIcon);
            composite.Children().Append(overlay);
            m_moreButton.Content(composite);
            m_moreButton.Foreground(accent);
            m_moreButton.Background(soft);
        }
        else
        {
            m_moreButton.Content(TemplateIcon(L"20-more", 20));
            m_moreButton.Foreground(normal);
            m_moreButton.Background(clear);
        }
    }

    void EditorWindow::ShowMoreMenu()
    {
        // The hidden tools, labelled without their " (X)" shortcut.
        winrt::MenuFlyout menu;
        for (std::size_t i = m_visibleTools; i < Core::kEditorTools.size(); ++i)
        {
            auto const& tool = Core::kEditorTools[i];
            winrt::MenuFlyoutItem item;
            item.Text(Core::StripShortcut(Strings::Get(tool.tooltipKey)));
            item.Icon(TemplateIcon(tool.asset, 16));
            std::wstring const id{ tool.id };
            item.Click([this, id](auto&&, auto&&) { SelectTool(id); });
            menu.Items().Append(item);
        }
        menu.ShowAt(m_moreButton);
    }

    void EditorWindow::UpdateToolbarMode()
    {
        // "selection", an object class ("Obj…") or "default".
        bool const selection = m_mode == L"selection";
        bool const object = m_mode.starts_with(L"Obj");
        // Groups fade in over 0.1 s (the OpacityTransitions set in SetUpToolbar); hidden ones go at once.
        auto show = [](winrt::UIElement const& element, bool visible) {
            if (visible)
            {
                element.Visibility(winrt::Visibility::Visible);
                element.Opacity(1);
            }
            else
            {
                element.Visibility(winrt::Visibility::Collapsed);
                element.Opacity(0);
            }
        };
        show(ToolsPanel(), !selection);
        show(DragOutButton(), !selection);
        show(CropButton(), selection);
        show(SelectionSegments(), selection);
        UpdateInfoFit();
        if (object)
        {
            m_optionsBar->Show();
        }
        else
        {
            m_optionsBar->Hide();
        }
        LayoutTools();
    }

    // ---- Info bar ----

    void EditorWindow::SetUpInfo()
    {
        ColorBox().Tapped([this](auto&&, auto&&) { ToggleHelper(*m_colorHelper); });
        SizeBox().Tapped([this](auto&&, auto&&) {
            FillResizeFields();
            ToggleHelper(*m_pixelsHelper);
        });
        ZoomBox().Tapped([this](auto&&, auto&&) { ToggleHelper(*m_zoomHelper); });
    }

    void EditorWindow::OnViewStateChanged()
    {
        if (!m_view)
        {
            return;
        }
        std::wstring mode = m_view->Mode();
        if (mode == L"default" && !m_view->Tool().empty())
        {
            mode = std::wstring{ ClassForTool(m_view->Tool()) };
        }
        bool const modeChanged = mode != m_mode;
        m_mode = std::move(mode);
        if (modeChanged)
        {
            UpdateToolbarMode();
        }
        if (m_mode.starts_with(L"Obj"))
        {
            UpdateOptionsBar();
        }
        UpdateInfo();
        UpdateIntro();
    }

    void EditorWindow::UpdateInfo()
    {
        auto const& document = m_view->Document();
        bool const realPixels = Settings::Current().RealPixels();
        if (document)
        {
            Core::Rect const rect = m_view->SelectionOrCanvas();
            SizeLabel().Text(Core::SizeLabel(rect.width, rect.height, document->Image().dpr, realPixels));
            SizeDescription().Text(Strings::Get(m_view->Selection() ? L"Info_SelectionSize" : L"Info_ImageSize"));
            ZoomLabel().Text(Core::ZoomLabel(m_view->Zoom()));
            ZoomPercentText().Text(Core::ZoomLabel(m_view->Zoom()));
        }
        else
        {
            SizeLabel().Text(L"");
            SizeDescription().Text(Strings::Get(L"Info_ImageSize"));
            ZoomLabel().Text(Core::ZoomLabel(1.0));
            ZoomPercentText().Text(Core::ZoomLabel(1.0));
        }
        UpdateColorInfo();
    }

    void EditorWindow::UpdateColorInfo()
    {
        if (!m_view)
        {
            return;
        }
        auto const color = m_view->HoverColor();
        std::wstring text = m_view->Pixels().ColorReadout();
        if (text.empty())
        {
            text = color ? HexOf(*color) : L"#FFFFFF";
        }
        ColorLabel().Text(text);
        ColorSwatch().Background(winrt::SolidColorBrush{ color ? *color : winrt::Windows::UI::Color{ 0xFF, 0xFF, 0xFF, 0xFF } });
        ColorBox().Width(20 + kColorLabelWidths[ColorFormatIndex()]);
    }

    // ---- Intro ----

    void EditorWindow::SetUpIntro()
    {
        IntroRunAtStartup().Click([this](auto&&, auto&&) {
            auto& app = ::Eikon::AppController::Current();
            if (!app.IsLaunchAtStartupEnabled())
            {
                app.ToggleLaunchAtStartup();
            }
            IntroRunAtStartup().Visibility(winrt::Visibility::Collapsed);
        });
        IntroSetupHotkeys().Click([](auto&&, auto&&) { ::Eikon::AppController::Current().OpenSettings(L"hotkeys"); });
        try
        {
            // Raised off the UI thread when high contrast (or any system colour) changes.
            m_colorValuesToken = m_uiSettings.ColorValuesChanged([weak = get_weak(), queue = DispatcherQueue()](auto&&, auto&&) {
                queue.TryEnqueue([weak] {
                    if (auto self = weak.get())
                    {
                        self->ApplyIntroTheme();
                    }
                });
            });
        }
        catch (...)
        {
        }
    }

    void EditorWindow::ApplyIntroTheme()
    {
        // Lucida's welcome look (the "Candy" direction): base colour, peach / lilac / mint fields, ink text, the
        // name in the #FF5A36 → #B14BFF gradient, a solid ink button and a translucent ghost button. High contrast
        // keeps to the system colours with no gradients.
        using C = winrt::Windows::UI::Color;
        auto const solid = [](C color) { return winrt::SolidColorBrush{ color }; };
        auto const system = [](int index) {
            COLORREF const c = ::GetSysColor(index);
            return C{ 0xFF, GetRValue(c), GetGValue(c), GetBValue(c) };
        };
        bool const highContrast = ::Eikon::Candy::HighContrast();
        bool const empty = !m_view || !m_view->Document();
        IntroBackdrop().Visibility(empty && !highContrast ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);
        auto const setGradient = [](winrt::LinearGradientBrush const& brush, C from, C to) {
            brush.GradientStops().GetAt(0).Color(from);
            brush.GradientStops().GetAt(1).Color(to);
        };
        if (highContrast)
        {
            C const text = system(COLOR_WINDOWTEXT);
            IntroWelcomeText().Foreground(solid(text));
            setGradient(IntroNameBrush(), text, text);
            IntroTagline().Foreground(solid(text));
            for (auto const& button : { IntroRunAtStartup(), IntroSetupHotkeys() })
            {
                button.Background(solid(system(COLOR_BTNFACE)));
                button.Foreground(solid(system(COLOR_BTNTEXT)));
                button.BorderBrush(solid(system(COLOR_BTNTEXT)));
                button.BorderThickness(winrt::ThicknessHelper::FromUniformLength(1));
            }
            return;
        }
        bool const dark = RootGrid().ActualTheme() == winrt::ElementTheme::Dark;
        // radial-gradient(rx ry at x y, colour 0%, transparent 62%), in the box's relative units. A new brush each
        // time: a RadialGradientBrush keeps the stop colours it was first drawn with.
        auto const field = [](winrt::Shapes::Rectangle const& rectangle, C color, winrt::Windows::Foundation::Point center, double rx,
                              double ry) {
            winrt::RadialGradientBrush brush;
            brush.Center(center);
            brush.GradientOrigin(center);
            brush.RadiusX(rx);
            brush.RadiusY(ry);
            winrt::GradientStop inner;
            inner.Color(color);
            inner.Offset(0);
            winrt::GradientStop outer;
            outer.Color(C{ 0, color.R, color.G, color.B });   // CSS "transparent" fades the hue, not to grey
            outer.Offset(0.62);
            brush.GradientStops().Append(inner);
            brush.GradientStops().Append(outer);
            rectangle.Fill(brush);
        };
        IntroBackdrop().Background(solid(dark ? C{ 0xFF, 0x15, 0x12, 0x1F } : C{ 0xFF, 0xFB, 0xF7, 0xFF }));
        field(IntroFieldPeach(), dark ? C{ 0xFF, 0x5A, 0x2A, 0x22 } : C{ 0xFF, 0xFF, 0xD9, 0xC4 }, { 0.08f, 0.08f }, 0.55, 0.75);
        field(IntroFieldLilac(), dark ? C{ 0xFF, 0x3A, 0x2A, 0x6E } : C{ 0xFF, 0xD8, 0xCC, 0xFF }, { 0.94f, 0.14f }, 0.55, 0.75);
        field(IntroFieldMint(), dark ? C{ 0xFF, 0x1D, 0x4A, 0x3C } : C{ 0xFF, 0xC3, 0xF0, 0xDE }, { 0.55f, 1.12f }, 0.7, 0.8);
        C const ink = dark ? C{ 0xFF, 0xF3, 0xEF, 0xFF } : C{ 0xFF, 0x1A, 0x15, 0x30 };
        C const gradientFrom{ 0xFF, 0xFF, 0x5A, 0x36 };
        C const gradientTo{ 0xFF, 0xB1, 0x4B, 0xFF };
        IntroWelcomeText().Foreground(solid(ink));
        setGradient(IntroNameBrush(), gradientFrom, gradientTo);
        IntroTagline().Foreground(solid(dark ? C{ 0xFF, 0xB9, 0xB2, 0xD6 } : C{ 0xFF, 0x4B, 0x45, 0x66 }));
        // "Run at Startup": solid ink, white text (inverted in dark). "Setup Hotkeys": the ghost, white α 0.67
        // (α 0.08 in dark) with a 1-px ink α 0.13 ring (white α 0.15).
        IntroRunAtStartup().Background(solid(ink));
        IntroRunAtStartup().Foreground(solid(dark ? C{ 0xFF, 0x15, 0x12, 0x1F } : C{ 0xFF, 0xFF, 0xFF, 0xFF }));
        IntroRunAtStartup().BorderThickness(winrt::ThicknessHelper::FromUniformLength(0));
        IntroSetupHotkeys().Background(solid(dark ? C{ 0x14, 0xFF, 0xFF, 0xFF } : C{ 0xAA, 0xFF, 0xFF, 0xFF }));
        IntroSetupHotkeys().Foreground(solid(ink));
        IntroSetupHotkeys().BorderBrush(solid(dark ? C{ 0x26, 0xFF, 0xFF, 0xFF } : C{ 0x22, 0x1A, 0x15, 0x30 }));
        IntroSetupHotkeys().BorderThickness(winrt::ThicknessHelper::FromUniformLength(1));
    }

    void EditorWindow::PlayIntroRise()
    {
        // The rows rise 8 px and fade in over 0.45 s, 60 ms apart, with a slight overshoot
        // (cubic-bezier(.3, 1.4, .5, 1)); nothing moves when Windows' animations are off.
        bool animate = true;
        try
        {
            animate = m_uiSettings.AnimationsEnabled();
        }
        catch (...)
        {
        }
        using winrt::Microsoft::UI::Xaml::Hosting::ElementCompositionPreview;
        int index = 0;
        for (winrt::UIElement const element : { IntroHeadline().as<winrt::UIElement>(), IntroTagline().as<winrt::UIElement>(),
                                                IntroButtons().as<winrt::UIElement>() })
        {
            ElementCompositionPreview::SetIsTranslationEnabled(element, true);
            auto visual = ElementCompositionPreview::GetElementVisual(element);
            visual.StopAnimation(L"Opacity");
            visual.StopAnimation(L"Translation");
            if (!animate)
            {
                visual.Opacity(1);
                visual.Properties().InsertVector3(L"Translation", { 0, 0, 0 });
                ++index;
                continue;
            }
            auto compositor = visual.Compositor();
            auto const rise = compositor.CreateCubicBezierEasingFunction({ 0.3f, 1.4f }, { 0.5f, 1.0f });
            auto const duration = std::chrono::milliseconds{ 450 };
            auto const delay = std::chrono::milliseconds{ 60 * index };
            auto opacity = compositor.CreateScalarKeyFrameAnimation();
            opacity.InsertKeyFrame(0.0f, 0.0f);
            opacity.InsertKeyFrame(1.0f, 1.0f, rise);
            opacity.Duration(duration);
            opacity.DelayTime(delay);
            opacity.DelayBehavior(winrt::Microsoft::UI::Composition::AnimationDelayBehavior::SetInitialValueBeforeDelay);
            auto translation = compositor.CreateVector3KeyFrameAnimation();
            translation.InsertKeyFrame(0.0f, { 0, 8, 0 });
            translation.InsertKeyFrame(1.0f, { 0, 0, 0 }, rise);
            translation.Duration(duration);
            translation.DelayTime(delay);
            translation.DelayBehavior(winrt::Microsoft::UI::Composition::AnimationDelayBehavior::SetInitialValueBeforeDelay);
            visual.StartAnimation(L"Opacity", opacity);
            visual.StartAnimation(L"Translation", translation);
            ++index;
        }
    }

    void EditorWindow::UpdateIntro()
    {
        bool const empty = !m_view || !m_view->Document();
        bool const appearing = empty && IntroPanel().Visibility() != winrt::Visibility::Visible;
        IntroPanel().Visibility(empty ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);
        ApplyIntroTheme();   // the backdrop follows the empty state
        // The toolbar is hidden until the first capture (the "⋯" menu stays).
        ButtonsGroup().Visibility(empty ? winrt::Visibility::Collapsed : winrt::Visibility::Visible);
        UpdateInfoFit();
        if (!empty)
        {
            return;
        }
        // The welcome panel: each button shows until its step is done.
        IntroRunAtStartup().Visibility(::Eikon::AppController::Current().IsLaunchAtStartupEnabled() ? winrt::Visibility::Collapsed
                                                                                                     : winrt::Visibility::Visible);
        IntroSetupHotkeys().Visibility(Settings::Current().EverVisitedHotkeys() ? winrt::Visibility::Collapsed : winrt::Visibility::Visible);
        if (appearing)
        {
            PlayIntroRise();
        }
    }

    // ---- Helpers ----

    void EditorWindow::SetUpHelpers()
    {
        using Options = Helper::Options;
        Options tooltip;
        tooltip.showDelay = 0.6;
        tooltip.targetOpacity = 1;
        m_tooltip = std::make_unique<Helper>(Tooltip(), tooltip);
        // Candy: the options bar and the helpers are floating 16-px cards with a shadow (ThemeShadow at Z 24).
        Options card;
        card.elevation = 24;
        Options bar = card;
        bar.transient = false;
        m_optionsBar = std::make_unique<Helper>(OptionsBar(), bar);
        m_zoomHelper = std::make_unique<Helper>(ZoomHelper(), card);
        m_colorHelper = std::make_unique<Helper>(ColorHelper(), card);
        m_pixelsHelper = std::make_unique<Helper>(PixelsHelper(), card);
        m_helpHelper = std::make_unique<Helper>(HelpHelper(), card);
        for (auto const& element : { ZoomHelper(), ColorHelper(), PixelsHelper(), HelpHelper(), OptionsBar() })
        {
            element.Shadow(winrt::ThemeShadow{});
        }
        // Clicks inside a helper must not reach the canvas behind it.
        for (auto const& element : { ZoomHelper(), ColorHelper(), PixelsHelper(), HelpHelper(), OptionsBar() })
        {
            element.PointerPressed([](auto&&, winrt::Input::PointerRoutedEventArgs const& args) { args.Handled(true); });
        }

        ZoomInButton().Click([this](auto&&, auto&&) { m_view->ZoomIn(); });
        ZoomOutButton().Click([this](auto&&, auto&&) { m_view->ZoomOut(); });
        ZoomPercentButton().Click([this](auto&&, auto&&) { m_view->ZoomTo100(); });
        HelpGotIt().Click([this](auto&&, auto&&) { m_helpHelper->Hide(); });

        for (auto const& [key, title] : kColorFormats)
        {
            ColorFormatBox().Items().Append(winrt::box_value(winrt::hstring{ title }));
        }
        ColorFormatBox().SelectedIndex(static_cast<int32_t>(ColorFormatIndex()));
        ColorFormatBox().SelectionChanged([this](auto&&, auto&&) {
            int32_t const index = ColorFormatBox().SelectedIndex();
            if (index >= 0 && index < static_cast<int32_t>(std::size(kColorFormats)))
            {
                Settings::Current().ColorFormat(std::wstring{ kColorFormats[index].first });
            }
        });

        PixelsPointsSwitch().IsOn(!Settings::Current().RealPixels());
        PixelsPointsSwitch().Toggled([this](auto&&, auto&&) {
            bool const points = PixelsPointsSwitch().IsOn();
            if (Settings::Current().RealPixels() == points)
            {
                Settings::Current().RealPixels(!points);
            }
        });
    }

    bool EditorWindow::AnyHelperOpen() const
    {
        return m_zoomHelper->IsOpen() || m_colorHelper->IsOpen() || m_pixelsHelper->IsOpen() || m_helpHelper->IsOpen();
    }

    bool EditorWindow::CloseHelpers()
    {
        // Zoom, colour, pixels, help (the backdrop panel and colour picker are flyouts that
        // close on their own when the canvas is clicked).
        bool closed = false;
        for (Helper* helper : { m_zoomHelper.get(), m_colorHelper.get(), m_pixelsHelper.get(), m_helpHelper.get() })
        {
            if (helper && helper->IsOpen())
            {
                helper->Hide();
                closed = true;
            }
        }
        return closed;
    }

    void EditorWindow::ToggleHelper(Helper& helper)
    {
        bool const open = helper.IsOpen();
        CloseHelpers();
        if (!open)
        {
            if (&helper == m_pixelsHelper.get())
            {
                FillResizeFields();
            }
            helper.Show();
        }
        try
        {
            Canvas().Focus(winrt::FocusState::Programmatic);
        }
        catch (...)
        {
        }
    }

    void EditorWindow::ShowTooltip(winrt::FrameworkElement const& anchor, std::wstring const& text)
    {
        // Split at "(", width = name + key + 40 (+ 32 without a key), centred under the button.
        auto const parts = Core::SplitTooltip(text);
        TooltipName().Text(parts.name);
        TooltipKey().Text(parts.key);
        TooltipKey().Visibility(parts.key.empty() ? winrt::Visibility::Collapsed : winrt::Visibility::Visible);
        TooltipName().Measure({ 1000, 100 });
        TooltipKey().Measure({ 1000, 100 });
        double const nameWidth = TooltipName().DesiredSize().Width;
        double const keyWidth = parts.key.empty() ? 0 : TooltipKey().DesiredSize().Width;
        double const width = Core::TooltipWidth(nameWidth, keyWidth, !parts.key.empty());
        Tooltip().Width(width);
        auto const origin = anchor.TransformToVisual(ContentArea()).TransformPoint({ 0, 0 });
        double x = origin.X + anchor.ActualWidth() / 2 - width / 2;
        x = std::clamp(x, 4.0, (std::max)(4.0, ContentArea().ActualWidth() - width - 4));
        Tooltip().Margin({ x, 5, 0, 0 });
        m_tooltipAnchor = anchor;
        if (m_tooltip->IsOpen())
        {
            return;   // moving between buttons keeps it up
        }
        m_tooltip->Show();
    }

    void EditorWindow::HideTooltip()
    {
        m_tooltipAnchor = nullptr;
        if (m_tooltip)
        {
            m_tooltip->Hide();
        }
    }

    // ---- Options bar ----

    EditorWindow::Segments& EditorWindow::AddSegments(std::wstring const& property, std::vector<std::wstring> codes,
                                                      std::vector<std::wstring> images, std::vector<std::wstring> labels, double width)
    {
        auto segments = std::make_unique<Segments>();
        segments->property = property;
        segments->codes = std::move(codes);
        winrt::Border root;
        root.CornerRadius({ 8, 8, 8, 8 });
        root.Background(ThemeBrush(L"SegmentsBackground", RootGrid().ActualTheme() == winrt::ElementTheme::Dark));
        root.Height(24);
        winrt::StackPanel panel;
        panel.Orientation(winrt::Orientation::Horizontal);
        root.Child(panel);
        for (std::size_t i = 0; i < segments->codes.size(); ++i)
        {
            winrt::Button button;
            button.Style(RootGrid().Resources().Lookup(winrt::box_value(L"IconButtonStyle")).as<winrt::Style>());
            button.Width(width);
            button.Height(24);
            button.CornerRadius({ 6, 6, 6, 6 });   // Candy inner segment
            if (i < images.size() && !images[i].empty())
            {
                button.Content(TemplateIcon(images[i], 16));
            }
            else if (i < labels.size())
            {
                winrt::TextBlock text;
                text.Text(labels[i]);
                text.FontSize(11);
                button.Content(text);
            }
            std::wstring const code = segments->codes[i];
            button.Click([this, property, code](auto&&, auto&&) { SetOptionsProperty(property, code); });
            panel.Children().Append(button);
            segments->buttons.push_back(button);
        }
        segments->root = root;
        OptionsStack().Children().Append(root);
        m_segments.push_back(std::move(segments));
        return *m_segments.back();
    }

    void EditorWindow::SetUpOptionsBar()
    {
        auto stack = OptionsStack();
        // editorUnselect: deselect / leave the tool.
        m_backButton = IconButton(L"20-back-lite-sf", 24, 24, {});
        m_backButton.Click([this](auto&&, auto&&) {
            if (!m_view->LeaveMode())
            {
                SelectTool(L"");
            }
        });
        stack.Children().Append(m_backButton);

        // CtrlColorButton 18 × 18 with the colour panel.
        m_colorButton = winrt::Button{};
        m_colorButton.Style(RootGrid().Resources().Lookup(winrt::box_value(L"IconButtonStyle")).as<winrt::Style>());
        m_colorButton.Width(24);
        m_colorButton.Height(24);
        m_colorSwatch = winrt::Border{};
        m_colorSwatch.Width(18);
        m_colorSwatch.Height(18);
        m_colorSwatch.CornerRadius({ 4, 4, 4, 4 });
        m_colorSwatch.BorderThickness({ 0.5, 0.5, 0.5, 0.5 });
        m_colorSwatch.BorderBrush(winrt::SolidColorBrush{ winrt::Windows::UI::Color{ 0x4D, 0, 0, 0 } });
        m_colorButton.Content(m_colorSwatch);
        m_colorPicker = winrt::ColorPicker{};
        m_colorPicker.IsAlphaEnabled(false);
        m_colorPicker.IsMoreButtonVisible(false);
        m_colorPicker.ColorChanged([this](auto&&, winrt::ColorChangedEventArgs const& args) {
            if (!m_updatingOptions)
            {
                SetOptionsProperty(L"color", HexOf(args.NewColor()));
            }
        });
        winrt::Flyout colorFlyout;
        colorFlyout.Content(m_colorPicker);
        m_colorButton.Flyout(colorFlyout);
        stack.Children().Append(m_colorButton);

        // sliderSize: 0–7, 8 ticks, snaps to ticks, width 100.
        m_sizeSlider = winrt::Slider{};
        m_sizeSlider.Width(100);
        m_sizeSlider.Minimum(0);
        m_sizeSlider.Maximum(7);
        m_sizeSlider.StepFrequency(1);
        m_sizeSlider.TickFrequency(1);
        m_sizeSlider.SnapsTo(winrt::SliderSnapsTo::Ticks);
        m_sizeSlider.IsThumbToolTipEnabled(false);
        m_sizeSlider.IsTabStop(false);
        m_sizeSlider.VerticalAlignment(winrt::VerticalAlignment::Center);
        m_sizeSlider.ValueChanged([this](auto&&, winrt::RangeBaseValueChangedEventArgs const& args) {
            if (!m_updatingOptions)
            {
                SetOptionsProperty(L"size", std::to_wstring(static_cast<int>(std::lround(args.NewValue()))));
            }
        });
        // cellSliderSize: no track or ticks, a wedge 2 pt tall at the left (x + 4.5) widening to 11 pt at the right.
        for (wchar_t const* key : { L"SliderTrackFill", L"SliderTrackFillPointerOver", L"SliderTrackFillPressed", L"SliderTrackValueFill",
                                    L"SliderTrackValueFillPointerOver", L"SliderTrackValueFillPressed" })
        {
            m_sizeSlider.Resources().Insert(winrt::box_value(key), winrt::SolidColorBrush{ winrt::Windows::UI::Color{ 0, 0, 0, 0 } });
        }
        m_sizeSlider.TickPlacement(winrt::TickPlacement::None);
        m_sizeWedge = winrt::Shapes::Polygon{};
        auto wedge = m_sizeWedge;
        for (auto const& [x, y] : { std::pair{ 4.5f, 11.0f }, std::pair{ 95.5f, 6.5f }, std::pair{ 95.5f, 17.5f }, std::pair{ 4.5f, 13.0f } })
        {
            wedge.Points().Append({ x, y });
        }
        wedge.StrokeThickness(1);
        wedge.IsHitTestVisible(false);
        m_sizeBox = winrt::Grid{};
        m_sizeBox.Width(100);
        m_sizeBox.Height(24);
        m_sizeBox.Children().Append(wedge);
        m_sizeBox.Children().Append(m_sizeSlider);
        stack.Children().Append(m_sizeBox);

        // Value codes are static arrays ( / the editorSet* actions).
        AddSegments(L"lineType", { L"line", L"dash" }, { L"16-line", L"16-dash" }, {}, 24);
        AddSegments(L"arrowType", { L"0", L"4", L"1", L"2" }, { L"16-arrow-shape", L"16-arrow-handdrawn", L"16-arrow-line", L"16-arrow-double" }, {}, 24);
        m_arrowStyleButton = IconButton(L"16-toggle", 24, 24, {});
        m_arrowStyleButton.Content(TemplateIcon(L"16-toggle", 16));
        m_arrowStyleButton.Click([this](auto&&, auto&&) { SetOptionsProperty(L"changeStyle", L""); });
        stack.Children().Append(m_arrowStyleButton);
        AddSegments(L"concealType", { L"pixelate", L"pixelate-text", L"erase", L"erase-text" }, {},
                    { Strings::Get(L"Options_Blur"), Strings::Get(L"Options_BlurText"), Strings::Get(L"Options_Erase"),
                      Strings::Get(L"Options_EraseText") },
                    44);
        AddSegments(L"rulerPixels", { L"px", L"pt" }, {}, { L"PX", L"PT" }, 30);
        AddSegments(L"fill", { L"-1", L"0", L"40", L"100" },
                    { L"16-objfill-handdrawn", L"16-objfill-transparent", L"16-objfill-translucent", L"16-objfill-opaque" }, {}, 24);
        AddSegments(L"shape", { L"rect", L"oval" }, { L"16-shape-rect", L"16-shape-oval" }, {}, 24);
        AddSegments(L"style", { L"0", L"1" }, { L"16-text-sans", L"16-text-goofy" }, {}, 24);
        AddSegments(L"correction", { L"x", L"s", L"c" }, { L"16-shape-blob", L"", L"" }, {}, 24);

        auto toggle = [&](std::wstring_view asset) {
            winrt::ToggleButton button;
            button.Width(24);
            button.Height(24);
            button.MinWidth(0);
            button.MinHeight(0);
            button.Padding({ 0, 0, 0, 0 });
            button.IsTabStop(false);
            button.Content(TemplateIcon(asset, 16));
            stack.Children().Append(button);
            return button;
        };
        m_transparencyButton = toggle(L"16-transparency");
        m_transparencyButton.Click([this](auto&&, auto&&) {
            SetOptionsProperty(L"transparency", m_transparencyButton.IsChecked().Value() ? L"true" : L"false");
        });
        m_gifButton = toggle(L"16-gif");
        m_gifButton.Click([](auto&&, auto&&) {
            // The toggle only shows its state; animated GIF export is not part of the editor.
        });
    }

    std::wstring EditorWindow::OptionsClass() const
    {
        if (auto* active = m_view ? m_view->Objects().Active() : nullptr)
        {
            return std::wstring{ active->ClassName() };
        }
        if (m_template)
        {
            return std::wstring{ m_template->ClassName() };
        }
        return m_view ? std::wstring{ ClassForTool(m_view->Tool()) } : std::wstring{};
    }

    std::wstring EditorWindow::OptionsProperty(std::wstring_view key) const
    {
        if (auto* active = m_view ? m_view->Objects().Active() : nullptr)
        {
            return active->Property(key);
        }
        if (m_template)
        {
            return m_template->Property(key);
        }
        auto& settings = Settings::Current();
        if (key == L"size")
        {
            return std::to_wstring(settings.ReadInt(L"Eikon." + OptionsClass() + L": size", 2));
        }
        if (key == L"color")
        {
            return settings.DefaultColor();
        }
        return {};
    }

    void EditorWindow::SetOptionsProperty(std::wstring_view key, std::wstring_view value)
    {
        if (m_view && m_view->Objects().Active())
        {
            m_view->Objects().SetActiveProperty(key, value);
        }
        else
        {
            // No object yet: the choice applies to the next one of this class.
            if (m_template)
            {
                m_template->SetProperty(key, value);
            }
            auto& settings = Settings::Current();
            if (key == L"size")
            {
                settings.WriteInt(L"Eikon." + OptionsClass() + L": size", _wtoi(std::wstring{ value }.c_str()));
            }
            else if (key == L"color")
            {
                settings.DefaultColor(std::wstring{ value });
            }
        }
        UpdateOptionsBar();
        m_view->Invalidate();
    }

    void EditorWindow::UpdateOptionsBar()
    {
        // The controls per class (by substring of the class name).
        std::wstring const cls = OptionsClass();
        auto has = [&](std::wstring_view part) { return cls.find(part) != std::wstring::npos; };
        auto show = [](winrt::UIElement const& element, bool visible) {
            element.Visibility(visible ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);
        };
        m_updatingOptions = true;
        show(m_colorButton, !has(L"Conceal"));
        show(m_sizeBox, !has(L"Ruler"));
        show(m_arrowStyleButton, has(L"Arrow") || has(L"Counter"));
        show(m_transparencyButton, has(L"Image"));
        show(m_gifButton, has(L"Image"));
        for (auto const& segments : m_segments)
        {
            std::wstring_view const property = segments->property;
            bool visible = false;
            if (property == L"lineType") visible = has(L"Line");
            else if (property == L"arrowType") visible = has(L"Arrow");
            else if (property == L"concealType") visible = has(L"Conceal");
            else if (property == L"rulerPixels") visible = has(L"Ruler");
            else if (property == L"fill") visible = has(L"Rectangle") || has(L"Oval");
            else if (property == L"shape") visible = has(L"Spotlight") || has(L"Magnifier");
            else if (property == L"style") visible = has(L"Text");
            else if (property == L"correction") visible = has(L"Freehand");
            show(segments->root, visible);
            if (!visible)
            {
                continue;
            }
            std::wstring const value = OptionsProperty(property);
            bool const dark = RootGrid().ActualTheme() == winrt::ElementTheme::Dark;
            auto const selected = ThemeBrush(L"CtrlButtonSelected", dark);
            auto const accent = ThemeBrush(L"CandyAccent", dark);
            auto const normal = ThemeBrush(L"CandyInk2", dark);
            winrt::SolidColorBrush const clear{ winrt::Windows::UI::Color{ 0, 0, 0, 0 } };
            for (std::size_t i = 0; i < segments->buttons.size(); ++i)
            {
                bool const on = segments->codes[i] == value;
                segments->buttons[i].Background(on ? selected : clear);
                segments->buttons[i].Foreground(on ? accent : normal);
            }
        }
        std::wstring const size = OptionsProperty(L"size");
        m_sizeSlider.Value(size.empty() ? 2 : std::clamp(_wtoi(size.c_str()), 0, 7));
        auto const color = ColorFromHex(OptionsProperty(L"color"));
        m_colorSwatch.Background(winrt::SolidColorBrush{ color });
        m_colorPicker.Color(color);
        m_transparencyButton.IsChecked(OptionsProperty(L"transparency") == L"true");
        m_updatingOptions = false;
    }

    // ---- Resize Image ----

    void EditorWindow::SetUpResize()
    {
        static constexpr std::wstring_view labels[] = { L"25%", L"33%", L"50%", L"1×", L"2×", L"4×" };
        auto grid = ResizePresets();
        for (int i = 0; i < 6; ++i)
        {
            grid.ColumnDefinitions().Append(winrt::ColumnDefinition{});
            winrt::Button button;
            button.Style(RootGrid().Resources().Lookup(winrt::box_value(L"IconButtonStyle")).as<winrt::Style>());
            button.HorizontalAlignment(winrt::HorizontalAlignment::Stretch);
            button.Height(24);
            winrt::TextBlock text;
            text.Text(labels[i]);
            text.FontSize(12);
            button.Content(text);
            winrt::Grid::SetColumn(button, i);
            button.Click([this, i](auto&&, auto&&) {
                auto const& document = m_view->Document();
                if (!document)
                {
                    return;
                }
                m_updatingResize = true;
                ResizeWidth().Text(std::to_wstring(Core::ResizePresetSize(document->Image().width, i)));
                ResizeHeight().Text(std::to_wstring(Core::ResizePresetSize(document->Image().height, i)));
                m_updatingResize = false;
                UpdateResizePresets();
            });
            grid.Children().Append(button);
            m_presetButtons.push_back(button);
        }
        auto locked = [this](winrt::TextBox const& edited, winrt::TextBox const& other, bool editedIsWidth) {
            if (m_updatingResize)
            {
                return;
            }
            auto const& document = m_view->Document();
            if (!document)
            {
                return;
            }
            std::int64_t const value = _wtoi64(edited.Text().c_str());
            std::int64_t const editedOriginal = editedIsWidth ? document->Image().width : document->Image().height;
            std::int64_t const otherOriginal = editedIsWidth ? document->Image().height : document->Image().width;
            m_updatingResize = true;
            other.Text(std::to_wstring(Core::AspectLocked(value, editedOriginal, otherOriginal)));
            m_updatingResize = false;
            UpdateResizePresets();
        };
        ResizeWidth().TextChanged([this, locked](auto&&, auto&&) { locked(ResizeWidth(), ResizeHeight(), true); });
        ResizeHeight().TextChanged([this, locked](auto&&, auto&&) { locked(ResizeHeight(), ResizeWidth(), false); });
        for (auto const& box : { ResizeWidth(), ResizeHeight() })
        {
            box.KeyDown([this](auto&&, winrt::Input::KeyRoutedEventArgs const& args) {
                if (args.Key() == winrt::Windows::System::VirtualKey::Enter)
                {
                    args.Handled(true);
                    ResizeImage();
                }
            });
        }
        ResizeButton().Click([this](auto&&, auto&&) { ResizeImage(); });
    }

    void EditorWindow::FillResizeFields()
    {
        auto const& document = m_view ? m_view->Document() : nullptr;
        m_updatingResize = true;
        ResizeWidth().Text(document ? std::to_wstring(document->Image().width) : L"");
        ResizeHeight().Text(document ? std::to_wstring(document->Image().height) : L"");
        m_updatingResize = false;
        UpdateResizePresets();
    }

    void EditorWindow::UpdateResizePresets()
    {
        auto const& document = m_view ? m_view->Document() : nullptr;
        std::optional<int> preset;
        if (document)
        {
            preset = Core::ResizePresetIndex(_wtoi64(ResizeWidth().Text().c_str()), document->Image().width);
        }
        auto const selected = ThemeBrush(L"CtrlButtonSelected", RootGrid().ActualTheme() == winrt::ElementTheme::Dark);
        winrt::SolidColorBrush const clear{ winrt::Windows::UI::Color{ 0, 0, 0, 0 } };
        for (int i = 0; i < static_cast<int>(m_presetButtons.size()); ++i)
        {
            m_presetButtons[i].Background(preset && *preset == i ? selected : clear);
        }
    }

    void EditorWindow::ResizeImage()
    {
        auto const document = m_view ? m_view->Document() : nullptr;
        std::int64_t const width = _wtoi64(ResizeWidth().Text().c_str());
        std::int64_t const height = _wtoi64(ResizeHeight().Text().c_str());
        auto const check = Core::CheckResize(width, height, document ? document->Image().width : 0, document ? document->Image().height : 0);
        switch (check)
        {
        case Core::ResizeCheck::NoImage:
            Notify(Strings::Get(L"Resize_ErrorTitle"), Strings::Get(L"Resize_NoImage"));
            return;
        case Core::ResizeCheck::TooSmall:
            Notify(Strings::Get(L"Resize_CantTitle"), Strings::Get(L"Resize_TooSmall"));
            return;
        case Core::ResizeCheck::TooLarge:
            Notify(Strings::Get(L"Resize_CantTitle"), Strings::Get(L"Resize_TooLarge"));
            return;
        case Core::ResizeCheck::SameSize:
            m_pixelsHelper->Hide();
            return;
        default:
            break;
        }
        try
        {
            namespace canvas = winrt::Microsoft::Graphics::Canvas;
            auto const& image = document->Image();
            auto device = canvas::CanvasDevice::GetSharedDevice();
            auto source = canvas::CanvasBitmap::CreateFromBytes(device, winrt::array_view<std::uint8_t const>(image.pixels), image.width,
                image.height, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 96.0f,
                canvas::CanvasAlphaMode::Premultiplied);
            canvas::CanvasRenderTarget target{ device, static_cast<float>(width), static_cast<float>(height), 96.0f };
            auto interpolation = canvas::CanvasImageInterpolation::HighQualityCubic;
            switch (Core::ResizeQualityFor(static_cast<double>(width) / image.width))
            {
            case Core::ResizeQuality::Nearest: interpolation = canvas::CanvasImageInterpolation::NearestNeighbor; break;
            case Core::ResizeQuality::Low: interpolation = canvas::CanvasImageInterpolation::Linear; break;
            default: break;
            }
            {
                auto session = target.CreateDrawingSession();
                session.Clear(winrt::Windows::UI::Color{ 0, 0, 0, 0 });
                session.DrawImage(source, winrt::Windows::Foundation::Rect{ 0, 0, static_cast<float>(width), static_cast<float>(height) },
                                  winrt::Windows::Foundation::Rect{ 0, 0, static_cast<float>(image.width), static_cast<float>(image.height) }, 1.0f,
                                  interpolation);
                session.Close();
            }
            Core::Bitmap resized;
            resized.width = static_cast<std::int32_t>(width);
            resized.height = static_cast<std::int32_t>(height);
            resized.dpr = image.dpr;
            resized.screenRect = image.screenRect;
            auto const bytes = target.GetPixelBytes();
            resized.pixels.assign(bytes.begin(), bytes.end());
            auto next = std::make_shared<Core::Document>(std::move(resized));
            next->metadata = document->metadata;
            m_pixelsHelper->Hide();
            // Loaded as a new capture keeping the window size; not undoable.
            ::Eikon::AppController::Current().SetCurrentDocument(next);
            ShowDocument(next, true);
        }
        catch (...)
        {
            Notify(Strings::Get(L"Editor_WrongTitle"), Strings::Get(L"Editor_WrongMessage"));
        }
    }

    void EditorWindow::Notify(std::wstring const& title, std::wstring const& message)
    {
        // Errors: 5 s with the warning beep.
        ::Eikon::AppController::Current().Notify({ title, message, true, 5.0, true });
    }

    // ---- Main menu ----

    void EditorWindow::SetUpMenu()
    {
        m_menu = winrt::MenuFlyout{};
        auto item = [this](std::wstring_view key, std::wstring const& accelerator, std::function<void()> action, bool needsDocument = true) {
            winrt::MenuFlyoutItem entry;
            entry.Text(Strings::Get(key));
            if (!accelerator.empty())
            {
                entry.KeyboardAcceleratorTextOverride(accelerator);
            }
            entry.Click([action = std::move(action)](auto&&, auto&&) { action(); });
            if (needsDocument)
            {
                m_documentItems.push_back(entry);
            }
            return entry;
        };
        auto submenu = [](std::wstring_view key, std::vector<winrt::MenuFlyoutItemBase> const& items) {
            winrt::MenuFlyoutSubItem sub;
            sub.Text(Strings::Get(key));
            for (auto const& entry : items)
            {
                sub.Items().Append(entry);
            }
            return sub;
        };
        auto separator = [] { return winrt::MenuFlyoutSeparator{}.as<winrt::MenuFlyoutItemBase>(); };
        auto& app = ::Eikon::AppController::Current();
        Modifiers const ctrl = Modifiers::Ctrl;
        Modifiers const ctrlShift = Modifiers::Ctrl | Modifiers::Shift;

        m_menu.Items().Append(submenu(L"Menu_App", {
            item(L"Menu_Settings", Shortcut(VK_OEM_COMMA, ctrl), [&app] { app.OpenSettings(); }, false),
            separator(),
            item(L"Menu_Quit", {}, [&app] { app.Quit(); }, false),
        }));

        m_menuOpenClipboard = item(L"Menu_OpenClipboard", Shortcut('V', ctrlShift), [&app] { app.LoadFromClipboard(L"menu"); }, false);
        m_menu.Items().Append(submenu(L"Menu_File", {
            item(L"Menu_CopyImage", Shortcut('C', ctrlShift), [this] { CopyImage(); }),
            item(L"Menu_SaveImage", Shortcut('S', ctrl), [this] { SaveImage(); }),
            item(L"Menu_SaveImageAs", Shortcut('S', ctrlShift), [this] { SaveImageAs(); }),
            item(L"Menu_UploadImage", Shortcut('E', ctrl), [this] { UploadImage(); }),
            item(L"Menu_Pin", Shortcut('P', ctrl), [this] { PinImage(); }),
            item(L"Menu_Print", Shortcut('P', ctrlShift), [this] { PrintImage(); }),
            separator(),
            item(L"Menu_OpenFile", Shortcut('O', ctrlShift), [&app] { app.OpenFile(L"menu"); }, false),
            m_menuOpenClipboard,
            separator(),
            item(L"Menu_Close", Shortcut('W', ctrl), [this] { HideEditor(); }, false),
        }));

        m_menuResetCrop = item(L"Menu_ResetCrop", {}, [this] { m_view->ResetCrop(); });
        m_menu.Items().Append(submenu(L"Menu_Edit", {
            item(L"Menu_Undo", Shortcut('Z', ctrl), [this] { m_view->Undo(); }),
            item(L"Menu_Redo", Shortcut('Z', ctrlShift), [this] { m_view->Redo(); }),
            m_menuResetCrop,
            item(L"Menu_Rasterize", {}, [this] {
                try
                {
                    m_view->Rasterize();
                }
                catch (...)
                {
                    Notify(Strings::Get(L"Editor_WrongTitle"), Strings::Get(L"Editor_WrongMessage"));
                }
            }),
        }));

        m_menuSnap = winrt::ToggleMenuFlyoutItem{};
        m_menuSnap.Text(Strings::Get(L"Menu_Snap"));
        m_menuSnap.Click([this](auto&&, auto&&) { SetSnappingMode(Settings::Current().SnappingMode() == 1 ? 0 : 1); });
        m_menuSnapSimilar = winrt::ToggleMenuFlyoutItem{};
        m_menuSnapSimilar.Text(Strings::Get(L"Menu_SnapSimilar"));
        m_menuSnapSimilar.Click([this](auto&&, auto&&) { SetSnappingMode(Settings::Current().SnappingMode() == 2 ? 0 : 2); });
        auto const snapping = Settings::Current().SnappingMode();
        m_menuSnap.IsChecked(snapping == 1);
        m_menuSnapSimilar.IsChecked(snapping == 2);
        auto tool = [&](std::wstring_view key, std::uint32_t letter, std::wstring_view id) {
            return item(key, Shortcut(letter), [this, id = std::wstring{ id }] { SelectTool(id); });
        };
        m_menu.Items().Append(submenu(L"Menu_Draw", {
            tool(L"Menu_Arrow", 'A', L"arrow"),
            tool(L"Menu_Text", 'T', L"text"),
            tool(L"Menu_Rectangle", 'R', L"rectangle"),
            tool(L"Menu_Freehand", 'D', L"freehand"),
            tool(L"Menu_Magnifier", 'M', L"magnifier"),
            tool(L"Menu_Highlighter", 'H', L"highlighter"),
            tool(L"Menu_Blur", 'B', L"blur"),
            tool(L"Menu_Spotlight", 'S', L"spotlight"),
            tool(L"Menu_Counter", 'C', L"counter"),
            tool(L"Menu_Oval", 'O', L"oval"),
            tool(L"Menu_Line", 'L', L"line"),
            item(L"Menu_AddCapture", {}, [&app] { app.AppendCapture(); }),
            separator(),
            m_menuSnap,
            m_menuSnapSimilar,
        }));

        m_menu.Items().Append(submenu(L"Menu_Zoom", {
            item(L"Menu_ZoomIn", Shortcut(VK_OEM_PLUS, ctrl), [this] { m_view->ZoomIn(); }),
            item(L"Menu_ZoomOut", Shortcut(VK_OEM_MINUS, ctrl), [this] { m_view->ZoomOut(); }),
            separator(),
            item(L"Menu_ZoomFit", Shortcut('1', ctrl), [this] { m_view->ZoomToFit(); }),
            item(L"Menu_Zoom100", Shortcut('0', ctrl), [this] { m_view->ZoomTo100(); }),
            separator(),
            item(L"Menu_ZoomSelection", Shortcut('2', ctrl), [this] { m_view->ZoomToSelection(); }),
            item(L"Menu_SelectionTopLeft", Shortcut('Q'), [this] { m_view->SelectionTopLeft(); }),
            item(L"Menu_SelectionBottomRight", Shortcut('W'), [this] { m_view->SelectionBottomRight(); }),
            separator(),
            item(L"Menu_FullScreen", Shortcut(VK_F11), [this] { ToggleFullScreen(); }, false),
        }));

        // The Help menu's items all open website; Lucida has none, so no Help menu.

        // validateMenuItem.
        m_menu.Opening([this](auto&&, auto&&) {
            bool const hasDocument = m_view && m_view->Document();
            for (auto const& entry : m_documentItems)
            {
                if (auto control = entry.try_as<winrt::Control>())
                {
                    control.IsEnabled(hasDocument);
                }
            }
            m_menuResetCrop.IsEnabled(hasDocument && m_view->CanResetCrop());
            m_menuOpenClipboard.IsEnabled(ClipboardHasImage());
            auto const mode = Settings::Current().SnappingMode();
            m_menuSnap.IsChecked(mode == 1);
            m_menuSnapSimilar.IsChecked(mode == 2);
        });
        MenuButton().Click([this](auto&&, auto&&) {
            HideTooltip();
            m_menu.ShowAt(MenuButton());
        });
    }

    void EditorWindow::SetSnappingMode(std::int64_t mode)
    {
        Settings::Current().SnappingMode(mode);
        m_menuSnap.IsChecked(mode == 1);
        m_menuSnapSimilar.IsChecked(mode == 2);
    }

    // ---- Actions ----

    void EditorWindow::SelectTool(std::wstring_view id)
    {
        if (!m_view || !m_view->Document())
        {
            return;
        }
        HideTooltip();
        auto& app = ::Eikon::AppController::Current();
        if (id == L"backdrop")
        {
            std::size_t const index = Core::EditorToolIndex(L"backdrop");
            winrt::FrameworkElement const anchor = index < m_visibleTools ? m_toolButtons[index].as<winrt::FrameworkElement>()
                                                                          : m_moreButton.as<winrt::FrameworkElement>();
            ::Eikon::Editor::ShowBackdropPanel(anchor, m_view->Document());
            return;
        }
        if (id == L"append")
        {
            app.AppendCapture();
            return;
        }
        if (id == L"ruler")
        {
            // The ruler works from the keyboard (1 / 2); its button shows how.
            ToggleHelper(*m_helpHelper);
            return;
        }
        if (id == L"image")
        {
            PasteImage();
            return;
        }
        m_view->Objects().Deselect();
        m_view->ResetSelection();
        m_selectedTool = std::wstring{ id };
        m_template = id.empty() ? nullptr : Core::CreateObject(id, Settings::Current());
        m_view->SetTool(id);
        UpdateToolTint();
        OnViewStateChanged();
        if (m_mode.starts_with(L"Obj"))
        {
            UpdateOptionsBar();
        }
    }

    void EditorWindow::CopyImage()
    {
        if (!m_view || !m_view->Document())
        {
            return;
        }
        ::Eikon::AppController::Current().CopyImage();
        // copyImage hides the editor afterwards.
        HideEditor();
    }

    void EditorWindow::SaveImage()
    {
        if (m_view && m_view->Document())
        {
            ::Eikon::AppController::Current().SaveImage();
        }
    }

    void EditorWindow::SaveImageAs()
    {
        if (m_view && m_view->Document())
        {
            ::Eikon::AppController::Current().SaveImageAs();
        }
    }

    void EditorWindow::UploadImage()
    {
        if (m_view && m_view->Document())
        {
            ::Eikon::AppController::Current().UploadImage();
        }
    }

    void EditorWindow::PrintImage()
    {
        if (m_view && m_view->Document())
        {
            ::Eikon::AppController::Current().PrintImage();
        }
    }

    void EditorWindow::PinImage()
    {
        if (m_view && m_view->Document())
        {
            ::Eikon::AppController::Current().PinImage(::Eikon::Editor::Compose(*m_view->Document()));
        }
    }

    void EditorWindow::RecognizeText()
    {
        auto const& document = m_view ? m_view->Document() : nullptr;
        if (!document)
        {
            return;
        }
        std::shared_ptr<Core::Bitmap const> image;
        if (auto const selection = m_view->Selection())
        {
            Core::Document region{ Core::Bitmap{ document->Image() } };
            region.SetCrop(selection->Standardized().Intersection(region.Source()));
            image = ::Eikon::Editor::Compose(region);
        }
        else
        {
            // Ctrl+O without a selection: the whole image if each side is at most 1000 pt.
            double const dpr = document->Image().dpr > 0 ? document->Image().dpr : 1.0;
            Core::Rect const crop = document->Crop();
            if (crop.width / dpr > 1000 || crop.height / dpr > 1000)
            {
                Alert(Strings::Get(L"Ocr_SelectTitle"), Strings::Get(L"Ocr_SelectMessage"));
                return;
            }
            image = ::Eikon::Editor::Compose(*document);
        }
        ::Eikon::AppController::Current().RecognizeText(image, L"editor");
    }

    void EditorWindow::ImprintSelectionSize()
    {
        // Imprints the selection's size label as a ruler object (ObjRuler).
        auto const selection = m_view->Selection();
        auto const& document = m_view->Document();
        if (!selection || !document)
        {
            return;
        }
        if (auto object = Core::CreateObject(L"ruler", Settings::Current()))
        {
            object->rect = selection->Standardized();
            object->state = std::wstring{ Core::ObjectState::Unselected };
            document->Add(std::move(object));
            m_view->ResetSelection();
            m_view->RecordUndo();
        }
    }

    void EditorWindow::PasteImage()
    {
        // Ctrl+V: a copied object; pasting a bitmap as ObjImage belongs to the annotations work.
        if (m_view && m_view->Objects().PasteCopied())
        {
            m_view->RecordUndo();
            OnViewStateChanged();
        }
    }

    void EditorWindow::Escape()
    {
        // In priority order.
        if (m_view && m_view->CancelCreation())
        {
            return;
        }
        if (CloseHelpers())
        {
            return;
        }
        if (m_view && m_view->LeaveMode())
        {
            return;
        }
        if (m_view && !m_view->Tool().empty())
        {
            SelectTool(L"");
            return;
        }
        if (!m_view || !m_view->Document())
        {
            HideEditor();
            return;
        }
        auto& settings = Settings::Current();
        bool const copy = settings.CopyOnEsc();
        bool const save = settings.SaveOnEsc();
        if (save)
        {
            SaveImage();
        }
        if (copy)
        {
            CopyImage();
        }
        if (!copy && !save)
        {
            HideEditor();
        }
    }

    winrt::fire_and_forget EditorWindow::QuitAction()
    {
        // cmdQAction.
        auto strong = get_strong();
        auto& settings = Settings::Current();
        std::wstring action = settings.CmdQAction();
        if (action != L"quit" && action != L"close")
        {
            if (m_dialogOpen)
            {
                co_return;
            }
            winrt::ContentDialog dialog;
            dialog.XamlRoot(RootGrid().XamlRoot());
            dialog.Title(winrt::box_value(winrt::hstring{ Strings::Get(L"Quit_Title") }));
            winrt::TextBlock text;
            text.Text(Strings::Get(L"Quit_Message"));
            text.TextWrapping(winrt::TextWrapping::Wrap);
            dialog.Content(text);
            dialog.PrimaryButtonText(Strings::Get(L"Quit_Quit"));
            dialog.SecondaryButtonText(Strings::Get(L"Quit_Close"));
            dialog.DefaultButton(winrt::ContentDialogButton::Primary);
            m_dialogOpen = true;
            auto const result = co_await dialog.ShowAsync();
            m_dialogOpen = false;
            if (result == winrt::ContentDialogResult::None)
            {
                co_return;
            }
            action = result == winrt::ContentDialogResult::Primary ? L"quit" : L"close";
            settings.CmdQAction(action);
        }
        if (action == L"quit")
        {
            ::Eikon::AppController::Current().Quit();
        }
        else
        {
            HideEditor();
        }
    }

    winrt::fire_and_forget EditorWindow::Alert(std::wstring title, std::wstring message)
    {
        auto strong = get_strong();
        if (m_dialogOpen)
        {
            co_return;
        }
        winrt::ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(winrt::box_value(winrt::hstring{ title }));
        dialog.Content(winrt::box_value(winrt::hstring{ message }));
        dialog.CloseButtonText(Strings::Get(L"Dialog_OK"));
        m_dialogOpen = true;
        co_await dialog.ShowAsync();
        m_dialogOpen = false;
    }

    // ---- Keys ----

    bool EditorWindow::HandleKeyDown(std::uint32_t key, Modifiers modifiers, bool repeat)
    {
        if (m_dialogOpen || !m_view)
        {
            return false;
        }
        // While the Snake is on, every key goes to it (isOn).
        if (m_snake && m_snake->IsOn())
        {
            using Direction = ::Eikon::Core::SnakeGame::Direction;
            switch (key)
            {
            case VK_LEFT: m_snake->Steer(Direction::Left); break;
            case VK_RIGHT: m_snake->Steer(Direction::Right); break;
            case VK_UP: m_snake->Steer(Direction::Up); break;
            case VK_DOWN: m_snake->Steer(Direction::Down); break;
            default: break;
            }
            return true;
        }
        // Text fields, the format popup and the slider keep their keys; Esc still closes the helper.
        if (auto focused = winrt::Input::FocusManager::GetFocusedElement(RootGrid().XamlRoot()))
        {
            // The backdrop panel's flyout keeps every key (Esc closes it, arrows move its sliders).
            for (auto node = focused.try_as<winrt::DependencyObject>(); node; node = winrt::Media::VisualTreeHelper::GetParent(node))
            {
                if (node.try_as<winrt::Eikon::BackdropPanel>())
                {
                    return false;
                }
            }
            if (focused.try_as<winrt::TextBox>() || focused.try_as<winrt::ComboBox>() || focused.try_as<winrt::ColorPicker>())
            {
                if (key == VK_ESCAPE)
                {
                    CloseHelpers();
                    Canvas().Focus(winrt::FocusState::Programmatic);
                    return true;
                }
                return false;
            }
        }
        if (key == VK_ESCAPE)
        {
            if (!repeat)
            {
                Escape();
            }
            return true;
        }
        if (key == VK_CONTROL || key == VK_SHIFT || key == VK_MENU || key == VK_LWIN || key == VK_RWIN)
        {
            return false;
        }
        HideTooltip();
        bool const hadDocument = m_view->Document() != nullptr;
        // (1) object, (2) crop selection, then the pixel tools and held keys.
        if (hadDocument && m_view->KeyDown(key, modifiers))
        {
            OnViewStateChanged();
            return true;
        }
        if (GlobalKey(key, modifiers))
        {
            return true;
        }
        // Tab never moves focus in the editor.
        return key == VK_TAB;
    }

    bool EditorWindow::GlobalKey(std::uint32_t key, Modifiers modifiers)
    {
        auto& app = ::Eikon::AppController::Current();
        bool const hasDocument = m_view->Document() != nullptr;
        Modifiers const ctrl = Modifiers::Ctrl;
        Modifiers const ctrlShift = Modifiers::Ctrl | Modifiers::Shift;

        // ---- Ctrl ----
        if (modifiers == ctrl)
        {
            switch (key)
            {
            case 'C': CopyImage(); return true;
            case 'S': SaveImage(); return true;
            case 'E': UploadImage(); return true;
            case 'P': PinImage(); return true;
            case 'O': RecognizeText(); return true;
            case 'A': app.AppendCapture(); return true;
            case 'Q': QuitAction(); return true;
            case 'W': HideEditor(); return true;
            case 'Z': if (hasDocument) m_view->Undo(); return true;
            case 'Y': if (hasDocument) m_view->Redo(); return true;
            case 'V': PasteImage(); return true;
            case '1': if (hasDocument) m_view->ZoomToFit(); return true;
            case '0': if (hasDocument) m_view->ZoomTo100(); return true;
            case '2': if (hasDocument) m_view->ZoomToSelection(); return true;
            case VK_OEM_PLUS:
            case VK_ADD: if (hasDocument) m_view->ZoomIn(); return true;
            case VK_OEM_MINUS:
            case VK_SUBTRACT: if (hasDocument) m_view->ZoomOut(); return true;
            case VK_OEM_COMMA: app.OpenSettings(); return true;
            default: return false;
            }
        }
        if (modifiers == ctrlShift)
        {
            switch (key)
            {
            case 'C': CopyImage(); return true;
            case 'S': SaveImageAs(); return true;
            case 'P': PrintImage(); return true;
            case 'O': app.OpenFile(L"menu"); return true;
            case 'V': app.LoadFromClipboard(L"menu"); return true;
            case 'Z': if (hasDocument) m_view->Redo(); return true;
            default: return false;
            }
        }
        if (modifiers == Modifiers::None && key == VK_F2)
        {
            UploadImage();
            return true;
        }
        if (modifiers == Modifiers::None && key == VK_F11)
        {
            ToggleFullScreen();
            return true;
        }
        if (!hasDocument)
        {
            return false;
        }

        // ---- Shift: zoom keys (require Shift), Shift+] resize ----
        if (modifiers == Modifiers::Shift)
        {
            switch (key)
            {
            case '1': m_view->ZoomToFit(); return true;
            case '2': m_view->ZoomToSelection(); return true;
            case VK_OEM_PLUS: m_view->ZoomIn(); return true;
            case VK_OEM_MINUS: m_view->ZoomOut(); return true;
            case '0': m_view->ZoomTo100(); return true;
            case VK_OEM_6:
                FillResizeFields();
                ToggleHelper(*m_pixelsHelper);
                return true;
            default: return false;
            }
        }
        if (modifiers != Modifiers::None)
        {
            return false;
        }

        // ---- Bare keys ----
        switch (key)
        {
        case VK_NUMPAD1: m_view->ZoomToFit(); return true;
        case VK_ADD: m_view->ZoomIn(); return true;
        case VK_SUBTRACT: m_view->ZoomOut(); return true;
        case 'Q': m_view->SelectionTopLeft(); return true;
        case 'W': m_view->SelectionBottomRight(); return true;
        case 'P': Settings::Current().RealPixels(!Settings::Current().RealPixels()); return true;
        case 'V': SelectTool(L""); return true;
        case 'K': SelectTool(L"backdrop"); return true;
        default: break;
        }
        // Tools: only without a crop selection.
        if (!m_view->Selection())
        {
            std::wstring_view tool;
            switch (key)
            {
            case 'A': tool = L"arrow"; break;
            case 'T': tool = L"text"; break;
            case 'R': tool = L"rectangle"; break;
            case 'D': tool = L"freehand"; break;
            case 'M': tool = L"magnifier"; break;
            case 'H': tool = L"highlighter"; break;
            case 'B': tool = L"blur"; break;
            case 'S': tool = L"spotlight"; break;
            case 'C': tool = L"counter"; break;
            case 'O': tool = L"oval"; break;
            case 'L': tool = L"line"; break;
            default: break;
            }
            if (!tool.empty())
            {
                SelectTool(tool);
                return true;
            }
        }
        return false;
    }

    // ---- Snake ----

    void EditorWindow::BuildSnakeField()
    {
        using Game = ::Eikon::Core::SnakeGame;
        if (!m_snakeCells.empty())
        {
            return;
        }
        // One 3 × 3 cell (corner radius 0.5) per board cell at (4·col + 1, 4·row + 1)
        // in the y-up field.
        for (int col = 0; col < Game::kCols; ++col)
        {
            for (int row = 0; row < Game::kRows; ++row)
            {
                winrt::Microsoft::UI::Xaml::Shapes::Rectangle cell;
                cell.Width(Game::kCellSize);
                cell.Height(Game::kCellSize);
                cell.RadiusX(0.5);
                cell.RadiusY(0.5);
                double const step = Game::kCellSize + Game::kGap;
                winrt::Canvas::SetLeft(cell, step * col + 1);
                winrt::Canvas::SetTop(cell, Game::FieldHeight() - (step * row + 1) - Game::kCellSize);
                SnakeField().Children().Append(cell);
                m_snakeCells.push_back(cell);
            }
        }
    }

    void EditorWindow::StartSnake()
    {
        if (!m_snake)
        {
            m_snake = std::make_unique<::Eikon::Core::SnakeGame>([engine = std::make_shared<std::mt19937_64>(std::random_device{}())](std::uint64_t bound) {
                return std::uniform_int_distribution<std::uint64_t>{ 0, bound - 1 }(*engine);
            });
        }
        BuildSnakeField();
        // The logo image goes, the field shows, the first step comes after 0.4 s.
        m_snake->Start();
        LogoText().Opacity(0);
        SnakeField().Visibility(winrt::Visibility::Visible);
        PaintSnake();
        if (!m_snakeTimer)
        {
            m_snakeTimer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
            m_snakeTimer.IsRepeating(false);
            m_snakeTimer.Tick([this](auto&&, auto&&) { SnakeStep(); });
        }
        m_snakeTimer.Stop();
        m_snakeTimer.Interval(std::chrono::milliseconds{ ::Eikon::Core::SnakeGame::kFirstTickMs });
        m_snakeTimer.Start();
    }

    void EditorWindow::SnakeStep()
    {
        if (!m_snake)
        {
            return;
        }
        int const delay = m_snake->Tick();
        if (delay < 0)
        {
            // When not playing: the logo comes back.
            SnakeField().Visibility(winrt::Visibility::Collapsed);
            LogoText().Opacity(1);
            return;
        }
        PaintSnake();
        m_snakeTimer.Interval(std::chrono::milliseconds{ delay });
        m_snakeTimer.Start();
    }

    void EditorWindow::PaintSnake()
    {
        using Game = ::Eikon::Core::SnakeGame;
        // Lit cells in clrMain (#FF0C01); the rest black (white in dark) at an alpha
        // that grows towards the border.
        bool const dark = RootGrid().ActualTheme() == winrt::ElementTheme::Dark;
        std::uint8_t const level = dark ? 0xFF : 0x00;
        for (int col = 0; col < Game::kCols; ++col)
        {
            for (int row = 0; row < Game::kRows; ++row)
            {
                winrt::Windows::UI::Color color{ 0xFF, 0xFF, 0x0C, 0x01 };
                if (!m_snake->Lit(col, row))
                {
                    double const alpha = Game::BandAlpha(Game::Band(col, row), dark);
                    color = { static_cast<std::uint8_t>(std::lround(alpha * 255)), level, level, level };
                }
                m_snakeCells[static_cast<std::size_t>(col * Game::kRows + row)].Fill(winrt::SolidColorBrush{ color });
            }
        }
    }
}
