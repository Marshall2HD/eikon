#include "pch.h"
#include <winrt/Microsoft.Graphics.Canvas.Brushes.h>
#include <winrt/Microsoft.Graphics.Canvas.Geometry.h>
#include <winrt/Microsoft.Graphics.Canvas.Text.h>
#include "Editor/ShotView.h"

#include "Editor/BackdropRender.h"

#include "Core/Capture.h"
#include "Core/CropGeometry.h"
#include "Core/CutOut.h"
#include "Platform/SubjectMask.h"
#include "Core/Objects/Factory.h"
#include "Core/Objects/ObjFreehand.h"
#include "Core/Objects/ObjText.h"
#include "Core/Settings.h"
#include "Core/Snapping.h"
#include "Editor/Cursors.h"
#include "Editor/RenderObjects.h"

#include <cmath>

namespace Eikon::Editor
{
    namespace canvas = winrt::Microsoft::Graphics::Canvas;
    namespace input = winrt::Microsoft::UI::Input;
    using winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs;
    using Core::Settings;

    namespace
    {
        constexpr double kUndoDebounceSeconds = 0.33;   // Undo.tmDebounce
        constexpr double kZoomDragMinimum = 6;          // Z + drag of at least 6 px zooms to the rect
        constexpr std::uint32_t kKeyY = 'Y';
        constexpr std::uint32_t kKeyZ = 'Z';

        Modifiers ModifiersOf(winrt::Windows::System::VirtualKeyModifiers keys)
        {
            using winrt::Windows::System::VirtualKeyModifiers;
            Modifiers result = Modifiers::None;
            auto const has = [keys](VirtualKeyModifiers flag) { return (keys & flag) == flag; };
            if (has(VirtualKeyModifiers::Control)) result |= Modifiers::Ctrl;
            if (has(VirtualKeyModifiers::Menu)) result |= Modifiers::Alt;
            if (has(VirtualKeyModifiers::Shift)) result |= Modifiers::Shift;
            if (has(VirtualKeyModifiers::Windows)) result |= Modifiers::Win;
            return result;
        }

        double NowSeconds()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        // Tools that draw objects (CreateObject's argument); the others act through the window.
        bool IsDrawingTool(std::wstring_view tool)
        {
            return tool == L"arrow" || tool == L"text" || tool == L"rectangle" || tool == L"freehand" || tool == L"magnifier" ||
                   tool == L"blur" || tool == L"highlighter" || tool == L"spotlight" || tool == L"counter" || tool == L"oval" ||
                   tool == L"line";
        }
    }

    ShotView::ShotView(canvas::UI::Xaml::CanvasControl const& control, Host host)
        : m_canvas(control),
          m_host(std::move(host)),
          m_objects(ObjectHandlers::Host{
              [this](std::wstring_view label) { RecordUndo(label); },
              [this] { RecordUndoDebounced(); },
              [this] { UpdateCanvasGrowth(); Changed(); },
              [this] { Changed(); },
          }),
          m_pixelTools(PixelTools::Host{
              [this] { Invalidate(); },
              [this] { if (m_host.hideEditor) m_host.hideEditor(); },
              [this] { return m_crop.Selection(); },
              [this](Core::ObjectPtr object) {
                  if (m_document && object)
                  {
                      object->state = std::wstring{ Core::ObjectState::Unselected };
                      m_document->Add(std::move(object));
                      RecordUndo();
                      UpdateCanvasGrowth();
                      Changed();
                  }
              },
              [this] { return Core::Size{ m_canvas.ActualWidth(), m_canvas.ActualHeight() }; },
              [this] { return m_objects.Active() != nullptr; },
              [this] { return m_host.anyHelperOpen && m_host.anyHelperOpen(); },
              [this] { if (m_host.hoverChanged) m_host.hoverChanged(); },
              [this](std::wstring const& title, std::wstring const& message) {
                  if (m_host.alert) m_host.alert(title, message);
              },
          })
    {
        m_drawToken = m_canvas.Draw([this](canvas::UI::Xaml::CanvasControl const& sender, canvas::UI::Xaml::CanvasDrawEventArgs const& args) {
            try
            {
                OnDraw(sender, args);
            }
            catch (...)
            {
                OutputDebugStringW(L"Eikon: drawing the editor canvas failed\n");
            }
        });
        // A new (or lost and recreated) device invalidates the bitmap.
        m_createResourcesToken = m_canvas.CreateResources([this](canvas::UI::Xaml::CanvasControl const&, canvas::UI::CanvasCreateResourcesEventArgs const&) {
            m_bitmap = nullptr;
            m_bitmapSource.reset();
        });
        m_pressedToken = m_canvas.PointerPressed([this](auto&&, PointerRoutedEventArgs const& args) { OnPressed(args); });
        m_movedToken = m_canvas.PointerMoved([this](auto&&, PointerRoutedEventArgs const& args) { OnMoved(args); });
        m_releasedToken = m_canvas.PointerReleased([this](auto&&, PointerRoutedEventArgs const& args) { OnReleased(args); });
        m_wheelToken = m_canvas.PointerWheelChanged([this](auto&&, PointerRoutedEventArgs const& args) { OnWheel(args); });
        m_exitedToken = m_canvas.PointerExited([this](auto&&, PointerRoutedEventArgs const&) {
            if (m_pointerOver)
            {
                m_pointerOver = false;
                Invalidate();
            }
        });
        m_captureLostToken = m_canvas.PointerCaptureLost([this](auto&&, PointerRoutedEventArgs const&) {
            if (m_drag == Drag::Pan || m_drag == Drag::ZoomRect)
            {
                m_drag = Drag::None;
                Invalidate();
            }
        });
        m_sizeToken = m_canvas.SizeChanged([this](auto&&, auto&&) { UpdateArea(); });

        auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        m_undoTimer = queue.CreateTimer();
        m_undoTimer.Interval(std::chrono::milliseconds(static_cast<int>(kUndoDebounceSeconds * 1000)));
        m_undoTimer.IsRepeating(false);
        m_undoTimer.Tick([this](auto&&, auto&&) { RecordUndo(); });
        m_animationTimer = queue.CreateTimer();
        m_animationTimer.Interval(std::chrono::milliseconds(16));
        m_animationTimer.IsRepeating(false);
        m_animationTimer.Tick([this](auto&&, auto&&) { Invalidate(); });
    }

    ShotView::~ShotView()
    {
        if (m_document)
        {
            m_document->Changed.Remove(m_documentChangedToken);
            m_document->backdrop.Changed.Remove(m_backdropChangedToken);
        }
        if (m_undoTimer)
        {
            m_undoTimer.Stop();
        }
        if (m_animationTimer)
        {
            m_animationTimer.Stop();
        }
        if (m_canvas)
        {
            m_canvas.Draw(m_drawToken);
            m_canvas.CreateResources(m_createResourcesToken);
            m_canvas.PointerPressed(m_pressedToken);
            m_canvas.PointerMoved(m_movedToken);
            m_canvas.PointerReleased(m_releasedToken);
            m_canvas.PointerWheelChanged(m_wheelToken);
            m_canvas.PointerExited(m_exitedToken);
            m_canvas.PointerCaptureLost(m_captureLostToken);
            m_canvas.SizeChanged(m_sizeToken);
        }
    }

    // ---- Document ----

    void ShotView::SetDocument(std::shared_ptr<Core::Document> document, bool keepWindowSize)
    {
        if (m_document)
        {
            m_document->Changed.Remove(m_documentChangedToken);
            m_document->backdrop.Changed.Remove(m_backdropChangedToken);
        }
        m_undoTimer.Stop();
        m_document = std::move(document);
        m_crop.Reset();
        m_objects.SetDocument(m_document);
        m_pixelTools.SetDocument(m_document);
        m_drag = Drag::None;
        m_keepWindowSize = keepWindowSize;
        m_framed = false;
        m_autoFit = true;
        if (m_document)
        {
            m_documentChangedToken = m_document->Changed.Add([this] { Invalidate(); });
            // Backdrop changes: refit when the canvas size changed, repaint, and
            // record an undo step.
            m_backdropChangedToken = m_document->backdrop.Changed.Add([this] {
                Core::Rect const before = m_framer.canvas;
                RefreshCanvas();
                if (m_framer.canvas.GetSize() != before.GetSize())
                {
                    m_framer.LowerMinZoomFor(m_framer.canvas.GetSize());
                    m_framer.Fit(m_framer.canvas);
                    m_framer.zoom = (std::min)(m_framer.zoom, 1.0);
                    m_framer.Center(m_framer.canvas);
                    m_framer.Clamp(false);
                    AfterZoom();
                }
                RecordUndoDebounced();
                Invalidate();
            });
            m_framer.canvas = m_document->Canvas();
            UpdateArea();
            // The hidden defaultTool setting. "crop" is the plain default tool.
            std::wstring const defaultTool = Settings::Current().DefaultTool();
            m_tool = defaultTool != L"crop" && IsDrawingTool(defaultTool) ? defaultTool : std::wstring{};
            // An initial undo state with an empty label.
            if (m_document->History().Count() == 0)
            {
                RecordUndo();
            }
        }
        Changed();
    }

    double ShotView::ImageDpr() const noexcept
    {
        return m_document && m_document->Image().dpr > 0 ? m_document->Image().dpr : 1.0;
    }

    bool ShotView::RealPixels() const
    {
        return Settings::Current().RealPixels();
    }

    void ShotView::UpdateArea()
    {
        if (!m_canvas)
        {
            return;
        }
        double scale = 1.0;
        try
        {
            if (auto root = m_canvas.XamlRoot())
            {
                scale = root.RasterizationScale();
            }
        }
        catch (...)
        {
        }
        // Whole device pixels (ActualWidth/Height are floats in DIPs: 921.6 × 1.25 would be 1151.99997).
        Core::Size const area{ std::round(m_canvas.ActualWidth() * scale), std::round(m_canvas.ActualHeight() * scale) };
        if (!(area.width > 0) || !(area.height > 0))
        {
            return;
        }
        Core::Size const previous = m_framer.area;
        m_framer.dpr = scale;
        m_framer.area = area;
        if (!m_document)
        {
            return;
        }
        if (!m_framed)
        {
            m_framer.canvas = m_document->Canvas();
            m_framer.InitialFit(Settings::Current().PreferLargeWindow(), m_keepWindowSize);
            m_framed = true;
            Changed();
        }
        else if (previous != area && m_autoFit)
        {
            // The window was sized for this capture after the document arrived: fit again.
            m_framer.canvas = m_document->Canvas();
            m_framer.InitialFit(Settings::Current().PreferLargeWindow(), m_keepWindowSize);
            Changed();
        }
        else if (previous != area)
        {
            // Window resized: keep the centre where it was, then keep the canvas in view.
            m_framer.pan.x += (area.width - previous.width) / 2;
            m_framer.pan.y += (area.height - previous.height) / 2;
            m_framer.Clamp(false);
        }
        Invalidate();
    }

    void ShotView::Invalidate()
    {
        if (m_canvas)
        {
            m_canvas.Invalidate();
        }
    }

    void ShotView::Changed()
    {
        Invalidate();
        if (m_host.stateChanged)
        {
            m_host.stateChanged();
        }
    }

    void ShotView::StartAnimationTimer()
    {
        if (m_animationTimer && !m_animationTimer.IsRunning())
        {
            m_animationTimer.Start();
        }
    }

    // ---- Tools and modes ----

    void ShotView::SetTool(std::wstring_view tool)
    {
        m_tool = IsDrawingTool(tool) ? std::wstring{ tool } : std::wstring{};
        Changed();
    }

    std::wstring ShotView::Mode() const
    {
        if (auto* active = m_objects.Active())
        {
            return std::wstring{ active->ClassName() };
        }
        if (m_crop.HasSelection())
        {
            return L"selection";
        }
        return L"default";
    }

    // ---- Zoom ----

    void ShotView::AfterZoom()
    {
        m_autoFit = false;
        m_pixelTools.ZoomChanged();
        Changed();
    }

    void ShotView::ZoomIn()
    {
        m_framer.ZoomStep(m_framer.AreaCenter(), true);
        m_framer.Clamp(false);
        AfterZoom();
    }

    void ShotView::ZoomOut()
    {
        m_framer.ZoomStep(m_framer.AreaCenter(), false);
        m_framer.Clamp(false);
        AfterZoom();
    }

    void ShotView::ZoomToFit()
    {
        if (!m_document)
        {
            return;
        }
        m_framer.Fit(m_framer.canvas);
        m_framer.Clamp(false);
        AfterZoom();
    }

    void ShotView::ZoomTo100()
    {
        if (!m_document)
        {
            return;
        }
        m_framer.ZoomTo100();
        AfterZoom();
    }

    void ShotView::ZoomToSelection()
    {
        if (!m_document)
        {
            return;
        }
        // The crop selection, else the active object, else the canvas.
        Core::Rect target = m_framer.canvas;
        if (auto const selection = m_crop.Selection())
        {
            target = *selection;
        }
        else if (auto* active = m_objects.Active())
        {
            target = active->rect.Standardized();
        }
        m_framer.Fit(target);
        m_framer.Clamp(false);
        AfterZoom();
    }

    void ShotView::SelectionTopLeft()
    {
        Core::Rect target = m_framer.canvas;
        if (auto const selection = m_crop.Selection())
        {
            target = *selection;
        }
        else if (auto* active = m_objects.Active())
        {
            target = active->rect.Standardized();
        }
        target = target.Standardized();
        m_framer.FocusOn({ target.Left(), target.Top() });
        AfterZoom();
    }

    void ShotView::SelectionBottomRight()
    {
        Core::Rect target = m_framer.canvas;
        if (auto const selection = m_crop.Selection())
        {
            target = *selection;
        }
        else if (auto* active = m_objects.Active())
        {
            target = active->rect.Standardized();
        }
        target = target.Standardized();
        m_framer.FocusOn({ target.Right(), target.Bottom() });
        AfterZoom();
    }

    // ---- Selection ----

    void ShotView::ResetSelection()
    {
        if (m_crop.HasSelection())
        {
            m_crop.Reset();
            Changed();
        }
    }

    void ShotView::DrawBrushPreview(canvas::CanvasDrawingSession const& session)
    {
        // ObjectHandlers.lplaceholder: with the
        // highlighter tool, no object being created and no key held, a w/2 × w rounded rect (radius
        // w/4) centred on the pointer, stroked 1 pt black α 0.4, inside a 1 pt white α 0.4 ring one
        // point larger. w is the highlighter width for "Eikon.ObjHighlighter: size" (default 1,
        // outside 0…7 → 1) as the stroke appears at this zoom.
        if (m_tool != L"highlighter" || !m_pointerOver || !m_heldKeys.empty() || m_drag != Drag::None)
        {
            return;
        }
        if (auto const* active = m_objects.Active(); active && active->state == Core::ObjectState::Creating)
        {
            return;
        }
        std::int64_t size = Settings::Current().ReadInt(L"Eikon.ObjHighlighter: size", 1);
        if (size < 0 || size > 7)
        {
            size = 1;
        }
        double const k = m_framer.dpr;
        double const w = Core::ObjHighlighter::kHighlighterWidths[static_cast<std::size_t>(size)] * ImageDpr() * m_framer.zoom / k;
        float const cx = static_cast<float>(m_lastView.x / k);
        float const cy = static_cast<float>(m_lastView.y / k);
        float const r = static_cast<float>(w / 4);
        winrt::Windows::Foundation::Rect const inner{ cx - r, static_cast<float>(cy - w / 2), static_cast<float>(w / 2), static_cast<float>(w) };
        winrt::Windows::Foundation::Rect const outer{ inner.X - 1, inner.Y - 1, inner.Width + 2, inner.Height + 2 };
        session.DrawRoundedRectangle(outer, r + 1, r + 1, winrt::Windows::UI::Color{ 0x66, 0xFF, 0xFF, 0xFF }, 1.0f);
        session.DrawRoundedRectangle(inner, r, r, winrt::Windows::UI::Color{ 0x66, 0x00, 0x00, 0x00 }, 1.0f);
    }

    void ShotView::CutOut()
    {
        // The selection's pixels of the screenshot, then Erase
        // (: an "erase" ObjConceal over the selection, selection reset), then the pixels
        // with their background removed as an ObjImage at the same place, size 0. The subject is found
        // off the UI thread; the document changes
        // when it is back, in one undo step.
        auto const selection = m_crop.Selection();
        if (!m_document || !selection)
        {
            return;
        }
        Core::Rect const area = selection->Standardized().Intersection(m_document->Source());
        int const x = static_cast<int>(std::lround(area.x));
        int const y = static_cast<int>(std::lround(area.y));
        int const w = static_cast<int>(std::lround(area.Right())) - x;
        int const h = static_cast<int>(std::lround(area.Bottom())) - y;
        if (w <= 0 || h <= 0)
        {
            return;
        }
        Core::Rect const rect{ static_cast<double>(x), static_cast<double>(y), static_cast<double>(w), static_cast<double>(h) };
        auto const pixels = std::make_shared<Core::Bitmap const>(Core::CropBitmap(m_document->Image(), x, y, w, h));
        std::weak_ptr<bool> const alive = m_alive;
        std::weak_ptr<Core::Document> const document = m_document;
        SubjectMask::CutOut(pixels, [this, alive, document, rect](Core::Bitmap subject) {
            if (alive.expired() || document.lock() != m_document || !m_document)
            {
                return;   // the editor closed or shows another capture
            }
            FinishCutOut(rect, std::make_shared<Core::Bitmap const>(std::move(subject)));
        });
    }

    void ShotView::FinishCutOut(Core::Rect const& rect, std::shared_ptr<Core::Bitmap const> subject)
    {
        auto& settings = Settings::Current();
        if (Core::ObjectPtr erase = Core::CreateObject(L"blur", settings))
        {
            erase->rect = rect;
            erase->SetProperty(L"concealType", L"erase");
            erase->state = std::wstring{ Core::ObjectState::Unselected };
            m_document->Add(std::move(erase));
        }
        m_crop.Reset();
        m_objects.Deselect();
        if (Core::ObjectPtr image = Core::CreateImageObject(subject, rect, settings))
        {
            image->size = 0;
            image->state = std::wstring{ Core::ObjectState::Selected };
            std::int64_t const id = m_document->Add(std::move(image)).id;
            m_objects.Select(id);
        }
        RecordUndo();
        UpdateCanvasGrowth();
        Changed();
    }

    void ShotView::SmartAdjust(std::wstring_view edge)
    {
        auto const selection = m_crop.Selection();
        if (!m_document || !selection)
        {
            return;
        }
        if (auto const adjusted = Core::SmartAdjust(m_document->Image(), *selection, edge))
        {
            m_crop.SetSelection(*adjusted, true);
            StartAnimationTimer();
            RecordUndo();
            Changed();
        }
    }

    Core::Rect ShotView::SelectionOrCanvas() const
    {
        if (auto const selection = m_crop.Selection())
        {
            return selection->Standardized();
        }
        return m_framer.canvas.Standardized();
    }

    // ---- Crop ----

    void ShotView::RefreshCanvas()
    {
        if (m_document)
        {
            m_framer.canvas = m_document->Canvas();
            if (m_document->backdrop.config.isOn == 1)
            {
                // With a backdrop the canvas is the whole output image, in image pixels.
                auto const layout = BackdropRender::DocumentLayout(*m_document);
                Core::Rect const crop = m_document->Crop().Standardized();
                m_framer.canvas = { crop.x - layout.screenshot.x, crop.y - layout.screenshot.y, layout.canvasW, layout.canvasH };
            }
        }
    }

    void ShotView::UpdateCanvasGrowth()
    {
        if (!m_document)
        {
            return;
        }
        Core::Insets growth;
        if (Settings::Current().ExpandableCanvas())
        {
            std::vector<Core::Rect> bounds;
            for (auto const& object : m_document->Objects())
            {
                if (object->state != Core::ObjectState::Creating)
                {
                    bounds.push_back(object->rect.Standardized());
                }
            }
            growth = Core::CanvasGrowth(m_document->Crop(), bounds, ImageDpr());
        }
        m_document->SetCanvasGrowth(growth);
        RefreshCanvas();
        m_framer.LowerMinZoomFor(m_framer.canvas.GetSize());
    }

    void ShotView::CropToSelection()
    {
        auto const selection = m_crop.Selection();
        if (!m_document || !selection)
        {
            return;
        }
        Core::Rect const crop = selection->Standardized().Intersection(m_document->Source());
        if (crop.IsEmpty())
        {
            return;
        }
        m_crop.Reset();
        m_document->SetCrop(crop);
        UpdateCanvasGrowth();
        RefreshCanvas();
        m_framer.Fit(m_framer.canvas);
        m_framer.zoom = (std::min)(m_framer.zoom, 1.0);
        m_framer.Center(m_framer.canvas);
        m_framer.Clamp(false);
        RecordUndo();
        AfterZoom();
    }

    bool ShotView::CanResetCrop() const
    {
        return m_document && m_document->IsCropped();
    }

    void ShotView::ResetCrop()
    {
        if (!m_document)
        {
            return;
        }
        m_document->SetCrop(m_document->Source());
        UpdateCanvasGrowth();
        RefreshCanvas();
        m_framer.Fit(m_framer.canvas);
        m_framer.ZoomStep(m_framer.AreaCenter(), false);
        m_framer.Clamp(false);
        RecordUndo();
        AfterZoom();
    }

    void ShotView::Rasterize()
    {
        if (!m_document || m_document->Objects().empty())
        {
            return;
        }
        try
        {
            // Everything drawn at one image pixel per pixel: the image and the canvas growth.
            Core::Rect const bounds = m_document->Source().Union(m_document->Canvas());
            int const width = static_cast<int>(std::ceil(bounds.width));
            int const height = static_cast<int>(std::ceil(bounds.height));
            auto device = canvas::CanvasDevice::GetSharedDevice();
            canvas::CanvasRenderTarget target{ device, static_cast<float>(width), static_cast<float>(height), 96.0f };
            auto const& image = m_document->ImagePtr();
            auto source = canvas::CanvasBitmap::CreateFromBytes(device, winrt::array_view<std::uint8_t const>(image->pixels), image->width,
                image->height, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 96.0f, canvas::CanvasAlphaMode::Premultiplied);
            {
                auto session = target.CreateDrawingSession();
                session.Clear(winrt::Windows::UI::Color{ 0, 0, 0, 0 });
                session.DrawImage(source, static_cast<float>(-bounds.x), static_cast<float>(-bounds.y));
                Render::Context context;
                context.zoom = 1;
                context.dpr = ImageDpr();
                context.rasterScale = 1;
                context.origin = { -bounds.x, -bounds.y };
                context.image = source;
                Render::DrawSpotlights(session, m_document->Objects(), context, m_document->Source());
                for (auto const& object : m_document->Objects())
                {
                    Render::Draw(session, *object, context);
                }
                session.Close();
            }
            Core::Bitmap flat;
            flat.width = width;
            flat.height = height;
            flat.dpr = image->dpr;
            flat.screenRect = image->screenRect;
            auto const bytes = target.GetPixelBytes();
            flat.pixels.assign(bytes.begin(), bytes.end());
            m_objects.Deselect();
            m_document->ReplaceObjects({});
            m_document->ReplaceImage(std::move(flat), { -bounds.x, -bounds.y });
            RefreshCanvas();
            RecordUndo();
            Changed();
        }
        catch (...)
        {
            OutputDebugStringW(L"Eikon: rasterizing failed\n");
            throw;
        }
    }

    // ---- Undo ----

    void ShotView::RecordUndo(std::wstring_view label)
    {
        if (!m_document)
        {
            return;
        }
        m_undoTimer.Stop();
        auto const snapshot = m_document->Snapshot(m_crop.Selection(), m_objects.ActiveId());
        m_document->History().Record(std::wstring{ label }, snapshot.Full(), snapshot.Comparable());
        if (m_host.stateChanged)
        {
            m_host.stateChanged();
        }
    }

    void ShotView::RecordUndoDebounced()
    {
        m_undoTimer.Stop();
        m_undoTimer.Start();
    }

    void ShotView::Restore(std::wstring const& full)
    {
        auto const snapshot = Core::UndoSnapshot::Parse(full);
        if (!snapshot || !m_document)
        {
            return;
        }
        Core::Rect const previousCrop = m_document->Crop();
        m_objects.Deselect();
        auto const selectedId = m_document->Restore(*snapshot);
        m_objects.SetDocument(m_document);
        if (selectedId)
        {
            m_objects.Select(*selectedId);
        }
        if (snapshot->selectionRect)
        {
            m_crop.SetSelection(*snapshot->selectionRect, true);
            StartAnimationTimer();
        }
        else
        {
            m_crop.Reset();
        }
        UpdateCanvasGrowth();
        RefreshCanvas();
        if (previousCrop != m_document->Crop())
        {
            m_framer.Fit(m_framer.canvas);
            m_framer.zoom = (std::min)(m_framer.zoom, 1.0);
            m_framer.Center(m_framer.canvas);
            m_framer.Clamp(false);
            m_pixelTools.ZoomChanged();
        }
        Changed();
    }

    void ShotView::Undo()
    {
        if (!m_document)
        {
            return;
        }
        if (m_undoTimer.IsRunning())
        {
            RecordUndo();
        }
        if (auto const full = m_document->History().StepBack())
        {
            Restore(*full);
        }
    }

    void ShotView::Redo()
    {
        if (!m_document)
        {
            return;
        }
        if (auto const full = m_document->History().StepForward())
        {
            Restore(*full);
        }
    }

    // ---- Keys ----

    bool ShotView::HeldZoomKey() const
    {
        return m_heldKeys.contains(kKeyZ) || m_heldKeys.contains(kKeyY);
    }

    bool ShotView::HeldPanKey() const
    {
        return m_heldKeys.contains(VK_SPACE);
    }

    bool ShotView::KeyDown(std::uint32_t key, Modifiers modifiers)
    {
        if (!m_document)
        {
            return false;
        }
        bool const ctrl = HasFlag(modifiers, Modifiers::Ctrl);
        bool const shift = HasFlag(modifiers, Modifiers::Shift);
        bool const plain = modifiers == Modifiers::None;

        // 1. The active object.
        if (m_objects.Active() && m_objects.KeyDown(key, modifiers, ImageDpr(), RealPixels()))
        {
            UpdateCanvasGrowth();
            Changed();
            return true;
        }

        // 2. The crop selection.
        if (auto const selection = m_crop.Selection())
        {
            double step = RealPixels() ? 1.0 : ImageDpr();
            if (shift)
            {
                step *= 10;
            }
            std::optional<Core::SelectionKey> move;
            switch (key)
            {
            case VK_LEFT: move = Core::SelectionKey::Left; break;
            case VK_RIGHT: move = Core::SelectionKey::Right; break;
            case VK_UP: move = Core::SelectionKey::Up; break;
            case VK_DOWN: move = Core::SelectionKey::Down; break;
            default: break;
            }
            if (!ctrl && !shift && (key == VK_OEM_6 || key == '9'))
            {
                move = Core::SelectionKey::Grow;
            }
            if (!ctrl && !shift && (key == VK_OEM_4 || key == '8'))
            {
                move = Core::SelectionKey::Shrink;
            }
            if (move)
            {
                Core::Rect next = Core::ApplySelectionKey(*selection, *move, step, ctrl);
                next = (*move == Core::SelectionKey::Grow || ctrl) ? next.Intersection(m_document->Source())
                                                                   : Core::KeepInside(next, m_document->Source());
                if (!next.IsEmpty())
                {
                    m_crop.SetSelection(next, false);
                    RecordUndoDebounced();
                    Changed();
                }
                return true;
            }
            if ((key == VK_RETURN) && !ctrl)
            {
                CropToSelection();
                return true;
            }
            if (plain && key == 'A')
            {
                SmartAdjust();
                return true;
            }
            if (plain && key == 'C')
            {
                return m_pixelTools.KeyDown(key, modifiers) || true;
            }
            // S, R, O, M: an object from the selection; B blur; Delete erase.
            std::wstring_view tool;
            std::wstring_view property;
            std::wstring_view value;
            if (plain)
            {
                switch (key)
                {
                case 'S': tool = L"spotlight"; break;
                case 'R': tool = L"rectangle"; break;
                case 'O': tool = L"oval"; break;
                case 'M': tool = L"magnifier"; break;
                case 'B': tool = L"blur"; break;
                case VK_DELETE:
                case VK_BACK: tool = L"blur"; property = L"concealType"; value = L"erase"; break;
                default: break;
                }
            }
            if (!tool.empty())
            {
                if (Core::ObjectPtr object = Core::CreateObject(tool, Settings::Current()))
                {
                    object->rect = selection->Standardized();
                    if (!property.empty())
                    {
                        object->SetProperty(property, value);
                    }
                    object->state = std::wstring{ Core::ObjectState::Unselected };
                    std::int64_t const id = m_document->Add(std::move(object)).id;
                    m_crop.Reset();
                    m_objects.Select(id);
                    RecordUndo();
                    UpdateCanvasGrowth();
                    Changed();
                }
                return true;
            }
        }

        // 3. The pixel tools (ruler keys, colour copy, P). Tab is swallowed while a helper is open.
        if (key == VK_TAB && m_host.anyHelperOpen && m_host.anyHelperOpen())
        {
            return true;
        }
        if (m_pixelTools.KeyDown(key, modifiers))
        {
            return true;
        }

        // Held keys for panning (Space) and zoom drags (Z, or Y on QWERTZ).
        if (plain && (key == VK_SPACE || key == kKeyZ || key == kKeyY))
        {
            m_heldKeys.insert(key);
            UpdateCursor(m_lastView, modifiers);
            return true;
        }
        return false;
    }

    bool ShotView::KeyUp(std::uint32_t key, Modifiers modifiers)
    {
        bool const held = m_heldKeys.erase(key) > 0;
        bool const pixel = m_pixelTools.KeyUp(key, modifiers);
        if (held)
        {
            UpdateCursor(m_lastView, modifiers);
        }
        return held || pixel;
    }

    bool ShotView::CancelCreation()
    {
        if (m_objects.CancelCreation())
        {
            m_drag = Drag::None;
            Changed();
            return true;
        }
        return false;
    }

    bool ShotView::LeaveMode()
    {
        if (m_objects.Active())
        {
            m_objects.Deselect();
            Changed();
            return true;
        }
        if (m_crop.HasSelection())
        {
            m_crop.Reset();
            RecordUndo();
            Changed();
            return true;
        }
        return false;
    }

    // ---- Pointer ----

    Core::Point ShotView::ViewPoint(PointerRoutedEventArgs const& args, bool shifted) const
    {
        auto const position = args.GetCurrentPoint(m_canvas).Position();
        // The mouse handlers take the device point as (x·dpr − 1, y·dpr); the wheel handler does
        // not shift.
        return { position.X * m_framer.dpr - (shifted ? 1.0 : 0.0), position.Y * m_framer.dpr };
    }

    void ShotView::UpdateCursor(Core::Point view, Modifiers modifiers)
    {
        if (!m_canvas)
        {
            return;
        }
        if (!m_document)
        {
            SetCursor(m_canvas, L"default");
            return;
        }
        if (m_drag == Drag::Pan)
        {
            SetCursor(m_canvas, L"closed-hand");
            return;
        }
        if (HeldPanKey())
        {
            SetCursor(m_canvas, L"open-hand");
            return;
        }
        if (HeldZoomKey() || m_drag == Drag::ZoomRect)
        {
            SetCursor(m_canvas, L"zoom");
            return;
        }
        Core::Point const image = m_framer.ToImage(view);
        if (m_drag == Drag::Objects)
        {
            return;
        }
        std::wstring const objectCursor = m_objects.CursorAt(image, m_framer.zoom);
        if (!objectCursor.empty())
        {
            SetCursor(m_canvas, objectCursor);
            return;
        }
        std::wstring_view const zone = m_crop.HitTest(m_framer, view);
        if (zone == L"units")
        {
            SetCursor(m_canvas, L"point");
            return;
        }
        std::wstring_view const zoneCursor = Core::CursorForZone(zone);
        if (!zoneCursor.empty())
        {
            SetCursor(m_canvas, zoneCursor);
            return;
        }
        if (m_tool == L"freehand" || m_tool == L"highlighter")
        {
            SetCursor(m_canvas, L"pen");
            return;
        }
        if (m_tool == L"text")
        {
            SetCursor(m_canvas, L"ibeam");
            return;
        }
        (void)modifiers;
        SetCursor(m_canvas, L"crosshair");
    }

    void ShotView::OnPressed(PointerRoutedEventArgs const& args)
    {
        if (m_host.canvasPressed)
        {
            m_host.canvasPressed();
        }
        try
        {
            m_canvas.Focus(winrt::Microsoft::UI::Xaml::FocusState::Pointer);
        }
        catch (...)
        {
        }
        if (!m_document)
        {
            return;
        }
        auto const point = args.GetCurrentPoint(m_canvas);
        auto const properties = point.Properties();
        Modifiers const modifiers = ModifiersOf(args.KeyModifiers());
        Core::Point const view = ViewPoint(args);
        Core::Point const image = m_framer.ToImage(view);
        m_downView = view;
        m_downPan = m_framer.pan;
        m_lastView = view;
        bool const left = properties.IsLeftButtonPressed();

        // Right or middle drag, or Space + left drag, pans; Z/Y + left drag zooms.
        if (properties.IsRightButtonPressed() || properties.IsMiddleButtonPressed() || (left && HeldPanKey()))
        {
            m_drag = Drag::Pan;
            m_canvas.CapturePointer(args.Pointer());
            UpdateCursor(view, modifiers);
            args.Handled(true);
            return;
        }
        if (!left)
        {
            return;
        }
        // Ctrl+Alt+drag starts the drag-out before any tool sees the drag. Ctrl+drag reverses a new
        // arrow and turns snapping off, and Alt+drag smart-adjusts the crop, so the drag-out uses the
        // free combination.
        if (modifiers == (Modifiers::Ctrl | Modifiers::Alt) && m_host.dragOut)
        {
            m_drag = Drag::DragOut;
            m_canvas.CapturePointer(args.Pointer());
            args.Handled(true);
            return;
        }
        if (HeldZoomKey())
        {
            m_drag = Drag::ZoomRect;
            m_canvas.CapturePointer(args.Pointer());
            args.Handled(true);
            return;
        }
        // The pixel tools first (ruler imprint, contrast badge).
        if (m_pixelTools.BadgePressed({ view.x / m_framer.dpr, view.y / m_framer.dpr }, modifiers) ||
            m_pixelTools.PointerPressed(image, modifiers))
        {
            m_drag = Drag::PixelTool;
            args.Handled(true);
            return;
        }
        m_canvas.CapturePointer(args.Pointer());
        // The crop selection's own zones win over objects underneath it.
        std::wstring_view const zone = m_crop.HitTest(m_framer, view);
        bool const onSelection = zone != Core::CropZone::None;
        if (!onSelection && m_objects.MouseDown(image, modifiers, m_framer.zoom, m_tool))
        {
            m_crop.Reset();
            m_drag = Drag::Objects;
            Changed();
            args.Handled(true);
            return;
        }
        if (!onSelection)
        {
            m_objects.Deselect();
        }
        // A new selection starts inside the image (the drag keeps its far corner there too).
        Core::Point start = image;
        if (zone == Core::CropZone::None)
        {
            Core::Rect const source = m_document->Source();
            start = { std::clamp(image.x, source.Left(), source.Right()), std::clamp(image.y, source.Top(), source.Bottom()) };
        }
        m_crop.MouseDown(zone, start, NowSeconds());
        m_drag = Drag::Crop;
        Changed();
        args.Handled(true);
    }

    void ShotView::OnMoved(PointerRoutedEventArgs const& args)
    {
        Core::Point const view = ViewPoint(args);
        Modifiers const modifiers = ModifiersOf(args.KeyModifiers());
        m_lastView = view;
        m_hasPointer = true;
        m_pointerOver = true;
        if (!m_document)
        {
            UpdateCursor(view, modifiers);
            return;
        }
        if (m_tool == L"highlighter")
        {
            Invalidate();   // the brush preview follows the pointer
        }
        Core::Point const image = m_framer.ToImage(view);
        switch (m_drag)
        {
        case Drag::Pan:
            m_autoFit = false;
            m_framer.pan = { m_downPan.x + view.x - m_downView.x, m_downPan.y + view.y - m_downView.y };
            m_framer.Clamp(true);
            Invalidate();
            return;
        case Drag::ZoomRect:
            Invalidate();
            return;
        case Drag::DragOut:
            // The first drag event starts it, as mouseDragged does.
            if (view.x != m_downView.x || view.y != m_downView.y)
            {
                m_drag = Drag::None;
                m_host.dragOut(args.GetCurrentPoint(m_canvas));
            }
            return;
        case Drag::Objects:
            m_objects.MouseDragged(image, modifiers, m_framer, ImageDpr());
            Invalidate();
            return;
        case Drag::Crop:
            m_crop.MouseDragged(image, m_document->Source());
            Changed();
            return;
        default:
            break;
        }
        m_pixelTools.PointerMoved(image);
        bool repaint = m_objects.Hover(image, m_framer.zoom);
        bool const overUnits = m_crop.HitTest(m_framer, view) == L"units";
        if (overUnits != m_hoverUnits)
        {
            m_hoverUnits = overUnits;
            repaint = true;
        }
        if (repaint)
        {
            Invalidate();
        }
        UpdateCursor(view, modifiers);
        if (m_host.hoverChanged)
        {
            m_host.hoverChanged();
        }
    }

    void ShotView::OnReleased(PointerRoutedEventArgs const& args)
    {
        Core::Point const view = ViewPoint(args);
        Modifiers const modifiers = ModifiersOf(args.KeyModifiers());
        Drag const drag = m_drag;
        m_drag = Drag::None;
        try
        {
            m_canvas.ReleasePointerCapture(args.Pointer());
        }
        catch (...)
        {
        }
        if (!m_document)
        {
            return;
        }
        Core::Point const image = m_framer.ToImage(view);
        switch (drag)
        {
        case Drag::ZoomRect:
        {
            double const dx = view.x - m_downView.x;
            double const dy = view.y - m_downView.y;
            if (std::abs(dx) >= kZoomDragMinimum || std::abs(dy) >= kZoomDragMinimum)
            {
                Core::Point const a = m_framer.ToImage(m_downView);
                m_framer.Fit(Core::Rect{ a.x, a.y, image.x - a.x, image.y - a.y }.Standardized());
            }
            else
            {
                m_framer.ZoomAt(Core::Framer::NextPreset(m_framer.zoom), view);
            }
            m_framer.Clamp(false);
            AfterZoom();
            break;
        }
        case Drag::Objects:
            m_objects.MouseUp(image, modifiers);
            if (auto* text = dynamic_cast<Core::ObjText*>(m_objects.Active()); text && text->TakeEditRequest() && m_host.editText)
            {
                m_host.editText(*text);
            }
            Changed();
            break;
        case Drag::Crop:
        {
            auto const result = m_crop.MouseUp(image, NowSeconds(), ImageDpr(), RealPixels());
            switch (result)
            {
            case CropTool::UpResult::ToggleUnits:
                Settings::Current().RealPixels(!Settings::Current().RealPixels());
                break;
            case CropTool::UpResult::AdjustEdge:
                SmartAdjust(m_crop.AdjustEdgeName());
                break;
            case CropTool::UpResult::Changed:
                if (HasFlag(modifiers, Modifiers::Alt))
                {
                    SmartAdjust();
                }
                else
                {
                    RecordUndo();
                }
                break;
            case CropTool::UpResult::Cleared:
                RecordUndo();
                break;
            default:
                break;
            }
            Changed();
            break;
        }
        case Drag::Pan:
            Invalidate();
            break;
        default:
            break;
        }
        UpdateCursor(view, modifiers);
    }

    void ShotView::OnWheel(PointerRoutedEventArgs const& args)
    {
        if (!m_document)
        {
            return;
        }
        auto const point = args.GetCurrentPoint(m_canvas);
        auto const properties = point.Properties();
        int const delta = properties.MouseWheelDelta();
        bool const horizontal = properties.IsHorizontalMouseWheel();
        Modifiers const modifiers = ModifiersOf(args.KeyModifiers());
        Core::Point const view = ViewPoint(args, false);
        args.Handled(true);
        if (!horizontal && m_pixelTools.Wheel(static_cast<double>(delta) / WHEEL_DELTA))
        {
            return;
        }
        bool const touchpad = point.PointerDeviceType() == winrt::Microsoft::UI::Input::PointerDeviceType::Touchpad;
        bool const ctrl = HasFlag(modifiers, Modifiers::Ctrl);
        // A touchpad scrolls (pans) unless Ctrl is held (pinch arrives as Ctrl + wheel);
        // a mouse wheel, or any scroll with Ctrl or the right button held, zooms.
        if ((touchpad && !ctrl && !properties.IsRightButtonPressed()) || horizontal)
        {
            m_autoFit = false;
            double const amount = static_cast<double>(delta) / WHEEL_DELTA * 40 * m_framer.dpr;
            if (horizontal)
            {
                m_framer.pan.x -= amount;
            }
            else if (HasFlag(modifiers, Modifiers::Shift))
            {
                m_framer.pan.x += amount;
            }
            else
            {
                m_framer.pan.y += amount;
            }
            m_framer.Clamp(true);
            Invalidate();
            return;
        }
        bool zoomIn = delta > 0;
        if (Settings::Current().AltZoomDirection())
        {
            zoomIn = !zoomIn;
        }
        if (touchpad)
        {
            // Precision touchpad pinch: one fine step per 10 accumulated units.
            m_wheelAccumulator += std::abs(static_cast<double>(delta)) / WHEEL_DELTA * 10;
            while (m_wheelAccumulator >= 10)
            {
                m_wheelAccumulator -= 10;
                m_framer.ZoomFine(view, zoomIn);
            }
        }
        else
        {
            m_framer.ZoomStep(view, zoomIn);
        }
        m_framer.Clamp(true);
        AfterZoom();
    }

    std::optional<winrt::Windows::UI::Color> ShotView::HoverColor() const
    {
        if (!m_document || !m_hasPointer)
        {
            return std::nullopt;
        }
        Core::Point const p = m_framer.ToImage(m_lastView);
        auto const& image = m_document->Image();
        int const x = static_cast<int>(std::floor(p.x));
        int const y = static_cast<int>(std::floor(p.y));
        if (x < 0 || y < 0 || x >= image.width || y >= image.height)
        {
            return std::nullopt;
        }
        std::uint8_t const* px = image.pixels.data() + image.Stride() * static_cast<std::size_t>(y) + static_cast<std::size_t>(x) * 4;
        std::uint8_t const a = px[3];
        auto un = [a](std::uint8_t c) { return a ? static_cast<std::uint8_t>((std::min)(255, c * 255 / a)) : std::uint8_t{ 0 }; };
        return winrt::Windows::UI::Color{ a, un(px[2]), un(px[1]), un(px[0]) };
    }

    // ---- Drawing ----

    void ShotView::OnDraw(canvas::UI::Xaml::CanvasControl const& sender, canvas::UI::Xaml::CanvasDrawEventArgs const& args)
    {
        if (!m_document || m_document->Image().IsEmpty())
        {
            return;
        }
        if (!m_framed)
        {
            UpdateArea();
            if (!m_framed)
            {
                return;
            }
        }
        auto const& image = m_document->ImagePtr();
        if (!m_bitmap || m_bitmapSource != image)
        {
            m_bitmap = canvas::CanvasBitmap::CreateFromBytes(sender, winrt::array_view<std::uint8_t const>(image->pixels), image->width,
                image->height, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 96.0f,
                canvas::CanvasAlphaMode::Premultiplied);
            m_bitmapSource = image;
        }
        double const k = m_framer.dpr;
        Core::Point const pan = m_framer.RoundedPan();
        m_context.zoom = m_framer.zoom;
        m_context.dpr = ImageDpr();
        m_context.rasterScale = k;
        m_context.origin = { pan.x / k, pan.y / k };
        m_context.image = m_bitmap;
        Render::SyncEnvironment(m_document, m_context, [this] { Invalidate(); });

        auto session = args.DrawingSession();
        bool const dark = m_canvas.ActualTheme() == winrt::Microsoft::UI::Xaml::ElementTheme::Dark;
        auto const canvasRect = m_context.ToCanvasRect(m_framer.canvas);
        Core::Rect const crop = m_document->Crop();
        auto const drawContent = [&] {
            Render::DrawImage(session, m_bitmap, crop, m_context.ToCanvasRect(crop), m_framer.zoom);
            auto const clip = session.CreateLayer(1.0f, canvasRect);
            Render::DrawSpotlights(session, m_document->Objects(), m_context, m_document->Source());
            for (auto const& object : m_document->Objects())
            {
                Render::Draw(session, *object, m_context);
            }
            clip.Close();
        };
        if (m_document->backdrop.config.isOn == 1)
        {
            BackdropRender::DrawPreview(session, *m_document, m_context, drawContent);
        }
        else
        {
            Render::DrawCheckerboard(session, canvasRect, dark);
            drawContent();
        }
        bool animating = m_crop.Draw(session, m_context, canvasRect, ImageDpr(), RealPixels(), m_hoverUnits, m_pixelTools.BadgeHint());
        m_objects.Draw(session, m_context);
        DrawBrushPreview(session);
        if (m_drag == Drag::ZoomRect)
        {
            winrt::Windows::Foundation::Rect const zoomRect{
                static_cast<float>((std::min)(m_downView.x, m_lastView.x) / k), static_cast<float>((std::min)(m_downView.y, m_lastView.y) / k),
                static_cast<float>(std::abs(m_lastView.x - m_downView.x) / k), static_cast<float>(std::abs(m_lastView.y - m_downView.y) / k) };
            session.DrawRectangle(zoomRect, winrt::Windows::UI::Color{ 0x99, 0, 0, 0 }, static_cast<float>(1 / k));
        }
        m_pixelTools.Draw(session, m_context);
        if (animating)
        {
            StartAnimationTimer();
        }
    }
}
