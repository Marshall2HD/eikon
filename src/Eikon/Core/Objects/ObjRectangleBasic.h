#pragma once

// ObjRectangleBasic and ObjImage. The basic rectangle is a plain rectangle with
// border/fill toggles and no hand-drawn mode or shape choice; ObjImage, its subclass, is a pasted
// image overlay whose pixels live in the image store under `imageHash`.

#include "Core/Bitmap.h"
#include "Core/Objects/ProtoObject.h"

#include <array>
#include <map>
#include <memory>

namespace Eikon::Core
{
    class ObjRectangleBasic : public ProtoObject
    {
    public:
        static constexpr std::array<double, 10> kWidths{ 1, 2, 3, 4, 6, 8, 11, 15, 21, 30 };

        std::wstring_view ClassName() const override { return L"ObjRectangleBasic"; }
        JsonValue Dump() const override;
        bool Load(JsonValue const& json) override;
        std::wstring HitTest(Point point, double zoom) const override;
        std::vector<Handle> Handles() const override { return BoxHandles(rect); }
        // ProtoObject keys, then B / F / digits as ObjOval.
        KeyResult KeyDown(KeyEvent const& key, KeyContext const& context) override;
        void MouseDown(Point point, Modifiers modifiers) override;
        void MouseDragged(Point point, Modifiers modifiers) override { DragBox(point, modifiers); }
        // "fill": alpha = n/100 (0.01 if unparseable), fill on when > 0, border on.
        void SetProperty(std::wstring_view key, std::wstring_view value) override;
        std::wstring Property(std::wstring_view key) const override;
        // The rounded rectangle (no hand-drawn mode).
        std::vector<Shape> Shapes(double dpr, double zoom) const override;

        bool showBorder = true;
        bool showBackground = false;
        double backgroundAlpha = 0.3;
    };

    // Pixels of pasted images, keyed by ObjImage::imageHash (they travel separately from the Dump).
    std::map<std::wstring, std::shared_ptr<Bitmap const>>& ImageStore();

    class ObjImage final : public ObjRectangleBasic
    {
    public:
        std::wstring_view ClassName() const override { return L"ObjImage"; }
        JsonValue Dump() const override;
        bool Load(JsonValue const& json) override;
        KeyResult KeyDown(KeyEvent const& key, KeyContext const& context) override;
        void MouseDragged(Point point, Modifiers modifiers) override;
        void ApplyDefaults(Settings& settings) override;
        // "transparency": "true" → opacity 0.5, else 1 (getter "true" when opacity ≠ 1); "fill" as the
        // basic rectangle; "saveGIF" runs the GIF action.
        void SetProperty(std::wstring_view key, std::wstring_view value) override;
        std::wstring Property(std::wstring_view key) const override;
        std::vector<Shape> Shapes(double dpr, double zoom) const override;

        std::wstring imageHash;

        // Insert: stores `image` under a new 24-character base-36 hash and places the
        // object at the image's pixel size, centred in `visible` (the part of the canvas in view, image
        // pixels); an image at least as wide (tall) as `canvas` goes to x (y) = min(canvas/2, 100).
        void Insert(std::shared_ptr<Bitmap const> image, Rect const& visible, Size const& canvas);
        std::shared_ptr<Bitmap const> Image() const;
        // Opacity of the overlay: backgroundAlpha (the "transparency" option).
        double Opacity() const noexcept { return backgroundAlpha; }

    private:
        double m_aspect = 1.0;
    };
}
