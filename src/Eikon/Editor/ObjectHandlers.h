#pragma once

// ObjectHandlers singleton: selecting, hovering,
// creating, moving and resizing annotation objects through the ProtoObject pointer API, the object
// keys, snapping while dragging, and drawing the knobs and guides. Objects come from
// Core::CreateObject (new) and Core::LoadObject (copies, undo).

#include "Core/Document.h"
#include "Core/Framer.h"
#include "Core/Snapping.h"
#include "Editor/Render.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Eikon::Editor
{
    class ObjectHandlers
    {
    public:
        struct Host
        {
            std::function<void(std::wstring_view label)> recordUndo;   // immediate undo step
            std::function<void()> recordUndoDebounced;                 // 0.33 s merged step
            std::function<void()> objectsChanged;                      // canvas growth, labels, options bar
            std::function<void()> activeChanged;                       // mode / options bar
        };

        explicit ObjectHandlers(Host host);

        void SetDocument(std::shared_ptr<Core::Document> document);

        Core::ProtoObject* Active() const noexcept;
        std::optional<std::int64_t> ActiveId() const noexcept { return m_active; }
        void Select(std::int64_t id);
        // Deselects. Returns true when something was selected.
        bool Deselect();

        // The topmost object whose HitTest is non-empty at `image`, with the part it reported.
        Core::ProtoObject* HitTest(Core::Point image, double zoom, std::wstring* part = nullptr) const;
        // The cursor for `image` ("" = not over an object).
        std::wstring CursorAt(Core::Point image, double zoom) const;
        // Hover highlight (state "highlighted"); returns true when it changed.
        bool Hover(Core::Point image, double zoom);

        // Starts a drag on an object (select/move/resize) or creates one with `tool` ("" = no tool).
        // Returns false when the press is not for objects (the crop tool gets it).
        bool MouseDown(Core::Point image, Modifiers modifiers, double zoom, std::wstring_view tool);
        void MouseDragged(Core::Point image, Modifiers modifiers, Core::Framer const& framer, double imageDpr);
        // Returns true when a drag ended.
        bool MouseUp(Core::Point image, Modifiers modifiers);
        bool IsDragging() const noexcept { return m_dragId.has_value(); }
        // Esc step 1: drops an object being created.
        bool CancelCreation();

        // `realPixels`/`dpr` size the arrow-key steps.
        bool KeyDown(std::uint32_t key, Modifiers modifiers, double imageDpr, bool realPixels);

        void DeleteActive();
        void DuplicateActive();
        void CopyActive();
        bool PasteCopied();
        void SelectNext();
        // Sets a property on the active object (options bar, ObjectHandlers.setProperty).
        void SetActiveProperty(std::wstring_view key, std::wstring_view value);

        void Draw(winrt::Microsoft::Graphics::Canvas::CanvasDrawingSession const& session, Render::Context const& context);

    private:
        Core::ProtoObject* Find(std::optional<std::int64_t> id) const noexcept;
        void SetHovered(std::optional<std::int64_t> id);

        Host m_host;
        std::shared_ptr<Core::Document> m_document;
        std::optional<std::int64_t> m_active;
        std::optional<std::int64_t> m_hovered;
        std::optional<std::int64_t> m_dragId;
        std::wstring m_dragPart;          // "creating", "move" or a handle name
        Core::Point m_downPoint;
        Core::Rect m_downRect;
        bool m_movedEnough = false;
        std::vector<Core::SnapGuide> m_guides;
        std::wstring m_copied;            // ObjectHandlers.copiedString (Dump JSON)
    };
}
