#pragma once

// ObjConceal: Blur / Blur Text / Erase / Erase Text. The object owns the bitmap it
// generated for its current rect ("reconceal"); the text modes need OCR word boxes,
// which the renderer obtains from Platform/Ocr and hands back with SetTextBoxes.

#include "Core/Bitmap.h"
#include "Core/Objects/ProtoObject.h"

#include <memory>
#include <optional>

namespace Eikon::Core
{
    class ObjConceal final : public ProtoObject
    {
    public:
        std::wstring_view ClassName() const override { return L"ObjConceal"; }
        // {rect, size, fillMode}: no colour.
        JsonValue Dump() const override;
        bool Load(JsonValue const& json) override;
        std::wstring HitTest(Point point, double zoom) const override;
        std::vector<Handle> Handles() const override { return BoxHandles(rect); }
        KeyResult KeyDown(KeyEvent const& key, KeyContext const& context) override { return BaseKeyDown(key, context); }
        void MouseDown(Point point, Modifiers modifiers) override;
        void MouseDragged(Point point, Modifiers modifiers) override { DragBox(point, modifiers); }
        void MouseUp(Point point, Modifiers modifiers) override;
        void ApplyDefaults(Settings& settings) override;
        // "concealType": the fillMode, saved as "fillMode", and the bitmap is redone.
        void SetProperty(std::wstring_view key, std::wstring_view value) override;
        std::wstring Property(std::wstring_view key) const override;
        // While drawn: a 1 dpr white border and black fill at 0.1; the hover outline.
        std::vector<Shape> Shapes(double dpr, double zoom) const override;

        std::wstring fillMode = L"pixelate";   // "pixelate", "pixelate-text", "erase", "erase-text"

        bool IsTextMode() const { return fillMode == L"pixelate-text" || fillMode == L"erase-text"; }
        // The pixel area concealed: rect clamped into the image (ClampArea).
        Rect Area() const;
        // The concealing bitmap for the current rect and mode, regenerated when they change; nullptr
        // while the text boxes for this rect are still unknown, or without a source image.
        std::shared_ptr<Bitmap const> Concealed(Bitmap const& source, double dpr) const;
        // OCR result for `forArea` (word boxes relative to that area, pixels).
        void SetTextBoxes(Rect const& forArea, std::vector<Rect> boxes);
        // The area whose OCR the text modes are waiting for (nullopt when nothing is needed).
        std::optional<Rect> PendingOcr() const;

    private:
        mutable std::shared_ptr<Bitmap const> m_bitmap;
        mutable Rect m_bitmapArea;
        mutable std::wstring m_bitmapMode;
        mutable int m_bitmapSize = -1;
        Rect m_boxesArea{ -1, -1, -1, -1 };
        std::vector<Rect> m_boxes;
    };
}
