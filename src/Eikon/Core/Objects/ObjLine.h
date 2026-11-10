#pragma once

// ObjLine: the line tool, start + (dx, dy) like the arrow, with optional caps and
// the dotted ("dash") style.

#include "Core/Objects/ProtoObject.h"

#include <array>

namespace Eikon::Core
{
    class ObjLine final : public ProtoObject
    {
    public:
        static constexpr std::array<double, 10> kWidths{ 1, 2, 3, 4, 6, 8, 11, 15, 21, 30 };

        std::wstring_view ClassName() const override { return L"ObjLine"; }
        JsonValue Dump() const override;
        bool Load(JsonValue const& json) override;
        std::wstring HitTest(Point point, double zoom) const override;
        std::vector<Handle> Handles() const override;
        KeyResult KeyDown(KeyEvent const& key, KeyContext const& context) override { return BaseKeyDown(key, context); }
        void MouseDown(Point point, Modifiers modifiers) override;
        void MouseDragged(Point point, Modifiers modifiers) override;
        void ApplyDefaults(Settings& settings) override;
        // "lineType": "line" (caps none, solid) or "dash" (caps none, dotted).
        void SetProperty(std::wstring_view key, std::wstring_view value) override;
        std::wstring Property(std::wstring_view key) const override;
        std::vector<Shape> Shapes(double dpr, double zoom) const override;

        std::wstring startCap = L"none";   // "none", "arrow", "bullet"
        std::wstring endCap = L"none";
        bool dotted = false;

        Point Start() const noexcept { return rect.Origin(); }
        Point End() const noexcept { return { rect.x + rect.width, rect.y + rect.height }; }

    private:
        Rect m_downRect;
    };
}
