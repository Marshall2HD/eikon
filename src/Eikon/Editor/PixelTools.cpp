#include "pch.h"
#include "Editor/PixelTools.h"

#include "AppController.h"
#include "Core/Bitmap.h"
#include "Core/Objects/Factory.h"
#include "Core/Ruler.h"
#include "Core/Settings.h"
#include "Strings.h"

#include <winrt/Microsoft.Graphics.Canvas.Text.h>

#include <algorithm>
#include <cmath>

// The pixel tools on the editor canvas. Their state lives in one State per editor.
namespace Eikon::Editor
{
    namespace canvas = winrt::Microsoft::Graphics::Canvas;
    using winrt::Microsoft::UI::Dispatching::DispatcherQueue;
    using winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer;

    namespace
    {
        // Ruler keys, as virtual keys: arrows and the top-row digits 1 and 2.
        constexpr std::uint32_t kKeyLeft = 0x25;
        constexpr std::uint32_t kKeyUp = 0x26;
        constexpr std::uint32_t kKeyRight = 0x27;
        constexpr std::uint32_t kKeyDown = 0x28;
        constexpr std::uint32_t kKey1 = 0x31;
        constexpr std::uint32_t kKey2 = 0x32;
        constexpr std::uint32_t kKeyTab = 0x09;
        constexpr std::uint32_t kKeyC = 0x43;
        constexpr std::uint32_t kKeyI = 0x49;
        constexpr std::array<std::uint32_t, 6> kRulerKeys{ kKeyLeft, kKeyRight, kKeyDown, kKeyUp, kKey1, kKey2 };
        // 1, ↓, ↑ measure vertically (..6a0); 2, ←, → horizontally.
        constexpr std::array<std::uint32_t, 3> kVerticalKeys{ kKey1, kKeyDown, kKeyUp };
        constexpr std::array<std::uint32_t, 3> kHorizontalKeys{ kKeyLeft, kKeyRight, kKey2 };

        // clrMain, sRGB (1, 0.0471, 0.0039).
        constexpr winrt::Windows::UI::Color kMain{ 255, 0xFF, 0x0C, 0x01 };
        constexpr winrt::Windows::UI::Color kWhite{ 255, 255, 255, 255 };
        // Badge: text generic grey 0.95, background rgb(0.04, 0.07, 0.08) at 0.5 (0.9 hovered).
        constexpr winrt::Windows::UI::Color kBadgeText{ 255, 242, 242, 242 };
        constexpr winrt::Windows::UI::Color kBadgeBackground{ 128, 10, 18, 20 };
        constexpr winrt::Windows::UI::Color kBadgeBackgroundHover{ 230, 10, 18, 20 };

        template <std::size_t N>
        bool Contains(std::array<std::uint32_t, N> const& keys, std::uint32_t key)
        {
            return std::find(keys.begin(), keys.end(), key) != keys.end();
        }

        // Text as CF_UNICODETEXT.
        void SetClipboardText(std::wstring const& text)
        {
            HWND const owner = AppController::Current().MessageWindowHandle();
            bool opened = false;
            for (int attempt = 0; attempt < 10 && !opened; ++attempt)
            {
                opened = OpenClipboard(owner) != FALSE;
                if (!opened)
                {
                    Sleep(10);
                }
            }
            if (!opened)
            {
                return;
            }
            EmptyClipboard();
            std::size_t const bytes = (text.size() + 1) * sizeof(wchar_t);
            if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes))
            {
                if (void* data = GlobalLock(memory))
                {
                    memcpy(data, text.c_str(), bytes);
                    GlobalUnlock(memory);
                    if (!SetClipboardData(CF_UNICODETEXT, memory))
                    {
                        GlobalFree(memory);
                    }
                }
                else
                {
                    GlobalFree(memory);
                }
            }
            CloseClipboard();
        }

        // Wide enough that NoWrap labels never touch the box; LayoutBounds is then the text's
        // natural size.
        constexpr float kLayoutWidth = 10000.0f;

        canvas::Text::CanvasTextFormat LabelFormat()
        {
            // Segoe UI Variable, weight 500, 11 DIP.
            canvas::Text::CanvasTextFormat format;
            format.FontFamily(L"Segoe UI Variable Text");
            format.FontSize(11.0f);
            format.FontWeight(winrt::Windows::UI::Text::FontWeight{ 500 });
            format.WordWrapping(canvas::Text::CanvasWordWrapping::NoWrap);
            return format;
        }

        winrt::Windows::Foundation::Rect ToRect(Core::Rect const& r)
        {
            return { static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.width), static_cast<float>(r.height) };
        }

        // The result of the contrast job, shared with the background task.
        struct ContrastSlot
        {
            std::uint64_t generation = 0;
            std::optional<Core::Contrast::Result> value;
            std::function<void()> invalidate;
        };

        winrt::fire_and_forget ComputeContrast(std::weak_ptr<ContrastSlot> weak, std::shared_ptr<Core::Bitmap const> image,
                                               Core::Rect crop, Core::Rect selection, std::uint64_t generation)
        {
            auto const queue = DispatcherQueue::GetForCurrentThread();
            co_await winrt::resume_background();
            auto const result = Core::Contrast::Measure(*image, crop, selection);
            co_await wil::resume_foreground(queue);
            if (auto slot = weak.lock(); slot && slot->generation == generation)
            {
                slot->value = result;
                if (slot->invalidate)
                {
                    slot->invalidate();
                }
            }
        }
    }

    struct PixelTools::State
    {
        // ---- Ruler ----
        bool on = false;
        bool vertical = true;
        bool guide = false;
        std::int64_t persistence = 0;
        double sensitivity = Core::Ruler::kDefaultSensitivity;
        double validZoom = 0;
        double value = 0;
        std::wstring text;
        Core::Rect rect = Core::Ruler::kInitialRect;   // crop-relative, y up
        std::vector<std::uint32_t> pressed;            // PressedKeys, ruler keys only
        DispatcherQueueTimer hideTimer{ nullptr };

        // ---- Pointer ----
        std::optional<Core::Point> cursor;   // last pointer, source image px, y down

        // ---- Colour readout (value, display string, copy string) ----
        Core::Color colorValue{ 0, 0, 0, 255 };
        std::wstring display;
        std::wstring copy;

        // ---- Contrast badge (CropTool.contrast) ----
        std::shared_ptr<ContrastSlot> contrast = std::make_shared<ContrastSlot>();
        std::optional<Core::Rect> badgeFrame;   // canvas DIPs, the background
        bool badgeHover = false;
        // The selection the contrast was last computed for (image px); unset after a press/reset so
        // the next settled selection recomputes even when it is unchanged.
        std::optional<std::optional<Core::Rect>> contrastFor;

        // ---- Drawing ----
        Render::Context context;
        bool hasContext = false;
        canvas::Text::CanvasTextFormat format{ nullptr };

        std::uint64_t settingsToken = 0;

        bool AnyRulerKeyHeld() const
        {
            return std::any_of(pressed.begin(), pressed.end(), [](std::uint32_t k) { return Contains(kRulerKeys, k); });
        }
        bool Held(std::uint32_t key) const { return std::find(pressed.begin(), pressed.end(), key) != pressed.end(); }
        void Release(std::uint32_t key) { std::erase(pressed, key); }
        void TurnOff()
        {
            on = false;
            sensitivity = Core::Ruler::kDefaultSensitivity;
        }
    };

    PixelTools::PixelTools(Host host) : m_host(std::move(host)), m_state(std::make_unique<State>())
    {
        m_state->contrast->invalidate = m_host.invalidate;
        m_state->settingsToken = Core::Settings::Current().Changed.Add([this](std::wstring_view key) {
            if (key == Core::Settings::kColorFormat)
            {
                // The strings are rebuilt from colorValue in the new format.
                auto formatted = Core::FormatColor(m_state->colorValue, Core::Settings::Current().ColorFormat());
                m_state->display = std::move(formatted.display);
                m_state->copy = std::move(formatted.copy);
                if (m_host.readoutChanged)
                {
                    m_host.readoutChanged();
                }
            }
            else if (key == Core::Settings::kContrastType || key == Core::Settings::kRealPixels)
            {
                if (m_host.invalidate)
                {
                    m_host.invalidate();
                }
            }
        });
    }

    PixelTools::~PixelTools()
    {
        Core::Settings::Current().Changed.Remove(m_state->settingsToken);
        if (m_state->hideTimer)
        {
            m_state->hideTimer.Stop();
        }
    }

    void PixelTools::SetDocument(std::shared_ptr<Core::Document> document)
    {
        m_document = std::move(document);
        m_state->TurnOff();
        ++m_state->contrast->generation;
        m_state->contrast->value.reset();
        m_state->contrastFor.reset();
        m_state->badgeFrame.reset();
        m_state->badgeHover = false;
    }

    namespace
    {
        bool HasImage(std::shared_ptr<Core::Document> const& document)
        {
            return document && !document->Image().IsEmpty();
        }

        // Source image px (y down) -> crop-relative y-up space.
        Core::Point ToOriginalSpace(Core::Rect const& crop, Core::Point p)
        {
            return { p.x - crop.x, crop.y + crop.height - p.y };
        }

        // A crop-relative y-up rect -> source image px, y down, positive size.
        Core::Rect FromOriginalSpace(Core::Rect const& crop, Core::Rect const& r)
        {
            Core::Rect const s = r.Standardized();
            return { crop.x + s.x, crop.y + crop.height - (s.y + s.height), s.width, s.height };
        }
    }

    // ---- Ruler ----

    namespace
    {
        void Invalidate(PixelTools::Host const& host)
        {
            if (host.invalidate)
            {
                host.invalidate();
            }
        }
    }

    bool PixelTools::KeyDown(std::uint32_t key, Modifiers modifiers)
    {
        State& s = *m_state;
        if (Contains(kRulerKeys, key) && !s.Held(key))
        {
            s.pressed.push_back(key);
        }

        bool const image = HasImage(m_document);
        bool const shift = HasFlag(modifiers, Modifiers::Shift);

        // Tab / I: copy the hovered colour (Shift: the darkest around the cursor).
        bool tab = false;
        if (key == kKeyTab)
        {
            if (m_host.helperOpen && m_host.helperOpen())
            {
                return true;   // keyDown returns early while a helper is open
            }
            tab = true;
        }
        bool const objectActive = m_host.objectActive && m_host.objectActive();
        if (image && (key == kKeyI || (tab && !objectActive)))
        {
            if (!shift)
            {
                CopyColor(s.copy, s.copy, s.colorValue);
            }
            else
            {
                Core::Rect const crop = m_document->Crop();
                Core::Point const at = ToOriginalSpace(crop, s.cursor.value_or(Core::Point{}));
                Core::Color const darkest = s.cursor ? Core::DarkestAround(m_document->Image(), crop, at) : Core::Color{ 255, 255, 255, 255 };
                auto const formatted = Core::FormatColor(darkest, Core::Settings::Current().ColorFormat());
                CopyColor(formatted.copy, Strings::Format(L"PixelTools_DarkestColor", formatted.copy), darkest);
            }
            return true;
        }

        // C with a selection: the average colour of the selection (no Ctrl).
        if (key == kKeyC && !HasFlag(modifiers, Modifiers::Ctrl) && image && m_host.selection)
        {
            if (auto const selection = m_host.selection())
            {
                Core::Rect const crop = m_document->Crop();
                Core::Rect const s0 = selection->Standardized();
                Core::Rect const up{ s0.x - crop.x, crop.y + crop.height - (s0.y + s0.height), s0.width, s0.height };
                Core::Color const average = Core::SampleArea(m_document->Image(), crop, up, Core::SampleMode::Average);
                auto const formatted = Core::FormatColor(average, Core::Settings::Current().ColorFormat());
                CopyColor(formatted.copy, Strings::Format(L"PixelTools_AverageColor", formatted.copy), average);
                return true;
            }
        }

        // Runs on every keyDown while a ruler key is held.
        if (!s.AnyRulerKeyHeld())
        {
            return false;
        }
        if (Contains(kVerticalKeys, key))
        {
            for (auto k : kHorizontalKeys)
            {
                s.Release(k);
            }
        }
        else if (Contains(kHorizontalKeys, key))
        {
            for (auto k : kVerticalKeys)
            {
                s.Release(k);
            }
        }
        bool const cropOn = m_host.selection && m_host.selection().has_value();
        if (cropOn || objectActive)
        {
            s.TurnOff();
            Invalidate(m_host);
            return false;
        }
        bool const vertical = std::any_of(kVerticalKeys.begin(), kVerticalKeys.end(), [&](std::uint32_t k) { return s.Held(k); });
        Measure(vertical, shift ? 1 : 0, HasFlag(modifiers, Modifiers::Alt));
        return false;
    }

    bool PixelTools::KeyUp(std::uint32_t key, Modifiers)
    {
        State& s = *m_state;
        s.Release(key);
        if (!s.AnyRulerKeyHeld())
        {
            bool const wasOn = s.on;
            s.TurnOff();
            if (wasOn)
            {
                Invalidate(m_host);
            }
        }
        return false;
    }

    void PixelTools::Measure(bool vertical, std::int64_t persistence, bool guide)
    {
        State& s = *m_state;
        if (!HasImage(m_document))
        {
            return;
        }
        s.on = true;
        s.validZoom = s.context.zoom;
        if (!s.hideTimer)
        {
            if (auto queue = DispatcherQueue::GetForCurrentThread())
            {
                s.hideTimer = queue.CreateTimer();
                s.hideTimer.Interval(std::chrono::milliseconds(800));
                s.hideTimer.IsRepeating(false);
                s.hideTimer.Tick([this](DispatcherQueueTimer const& sender, auto&&) {
                    sender.Stop();
                    // Hide; the sensitivity stays.
                    m_state->on = false;
                    Invalidate(m_host);
                });
            }
        }
        if (s.hideTimer)
        {
            s.hideTimer.Stop();
            s.hideTimer.Start();
        }
        s.vertical = vertical;
        s.persistence = persistence;
        s.guide = guide;

        if (s.cursor)
        {
            Core::Rect const crop = m_document->Crop();
            auto const m = Core::Ruler::Measure(m_document->Image(), crop, ToOriginalSpace(crop, *s.cursor), vertical, persistence, guide, s.sensitivity);
            if (m)
            {
                s.rect = m->rect;
                s.value = m->value;
                s.text = Core::Ruler::LabelText(m->value, m_document->Image().dpr, Core::Settings::Current().RealPixels());
            }
        }
        Invalidate(m_host);
    }

    bool PixelTools::Wheel(double deltaY)
    {
        State& s = *m_state;
        if (!s.on)
        {
            return false;
        }
        // Stored only; the next move or key repeat measures with it.
        s.sensitivity = Core::Ruler::AdjustSensitivity(s.sensitivity, deltaY);
        return true;
    }

    void PixelTools::ZoomChanged()
    {
        if (m_state->on)
        {
            m_state->on = false;
            Invalidate(m_host);
        }
    }

    // ---- Pointer ----

    void PixelTools::PointerMoved(Core::Point imagePoint)
    {
        State& s = *m_state;
        if (!HasImage(m_document))
        {
            return;
        }
        s.cursor = imagePoint;

        // Follow the mouse while on.
        s.validZoom = s.context.zoom;
        if (s.on)
        {
            Measure(s.vertical, s.persistence, s.guide);
        }
        if (!s.AnyRulerKeyHeld())
        {
            bool const wasOn = s.on;
            s.TurnOff();
            if (wasOn)
            {
                Invalidate(m_host);
            }
        }

        // Hovering the badge (only while the crop tool is on).
        bool hover = false;
        if (s.badgeFrame && s.hasContext && m_host.selection && m_host.selection())
        {
            auto const p = s.context.ToCanvas(imagePoint);
            Core::Rect const& f = *s.badgeFrame;
            hover = p.X >= f.x && p.X <= f.x + f.width && p.Y >= f.y && p.Y <= f.y + f.height;
        }
        if (hover != s.badgeHover)
        {
            s.badgeHover = hover;
            Invalidate(m_host);
        }

        // The colour under the cursor, or with Shift the darkest around it.
        Core::Rect const crop = m_document->Crop();
        Core::Point const at = ToOriginalSpace(crop, imagePoint);
        bool const shift = HasFlag(CurrentModifiers(), Modifiers::Shift);
        s.colorValue = shift ? Core::DarkestAround(m_document->Image(), crop, at) : Core::PickPixel(m_document->Image(), crop, at);
        auto formatted = Core::FormatColor(s.colorValue, Core::Settings::Current().ColorFormat());
        s.display = std::move(formatted.display);
        s.copy = std::move(formatted.copy);
        if (m_host.readoutChanged)
        {
            m_host.readoutChanged();
        }
    }

    bool PixelTools::PointerPressed(Core::Point, Modifiers)
    {
        State& s = *m_state;
        if (!s.on || !HasImage(m_document))
        {
            return false;
        }
        Imprint();
        return true;
    }

    void PixelTools::Imprint()
    {
        State& s = *m_state;
        auto& settings = Core::Settings::Current();
        Core::Rect const crop = m_document->Crop();
        double const dpr = m_document->Image().dpr;
        Core::Color const color = Core::Color::FromHex(settings.DefaultColor()).value_or(Core::Color{ 0xFF, 0x0C, 0x01, 255 });

        Core::ObjectPtr object;
        if (s.guide)
        {
            // ObjLine over the guide, 1 px across the axis when dpr != 1 (y up: +1 is one row higher).
            double const offset = dpr != 1.0 ? 1.0 : 0.0;
            Core::Rect up = s.rect;
            if (s.vertical)
            {
                up.x += offset;
            }
            else
            {
                up.y += offset;
            }
            object = Core::CreateObject(L"line", settings);
            if (object)
            {
                // A line's rect is start + (dx, dy): flip the start point, negate dy.
                object->rect = { crop.x + up.x, crop.y + crop.height - up.y, up.width, -up.height };
                object->size = 0;
                object->color = color;
            }
        }
        else
        {
            object = Core::CreateObject(L"ruler", settings);
            if (object)
            {
                Core::Rect const rect = FromOriginalSpace(crop, s.rect);
                Core::Point const cursor = s.cursor.value_or(Core::Point{ rect.x, rect.y });
                // tperc: where the cursor was along the ruler, so the label starts there.
                double const tperc = s.vertical ? (cursor.y - rect.y) / rect.height : (cursor.x - rect.x) / rect.width;
                object->rect = rect;
                object->color = color;
                Core::JsonValue json = object->Dump();
                json.Set(L"vertical", Core::JsonValue::Bool(s.vertical));
                json.Set(L"tperc", Core::JsonValue::Number(tperc));
                json.Set(L"tdist", Core::JsonValue::Number(Core::Ruler::kImprintTextDistance));
                json.Set(L"dpr", Core::JsonValue::Number(settings.RealPixels() ? 1.0 : dpr));
                // Not in ObjRuler.Dump: the first render moves the label inside the image if needed.
                json.Set(L"flipIfNeeded", Core::JsonValue::Bool(true));
                object->Load(json);
            }
        }
        if (object)
        {
            object->state = std::wstring{ Core::ObjectState::Unselected };
            if (m_host.addObject)
            {
                m_host.addObject(std::move(object));
            }
            else
            {
                m_document->Add(std::move(object));
            }
        }
        Invalidate(m_host);
    }

    // ---- Colour copy ----

    void PixelTools::CopyColor(std::wstring const& text, std::wstring const& title, Core::Color color)
    {
        // Pasteboard, hide the editor, notification.
        SetClipboardText(text);
        if (m_host.hideEditor)
        {
            m_host.hideEditor();
        }
        else
        {
            AppController::Current().HideEditor();
        }
        Notification notification;
        notification.title = title;
        notification.message = Strings::Get(L"PixelTools_CopiedToClipboard");
        notification.buttonTitle = Strings::Get(L"PixelTools_ShowEditor");
        notification.action = [] { AppController::Current().Reopen(L"notification"); };
        // The copied colour as the notification's swatch.
        notification.swatch = (std::uint32_t{ color.r } << 16) | (std::uint32_t{ color.g } << 8) | color.b;
        AppController::Current().Notify(std::move(notification));
    }

    std::wstring PixelTools::ColorReadout() const
    {
        return m_state->display;
    }

    Core::Color PixelTools::ReadoutColor() const
    {
        return m_state->colorValue;
    }

    // ---- Contrast ----

    void PixelTools::SelectionPressed()
    {
        m_state->contrastFor.reset();
        ++m_state->contrast->generation;
        if (m_state->contrast->value)
        {
            m_state->contrast->value.reset();
            Invalidate(m_host);
        }
    }

    void PixelTools::SelectionChanged()
    {
        if (!HasImage(m_document) || !m_host.selection)
        {
            return;
        }
        auto const selection = m_host.selection();
        m_state->contrastFor = selection;
        if (!selection)
        {
            ++m_state->contrast->generation;
            m_state->contrast->value.reset();
            return;
        }
        Core::Rect const crop = m_document->Crop();
        Core::Rect const s0 = selection->Standardized();
        Core::Rect const up{ s0.x - crop.x, crop.y + crop.height - (s0.y + s0.height), s0.width, s0.height };
        std::uint64_t const generation = ++m_state->contrast->generation;
        ComputeContrast(m_state->contrast, m_document->ImagePtr(), crop, up, generation);
    }

    bool PixelTools::BadgePressed(Core::Point canvasPoint, Modifiers modifiers)
    {
        State& s = *m_state;
        if (!s.badgeFrame || !s.contrast->value || !m_host.selection || !m_host.selection())
        {
            return false;
        }
        Core::Rect const& f = *s.badgeFrame;
        if (!(canvasPoint.x >= f.x && canvasPoint.x <= f.x + f.width && canvasPoint.y >= f.y && canvasPoint.y <= f.y + f.height))
        {
            return false;
        }
        auto& settings = Core::Settings::Current();
        if (HasFlag(modifiers, Modifiers::Alt))
        {
            // Toggles wcag2 <-> apca, then repaints.
            settings.ContrastType(settings.ContrastType() == L"apca" ? L"wcag2" : L"apca");
            Invalidate(m_host);
        }
        else if (m_host.showInlineHelp)
        {
            Core::Contrast::Result const r = *s.contrast->value;
            m_host.showInlineHelp(Strings::Format(L"PixelTools_ContrastHelpTitle", Core::Ruler::ShortestDescription(r.wcag), Core::Ruler::ShortestDescription(r.apca)),
                                  Strings::Get(L"PixelTools_ContrastHelpMessage"));
        }
        return true;
    }

    std::wstring PixelTools::BadgeHint() const
    {
        if (!m_state->badgeHover)
        {
            return {};
        }
        return Strings::Get(Core::Settings::Current().ContrastType() == L"apca" ? L"PixelTools_ApcaHint" : L"PixelTools_WcagHint");
    }

    // ---- Drawing ----

    void PixelTools::Draw(canvas::CanvasDrawingSession const& session, Render::Context const& context)
    {
        State& s = *m_state;
        s.context = context;
        s.hasContext = true;
        s.badgeFrame.reset();
        if (!HasImage(m_document))
        {
            return;
        }
        if (!s.format)
        {
            s.format = LabelFormat();
        }

        // Y-up view: device pixels, origin at the canvas's bottom-left corner.
        Core::Size const view = m_host.viewSize ? m_host.viewSize() : Core::Size{};
        double const scale = context.rasterScale > 0 ? context.rasterScale : 1.0;
        double const areaHeight = std::round(view.height * scale);
        Core::Rect const crop = m_document->Crop();
        Core::Ruler::ViewMap map;
        map.zoom = context.zoom;
        map.scale = scale;
        map.area = { std::round(view.width * scale), areaHeight };
        map.offset = { context.origin.x * scale + crop.x * context.zoom,
                       areaHeight - context.origin.y * scale - (crop.y + crop.height) * context.zoom };
        double const areaPoints = areaHeight / scale;
        auto flip = [&](Core::Rect const& r) { return Core::Rect{ r.x, areaPoints - (r.y + r.height), r.width, r.height }; };

        // A zoom change since the last measurement hides the ruler.
        if (s.on && context.zoom != s.validZoom)
        {
            s.on = false;
        }
        if (s.on)
        {
            canvas::Text::CanvasTextLayout textLayout{ session, s.text, s.format, kLayoutWidth, 0.0f };
            auto const bounds = textLayout.LayoutBounds();
            Core::Size const textSize{ s.text.empty() ? 0.0 : bounds.Width, bounds.Height };
            Core::Point const cursor = ToOriginalSpace(crop, s.cursor.value_or(Core::Point{ crop.x, crop.y + crop.height }));
            auto const layout = Core::Ruler::LayoutLive(s.rect, s.vertical, s.guide, cursor, map, textSize);

            // The bars are pixel-snapped by offsets; draw them aliased so that fractional
            // scales stay crisp too.
            auto const antialiasing = session.Antialiasing();
            session.Antialiasing(canvas::CanvasAntialiasing::Aliased);
            for (auto const& bar : layout.bars)
            {
                session.FillRectangle(ToRect(flip(bar)), kMain);
            }
            session.Antialiasing(antialiasing);
            if (layout.bubble)
            {
                session.FillRoundedRectangle(ToRect(flip(*layout.bubble)), 2.0f, 2.0f, kMain);
                Core::Rect const text = flip(layout.text);
                session.DrawTextLayout(textLayout, static_cast<float>(text.x), static_cast<float>(text.y), kWhite);
            }
        }

        // The contrast badge above the selection.
        std::optional<Core::Rect> selection = m_host.selection ? m_host.selection() : std::nullopt;
        // The badge is measured when a selection change is done (mouse-up, keyboard adjust, undo)
        // and hidden while it is in progress. The editor does not report those moments, so a selection that differs from the measured one hides the badge while the
        // primary button is down (a drag) and is measured once the button is up.
        if (!s.contrastFor || *s.contrastFor != selection)
        {
            int const button = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
            if (GetAsyncKeyState(button) & 0x8000)
            {
                s.contrastFor.reset();
                ++s.contrast->generation;
                s.contrast->value.reset();
            }
            else
            {
                SelectionChanged();
            }
        }
        if (selection && s.contrast->value)
        {
            std::wstring const text = Core::Contrast::BadgeText(*s.contrast->value, Core::Settings::Current().ContrastType());
            canvas::Text::CanvasTextLayout textLayout{ session, text, s.format, kLayoutWidth, 0.0f };
            auto const bounds = textLayout.LayoutBounds();
            Core::Size const textSize{ bounds.Width, bounds.Height };

            Core::Rect const sel = selection->Standardized();
            Core::Point const origin = map.ToView({ sel.x - crop.x, crop.y + crop.height - (sel.y + sel.height) });
            Core::Rect const selectionPoints{ origin.x / scale, origin.y / scale, sel.width * context.zoom / scale, sel.height * context.zoom / scale };
            auto const badge = Core::Contrast::LayoutBadge(selectionPoints, textSize);
            Core::Rect const background = flip(badge.background);
            Core::Rect const textFrame = flip(badge.text);
            session.FillRoundedRectangle(ToRect(background), 3.0f, 3.0f, s.badgeHover ? kBadgeBackgroundHover : kBadgeBackground);
            session.DrawTextLayout(textLayout, static_cast<float>(textFrame.x), static_cast<float>(textFrame.y), kBadgeText);
            s.badgeFrame = background;
        }
    }
}
