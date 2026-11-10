#pragma once

// The image open in the editor (ARCHITECTURE.md layout: "Document"): the bitmap, the crop, the
// canvas growth, the annotation objects and the counter, plus capture metadata and the
// undo history.

#include "Core/Backdrop.h"
#include "Core/Bitmap.h"
#include "Core/Event.h"
#include "Core/Framer.h"
#include "Core/Geometry.h"
#include "Core/Objects/ProtoObject.h"
#include "Core/Undo.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Eikon::Core
{
    struct CaptureMetadata
    {
        std::wstring mode;      // "fullscreen", "interactive", ..., "clipboard", "file"
        std::wstring app;       // window attribution, used by file-name macros
        std::wstring window;
        std::chrono::system_clock::time_point screenshotDate{};
        bool backdropApplied = false;
        // The name the image was last saved under (metadata `autoName`): saving
        // again reuses it.
        std::wstring autoName;
    };

    class Document
    {
    public:
        explicit Document(Bitmap image);
        // Not copyable: the registered undo parts refer to this instance.
        Document(Document const&) = delete;
        Document& operator=(Document const&) = delete;

        Bitmap const& Image() const noexcept { return *m_image; }
        std::shared_ptr<Bitmap const> const& ImagePtr() const noexcept { return m_image; }
        // Rasterize: the flattened image becomes the base; the crop moves by
        // `cropOffset` (when the new image starts left of/above the old one) and the growth is cleared.
        void ReplaceImage(Bitmap image, Point cropOffset = {});

        // Framer's source/original (0, 0, W, H) in image pixels.
        Rect Source() const noexcept;
        // The non-destructive crop; initially Source().
        Rect Crop() const noexcept { return m_crop; }
        void SetCrop(Rect crop);
        bool IsCropped() const noexcept { return m_crop != Source(); }

        // Expandable canvas: growth beyond the crop, transparent when drawn.
        Insets CanvasGrowth() const noexcept { return m_growth; }
        void SetCanvasGrowth(Insets growth);
        // The crop plus the growth: what the editor shows and the composed image covers.
        Rect Canvas() const noexcept;

        // Objects in drawing order (last is on top).
        std::vector<ObjectPtr> const& Objects() const noexcept { return m_objects; }
        // Takes ownership, assigns the next id and returns the object.
        ProtoObject& Add(ObjectPtr object);
        // Removes and returns the object with `id` (nullptr when there is none).
        ObjectPtr Remove(std::int64_t id);
        ProtoObject* Find(std::int64_t id) const noexcept;
        std::ptrdiff_t IndexOf(std::int64_t id) const noexcept;
        // Replaces every object (undo restore); ids are reassigned.
        void ReplaceObjects(std::vector<ObjectPtr> objects);

        // ObjectHandlers.counterCount: the number the last ObjCounter took.
        std::int32_t CounterCount() const noexcept { return m_counterCount; }
        void SetCounterCount(std::int32_t value) noexcept { m_counterCount = value; }
        std::int32_t NextCounter() noexcept { return ++m_counterCount; }

        CaptureMetadata metadata;
        Core::Undo& History() noexcept { return m_undo; }

        // The backdrop around the image.
        Backdrop::State backdrop;

        // ---- Undo snapshots ----
        // Other owners (the backdrop) add their state to every snapshot under `name`.
        struct UndoPart
        {
            std::wstring name;
            std::function<JsonValue()> save;
            std::function<void(JsonValue const&)> restore;
        };
        void RegisterUndoPart(UndoPart part);
        // The current state; `selection` and `selectedId` are the editor's crop selection and active object.
        UndoSnapshot Snapshot(std::optional<Rect> selection, std::optional<std::int64_t> selectedId) const;
        // Restores objects (through LoadObject), crop, counter and the registered parts. Returns the id
        // of the object that was selected, if any.
        std::optional<std::int64_t> Restore(UndoSnapshot const& snapshot);

        // Raised after the image, the crop, the growth or the object list changed.
        Event<> Changed;

    private:
        std::shared_ptr<Bitmap const> m_image;
        Rect m_crop;
        Insets m_growth;
        std::vector<ObjectPtr> m_objects;
        std::int64_t m_nextId = 1;
        std::int32_t m_counterCount = 0;
        Core::Undo m_undo;
        std::vector<UndoPart> m_undoParts;
    };
}
