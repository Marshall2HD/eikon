#pragma once

// The editor canvas: hosts the Win2D CanvasControl
// of the EditorWindow, draws the Document (checkerboard, image, objects, crop marquee, handles,
// guides, pixel tools), owns the zoom/pan state, and routes pointer, wheel and key input to the
// object handlers, the crop tool and the pixel tools. UI thread only.

#include "Core/Document.h"
#include "Core/Framer.h"
#include "Editor/CropTool.h"
#include "Editor/ObjectHandlers.h"
#include "Editor/PixelTools.h"
#include "Editor/Render.h"

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace Eikon::Editor
{
    class ShotView
    {
    public:
        struct Host
        {
            std::function<void()> stateChanged;      // mode, zoom, size label, menu state, options bar
            std::function<void()> hoverChanged;      // the colour readout under the cursor
            std::function<bool()> anyHelperOpen;     // Tab is swallowed while a helper is open
            std::function<void()> hideEditor;        // colour copy hides the editor
            std::function<void()> canvasPressed;     // a press on the canvas closes transient helpers
            std::function<void(Core::ProtoObject&)> editText;   // open the text editor on an ObjText
            std::function<void(winrt::Microsoft::UI::Input::PointerPoint const&)> dragOut;   // Ctrl+Alt + drag: the file drag-out
            std::function<void(std::wstring const&, std::wstring const&)> alert;   // modal alert (title, text, OK)
        };

        ShotView(winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasControl const& canvas, Host host);
        ~ShotView();
        ShotView(ShotView const&) = delete;
        ShotView& operator=(ShotView const&) = delete;

        // A new capture. `keepWindowSize` keeps the window's size.
        void SetDocument(std::shared_ptr<Core::Document> document, bool keepWindowSize = false);
        std::shared_ptr<Core::Document> const& Document() const noexcept { return m_document; }

        // Current zoom (device pixels per image pixel) and where image pixel (0, 0) is drawn.
        Render::Context const& Context() const noexcept { return m_context; }
        Core::Framer const& Framer() const noexcept { return m_framer; }
        double Zoom() const noexcept { return m_framer.zoom; }
        void Invalidate();

        // ---- Tools and modes ----
        // "" (V, the default select/crop tool) or a tool id of that draws objects.
        void SetTool(std::wstring_view tool);
        std::wstring const& Tool() const noexcept { return m_tool; }
        // Toolbar mode: "selection" with a crop selection, the active
        // object's class name ("ObjArrow", ...) with an object, else "default".
        std::wstring Mode() const;
        ObjectHandlers& Objects() noexcept { return m_objects; }
        PixelTools& Pixels() noexcept { return m_pixelTools; }

        // ---- Zoom ----
        void ZoomIn();
        void ZoomOut();
        void ZoomToFit();
        void ZoomTo100();
        void ZoomToSelection();
        void SelectionTopLeft();
        void SelectionBottomRight();

        // ---- Crop selection ----
        std::optional<Core::Rect> Selection() const noexcept { return m_crop.Selection(); }
        void ResetSelection();
        // "Cut Out": erase the selection and put its subject back as an image object. The
        // subject is found off the UI thread; the document changes when it arrives.
        void CutOut();
        void SmartAdjust(std::wstring_view edge = {});
        // The selection, or the canvas: what "Selection Size" and Resize Image refer to (image pixels).
        Core::Rect SelectionOrCanvas() const;

        // ---- Crop, uncrop, rasterize ----
        void CropToSelection();
        void ResetCrop();
        bool CanResetCrop() const;
        void Rasterize();

        // ---- Undo ----
        void RecordUndo(std::wstring_view label = {});
        void RecordUndoDebounced();
        void Undo();
        void Redo();

        // ---- Keys ----
        // Object keys, selection keys, pixel tools and the canvas keys; false when not handled.
        bool KeyDown(std::uint32_t key, Modifiers modifiers);
        bool KeyUp(std::uint32_t key, Modifiers modifiers);
        // Esc step 1.
        bool CancelCreation();
        // Esc step 3: leaves croptool / objectHandlers mode. False when already in default mode.
        bool LeaveMode();

        // The pixel under the cursor (image pixels) and its colour, for the info bar.
        std::optional<winrt::Windows::UI::Color> HoverColor() const;

    private:
        void OnDraw(winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasControl const& sender,
                    winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasDrawEventArgs const& args);
        void OnPressed(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnMoved(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnReleased(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnWheel(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void UpdateArea();
        Core::Point ViewPoint(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args, bool shifted = true) const;
        void UpdateCursor(Core::Point view, Modifiers modifiers);
        void DrawBrushPreview(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session);
        void AfterZoom();
        void FinishCutOut(Core::Rect const& rect, std::shared_ptr<Core::Bitmap const> subject);
        void Changed();
        void UpdateCanvasGrowth();
        void RefreshCanvas();
        void Restore(std::wstring const& full);
        double ImageDpr() const noexcept;
        bool RealPixels() const;
        bool HeldZoomKey() const;
        bool HeldPanKey() const;
        void StartAnimationTimer();

        winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasControl m_canvas{ nullptr };
        Host m_host;
        winrt::event_token m_drawToken{};
        winrt::event_token m_createResourcesToken{};
        winrt::event_token m_pressedToken{};
        winrt::event_token m_movedToken{};
        winrt::event_token m_releasedToken{};
        winrt::event_token m_wheelToken{};
        winrt::event_token m_exitedToken{};
        winrt::event_token m_captureLostToken{};
        winrt::event_token m_sizeToken{};
        std::shared_ptr<Core::Document> m_document;
        // Expires with the view: Cut Out's background work checks it before touching the document.
        std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
        std::uint64_t m_documentChangedToken = 0;
        std::uint64_t m_backdropChangedToken = 0;   // document.backdrop.Changed
        winrt::Microsoft::Graphics::Canvas::CanvasBitmap m_bitmap{ nullptr };
        std::shared_ptr<Core::Bitmap const> m_bitmapSource;
        Render::Context m_context;

        Core::Framer m_framer;
        bool m_framed = false;            // the Framer has been fitted to the current document
        bool m_keepWindowSize = false;
        bool m_autoFit = false;           // re-fit on area changes until the user zooms or pans
        CropTool m_crop;
        ObjectHandlers m_objects;
        PixelTools m_pixelTools;
        std::wstring m_tool;

        // Pointer state.
        enum class Drag
        {
            None,
            Pan,
            ZoomRect,
            Objects,
            Crop,
            PixelTool,
            DragOut,
        };
        Drag m_drag = Drag::None;
        Core::Point m_downView;
        Core::Point m_downPan;
        Core::Point m_lastView;
        bool m_hasPointer = false;
        bool m_pointerOver = false;   // over the canvas (the highlighter brush preview)
        bool m_hoverUnits = false;
        std::set<std::uint32_t> m_heldKeys;   // Space, Z, Y (PressedKeys)
        double m_wheelAccumulator = 0;

        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_undoTimer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_animationTimer{ nullptr };
    };
}
