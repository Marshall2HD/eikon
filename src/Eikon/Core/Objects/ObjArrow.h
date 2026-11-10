#pragma once

// ObjArrow: the arrow tool. `rect` is start + (dx, dy); the head is at the end.

#include "Core/Objects/ProtoObject.h"

#include <optional>

namespace Eikon::Core
{
    class ObjArrow final : public ProtoObject
    {
    public:
        // `type` values (toolbar segments, static table).
        enum Type : std::int32_t
        {
            Shape = 0,      // 16-arrow-shape: filled SVG profile
            Line = 1,       // 16-arrow-line: stroked, head at the end
            Double = 2,     // 16-arrow-double: stroked, heads at both ends
            Handdrawn = 4,  // 16-arrow-handdrawn: variable-width outline
        };

        std::wstring_view ClassName() const override { return L"ObjArrow"; }
        JsonValue Dump() const override;
        bool Load(JsonValue const& json) override;
        std::wstring HitTest(Point point, double zoom) const override;
        std::vector<Handle> Handles() const override;
        std::vector<Handle> HandlesAt(double zoom, double dpr) const override;
        KeyResult KeyDown(KeyEvent const& key, KeyContext const& context) override;
        void MouseDown(Point point, Modifiers modifiers) override;
        void MouseDragged(Point point, Modifiers modifiers) override;
        void MouseUp(Point point, Modifiers modifiers) override;
        void ApplyDefaults(Settings& settings) override;
        // "arrowType" (saved as the arrowType preference) and the "changeStyle" action:
        // a hand-drawn arrow gets a new seed, a shape arrow the next of the four profiles (saved as arrowStyle).
        void SetProperty(std::wstring_view key, std::wstring_view value) override;
        std::wstring Property(std::wstring_view key) const override;
        std::vector<Core::Shape> Shapes(double dpr, double zoom) const override;

        std::int32_t style = 0;          // arrowStyle 0–3 (shape profile)
        std::int32_t type = Shape;       // arrowType
        double midpa = 0;                // curve angle (rad, y down: positive bends clockwise), wrapped to ±π
        double midpd = 0.5;              // curve knob distance as a fraction of the length
        bool reversed = false;           // Ctrl held while creating: the head goes at the press point
        mutable std::optional<std::uint64_t> seed;  // drawn on first render

        Point Start() const noexcept { return rect.Origin(); }
        Point End() const noexcept { return { rect.x + rect.width, rect.y + rect.height }; }
        bool IsCurved() const noexcept { return midpa != 0; }
        // The `point-curve` knob: start + len·midpd in the direction angle(arrow) + midpa.
        Point CurveKnob() const noexcept;
        // Stroke width (render): min(len/10, T2[size]) × dpr × clamp(√((len/dpr + 550)/600), 1, 1.5),
        // whole pixels when > 4 at dpr 1.
        static double Width(double length, int size, double dpr);
        // Head and tail scale factors of the shape profile for an arrow of `length`.
        static void HeadTail(double length, int size, double& head, double& tail);
        // The seeded random values; draws and keeps a seed on first use.
        std::vector<double> const& Rnd() const;

    private:
        mutable std::vector<double> m_rnd;
        mutable std::uint64_t m_rndSeed = 0;
        Rect m_downRect;
    };

    // The hand-drawn arrow's filled outline (shaft and
    // the two barbs). `w` = min(len/15, (T2[size]·0.7 + 1.3)·dpr), `head` the head scale factor.
    Path HanddrawnArrowPath(Point start, Point delta, double w, double head, std::vector<double> const& rnd, double dpr,
                            bool curved, Point knob);
}
