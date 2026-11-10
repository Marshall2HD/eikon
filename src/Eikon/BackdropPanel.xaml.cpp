#include "pch.h"
#include "BackdropPanel.xaml.h"
#if __has_include("BackdropPanel.g.cpp")
#include "BackdropPanel.g.cpp"
#endif

#include "AppController.h"
#include "Core/Backdrop.h"
#include "Core/Settings.h"
#include "Editor/BackdropUi.h"
#include "Platform/Exporter.h"
#include "Strings.h"

#include <shobjidl.h>
#include <winrt/Windows.Storage.Pickers.h>

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Controls::Primitives;
    using namespace Microsoft::UI::Xaml::Media;
    using namespace Microsoft::UI::Xaml::Media::Imaging;
    using namespace Microsoft::UI::Xaml::Input;
    using namespace Microsoft::UI::Xaml::Automation;
}

namespace
{
    namespace bd = ::Eikon::Core::Backdrop;
    using ::Eikon::Core::JsonValue;
    using ::Eikon::Core::Settings;

    constexpr int kCustomImagesLimit = 6;
    // The aspect pull-down's items; their titles are what parses.
    constexpr std::wstring_view kAspectItems[] = { L"Reset", L"1:1", L"4:3", L"3:2", L"16:9", L"Twitter", L"Facebook", L"YouTube", L"LinkedIn", L"Pinterest" };

    winrt::Windows::UI::Color ToColor(bd::Rgb c, std::uint8_t alpha = 255)
    {
        auto const byte = [](double v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0), 0L, 255L)); };
        return { alpha, byte(c.r), byte(c.g), byte(c.b) };
    }

    std::wstring HexOf(winrt::Windows::UI::Color c)
    {
        wchar_t text[8];
        swprintf_s(text, L"#%02X%02X%02X", c.R, c.G, c.B);
        return text;
    }

    winrt::Brush SolidBrush(std::wstring const& colors)
    {
        auto const parsed = bd::ParseColors(colors);
        return winrt::SolidColorBrush(ToColor(parsed.empty() ? bd::Rgb{} : parsed.front()));
    }

    // CtrlRadioImage's gradient: start (0.5, 0) → end (0.5, 1) with y up, so the first colour is at the
    // bottom; locations get 0 and 1 added like the backdrop gradient.
    winrt::Brush TemplateGradientBrush(std::wstring const& colors, std::wstring const& stops)
    {
        bd::Config config;
        config.colors = colors;
        config.stops = stops;
        winrt::LinearGradientBrush brush;
        brush.StartPoint({ 0.5f, 1.0f });
        brush.EndPoint({ 0.5f, 0.0f });
        for (auto const& stop : bd::ConfigGradient(config).stops)
        {
            winrt::GradientStop gradientStop;
            gradientStop.Offset(stop.location);
            gradientStop.Color(ToColor(stop.color));
            brush.GradientStops().Append(gradientStop);
        }
        return brush;
    }

    std::wstring StringOf(JsonValue const& dict, std::wstring_view key)
    {
        JsonValue const* value = dict.Find(key);
        return value ? value->AsString() : std::wstring{};
    }

    ::Eikon::Core::Bitmap Scaled(::Eikon::Core::Bitmap const& image, int width, int height, bool aspectFill)
    {
        namespace canvas = winrt::Microsoft::Graphics::Canvas;
        ::Eikon::Core::Bitmap out = ::Eikon::Core::Bitmap::Create(width, height);
        try
        {
            auto const device = canvas::CanvasDevice::GetSharedDevice();
            auto const source = canvas::CanvasBitmap::CreateFromBytes(device, winrt::array_view<std::uint8_t const>(image.pixels), image.width,
                image.height, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 96.0f,
                canvas::CanvasAlphaMode::Premultiplied);
            winrt::Windows::Foundation::Rect sourceRect = source.Bounds();
            if (aspectFill)
            {
                double const target = static_cast<double>(width) / height;
                if (sourceRect.Width / sourceRect.Height > target)
                {
                    float const w = static_cast<float>(sourceRect.Height * target);
                    sourceRect = { (sourceRect.Width - w) / 2, 0, w, sourceRect.Height };
                }
                else
                {
                    float const h = static_cast<float>(sourceRect.Width / target);
                    sourceRect = { 0, (sourceRect.Height - h) / 2, sourceRect.Width, h };
                }
            }
            canvas::CanvasRenderTarget target(device, static_cast<float>(width), static_cast<float>(height), 96.0f);
            {
                auto const session = target.CreateDrawingSession();
                session.Clear(winrt::Windows::UI::Colors::Transparent());
                session.DrawImage(source, winrt::Windows::Foundation::Rect{ 0, 0, static_cast<float>(width), static_cast<float>(height) }, sourceRect,
                                  1.0f, canvas::CanvasImageInterpolation::HighQualityCubic);
                session.Close();
            }
            auto const bytes = target.GetPixelBytes();
            std::memcpy(out.pixels.data(), bytes.data(), std::min<std::size_t>(bytes.size(), out.pixels.size()));
        }
        catch (...)
        {
        }
        return out;
    }

    winrt::ImageSource ToImageSource(::Eikon::Core::Bitmap const& bitmap)
    {
        winrt::WriteableBitmap writeable(bitmap.width, bitmap.height);
        auto buffer = writeable.PixelBuffer();
        std::memcpy(buffer.data(), bitmap.pixels.data(), std::min<std::size_t>(buffer.Length(), bitmap.pixels.size()));
        writeable.Invalidate();
        return writeable;
    }

    std::filesystem::path LibraryFolder()
    {
        return ::Eikon::Core::DataFolder() / L"Backdrops";
    }

    std::filesystem::path WallpaperPath()
    {
        wchar_t path[MAX_PATH + 1]{};
        if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0) && path[0])
        {
            return path;
        }
        return {};
    }

    // Files in the library folder whose names start with `prefix`, newest (largest unix time) first.
    std::vector<std::filesystem::path> LibraryFiles(std::wstring_view prefix)
    {
        std::vector<std::filesystem::path> files;
        std::error_code error;
        for (auto const& entry : std::filesystem::directory_iterator(LibraryFolder(), error))
        {
            std::wstring const name = entry.path().filename().wstring();
            if (entry.is_regular_file(error) && name.starts_with(prefix))
            {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end(), [](auto const& a, auto const& b) { return a.filename().wstring() > b.filename().wstring(); });
        return files;
    }

    // Deletes the older backdrop files.
    void DeleteOlderFiles(std::wstring_view prefix, std::size_t limit)
    {
        auto const files = LibraryFiles(prefix);
        for (std::size_t i = limit; i < files.size(); ++i)
        {
            std::error_code error;
            std::filesystem::remove(files[i], error);
        }
    }

    // Recenter's auto-crop (approximation of the editor's smart fit): the crop shrunk to the pixels that
    // differ from the colour of its top-left corner.
    ::Eikon::Core::Rect TrimUniformBorder(::Eikon::Core::Bitmap const& image, ::Eikon::Core::Rect crop)
    {
        crop = crop.Standardized();
        int const left = std::clamp(static_cast<int>(crop.Left()), 0, image.width);
        int const top = std::clamp(static_cast<int>(crop.Top()), 0, image.height);
        int const right = std::clamp(static_cast<int>(crop.Right()), left, image.width);
        int const bottom = std::clamp(static_cast<int>(crop.Bottom()), top, image.height);
        if (right - left < 2 || bottom - top < 2)
        {
            return crop;
        }
        auto const pixel = [&](int x, int y) {
            std::uint8_t const* p = image.pixels.data() + image.Stride() * y + static_cast<std::size_t>(x) * 4;
            return static_cast<std::uint32_t>(p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24);
        };
        std::uint32_t const background = pixel(left, top);
        int minX = right, minY = bottom, maxX = left - 1, maxY = top - 1;
        for (int y = top; y < bottom; ++y)
        {
            for (int x = left; x < right; ++x)
            {
                if (pixel(x, y) != background)
                {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
        }
        if (maxX < minX || maxY < minY)
        {
            return crop;
        }
        return { static_cast<double>(minX), static_cast<double>(minY), static_cast<double>(maxX - minX + 1), static_cast<double>(maxY - minY + 1) };
    }
}

namespace winrt::Eikon::implementation
{
    void BackdropPanel::InitializeComponent()
    {
        BackdropPanelT<BackdropPanel>::InitializeComponent();
        BuildAspectMenu();
        // Ok's Return is a key equivalent: it wins over the focused control. A focused WinUI
        // button handles Enter itself before the accelerator (the panel opens with focus on "•••", so
        // Enter opened the aspect menu), hence the preview handler. An open combo box keeps Enter.
        PreviewKeyDown([this](winrt::Windows::Foundation::IInspectable const&, winrt::KeyRoutedEventArgs const& args) {
            if (args.Key() != winrt::Windows::System::VirtualKey::Enter)
            {
                return;
            }
            if (auto const focused = winrt::FocusManager::GetFocusedElement(XamlRoot()))
            {
                if (auto const combo = focused.try_as<winrt::ComboBox>(); combo && combo.IsDropDownOpen())
                {
                    return;
                }
                if (focused.try_as<winrt::ComboBoxItem>())
                {
                    return;
                }
            }
            args.Handled(true);
            Ok_Click(nullptr, nullptr);
        });
    }

    void BackdropPanel::Open(std::shared_ptr<::Eikon::Core::Document> document, std::function<void()> close)
    {
        m_document = std::move(document);
        m_close = std::move(close);
        BuildSolids();
        BuildGradients();
        ScanLibrary();
        BuildImages();
        RefreshCustomGradBox();
        SyncControls();
    }

    // ---- Swatches ----

    BackdropPanel::Swatch BackdropPanel::MakeSwatch(double width, double height, Brush const& brush, int tag, std::function<void()> onClick)
    {
        bool const dark = ActualTheme() == ElementTheme::Dark;
        Swatch swatch;
        swatch.tag = tag;
        swatch.root = Grid();
        swatch.root.Width(width);
        swatch.root.Height(height);
        swatch.root.Background(SolidColorBrush(Windows::UI::Colors::Transparent()));
        swatch.fill = Border();
        swatch.fill.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
        swatch.fill.Background(brush);
        swatch.ring = Border();
        swatch.ring.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
        // linner: windowBackgroundColor @0.9, 1 px, radius 4, inset 2; linner2: textColor @0.25, 0.5 px,
        // radius 3, inset 3.
        swatch.inner = Border();
        swatch.inner.Margin(ThicknessHelper::FromUniformLength(2));
        swatch.inner.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
        swatch.inner.BorderThickness(ThicknessHelper::FromUniformLength(1));
        swatch.inner.BorderBrush(SolidColorBrush(dark ? Windows::UI::Color{ 230, 0x20, 0x20, 0x20 } : Windows::UI::Color{ 230, 0xF3, 0xF3, 0xF3 }));
        swatch.inner2 = Border();
        swatch.inner2.Margin(ThicknessHelper::FromUniformLength(3));
        swatch.inner2.CornerRadius(CornerRadiusHelper::FromUniformRadius(3));
        swatch.inner2.BorderThickness(ThicknessHelper::FromUniformLength(0.5));
        swatch.inner2.BorderBrush(SolidColorBrush(dark ? Windows::UI::Color{ 64, 255, 255, 255 } : Windows::UI::Color{ 64, 0, 0, 0 }));
        swatch.pressed = Border();
        swatch.pressed.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
        swatch.pressed.Background(SolidColorBrush(Windows::UI::Color{ 51, 0, 0, 0 }));
        swatch.pressed.Visibility(Visibility::Collapsed);
        for (auto const& child : { swatch.fill, swatch.ring, swatch.inner, swatch.inner2, swatch.pressed })
        {
            swatch.root.Children().Append(child);
        }
        SetSelected(swatch, false);

        Border pressed = swatch.pressed;
        swatch.root.PointerPressed([pressed](auto&&, PointerRoutedEventArgs const&) { pressed.Visibility(Visibility::Visible); });
        auto const release = [pressed](auto&&, PointerRoutedEventArgs const&) { pressed.Visibility(Visibility::Collapsed); };
        swatch.root.PointerReleased(release);
        swatch.root.PointerExited(release);
        swatch.root.PointerCaptureLost(release);
        swatch.root.Tapped([onClick](auto&&, TappedRoutedEventArgs const&) {
            try
            {
                onClick();
            }
            catch (...)
            {
                OutputDebugStringW(L"Eikon: backdrop swatch click failed\n");
            }
        });
        return swatch;
    }

    void BackdropPanel::SetSelected(Swatch& swatch, bool selected)
    {
        auto const resources = Application::Current().Resources();
        Windows::UI::Color accent{ 255, 0, 120, 212 };
        if (auto const value = resources.TryLookup(box_value(L"SystemAccentColor")))
        {
            accent = unbox_value<Windows::UI::Color>(value);
        }
        bool const dark = swatch.root.ActualTheme() == ElementTheme::Dark;
        swatch.ring.BorderThickness(ThicknessHelper::FromUniformLength(selected ? 2 : 1));
        swatch.ring.BorderBrush(SolidColorBrush(selected ? accent : (dark ? Windows::UI::Color{ 26, 255, 255, 255 } : Windows::UI::Color{ 26, 0, 0, 0 })));
        swatch.inner.Visibility(selected ? Visibility::Visible : Visibility::Collapsed);
        swatch.inner2.Visibility(selected ? Visibility::Visible : Visibility::Collapsed);
    }

    // CtrlWrappedStack: left-aligned rows, top-down; per row floor(W / (w + g)) + 1, one less if that
    // doesn't fit.
    void BackdropPanel::Wrap(StackPanel const& host, std::vector<Swatch> const& items, double itemWidth, double gap)
    {
        host.Children().Clear();
        double const width = 258;
        int perRow = static_cast<int>(std::floor(width / (itemWidth + gap))) + 1;
        if (perRow * itemWidth + (perRow - 1) * gap > width)
        {
            --perRow;
        }
        perRow = std::max(perRow, 1);
        StackPanel row{ nullptr };
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (i % perRow == 0)
            {
                row = StackPanel();
                row.Orientation(Orientation::Horizontal);
                row.Spacing(gap);
                host.Children().Append(row);
            }
            row.Children().Append(items[i].root);
        }
    }

    void BackdropPanel::Select(std::vector<Swatch>& group, int tag)
    {
        for (auto& swatch : group)
        {
            SetSelected(swatch, swatch.tag == tag);
        }
    }

    void BackdropPanel::BuildAspectMenu()
    {
        AspectMenu().Items().Clear();
        for (auto const title : kAspectItems)
        {
            MenuFlyoutItem item;
            item.Text(hstring{ title });
            std::wstring const value{ title };
            item.Click([this, value](auto&&, auto&&) {
                if (!m_document)
                {
                    return;
                }
                // pickAspect →.
                auto& config = m_document->backdrop.config;
                auto const aspect = bd::AspectForItem(value);
                config.ratio = aspect.ratio;
                config.minWidth = aspect.minWidth;
                AspectButton().Content(box_value(hstring{ bd::AspectButtonTitle(value) }));
                Render();
                UpdateInnerEnabled();
            });
            AspectMenu().Items().Append(item);
        }
    }

    void BackdropPanel::BuildSolids()
    {
        m_solids.clear();
        auto const& templates = bd::SolidTemplates();
        for (std::size_t i = 0; i < templates.size(); ++i)
        {
            int const tag = bd::TemplateTag(templates[i], static_cast<int>(i));
            std::wstring colors = StringOf(templates[i], L"colors");
            if (tag == bd::kCustomTag)
            {
                colors = Settings::Current().CustomBackdropColor();
            }
            auto swatch = MakeSwatch(30, 30, SolidBrush(colors), tag, [this, tag] { PickSolid(tag); });
            Automation::AutomationProperties::SetName(swatch.root, hstring{ bd::TemplateName(templates[i]) });
            m_solids.push_back(swatch);
        }
        Wrap(SolidSwatches(), m_solids, 30, 8);
    }

    void BackdropPanel::BuildGradients()
    {
        m_gradients.clear();
        auto const& templates = bd::GradientTemplates();
        for (std::size_t i = 0; i < templates.size(); ++i)
        {
            int const tag = bd::TemplateTag(templates[i], static_cast<int>(i));
            std::wstring colors = StringOf(templates[i], L"colors");
            if (tag == bd::kCustomTag)
            {
                colors = Settings::Current().CustomGradFrom() + L"," + Settings::Current().CustomGradTo();
            }
            auto swatch = MakeSwatch(37, 36, TemplateGradientBrush(colors, StringOf(templates[i], L"stops")), tag, [this, tag] { PickGradient(tag); });
            Automation::AutomationProperties::SetName(swatch.root, hstring{ bd::TemplateName(templates[i]) });
            m_gradients.push_back(swatch);
        }
        Wrap(GradientSwatches(), m_gradients, 37, 7);
    }

    void BackdropPanel::ScanLibrary()
    {
        // scanCustomImages: the wallpaper, then the newest six thumbnails whose
        // full-size file exists.
        m_library.clear();
        m_library.push_back({ L"wallpaper", WallpaperPath() });
        for (auto const& thumb : LibraryFiles(L"backdrop-th-"))
        {
            if (m_library.size() > kCustomImagesLimit)
            {
                break;
            }
            std::wstring name = thumb.filename().wstring();
            name.replace(0, std::wstring_view{ L"backdrop-th-" }.size(), L"backdrop-fs-");
            std::filesystem::path const full = thumb.parent_path() / name;
            std::error_code error;
            if (std::filesystem::exists(full, error))
            {
                m_library.push_back({ full.wstring(), thumb });
            }
        }
    }

    void BackdropPanel::BuildImages()
    {
        m_images.clear();
        for (std::size_t i = 0; i < m_library.size(); ++i)
        {
            auto const& entry = m_library[i];
            Brush brush{ nullptr };
            ::Eikon::Core::Bitmap const picture = entry.thumb.empty() ? ::Eikon::Core::Bitmap{} : ::Eikon::Exporter::LoadImageFile(entry.thumb);
            if (!picture.IsEmpty())
            {
                ImageBrush image;
                image.Stretch(Stretch::UniformToFill);
                image.ImageSource(ToImageSource(i == 0 ? Scaled(picture, 114, std::max(1, static_cast<int>(114.0 * picture.height / picture.width)), false)
                                                       : picture));
                brush = image;
            }
            else
            {
                brush = SolidColorBrush(Windows::UI::Color{ 255, 0xCC, 0xCC, 0xCC });
            }
            int const tag = static_cast<int>(i);
            auto swatch = MakeSwatch(57, 38, brush, tag, [this, tag] { PickImage(tag); });
            if (entry.path != L"wallpaper")
            {
                MenuFlyout menu;
                MenuFlyoutItem forget;
                forget.Text(hstring{ ::Eikon::Strings::Get(L"BackdropPanel_Forget") });
                forget.Click([this, tag](auto&&, auto&&) { ForgetImage(tag); });
                menu.Items().Append(forget);
                swatch.root.ContextFlyout(menu);
            }
            m_images.push_back(swatch);
        }
        // The add button (tag 888).
        Grid add;
        add.Width(57);
        add.Height(38);
        FontIcon plus;
        plus.Glyph(L"\uE710");
        plus.FontSize(16);
        add.Children().Append(plus);
        auto swatch = MakeSwatch(57, 38, SolidColorBrush(Windows::UI::Colors::Transparent()), bd::kCustomTag, [this] { AddImage(); });
        swatch.root.Children().Append(add);
        Automation::AutomationProperties::SetName(swatch.root, hstring{ ::Eikon::Strings::Get(L"BackdropPanel_AddImage") });
        m_images.push_back(swatch);
        Wrap(ImageSwatches(), m_images, 57, 10);
    }

    void BackdropPanel::RefreshCustomGradBox()
    {
        auto& settings = Settings::Current();
        for (auto* target : { &m_gradFrom, &m_gradTo })
        {
            bool const from = target == &m_gradFrom;
            Grid host = from ? GradFromSwatch() : GradToSwatch();
            host.Children().Clear();
            std::wstring const color = from ? settings.CustomGradFrom() : settings.CustomGradTo();
            *target = MakeSwatch(44, 24, SolidBrush(color), from ? 1 : 2, [this, from] {
                auto& s = Settings::Current();
                // clickGradFrom/To →: the colour picker bound to customGradFrom/To,
                // live-updating the custom gradient.
                PickColor(from ? GradFromSwatch() : GradToSwatch(), from ? s.CustomGradFrom() : s.CustomGradTo(), [this, from](std::wstring const& hex) {
                    auto& st = Settings::Current();
                    if (from)
                    {
                        st.CustomGradFrom(hex);
                    }
                    else
                    {
                        st.CustomGradTo(hex);
                    }
                    if (m_document && m_document->backdrop.config.name == L"custom-saved-gradient")
                    {
                        m_document->backdrop.config.colors = st.CustomGradFrom() + L"," + st.CustomGradTo();
                        Render();
                    }
                    (from ? m_gradFrom : m_gradTo).fill.Background(SolidBrush(hex));
                    for (auto& swatch : m_gradients)
                    {
                        if (swatch.tag == bd::kCustomTag)
                        {
                            swatch.fill.Background(TemplateGradientBrush(st.CustomGradFrom() + L"," + st.CustomGradTo(), L"0.00,1.00"));
                        }
                    }
                });
            });
            host.Children().Append(target->root);
        }
    }

    // ---- State → controls (viewWillAppear) ----

    void BackdropPanel::SyncControls()
    {
        if (!m_document)
        {
            return;
        }
        m_syncing = true;
        auto const& config = m_document->backdrop.config;
        PaddingSlider().Value(bd::SliderFromPadding(config.padding));
        ShadowSlider().Value(bd::SliderFromShadow(config.shadowIntensity));
        InsetSlider().Value(bd::SliderFromInset(config.inset));
        OuterRadiusSlider().Value(bd::SliderFromRadius(config.outerRadius));
        InnerRadiusSlider().Value(bd::SliderFromRadius(config.innerRadius));
        BlurSlider().Value(bd::SliderFromBlur(config.blur));
        if (config.ratio == 0)
        {
            AspectButton().Content(box_value(L"•••"));
        }
        else
        {
            // The panel is new each time, so the pull-down's title comes back from the preset with this ratio and minimum width.
            for (auto const title : kAspectItems)
            {
                auto const aspect = bd::AspectForItem(title);
                if (aspect.ratio == config.ratio && aspect.minWidth == config.minWidth)
                {
                    AspectButton().Content(box_value(hstring{ bd::AspectButtonTitle(title) }));
                }
            }
        }
        GradientTypeBox().SelectedIndex(static_cast<int>(bd::SelectedGradientType(config)));
        Group group = Group::Solid;
        if (config.mode == L"mesh" || config.mode == L"gradient")
        {
            group = Group::Colormix;
        }
        else if (config.mode == L"image" || config.mode == L"wallpaper")
        {
            group = Group::Image;
        }
        ShowGroup(group);
        auto const selectByName = [&](std::vector<Swatch>& swatches, std::vector<JsonValue> const& templates) {
            int tag = -1;
            for (std::size_t i = 0; i < templates.size(); ++i)
            {
                if (bd::TemplateName(templates[i]) == config.name)
                {
                    tag = bd::TemplateTag(templates[i], static_cast<int>(i));
                }
            }
            Select(swatches, tag);
        };
        selectByName(m_solids, bd::SolidTemplates());
        selectByName(m_gradients, bd::GradientTemplates());
        int imageTag = -1;
        for (std::size_t i = 0; i < m_library.size(); ++i)
        {
            if (m_library[i].path == config.filepath)
            {
                imageTag = static_cast<int>(i);
            }
        }
        Select(m_images, imageTag);
        UpdateInnerEnabled();
        UpdateRandomizeEnabled();
        m_syncing = false;
    }

    void BackdropPanel::ShowGroup(Group group)
    {
        SolidGroup().Visibility(group == Group::Solid ? Visibility::Visible : Visibility::Collapsed);
        ColormixGroup().Visibility(group == Group::Colormix ? Visibility::Visible : Visibility::Collapsed);
        ImageGroup().Visibility(group == Group::Image ? Visibility::Visible : Visibility::Collapsed);
        SolidTab().IsChecked(group == Group::Solid);
        ColormixTab().IsChecked(group == Group::Colormix);
        ImageTab().IsChecked(group == Group::Image);
    }

    void BackdropPanel::UpdateInnerEnabled()
    {
        if (m_document)
        {
            InnerRadiusSlider().IsEnabled(bd::InnerRadiusEnabled(m_document->backdrop.config));
        }
    }

    void BackdropPanel::UpdateRandomizeEnabled()
    {
        // Enabled only while the popup says "Mesh gradient".
        RandomizeButton().IsEnabled(GradientTypeBox().SelectedIndex() == 0);
    }

    // ---- Actions ----

    void BackdropPanel::Render()
    {
        if (m_document)
        {
            ::Eikon::Editor::RenderBackdrop(*m_document);
        }
    }

    void BackdropPanel::Reseed()
    {
        if (m_document && m_document->backdrop.config.isOn == 1)
        {
            m_document->backdrop.config.seed = bd::RandomSeed();
            Render();
        }
    }

    void BackdropPanel::ApplyTemplate(JsonValue const& dict)
    {
        bd::Apply(m_document->backdrop.config, dict);
    }

    void BackdropPanel::PickGradient(int tag)
    {
        if (!m_document)
        {
            return;
        }
        auto const& templates = bd::GradientTemplates();
        if (tag == bd::kCustomTag)
        {
            // Tag 888: the first click applies custom-saved-gradient, the next ones toggle the box.
            if (m_document->backdrop.config.name == L"custom-saved-gradient")
            {
                CustomGradBox().Visibility(CustomGradBox().Visibility() == Visibility::Visible ? Visibility::Collapsed : Visibility::Visible);
                return;
            }
            JsonValue dict = templates.back();
            dict.Set(L"colors", JsonValue::String(Settings::Current().CustomGradFrom() + L"," + Settings::Current().CustomGradTo()));
            ApplyTemplate(dict);
        }
        else if (tag >= 0 && tag < static_cast<int>(templates.size()))
        {
            ApplyTemplate(templates[static_cast<std::size_t>(tag)]);
        }
        Select(m_gradients, tag);
        Reseed();
    }

    void BackdropPanel::PickSolid(int tag)
    {
        if (!m_document)
        {
            return;
        }
        auto const& templates = bd::SolidTemplates();
        if (tag == bd::kCustomTag)
        {
            if (m_document->backdrop.config.name == L"custom-saved-color")
            {
                // The custom colour: a colour picker bound to customBackdropColor.
                Swatch* custom = nullptr;
                for (auto& swatch : m_solids)
                {
                    if (swatch.tag == bd::kCustomTag)
                    {
                        custom = &swatch;
                    }
                }
                if (custom)
                {
                    PickColor(custom->root, Settings::Current().CustomBackdropColor(), [this](std::wstring const& hex) {
                        Settings::Current().CustomBackdropColor(hex);
                        for (auto& swatch : m_solids)
                        {
                            if (swatch.tag == bd::kCustomTag)
                            {
                                swatch.fill.Background(SolidBrush(hex));
                            }
                        }
                        if (m_document && m_document->backdrop.config.name == L"custom-saved-color")
                        {
                            m_document->backdrop.config.colors = hex;
                            Render();
                        }
                    });
                }
                return;
            }
            JsonValue dict = templates.back();
            dict.Set(L"colors", JsonValue::String(Settings::Current().CustomBackdropColor()));
            ApplyTemplate(dict);
        }
        else if (tag >= 0 && tag < static_cast<int>(templates.size()))
        {
            ApplyTemplate(templates[static_cast<std::size_t>(tag)]);
        }
        Select(m_solids, tag);
        Reseed();
    }

    void BackdropPanel::PickImage(int tag)
    {
        if (!m_document || tag < 0 || tag >= static_cast<int>(m_library.size()))
        {
            return;
        }
        // onImageClick: filepath = the entry, name custom-saved-image, load.
        auto& config = m_document->backdrop.config;
        config.filepath = m_library[static_cast<std::size_t>(tag)].path;
        config.name = L"custom-saved-image";
        Select(m_images, tag);
        LoadImage(config.filepath);
    }

    void BackdropPanel::LoadImage(std::wstring const& path)
    {
        if (m_document)
        {
            m_document->backdrop.config.filepath = path;
            ::Eikon::Editor::LoadBackdropImage(m_document);
        }
    }

    void BackdropPanel::PickColor(FrameworkElement const& anchor, std::wstring const& current, std::function<void(std::wstring const&)> changed)
    {
        ColorPicker picker;
        picker.IsAlphaEnabled(false);
        picker.IsMoreButtonVisible(false);
        picker.IsColorSliderVisible(true);
        picker.IsColorChannelTextInputVisible(false);
        picker.IsHexInputVisible(true);
        if (auto const color = bd::ParseHex(current))
        {
            picker.Color(ToColor(*color));
        }
        picker.ColorChanged([changed](ColorPicker const&, ColorChangedEventArgs const& args) {
            try
            {
                changed(HexOf(args.NewColor()));
            }
            catch (...)
            {
            }
        });
        Flyout flyout;
        flyout.Content(picker);
        flyout.ShowAt(anchor);
    }

    fire_and_forget BackdropPanel::AddImage()
    {
        auto strong = get_strong();
        // Load Custom File.
        Windows::Storage::Pickers::FileOpenPicker picker;
        HWND owner = ::Eikon::AppController::Current().EditorHandle();
        if (auto init = picker.try_as<::IInitializeWithWindow>(); init && owner)
        {
            init->Initialize(owner);
        }
        for (auto const extension : { L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".tif", L".tiff", L".heic", L".webp" })
        {
            picker.FileTypeFilter().Append(extension);
        }
        picker.ViewMode(Windows::Storage::Pickers::PickerViewMode::Thumbnail);
        Windows::Storage::StorageFile file{ nullptr };
        try
        {
            file = co_await picker.PickSingleFileAsync();
        }
        catch (...)
        {
        }
        if (!file || !m_document)
        {
            co_return;
        }
        ::Eikon::Core::Bitmap const image = ::Eikon::Exporter::LoadImageFile(std::wstring{ file.Path() });
        if (image.IsEmpty())
        {
            OutputDebugStringW(L"Eikon: Load Custom File: Failed to open image file\n");
            co_return;
        }
        std::error_code error;
        std::filesystem::create_directories(LibraryFolder(), error);
        if (!std::filesystem::is_directory(LibraryFolder(), error))
        {
            OutputDebugStringW(L"Eikon: Failed to get app support folder, huh\n");
            co_return;
        }
        auto const stamp = std::to_wstring(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        std::filesystem::path const full = LibraryFolder() / (L"backdrop-fs-" + stamp + L".jpeg");
        std::filesystem::path const thumb = LibraryFolder() / (L"backdrop-th-" + stamp + L".jpeg");
        if (!::Eikon::Exporter::WriteFileAtomic(full, ::Eikon::Exporter::EncodeJpeg(image, 0.99, false)))
        {
            OutputDebugStringW(L"Eikon: Failed to save backdrop\n");
            co_return;
        }
        if (!::Eikon::Exporter::WriteFileAtomic(thumb, ::Eikon::Exporter::EncodeJpeg(Scaled(image, 114, 76, true), 0.9, false)))
        {
            OutputDebugStringW(L"Eikon: Failed to create thumbnail\n");
        }
        DeleteOlderFiles(L"backdrop-fs", kCustomImagesLimit);
        DeleteOlderFiles(L"backdrop-th", kCustomImagesLimit);
        auto& config = m_document->backdrop.config;
        config.name = L"custom-image";
        config.mode = L"image";
        config.filepath = full.wstring();
        ScanLibrary();
        BuildImages();
        SyncControls();
        LoadImage(config.filepath);
    }

    void BackdropPanel::ForgetImage(int index)
    {
        if (index <= 0 || index >= static_cast<int>(m_library.size()))
        {
            return;
        }
        // deleteCustomImage.
        auto const& entry = m_library[static_cast<std::size_t>(index)];
        std::error_code error;
        bool ok = std::filesystem::remove(entry.path, error);
        ok = std::filesystem::remove(entry.thumb, error) && ok;
        if (!ok)
        {
            OutputDebugStringW(L"Eikon: backdrop: can't delete the custom image\n");
        }
        ScanLibrary();
        BuildImages();
        SyncControls();
    }

    // ---- XAML handlers ----

    void BackdropPanel::PaddingSlider_ValueChanged(IInspectable const&, RangeBaseValueChangedEventArgs const& args)
    {
        if (m_syncing || !m_document)
        {
            return;
        }
        m_document->backdrop.config.padding = bd::PaddingFromSlider(args.NewValue());
        Render();
        UpdateInnerEnabled();
    }

    void BackdropPanel::ShadowSlider_ValueChanged(IInspectable const&, RangeBaseValueChangedEventArgs const& args)
    {
        if (m_syncing || !m_document)
        {
            return;
        }
        m_document->backdrop.config.shadowIntensity = bd::ShadowFromSlider(args.NewValue());
        Render();
    }

    void BackdropPanel::InsetSlider_ValueChanged(IInspectable const&, RangeBaseValueChangedEventArgs const& args)
    {
        if (m_syncing || !m_document)
        {
            return;
        }
        auto& config = m_document->backdrop.config;
        if (bd::InsetSliderApplies(config.inset, args.NewValue()))
        {
            config.inset = bd::InsetFromSlider(args.NewValue());
            Render();
        }
    }

    void BackdropPanel::OuterRadiusSlider_ValueChanged(IInspectable const&, RangeBaseValueChangedEventArgs const& args)
    {
        if (m_syncing || !m_document)
        {
            return;
        }
        m_document->backdrop.config.outerRadius = bd::RadiusFromSlider(args.NewValue());
        Render();
    }

    void BackdropPanel::InnerRadiusSlider_ValueChanged(IInspectable const&, RangeBaseValueChangedEventArgs const& args)
    {
        if (m_syncing || !m_document)
        {
            return;
        }
        m_document->backdrop.config.innerRadius = bd::RadiusFromSlider(args.NewValue());
        Render();
    }

    void BackdropPanel::BlurSlider_ValueChanged(IInspectable const&, RangeBaseValueChangedEventArgs const& args)
    {
        if (m_syncing || !m_document)
        {
            return;
        }
        m_document->backdrop.config.blur = bd::BlurFromSlider(std::round(args.NewValue()));
        Render();
    }

    void BackdropPanel::Recenter_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_document || m_document->Image().IsEmpty())
        {
            return;
        }
        // "Smart Adjust": auto-crop, inset 32, recompute.
        m_document->SetCrop(TrimUniformBorder(m_document->Image(), m_document->Crop()));
        m_document->backdrop.config.inset = 32;
        m_syncing = true;
        InsetSlider().Value(bd::SliderFromInset(32));
        m_syncing = false;
        Render();
    }

    void BackdropPanel::ModeTab_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (!m_document)
        {
            return;
        }
        int const tab = std::stoi(std::wstring{ unbox_value<hstring>(sender.as<FrameworkElement>().Tag()) });
        // tabPickMode.
        if (tab == 0)
        {
            ShowGroup(Group::Solid);
            PickSolid(m_solids.empty() ? 0 : m_solids.front().tag);
        }
        else if (tab == 2)
        {
            ShowGroup(Group::Image);
            m_document->backdrop.config.mode = L"image";
            LoadImage(m_document->backdrop.config.filepath);
        }
        else
        {
            ShowGroup(Group::Colormix);
            bd::ApplyGradientType(m_document->backdrop.config, static_cast<bd::GradientType>(std::max(0, GradientTypeBox().SelectedIndex())));
            PickGradient(m_gradients.empty() ? 0 : m_gradients.front().tag);
        }
    }

    void BackdropPanel::CustomGradOk_Click(IInspectable const&, RoutedEventArgs const&)
    {
        CustomGradBox().Visibility(Visibility::Collapsed);
    }

    void BackdropPanel::GradientType_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        UpdateRandomizeEnabled();
        if (m_syncing || !m_document)
        {
            return;
        }
        int const index = GradientTypeBox().SelectedIndex();
        if (index < 0)
        {
            return;
        }
        bd::ApplyGradientType(m_document->backdrop.config, static_cast<bd::GradientType>(index));
        Render();
    }

    void BackdropPanel::Reverse_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_document)
        {
            bd::Reverse(m_document->backdrop.config);
            Render();
        }
    }

    void BackdropPanel::Randomize_Click(IInspectable const&, RoutedEventArgs const&)
    {
        Reseed();
    }

    void BackdropPanel::Remove_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_document)
        {
            ::Eikon::Editor::TurnOffBackdrop(*m_document);
        }
        if (m_close)
        {
            m_close();
        }
    }

    void BackdropPanel::Ok_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_close)
        {
            m_close();
        }
    }
}
