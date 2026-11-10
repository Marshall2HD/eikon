#pragma once

// ObjRuler, geometry only: a bar across the measured span with an end tick at
// each end. The label (tperc, tdist, the PX/PT text) belongs to the measure tool.

#include "Core/Objects/ProtoObject.h"

namespace Eikon::Core
{
    class ObjRuler final : public ProtoObject
    {
    public:
        std::wstring_view ClassName() const override { return L"ObjRuler"; }
        JsonValue Dump() const override;
        bool Load(JsonValue const& json) override;
        std::wstring HitTest(Point point, double zoom) const override;
        // Handles: l/r-resize along a horizontal ruler, t/b-resize along a vertical one.
        std::vector<Handle> Handles() const override;
        KeyResult KeyDown(KeyEvent const& key, KeyContext const& context) override { return BaseKeyDown(key, context); }
        void MouseDown(Point point, Modifiers modifiers) override;
        void MouseDragged(Point point, Modifiers modifiers) override;
        std::vector<Shape> Shapes(double dpr, double zoom) const override;
        // "rulerPixels": "px" sets dpr 1, anything else the screen's scale;
        // reads "px" when dpr is 1, else "pt".
        void SetProperty(std::wstring_view key, std::wstring_view value) override;
        std::wstring Property(std::wstring_view key) const override;

        bool vertical = false;
        double dpr = 1.0;        // the scale it measures in
        double tperc = 0.5;      // label position along the bar
        double tdist = 0;        // label distance from the bar
        bool flipIfNeeded = true;

        // The bar (thickness max(1, size) px, origin floored with −0.5 on the cross axis)
        // and the two 1 × 7 px end ticks centred on it (offset −3).
        std::vector<Rect> BarRects() const;
    };
}
